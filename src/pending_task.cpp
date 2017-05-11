/*
//@HEADER
// ************************************************************************
//
//                      pending_task.cpp
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
#include "util.hpp"

#include "debug.hpp"

using namespace simple_backend;

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

//==============================================================================

Runtime::PendingTaskHolder::~PendingTaskHolder() {
  --Runtime::instance->pending_tasks_;
}

//==============================================================================

void Runtime::PendingTaskHolder::enqueue_or_run(bool allow_run_on_stack) {
  enqueue_or_run(Runtime::this_worker_id, allow_run_on_stack);
}

//==============================================================================

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

//==============================================================================

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
