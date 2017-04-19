/*
//@HEADER
// ************************************************************************
//
//                      runtime.cpp
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
// Questions? Contact David S. Hollman (dshollm@sandia.gov)
//
// ************************************************************************
//@HEADER
*/


#include "runtime.hpp"
#include "flow.hpp"

#include <darma/interface/frontend/top_level.h>

#include <darma.h>

using namespace simple_backend;

std::unique_ptr<Runtime> Runtime::instance = nullptr;

static thread_local darma_runtime::abstract::frontend::Task* running_task = nullptr;

void
Runtime::initialize_top_level_instance(int argc, char** argv) {
  // TODO parse number of threads out of command line arguments
  auto top_level_task = darma_runtime::frontend::darma_top_level_setup(argc, argv);
  instance = std::make_unique<Runtime>(std::move(top_level_task), 1);
}

//==============================================================================

void
Runtime::wait_for_top_level_instance_to_shut_down() {
  for(auto&& thr : instance->workers_) { thr.join(); }
}

//==============================================================================

// Construct from a task that is ready to run
Runtime::Runtime(task_unique_ptr&& top_level_task, std::size_t nthreads)
  : nthreads_(nthreads), shutdown_trigger_(1)
{
  shutdown_trigger_.add_action([this]{
    for(int i = 0; i < nthreads_; ++i) { ready_tasks_.emplace_back(nullptr); }
  });
  ready_tasks_.emplace_back(std::move(top_level_task));

  _spin_up_worker_threads();
}

//==============================================================================

Runtime::task_t*
Runtime::get_running_task() const { return running_task; }

//==============================================================================

void
Runtime::register_task(task_unique_ptr&& task) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger_.increment_count();

  // PendingTaskHolder deletes itself, so this isn't a memory leak
  new PendingTaskHolder(std::move(task));
}

void
Runtime::register_task_collection(task_collection_unique_ptr&& tc) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger_.increment_count();

  for(size_t i = 0; i < tc->size(); ++i) {
    register_task(tc->create_task_for_index(i));
  }

  tc = nullptr;
  shutdown_trigger_.decrement_count();
}

void
Runtime::_spin_up_worker_threads()
{
  workers_.reserve(nthreads_);
  for(int i = 0; i < nthreads_; ++i) {
    workers_.emplace_back([this]{

      while(true) {
        auto ready = ready_tasks_.get_and_pop_front();
        if(ready) {

          // if it's null, this is the signal to stop the workers
          if(ready->get() == nullptr) { break; }
          else {
            // setup data
            for(auto&& dep : ready->get()->get_dependencies()) {
              if(dep->immediate_permissions() != use_t::None) {
                dep->get_data_pointer_reference() =
                  dep->get_in_flow()->control_block->data;
              }
            }
            // set the running task
            running_task = ready->get();

            ready->get()->run();
            // Delete the task object
            *ready = nullptr;

            // Reset the running task ptr
            running_task = nullptr;

            shutdown_trigger_.decrement_count();
          }

        } // end if any ready tasks exist
      } // end while true loop

    });
  }
}


