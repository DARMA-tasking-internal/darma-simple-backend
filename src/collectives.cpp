/*
//@HEADER
// ************************************************************************
//
//                      collectives.cpp
//                         DARMA
//              Copyright (C) 2017 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact the DARMA developers (darma-admins@sandia.gov)
//
// ************************************************************************
//@HEADER
*/


#include "runtime/runtime.hpp"
#include "flow/flow.hpp"
#include "task_collection_token.hpp"

#include "debug.hpp"

using namespace simple_backend;

//==============================================================================

void Runtime::allreduce_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_in_out,
  darma_runtime::abstract::frontend::CollectiveDetails const* details,
  darma_runtime::types::key_t const& tag
) {

  auto token = details->get_task_collection_token();
  assert(token);
  auto size = token->size;
  auto* reduce_op = details->reduce_operation();
  assert(size == details->n_contributions());

  token->collectives.evaluate_or_evaluate_first(tag,
    //==========================================================================
    // Evaluated if collective already exists
    [size, reduce_op, this, token](
      std::shared_ptr<TaskCollectionToken::CollectiveInvocation> const& invocation,
      auto&& use_in_out
    ) mutable {

      invocation->input_uses.push_back(std::move(use_in_out));

      auto& use_in_out_in_list = invocation->input_uses.back();

      // Do antiflow first, since if it doesn't exist, the use could be deleted
      // by the decrement of the in flow
      if(use_in_out_in_list->get_anti_in_flow()) {
        use_in_out_in_list->get_anti_in_flow()->get_ready_trigger()->add_action([invocation]{
          // This doesn't race with the completion of the invocation because this
          // Use's anti-in flow is responsible for one decrement of the invocation ready
          // trigger, which is done at the end of this lambda
          invocation->ready_trigger.decrement_count();
        });
      }
      else {
        invocation->ready_trigger.decrement_count();
      }

      use_in_out_in_list->get_in_flow()->get_ready_trigger()->add_action([
        invocation,
        handle = invocation->input_uses.back()->get_handle(),
        control_block = invocation->input_uses.back()->get_in_flow()->control_block,
        reduce_op
      ]{
        // This doesn't race with the completion of the invocation because this
        // Use's in flow is responsible for one decrement of the invocation ready
        // trigger, which is done at the end of this lambda


        // We'll transfer the responsibility for making the invocation ready
        // to the action that does the actual contribution to the reduction
        // Since all of the reduction op invocations are performed as part of the
        // action list on the first Use's in flow, they will be serialized and thus
        // won't race (at least until ActionLists get parallelized, if ever)
        // TODO this should be something like a when_all_triggers_ready() action or something
        if(invocation->input_uses[0]->get_anti_in_flow()) {
          invocation->input_uses[0]->get_anti_in_flow()->get_ready_trigger()->add_action(
            [invocation, reduce_op, control_block, handle] {

              invocation->input_uses[0]->get_in_flow()->get_ready_trigger()->add_action(
                [invocation, reduce_op, control_block, handle] {
                  auto* in_data = control_block->data;
                  auto nelem = handle->get_array_concept_manager()->n_elements(in_data);

                  auto first_use_data = invocation->input_uses[0]->get_in_flow()->control_block->data;
                  reduce_op->reduce_unpacked_into_unpacked(
                    in_data, first_use_data, 0, nelem
                  );

                  invocation->ready_trigger.decrement_count();
                }
              );

            }
          );
        }
        else {
          // frustratingly, I don't see an easy way to do this without copy-and-paste
          invocation->input_uses[0]->get_in_flow()->get_ready_trigger()->add_action(
            [invocation, reduce_op, control_block, handle] {
              auto* in_data = control_block->data;
              auto nelem = handle->get_array_concept_manager()->n_elements(in_data);

              auto first_use_data = invocation->input_uses[0]->get_in_flow()->control_block->data;
              reduce_op->reduce_unpacked_into_unpacked(
                in_data, first_use_data, 0, nelem
              );

              invocation->ready_trigger.decrement_count();
            }
          );
        }

      });

    },
    //==========================================================================
    // Evaluated if we got here first
    [size, reduce_op, this, token, tag](
      std::shared_ptr<TaskCollectionToken::CollectiveInvocation> const& invocation,
      auto&& use_in_out
    ) mutable {

      // Add the completion of the reduction to the invocation ready trigger
      // This should never do direct descent since, at the very least, the trigger
      // contributions from use_in_out haven't been decremented (which happens below)
      invocation->ready_trigger.add_action([
        invocation_ptr = std::weak_ptr<TaskCollectionToken::CollectiveInvocation>(invocation),
        this, token, tag
      ]() mutable {
        auto invocation = invocation_ptr.lock();
        assert(invocation);

        // TODO use deep copy instead here, if it's available
        auto* first_use_data = invocation->input_uses[0]->get_in_flow()->control_block->data;
        auto ser_man = invocation->input_uses[0]->get_handle()->get_serialization_manager();
        auto ser_pol = darma_runtime::abstract::backend::SerializationPolicy();

        auto packed_size = ser_man->get_packed_data_size(first_use_data, &ser_pol);
        auto* packed_data = new char[packed_size];

        ser_man->pack_data(first_use_data, packed_data, &ser_pol);

        for(int iuse = 1; iuse < invocation->input_uses.size(); ++iuse) {
          auto& u = invocation->input_uses.at(iuse);
          auto* udata = u->get_in_flow()->control_block->data;
          ser_man->destroy(udata);
          ser_man->unpack_data(udata, packed_data, &ser_pol);
        }

        delete[] packed_data;

        for(auto&& use : invocation->input_uses) {
          this->release_use(
            darma_runtime::abstract::frontend::use_cast<
              darma_runtime::abstract::frontend::UsePendingRelease*
            >(use.get())
          );
        }

        // TODO ! This would potentially deadlock if there's only one element, since
        // we're holding a lock to the map from the outer evaluate_or_evaluate_first
        assert(token->size != 1);
        token->collectives.erase(tag);

      });

      // Move the use to the list of input uses to be released after the collective finishes
      // This has to be done before we add the triggers to the use because those
      // triggers might cause the collective invocation to complete
      invocation->input_uses.push_back(std::move(use_in_out));

      auto& use_in_out_in_list = invocation->input_uses.back();

      // Add the decrement of the invocation ready trigger to the ready trigger
      // of the anti-in flow
      // We have to do this before the in version for the same reason: the in flow
      // ready trigger could trigger the invocation completion, releasing use_in_out
      if(use_in_out_in_list->get_anti_in_flow()) {
        use_in_out_in_list->get_anti_in_flow()->get_ready_trigger()->add_action([
          invocation_ptr = std::weak_ptr<TaskCollectionToken::CollectiveInvocation>(invocation)
        ]{
          auto invocation = invocation_ptr.lock();
          assert(invocation);
          invocation->ready_trigger.decrement_count();
        });
      }
      else {
        invocation->ready_trigger.decrement_count();
      }

      // Add the decrement of the invocation ready trigger to the ready trigger
      // of the in flow
      use_in_out_in_list->get_in_flow()->get_ready_trigger()->add_action([
        invocation_ptr = std::weak_ptr<TaskCollectionToken::CollectiveInvocation>(invocation)
      ]{
        auto invocation = invocation_ptr.lock();
        assert(invocation);
        invocation->ready_trigger.decrement_count();
      });



    },
    //==========================================================================
    std::forward_as_tuple(std::make_shared<TaskCollectionToken::CollectiveInvocation>(2*size)),
    std::move(use_in_out)
  );
}

