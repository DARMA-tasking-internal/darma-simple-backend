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

#include <random>

#include "runtime.hpp"
#include "flow.hpp"
#include "worker.hpp"

#include <darma/interface/frontend/top_level.h>

#include <darma.h>

using namespace simple_backend;

std::unique_ptr<Runtime> Runtime::instance = nullptr;

static thread_local darma_runtime::abstract::frontend::Task* running_task = nullptr;
static thread_local std::size_t this_worker_id = 0;

void
Runtime::initialize_top_level_instance(int argc, char** argv) {
  // TODO parse number of threads out of command line arguments
  auto top_level_task = darma_runtime::frontend::darma_top_level_setup(argc, argv);
  instance = std::make_unique<Runtime>(std::move(top_level_task), 8);
}

//==============================================================================

void
Runtime::wait_for_top_level_instance_to_shut_down() {
  for(int i = 1; i < instance->nthreads_; ++i) {
    instance->workers[i].join();
  }
}

//==============================================================================

// Construct from a task that is ready to run
Runtime::Runtime(task_unique_ptr&& top_level_task, std::size_t nthreads)
  : nthreads_(nthreads), shutdown_trigger(1)
{
  shutdown_trigger.add_action([this]{
    for(int i = 0; i < nthreads_; ++i) {
      workers[i].ready_tasks.emplace_back(nullptr);
    }
  });

  // Create the workers
  for(size_t i = 0; i < nthreads_; ++i) {
    workers.emplace_back(i);
  }
  workers[0].ready_tasks.emplace_back(std::move(top_level_task));
}

//==============================================================================

Runtime::task_t*
Runtime::get_running_task() const { return running_task; }

//==============================================================================

void
Runtime::register_task(task_unique_ptr&& task) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  // PendingTaskHolder deletes itself, so this isn't a memory leak
  auto* holder = new PendingTaskHolder(std::move(task));
  holder->enqueue_or_run();
}

//==============================================================================

void
Runtime::register_task_collection(task_collection_unique_ptr&& tc) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  for(size_t i = 0; i < tc->size(); ++i) {
    // Enqueueing another task, so increment shutdown trigger
    shutdown_trigger.increment_count();

    // PendingTaskHolder deletes itself, so this isn't a memory leak
    auto* holder = new PendingTaskHolder(tc->create_task_for_index(i));

    holder->enqueue_or_run((this_worker_id + i) % nthreads_,
      // Prevent immediate execution so that all tc indices get spawned
      // and have a chance to run concurrently
      // TODO this should be dependent on some lookahead variable
      /* allow_run_on_stack = */ false
    );
  }

  tc = nullptr;
  shutdown_trigger.decrement_count();
}

//==============================================================================

void
Runtime::spin_up_worker_threads()
{
  // needs to be two seperate loops to make sure ready tasks is initialized on
  // all workers.  Also, only spawn threads on 1-n
  for(size_t i = 1; i < nthreads_; ++i) {
    workers[i].spawn_work_loop(nthreads_);
  }

  workers[0].run_work_loop(nthreads_);
}