void
Runtime::register_use(use_pending_registration_t* use) {
  using namespace darma_runtime::abstract::frontend; // FlowRelationship

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
      in_flow->trigger.decrement_count();
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
      in_flow->trigger.decrement_count();
      break;
    }
    case FlowRelationship::IndexedLocal : {
      assert(in_rel.related_flow());
      auto coll_cntrl = std::static_pointer_cast<CollectionControlBlock>(
        (*in_rel.related_flow())->control_block
      );
      in_flow = std::make_shared<Flow>(
        std::make_shared<ControlBlock>(coll_cntrl->data_for_index(in_rel.index())),
        1 // start with a count so that the collection flow can make it ready
      );
      (*in_rel.related_flow())->trigger.add_action([in_flow] {
        in_flow->trigger.decrement_count();
      });
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
    case FlowRelationship::AntiIndexedLocal : {
      assert(anti_in_rel.related_anti_flow());
      // Only create an indexed local version if the collection flow isn't insignificant
      if(*anti_in_rel.related_anti_flow()) {
        anti_in_flow = std::make_shared<AntiFlow>(
          1 // start with a count so that the collection flow can make it ready
        );
        (*anti_in_rel.related_anti_flow())->trigger.add_action([anti_in_flow] {
            anti_in_flow->trigger.decrement_count();
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
      out_flow_related->trigger.increment_count();
      out_flow = std::make_shared<Flow>(std::make_shared<ControlBlock>(nullptr));
      out_flow->trigger.add_action([out_flow_related]{
        out_flow_related->trigger.decrement_count();
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
    out_flow->trigger.increment_count();
    use->set_out_flow(out_flow);
  }

  // </editor-fold> end out flow relationship }}}2
  //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  // <editor-fold desc="anti-out flow relationship"> {{{2

  std::shared_ptr<AntiFlow> anti_out_flow = nullptr;

  auto const& anti_out_rel = use->get_anti_in_flow_relationship();
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
    case FlowRelationship::Same :
    case FlowRelationship::SameCollection : {
      assert(anti_out_rel.related_anti_flow());
      anti_out_flow = *anti_out_related_anti_flow;
      break;
    }
    case FlowRelationship::AntiNext :
    case FlowRelationship::AntiNextCollection : {
      assert(anti_out_related_flow);
      anti_out_flow = std::make_shared<AntiFlow>();
      break;
    }
    case FlowRelationship::AntiIndexedLocal : {
      assert(anti_out_related_anti_flow);
      // Only create an indexed local version if the collection flow isn't insignificant
      if(*anti_out_related_anti_flow) {
        auto anti_out_flow_related = *anti_out_related_anti_flow;
        anti_out_flow_related->trigger.increment_count();
        anti_out_flow = std::make_shared<AntiFlow>();
        out_flow->trigger.add_action([anti_out_flow_related] {
          anti_out_flow_related->trigger.decrement_count();
        });
      }
      break;
    }
    default : {
      // None of the others should be used for now
      assert(false); // not implemented description
    }
  } // end switch over anti-in flow relationship

  if(anti_out_flow) {
    // It's being used as an out, so make it not ready until this is released
    anti_out_flow->trigger.increment_count();
    use->set_anti_out_flow(anti_out_flow);
  }

  // </editor-fold> end in flow relationship }}}2
  //----------------------------------------------------------------------------

}

//==============================================================================

void
Runtime::release_use(use_pending_release_t* use) {

  if(use->establishes_alias()) {
    assert(use->get_in_flow() and use->get_out_flow());
    auto& out_flow = use->get_out_flow();
    // Increment the count to indicate responsibility for all of the
    // producers of the in flow
    out_flow->trigger.increment_count();
    // Then decrement that count when the in flow becomes ready
    use->get_in_flow()->trigger.add_action([out_flow]{
      out_flow->trigger.decrement_count();
    });
  }

  if(use->get_out_flow()) use->get_out_flow()->trigger.decrement_count();

  if(use->get_anti_out_flow()) use->get_anti_out_flow()->trigger.decrement_count();

}

//==============================================================================

Runtime::PendingTaskHolder::PendingTaskHolder(task_unique_ptr&& task)
  : task_(std::move(task)),
    trigger_(task_->get_dependencies().size() * 2)
{
  for(auto dep : task_->get_dependencies()) {
    // Dependencies
    if(dep->get_in_flow() and dep->immediate_permissions() != use_t::None) {
      dep->get_in_flow()->trigger.add_action([this] {
        trigger_.decrement_count();
      });
    }
    else {
      trigger_.decrement_count();
    }

    // Antidependencies
    if(dep->get_anti_in_flow()) {
      dep->get_in_flow()->trigger.add_action([this] {
        trigger_.decrement_count();
      });
    }
    else {
      trigger_.decrement_count();
    }
  }
  trigger_.add_action([this] {
    Runtime::instance->ready_tasks_.emplace_front(std::move(task_));
    delete this;
  });
}

//==============================================================================

namespace darma_runtime {
namespace abstract {
namespace backend {

Context* get_backend_context() { return simple_backend::Runtime::instance.get(); }
Runtime* get_backend_runtime() { return simple_backend::Runtime::instance.get(); }
MemoryManager* get_backend_memory_manager() { return simple_backend::Runtime::instance.get(); }

} // end namespace backend
} // end namespace abstract
} // end namespace darma_runtime