//==============================================================================

void
Runtime::allreduce_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_in,
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_out,
  darma_runtime::abstract::frontend::CollectiveDetails const* details,
  darma_runtime::types::key_t const& tag
) {

  assert(false); // not yet implemented

  // TODO better code reuse!  This is copy-and-pasted (mostly) from the single-use version

  auto token = details->get_task_collection_token();
  assert(token);
  auto size = token->size;
  auto* reduce_op = details->reduce_operation();
  assert(size == details->n_contributions());

  token->collectives.evaluate_or_evaluate_first(tag,
    //==========================================================================
    // Evaluated if collective already exists
    [size, reduce_op, this, token](
      std::shared_ptr<TaskCollectionToken::CollectiveInvocation> const& invocation,
      auto&& use_in,
      auto&& use_out
    ) mutable {

      assert(use_out->get_anti_in_flow());

      invocation->input_uses.push_back(std::move(use_in));
      invocation->output_uses.push_back(std::move(use_out));

      auto& use_in_in_list = invocation->input_uses.back();
      auto& use_out_in_list = invocation->output_uses.back();

      // Do antiflow first, since if it doesn't exist, the use could be deleted
      // by the decrement of the in flow
      use_out_in_list->get_anti_in_flow()->get_ready_trigger()->add_action([invocation]{
        // This doesn't race with the completion of the invocation because this
        // Use's anti-in flow is responsible for one decrement of the invocation ready
        // trigger, which is done at the end of this lambda
        invocation->ready_trigger.decrement_count();
      });

      use_in_in_list->get_in_flow()->get_ready_trigger()->add_action([
        invocation,
        handle = invocation->input_uses.back()->get_handle(),
        control_block = invocation->input_uses.back()->get_in_flow()->control_block,
        reduce_op
      ]{
        // This doesn't race with the completion of the invocation because this
        // Use's in flow is responsible for one decrement of the invocation ready
        // trigger, which is done at the end of this lambda

        // We'll transfer the responsibility for making the invocation ready
        // to the action that does the actual contribution to the reduction
        // Since all of the reduction op invocations are performed as part of the
        // action list on the first Use's in flow, they will be serialized and thus
        // won't race (at least until ActionLists get parallelized, if ever)
        // TODO this should be something like a when_all_triggers_ready() action or something
        invocation->output_uses[0]->get_anti_in_flow()->get_ready_trigger()->add_action(
          [invocation, reduce_op, control_block, handle] {

            invocation->input_uses[0]->get_in_flow()->get_ready_trigger()->add_action(
              [invocation, reduce_op, control_block, handle] {
                auto* in_data = control_block->data;
                auto nelem = handle->get_array_concept_manager()->n_elements(in_data);

                void* data_dest = nullptr;

                // TODO Finish this!!!


                // TODO we need to make sure this has been copied over before we do this!!!
                auto first_use_data = invocation->output_uses[0]->get_in_flow()->control_block->data;
                reduce_op->reduce_unpacked_into_unpacked(
                  in_data, first_use_data, 0, nelem
                );

                invocation->ready_trigger.decrement_count();
              }
            );

          }
        );

      });

    },
    //==========================================================================
    // Evaluated if we got here first
    [size, reduce_op, this, token, tag](
      std::shared_ptr<TaskCollectionToken::CollectiveInvocation> const& invocation,
      auto&& use_in, auto&& use_out
    ) mutable {

      // Add the completion of the reduction to the invocation ready trigger
      // This should never do direct descent since, at the very least, the trigger
      // contributions from use_in_out haven't been decremented (which happens below)
      invocation->ready_trigger.add_action([
        invocation_ptr = std::weak_ptr<TaskCollectionToken::CollectiveInvocation>(invocation),
        this, token, tag
      ]() mutable {
        auto invocation = invocation_ptr.lock();
        assert(invocation);

        // TODO use deep copy instead here, if it's available
        auto* first_use_data = invocation->output_uses[0]->get_in_flow()->control_block->data;
        auto ser_man = invocation->output_uses[0]->get_handle()->get_serialization_manager();
        auto ser_pol = darma_runtime::abstract::backend::SerializationPolicy();

        auto packed_size = ser_man->get_packed_data_size(first_use_data, &ser_pol);
        auto* packed_data = new char[packed_size];

        ser_man->pack_data(first_use_data, packed_data, &ser_pol);

        for(int iuse = 1; iuse < invocation->input_uses.size(); ++iuse) {
          auto& u = invocation->output_uses.at(iuse);
          auto* udata = u->get_in_flow()->control_block->data;
          ser_man->destroy(udata);
          ser_man->unpack_data(udata, packed_data, &ser_pol);
        }

        delete[] packed_data;

        for(auto&& use : invocation->input_uses) {
          this->release_use(
            darma_runtime::abstract::frontend::use_cast<
              darma_runtime::abstract::frontend::UsePendingRelease*
            >(use.get())
          );
        }

        for(auto&& use : invocation->output_uses) {
          this->release_use(
            darma_runtime::abstract::frontend::use_cast<
              darma_runtime::abstract::frontend::UsePendingRelease*
            >(use.get())
          );
        }

        // TODO ! This would potentially deadlock if there's only one element, since
        // we're holding a lock to the map from the outer evaluate_or_evaluate_first
        assert(token->size != 1);
        token->collectives.erase(tag);

      });

      // Move the use to the list of input uses to be released after the collective finishes
      // This has to be done before we add the triggers to the use because those
      // triggers might cause the collective invocation to complete
      invocation->input_uses.push_back(std::move(use_in));
      auto& use_in_in_list = invocation->input_uses.back();

      invocation->output_uses.push_back(std::move(use_out));
      auto& use_out_in_list = invocation->output_uses.back();

      // TODO finish this!
//      use_out_in_list->get_anti_in_flow()->get_ready_trigger()->add_action([invocation] {
//        use_in_in_list->get_in_flow()->get_ready_trigger()->add_action([invocation] {
//          auto* first_use_data = invocation->output_uses[0]->get_in_flow()->control_block->data;
//          auto ser_man = invocation->output_uses[0]->get_handle()->get_serialization_manager();
//          auto ser_pol = darma_runtime::abstract::backend::SerializationPolicy();
//
//          auto packed_size = ser_man->get_packed_data_size(first_use_data, &ser_pol);
//          auto* packed_data = new char[packed_size];
//
//          ser_man->pack_data(first_use_data, packed_data, &ser_pol);
//
//          for(int iuse = 1; iuse < invocation->input_uses.size(); ++iuse) {
//            auto& u = invocation->output_uses.at(iuse);
//            auto* udata = u->get_in_flow()->control_block->data;
//            ser_man->destroy(udata);
//            ser_man->unpack_data(udata, packed_data, &ser_pol);
//          }
//
//          delete[] packed_data;
//        });
//      });
      assert(false);

      // Add the decrement of the invocation ready trigger to the ready trigger
      // of the anti-in flow
      // We have to do this before the in version for the same reason: the in flow
      // ready trigger could trigger the invocation completion, releasing use_in_out
      use_out_in_list->get_anti_in_flow()->get_ready_trigger()->add_action([invocation] {
        invocation->ready_trigger.decrement_count();
      });

      // Add the decrement of the invocation ready trigger to the ready trigger
      // of the in flow
      use_in_in_list->get_in_flow()->get_ready_trigger()->add_action([invocation] {
        // Doesn't race with previous because of the lock
        // TODO use deep copy instead here, if it's available

        invocation->ready_trigger.decrement_count();
      });

    },
    //==========================================================================
    std::forward_as_tuple(std::make_shared<TaskCollectionToken::CollectiveInvocation>(2*size)),
    std::move(use_in), std::move(use_out)
  );
}

