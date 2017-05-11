/*
//@HEADER
// ************************************************************************
//
//                      register_use.cpp
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

#include "runtime.hpp"
#include "debug.hpp"

using namespace simple_backend;

static std::atomic<size_t> current_generated_key_index = { 0 };

void
Runtime::register_use(use_pending_registration_t* use) {
  using namespace darma_runtime::abstract::frontend; // FlowRelationship

  if(not use->get_handle()->has_user_defined_key()) {
    const_cast<darma_runtime::abstract::frontend::Handle*>(use->get_handle().get())
      ->set_key(
        darma_runtime::detail::key_traits<darma_runtime::types::key_t>::backend_maker{}(
          current_generated_key_index++
        )
      );
  }

  // TODO make this debugging work again
  _SIMPLE_DBG_DO([use](auto& state) { state.add_registered_use(use); });

  //----------------------------------------------------------------------------
  // <editor-fold desc="in flow relationship"> {{{2

  std::shared_ptr<Flow> in_flow = nullptr;

  auto const& in_rel = use->get_in_flow_relationship();

  switch (in_rel.description()) {
    case FlowRelationship::Insignificant :
    case FlowRelationship::InsignificantCollection : {
      in_flow = nullptr;
      break;
    }
    case FlowRelationship::Same :
    case FlowRelationship::SameCollection : {
      assert(in_rel.related_flow());
      in_flow = *in_rel.related_flow();
      break;
    }
    case FlowRelationship::Next :
    case FlowRelationship::NextCollection : {
      assert(in_rel.related_flow());
      in_flow = std::make_shared<Flow>(
        (*in_rel.related_flow())->control_block
      );
      break;
    }
    case FlowRelationship::Initial : {
      assert(in_rel.related_flow() == nullptr);
      in_flow = std::make_shared<Flow>(
        std::make_shared<ControlBlock>(use->get_handle()),
        1 // start with a count so we can make it ready immediately
      );
      in_flow->ready_trigger.decrement_count();
      break;
    }
    case FlowRelationship::InitialCollection : {
      assert(in_rel.related_flow() == nullptr);
      in_flow = std::make_shared<Flow>(
        std::make_shared<CollectionControlBlock>(
          use->get_handle(),
          darma_runtime::abstract::frontend::use_cast<
            darma_runtime::abstract::frontend::CollectionManagingUse*
          >(use)->get_managed_collection()->size()
        ),
        1 // start with a count so we can make it ready immediately
      );
      in_flow->ready_trigger.decrement_count();
      break;
    }
    case FlowRelationship::IndexedLocal : {
      assert(in_rel.related_flow());
      auto coll_cntrl = std::static_pointer_cast<CollectionControlBlock>(
        (*in_rel.related_flow())->control_block
      );
      in_flow = std::make_shared<Flow>(
        std::make_shared<ControlBlock>(
          coll_cntrl->data_for_index(in_rel.index()),
          coll_cntrl,
          in_rel.index()
        ),
        1 // start with a count so that the collection flow can make it ready
      );
      (*in_rel.related_flow())->ready_trigger.add_action([in_flow] {
        in_flow->ready_trigger.decrement_count();
      });
      break;
    }
    case FlowRelationship::IndexedFetching : {
      assert(in_rel.related_flow());
      auto coll_cntrl = std::static_pointer_cast<CollectionControlBlock>(
        (*in_rel.related_flow())->control_block
      );
      in_flow = std::make_shared<Flow>(
        std::make_shared<ControlBlock>(use->get_handle(), nullptr)
      );
      in_flow->ready_trigger.increment_count();
      coll_cntrl->current_published_entries.evaluate_at(
        std::make_pair(
          *in_rel.version_key(), in_rel.index()
        ),
        [in_flow](PublicationTableEntry& entry) {
          if(not entry.entry) {
            entry.entry = std::make_shared<PublicationTableEntry::Impl>();
          }
          entry.entry->fetching_trigger.add_action([entry_entry=entry.entry, in_flow]{
            in_flow->control_block->data =
              (*entry_entry->source_flow)->control_block->data;
            in_flow->ready_trigger.decrement_count();
          });
        }
      );

      break;
    }
    case FlowRelationship::Forwarding : {
      assert(in_rel.related_flow());
      // This should just work the same way as Same
      in_flow = *in_rel.related_flow();
      break;
    }
    default : {
      assert(false); // not implemented description
    }
  } // end switch over in flow relationship

  use->set_in_flow(in_flow);

  // </editor-fold> end in flow relationship }}}2
  //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  // <editor-fold desc="anti-in flow relationship"> {{{2

  std::shared_ptr<AntiFlow> anti_in_flow = nullptr;

  auto const& anti_in_rel = use->get_anti_in_flow_relationship();

  switch (anti_in_rel.description()) {
    case FlowRelationship::Insignificant :
    case FlowRelationship::InsignificantCollection : {
      anti_in_flow = nullptr;
      break;
    }
    case FlowRelationship::Same :
    case FlowRelationship::SameCollection : {
      assert(anti_in_rel.related_anti_flow());
      anti_in_flow = *anti_in_rel.related_anti_flow();
      break;
    }
    case FlowRelationship::Next :
    case FlowRelationship::NextCollection : {
      assert(anti_in_rel.related_anti_flow());
      // The related anti-flow is unused for now, but we should still assert that it's there
      anti_in_flow = std::make_shared<AntiFlow>();
      break;
    }
    case FlowRelationship::Forwarding : {
      assert(anti_in_rel.related_anti_flow());
      // The related anti-flow is unused for now, but we should still assert that it's there
      assert(*anti_in_rel.related_anti_flow());
      anti_in_flow = std::make_shared<AntiFlow>(
        1 // start with a count so that the flow we forwarded it from can make it ready
      );
      // The forwarded is anti-flow actually ready as soon as it's created, even
      // though the related anti-flow will not be ready (since it needs to be held
      // by the continuation for the purposes of the outer scope anti-dependency).
      // We should decrement the count here to make the new anti-flow ready now:
      anti_in_flow->ready_trigger.decrement_count();
      break;
    }
    case FlowRelationship::IndexedLocal : {
      assert(anti_in_rel.related_anti_flow());
      // Only create an indexed local version if the collection flow isn't insignificant
      if(*anti_in_rel.related_anti_flow()) {
        anti_in_flow = std::make_shared<AntiFlow>(
          1 // start with a count so that the collection flow can make it ready
        );
        (*anti_in_rel.related_anti_flow())->ready_trigger.add_action([anti_in_flow] {
          anti_in_flow->ready_trigger.decrement_count();
        });
      }
      break;
    }
    default : {
      // None of the other cases should be used for now
      assert(false); // not implemented description
    }
  } // end switch over anti-in flow relationship

  use->set_anti_in_flow(anti_in_flow);
  if(anti_in_flow and not use->is_anti_dependency()) {
    anti_in_flow->ready_trigger.increment_count();
  }

  // </editor-fold> end in flow relationship }}}2
  //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  // <editor-fold desc="out flow relationship"> {{{2

  auto const& out_rel = use->get_out_flow_relationship();
  auto* out_related = out_rel.related_flow();
  if(out_rel.use_corresponding_in_flow_as_related()) {
    assert(in_flow);
    out_related = &in_flow;
  }

  std::shared_ptr<Flow> out_flow = nullptr;

  switch (out_rel.description()) {
    case FlowRelationship::Insignificant :
    case FlowRelationship::InsignificantCollection : {
      use->set_out_flow(nullptr);
      break;
    }
    case FlowRelationship::Same :
    case FlowRelationship::SameCollection : {
      assert(out_related);
      out_flow = *out_related;
      break;
    }
    case FlowRelationship::Next :
    case FlowRelationship::NextCollection : {
      assert(out_related);
      out_flow = std::make_shared<Flow>((*out_related)->control_block);
      break;
    }
    case FlowRelationship::Null :
    case FlowRelationship::NullCollection : {
      assert(in_flow);
      out_flow = std::make_shared<Flow>(in_flow->control_block);
      break;
    }
    case FlowRelationship::IndexedLocal : {
      assert(out_related);
      // Indexed local out doesn't need a control block
      auto out_flow_related = *out_related;
      out_flow_related->ready_trigger.increment_count();
      out_flow = std::make_shared<Flow>(std::make_shared<ControlBlock>(nullptr));
      out_flow->ready_trigger.add_action([out_flow_related]{
        out_flow_related->ready_trigger.decrement_count();
      });
      break;
    }
    default : {
      // None of the others should be used for now
      assert(false); // not implemented description
    }
  } // end switch over in flow relationship

  if(out_flow) {
    // It's being used as an out, so make it not ready until this is released
    out_flow->ready_trigger.increment_count();
    use->set_out_flow(out_flow);
  }

  // </editor-fold> end out flow relationship }}}2
  //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  // <editor-fold desc="anti-out flow relationship"> {{{2

  std::shared_ptr<AntiFlow> anti_out_flow = nullptr;

  auto const& anti_out_rel = use->get_anti_out_flow_relationship();
  auto anti_out_related_flow = anti_out_rel.related_flow();
  auto anti_out_related_anti_flow = anti_out_rel.related_anti_flow();
  if(anti_out_rel.use_corresponding_in_flow_as_related()) {
    anti_out_related_flow = &in_flow;
  }
  if(anti_out_rel.use_corresponding_in_flow_as_anti_related()) {
    anti_out_related_anti_flow = &anti_in_flow;
  }

  switch (anti_out_rel.description()) {
    case FlowRelationship::Insignificant :
    case FlowRelationship::InsignificantCollection : {
      anti_out_flow = nullptr;
      break;
    }
    case FlowRelationship::Initial :
    case FlowRelationship::InitialCollection : {
      anti_out_flow = std::make_shared<AntiFlow>();
      break;
    }
    case FlowRelationship::Same :
    case FlowRelationship::SameCollection : {
      assert(anti_out_rel.related_anti_flow());
      anti_out_flow = *anti_out_related_anti_flow;
      break;
    }
    case FlowRelationship::Next :
    case FlowRelationship::NextCollection : {
      assert(anti_out_related_anti_flow);
      assert(*anti_out_related_anti_flow);
      // The related anti-flow is unused for now, but we should still assert that it's there
      anti_out_flow = std::make_shared<AntiFlow>();
      break;
    }
    case FlowRelationship::IndexedLocal : {
      assert(anti_out_related_anti_flow);
      assert(*anti_out_related_anti_flow);
      auto anti_out_flow_related = *anti_out_related_anti_flow;
      anti_out_flow_related->ready_trigger.increment_count();
      anti_out_flow = std::make_shared<AntiFlow>();
      anti_out_flow->ready_trigger.add_action([anti_out_flow_related] {
        anti_out_flow_related->ready_trigger.decrement_count();
      });
      break;
    }
    case FlowRelationship::IndexedFetching : {
      // We need to get the published entries from the in flow
      assert(in_rel.related_flow());
      auto coll_cntrl = std::static_pointer_cast<CollectionControlBlock>(
        (*in_rel.related_flow())->control_block
      );
      anti_out_flow = std::make_shared<AntiFlow>();

      coll_cntrl->current_published_entries.evaluate_at(
        std::make_pair(
          *anti_out_rel.version_key(), anti_out_rel.index()
        ),
        [anti_out_flow](PublicationTableEntry& entry) {
          assert(entry.entry);
          anti_out_flow->ready_trigger.add_action([entry_entry=entry.entry]{
            entry_entry->release_trigger.decrement_count();
          });
        }
      );
      break;
    }
    default : {
      // None of the others should be used for now
      assert(false); // not implemented description
    }
  } // end switch over anti-in flow relationship

  if(anti_out_flow) {
    // It's being used as an out, so make it not ready until this is released
    anti_out_flow->ready_trigger.increment_count();
    use->set_anti_out_flow(anti_out_flow);
  }

  // </editor-fold> end anti_out flow relationship }}}2
  //----------------------------------------------------------------------------

}