//==============================================================================

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
        std::make_shared<ControlBlock>(
          coll_cntrl->data_for_index(in_rel.index()),
          coll_cntrl.get(),
          in_rel.index()
        ),
        1 // start with a count so that the collection flow can make it ready
      );
      (*in_rel.related_flow())->trigger.add_action([in_flow] {
        in_flow->trigger.decrement_count();
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
      in_flow->trigger.increment_count();
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
            in_flow->trigger.decrement_count();
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
    case FlowRelationship::AntiIndexedFetching : {
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
          anti_out_flow->trigger.add_action([entry_entry=entry.entry]{
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

void
Runtime::publish_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& pub_use,
  darma_runtime::abstract::frontend::PublicationDetails* details
) {
  assert(pub_use->get_in_flow()->control_block->parent_collection);

  auto* parent_cntrl = pub_use->get_in_flow()->control_block->parent_collection;

  parent_cntrl->current_published_entries.evaluate_at(
    std::make_pair(
      details->get_version_name(),
      pub_use->get_in_flow()->control_block->collection_index
    ),
    [this, details, &pub_use] (PublicationTableEntry& entry) {
      if(not entry.entry) {
        entry.entry = std::make_shared<PublicationTableEntry::Impl>();
      }
      entry.entry->release_trigger.advance_count(details->get_n_fetchers());
      *entry.entry->source_flow = pub_use->get_in_flow();

      auto& pub_anti_out = pub_use->get_anti_out_flow();
      pub_anti_out->trigger.increment_count();
      entry.entry->release_trigger.add_action([
        this, pub_anti_out, pub_use=std::move(pub_use)
      ]{
        pub_anti_out->trigger.decrement_count();
        release_use(
          darma_runtime::abstract::frontend::use_cast<
            darma_runtime::abstract::frontend::UsePendingRelease*
          >(pub_use.get())
        );
      });

      entry.entry->fetching_trigger.decrement_count();
    }
  );

}


//==============================================================================

Runtime::PendingTaskHolder::PendingTaskHolder(task_unique_ptr&& task)
  : task_(std::move(task)),
    trigger_(task_->get_dependencies().size() * 2)
{ }

void Runtime::PendingTaskHolder::enqueue_or_run() {
  enqueue_or_run(this_worker_id);
}

void Runtime::PendingTaskHolder::enqueue_or_run(
  size_t worker_id,
  bool allow_run_on_stack
) {

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

  if(allow_run_on_stack and worker_id == this_worker_id) {
    trigger_.add_or_do_action(
      // If all of the dependencies and antidependencies aren't ready, place it on
      // the queue when it becomes ready
      [this, worker_id] {
        Runtime::instance->workers[worker_id].ready_tasks.emplace_front(std::move(task_));
        delete this;
      },
      // Otherwise, just run it in place
      // TODO enforce a maximum stack descent depth
      [this, worker_id] {
        Runtime::instance->workers[worker_id].run_task(std::move(task_));
        delete this;
      }
    );
  }
  else {
    trigger_.add_action(
      // If all of the dependencies and antidependencies aren't ready, place it on
      // the queue when it becomes ready
      [this, worker_id] {
        Runtime::instance->workers[worker_id].ready_tasks.emplace_front(std::move(task_));
        delete this;
      }
    );
  }
}

//==============================================================================

void Worker::run_task(Runtime::task_unique_ptr&& task) {

  // setup data
  for(auto&& dep : task->get_dependencies()) {
    if(dep->immediate_permissions() != Runtime::use_t::None) {
      dep->get_data_pointer_reference() = dep->get_in_flow()->control_block->data;
    }
  }
  // set the running task
  auto* old_running_task = running_task;
  running_task = task.get();

  task->run();

  // Delete the task object
  task = nullptr;

  // Reset the running task ptr
  running_task = old_running_task;

  Runtime::instance->shutdown_trigger.decrement_count();

}

void Worker::run_work_loop(size_t n_threads_total) {

  std::random_device rd;
  std::seed_seq seed{ rd(), rd(),rd(), rd(), rd(), rd(), rd(), rd(), rd() };
  std::mt19937 steal_gen(seed);
  std::uniform_int_distribution<> steal_dis(n_threads_total - 1);

  this_worker_id = id;

  while(true) {
    auto ready = ready_tasks.get_and_pop_front();
    // If there are any tasks on the front of our queue, get them
    if(ready) {
      // pop_front was successful, run the task

      // if it's null, this is the signal to stop the workers
      if(ready->get() == nullptr) { break; }
      else { run_task(std::move(*ready.get())); }

    } // end if any ready tasks exist
    else {
      // pop_front failed because queue was empty; try to do a steal
      auto steal_from = (steal_dis(steal_gen) + id) % n_threads_total;

      auto new_ready = Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_back();
      if(new_ready) {
        if (new_ready->get() == nullptr) {
          // oops, we stole the termination signal.  Put it back!
          Runtime::instance->workers[steal_from].ready_tasks.emplace_back(
            nullptr
          );
        } else {
          run_task(std::move(*new_ready.get()));
        }
      }
    }
  } // end while true loop

}

void Worker::spawn_work_loop(size_t n_threads_total) {

  thread_ = std::make_unique<std::thread>([this, n_threads_total]{
    run_work_loop(n_threads_total);
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