//==============================================================================

void Runtime::reduce_collection_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_collection_in,
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_out,
  darma_runtime::abstract::frontend::CollectiveDetails const* details,
  darma_runtime::types::key_t const&
) {
  assert(use_collection_in->manages_collection());
  assert(not use_out->manages_collection());

  auto in_ctrl_block = std::static_pointer_cast<CollectionControlBlock>(
    use_collection_in->get_in_flow()->control_block
  );

  auto* in_coll = darma_runtime::abstract::frontend::use_cast<
    darma_runtime::abstract::frontend::CollectionManagingUse*
  >(use_collection_in.get())->get_managed_collection();

  // TODO more asynchrony here (i.e., start when indices of prev collection are ready rather than whole)

  // This is a read, so it should only consume flows and produce anti-flows
  // of the
  darma_runtime::types::flow_t in_coll_in_flow(use_collection_in->get_in_flow());
  in_coll_in_flow->get_ready_trigger()->add_action([
    this,
    use_collection_in = std::move(use_collection_in),
    use_out = std::move(use_out),
    reduce_op = details->reduce_operation(), in_coll, in_ctrl_block
  ]() mutable {
    darma_runtime::types::anti_flow_t out_anti_in_flow(use_out->get_anti_in_flow());
    out_anti_in_flow->get_ready_trigger()->add_action([
      this,
      use_collection_in = std::move(use_collection_in),
      use_out = std::move(use_out), reduce_op, in_coll, in_ctrl_block
    ]{
      use_out->get_handle()->get_serialization_manager()->destroy(
        use_out->get_in_flow()->control_block->data
      );
      auto* out_data = use_out->get_in_flow()->control_block->data;

      auto array_concept_manager = use_collection_in->get_handle()->get_array_concept_manager();

      assert(in_ctrl_block->n_indices > 0);

      simple_backend::copy_data(
        use_out->get_handle(),
        in_ctrl_block->data_for_index(0),
        out_data
      );

      for(size_t idx = 1; idx < in_coll->size(); ++idx) {
        auto in_data = in_ctrl_block->data_for_index(idx);
        auto nelem = array_concept_manager->n_elements(
          in_data
        );
        reduce_op->reduce_unpacked_into_unpacked(
          in_data, out_data,
          0, nelem
        );
      }

      release_use(
        darma_runtime::abstract::frontend::use_cast<
          darma_runtime::abstract::frontend::UsePendingRelease*
        >(use_collection_in.get())
      );
      release_use(
        darma_runtime::abstract::frontend::use_cast<
          darma_runtime::abstract::frontend::UsePendingRelease*
        >(use_out.get())
      );

    });
  });

}

//==============================================================================

