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
#include "util.hpp"
#include "debug.hpp"

#include <darma/interface/frontend/top_level.h>

#include <darma.h>

using namespace simple_backend;

std::unique_ptr<Runtime> Runtime::instance = nullptr;

static thread_local darma_runtime::abstract::frontend::Task* running_task = nullptr;
static thread_local std::size_t this_worker_id = 0;


void
Runtime::initialize_top_level_instance(int argc, char** argv) {

  SimpleBackendOptions options;

  auto top_level_task = darma_runtime::frontend::darma_top_level_setup(
    options.parse_args(argc, argv)
  );

  instance = std::make_unique<Runtime>(std::move(top_level_task),
    options.n_threads,
    options.lookahead
  );
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
Runtime::Runtime(task_unique_ptr&& top_level_task, std::size_t nthreads, std::size_t lookahead)
  : nthreads_(nthreads), shutdown_trigger(1), lookahead_(lookahead)
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
  holder->enqueue_or_run(
    // only run on the stack if the number of pending tasks is greater than
    // or equal to the lookahead
    pending_tasks_.load() >= lookahead_
  );
}

//==============================================================================

void
Runtime::register_task_collection(task_collection_unique_ptr&& tc) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  tc->set_task_collection_token(std::make_shared<TaskCollectionToken>(tc->size()));

  for(size_t i = 0; i < tc->size(); ++i) {
    // Enqueueing another task, so increment shutdown ready_trigger
    shutdown_trigger.increment_count();

    // PendingTaskHolder deletes itself, so this isn't a memory leak
    auto* holder = new PendingTaskHolder(tc->create_task_for_index(i));

    if(
      not darma_runtime::detail::key_traits<darma_runtime::types::key_t>::key_equal{}(
        tc->get_name(),
        darma_runtime::make_key()
      )
    ) {
      // TODO do this faster
      holder->task_->set_name(
        darma_runtime::make_key(
          std::string("backend index ") + std::to_string(i) + std::string(" of "),
          tc->get_name()
        )
      );
    }

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
Runtime::publish_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& pub_use,
  darma_runtime::abstract::frontend::PublicationDetails* details
) {
  assert(pub_use->get_in_flow()->control_block->parent_collection);

  auto parent_cntrl = pub_use->get_in_flow()->control_block->parent_collection;

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
      pub_anti_out->ready_trigger.increment_count();
      entry.entry->release_trigger.add_action([
        this, pub_anti_out, pub_use=std::move(pub_use)
      ]{
        pub_anti_out->ready_trigger.decrement_count();
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
{
  ++Runtime::instance->pending_tasks_;
  _SIMPLE_DBG_DO([this](auto& state){
    state.pending_tasks.insert(task_.get());
  });
}

Runtime::PendingTaskHolder::~PendingTaskHolder() {
  --Runtime::instance->pending_tasks_;
}

void Runtime::PendingTaskHolder::enqueue_or_run(bool allow_run_on_stack) {
  enqueue_or_run(this_worker_id, allow_run_on_stack);
}

struct ObtainExclusiveAccessAction {
  Runtime::PendingTaskHolder& task_holder;
  std::vector<std::reference_wrapper<std::mutex>> comm_locks;
  std::vector<std::shared_ptr<Flow>> comm_in_flows;
  std::size_t worker_id;
  bool allow_run_on_stack;

  ObtainExclusiveAccessAction(
    Runtime::PendingTaskHolder& task_holder,
    std::vector<std::reference_wrapper<std::mutex>>&& comm_locks,
    std::vector<std::shared_ptr<Flow>>&& comm_in_flows,
    std::size_t worker_id,
    bool allow_run_on_stack
  ) : task_holder(task_holder),
      comm_locks(std::move(comm_locks)),
      comm_in_flows(std::move(comm_in_flows)),
      worker_id(worker_id),
      allow_run_on_stack(allow_run_on_stack)
  { }

  ObtainExclusiveAccessAction(ObtainExclusiveAccessAction&&) = default;

  void operator()() {

    // TODO we probably should sort the locks before trying to lock them
    auto lock_result = simple_backend::try_lock(
      comm_locks.begin(), comm_locks.end()
    );
    if(lock_result == TryLockSuccess) {
      // enqueue the task and add all of the release actions to the in flows
      for(auto&& in_flow : comm_in_flows) {
        // we need to reset the trigger first so that it can be triggered
        // when the flow is released inside of the task
        in_flow->comm_in_flow_release_trigger.reset();
        in_flow->comm_in_flow_release_trigger.add_priority_action([in_flow] {
          in_flow->commutative_mtx.unlock();
        });
      }

      // Enqueue or run the task, since the task holder triggering is what
      // got us here
      if(allow_run_on_stack) {
        // TODO: theoretically we should delete the task holder before we
        //       run or enqueue task, but this could be tricky
        Runtime::instance->workers[worker_id].run_task(std::move(task_holder.task_));
        delete &task_holder;
      }
      else {
        Runtime::instance->workers[worker_id].ready_tasks.emplace_front(
          std::move(task_holder.task_)
        );
        delete &task_holder;
      }

    } // end if success
    else {
      // otherwise, enqueue the try-again action on the one that failed
      // This *does* race with the reset and the triggering of the release
      // as in flow trigger.
      comm_in_flows[lock_result]->comm_in_flow_release_trigger.add_action(
        std::move(*this)
      );
    }

  }

};

void Runtime::PendingTaskHolder::enqueue_or_run(
  size_t worker_id,
  bool allow_run_on_stack
) {

  // Commutative general strategy:
  // - try to obtain all locks with an iterator analog of std::try_lock
  // - if it fails, add an action that tries again to the ready_trigger list of the
  //   flow corresponding to the failed lock
  // - if it succeeds, add a triggered action to be run on release (we need a
  //   new ready_trigger list for this) that releases the locks and transfer ownership
  //   of the list to that action.
  std::vector<std::reference_wrapper<std::mutex>> commutative_locks_to_obtain;
  std::vector<std::shared_ptr<Flow>> commutative_in_flows;

  // Increment the trigger so that the corresponding decrement after finding
  // no dependencies will enqueue the task
  trigger_.increment_count();

  for(auto dep : task_->get_dependencies()) {
    // Dependencies
    if(dep->is_dependency()) {
      if(dep->immediate_permissions() == use_t::Commutative) {
        commutative_locks_to_obtain.emplace_back(std::ref(dep->get_in_flow()->commutative_mtx));
        commutative_in_flows.emplace_back(dep->get_in_flow());
      }
      dep->get_in_flow()->ready_trigger.add_action([this] {
        trigger_.decrement_count();
      });
    }
    else {
      trigger_.decrement_count();
    }

    // Antidependencies
    if(dep->is_anti_dependency()) {
      assert(dep->get_anti_in_flow());
      dep->get_anti_in_flow()->ready_trigger.add_action([this] {
          trigger_.decrement_count();
        }
      );
    }
    else {
      trigger_.decrement_count();
    }
  }

  // decrement indicating that we've looked at all of the dependencies; corresponds
  // to the increment before the for loop.
  trigger_.decrement_count();

  if(commutative_locks_to_obtain.size() > 0) {
    // When everything else is ready, we want to try and obtain exclusive
    // access to all commutative in-flows
    trigger_.add_action(
      ObtainExclusiveAccessAction(*this,
        std::move(commutative_locks_to_obtain),
        std::move(commutative_in_flows),
        worker_id,
        allow_run_on_stack
      )
    );
  }
  else {
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

  // CAREFUL!  Don't put anything down here because this object may not exist at this point
  // TODO also make sure all stack variables that might reference this are gone before the delete this executes

}

//==============================================================================

void Worker::run_task(Runtime::task_unique_ptr&& task) {

  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.insert(task.get());
    state.pending_tasks.erase(task.get());
  });

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

  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.erase(task.get());
  });

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
    // "Random" version:
    //auto ready = steal_dis(steal_gen) % 2 == 0 ?
    //  ready_tasks.get_and_pop_front() : ready_tasks.get_and_pop_back();

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
      // "Random" version:
      //auto new_ready = steal_dis(steal_gen) % 2 == 0 ?
      //  Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_back()
      //    : Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_front();
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
