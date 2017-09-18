/*
//@HEADER
// ************************************************************************
//
//                      pending_operation.hpp
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

#ifndef DARMASIMPLEBACKEND_PENDING_OPERATION_HPP
#define DARMASIMPLEBACKEND_PENDING_OPERATION_HPP

#include <tuple>

#include <data_structures/join_counter.hpp>
#include <runtime/runtime.hpp>

#include "ready_operation.hpp"

namespace simple_backend {

struct PendingOperation {

  protected:

    std::shared_ptr<JoinCounter> ready_event_;

  public:

    template <typename EventT>
    int
    add_dependency_on(EventT&& prereq) {
      ready_event_->increment_count();
      prereq->attach_action([ready_event=ready_event_]{
        ready_event->decrement_count();
      });
      return 0; // just for fold expression emulation
    }

    PendingOperation() : ready_event_(std::make_shared<JoinCounter>(1)) { }

    void make_ready() {
      ready_event_->decrement_count();
    }

    template <
      typename ReadyOperationPtr
    >
    void
    enqueue_or_run(
      ReadyOperationPtr&& ready_op,
      int worker_id,
      bool allow_run_on_stack=true
    ) {
      ++Runtime::instance->pending_tasks;
      allow_run_on_stack = allow_run_on_stack && (
        Runtime::thread_stack_depth < Runtime::instance->max_task_depth
        // also only run on the stack if we're enqueueing on the current thread
        && worker_id == Runtime::this_worker_id
      );
      if(allow_run_on_stack) {
        ready_event_->attach_or_do_action(
          // action to add
          [this](ReadyOperationPtr&& op){
            Runtime::instance->workers[Runtime::this_worker_id].ready_tasks.emplace(
              std::forward<ReadyOperationPtr>(op)
            );
            --Runtime::instance->pending_tasks;
            delete this;
          },
          // action to do
          [this](ReadyOperationPtr&& op){
            // If we're running directly on the stack, increment the thread
            // stack depth counter to make sure we don't blow the stack
            ++Runtime::thread_stack_depth;
            Runtime::instance->workers[Runtime::this_worker_id].run_operation(
              std::forward<ReadyOperationPtr>(op)
            );
            --Runtime::thread_stack_depth;
            --Runtime::instance->pending_tasks;
            delete this;
          },
          std::forward<ReadyOperationPtr>(ready_op)
        );
      }
      else {
        ready_event_->attach_action(
          // action to add
          [this](ReadyOperationPtr&& op){
            Runtime::instance->workers[Runtime::this_worker_id].ready_tasks.emplace(
              std::forward<ReadyOperationPtr>(op)
            );
            --Runtime::instance->pending_tasks;
            delete this;
          },
          std::forward<ReadyOperationPtr>(ready_op)
        );
      }
    }

};


namespace detail {

/**
 *  Helper that pops the callable off of the end
 */
template <
  size_t... EventIdxs,
  typename... EventsAndCallable
>
void
_when_all_helper(
  std::integer_sequence<size_t, EventIdxs...>,
  EventsAndCallable&&... triggers_and_callable
) {
  auto self_deleting_object = new PendingOperation();
  std::make_tuple( // just for fold emulation
    self_deleting_object->add_dependency_on(
      std::get<EventIdxs>(std::forward_as_tuple(
        std::forward<EventsAndCallable>(triggers_and_callable)...
      ))
    )...
  );
  self_deleting_object->make_ready();
  self_deleting_object->enqueue_or_run(
    simple_backend::make_callable_operation(
      std::get<sizeof...(EventIdxs)>(
        std::forward_as_tuple(
          std::forward<EventsAndCallable>(triggers_and_callable)...
        )
      )
    ),
    simple_backend::Runtime::this_worker_id
  );
}

} // end namespace detail

template <typename... EventsAndCallable>
auto
when_all_ready_do(EventsAndCallable&&... args) {
  // Call a helper that extracts the last variadic argument
  return detail::_when_all_helper(
    std::make_index_sequence<sizeof...(EventsAndCallable) - 1>{},
    std::forward<EventsAndCallable>(args)...
  );
}

template <typename Event, typename Callable>
auto
when_ready_do(Event&& trigger, Callable&& callable) {
  // call the same helper as when_all_do
  return detail::_when_all_helper(
    std::make_index_sequence<1>{},
    std::forward<Event>(trigger), std::forward<Callable>(callable)
  );
}

template <typename EventContainer, typename Callable>
auto
when_all_container_do(
  EventContainer&& trigger_container,
  Callable&& callable
) {
  auto self_deleting_object = new PendingOperation();
  for(auto&& trigger : trigger_container) {
    self_deleting_object->add_dependency_on(trigger);
  }
  self_deleting_object->make_ready();
  self_deleting_object->enqueue_or_run(
    std::forward<Callable>(callable),
    simple_backend::Runtime::this_worker_id
  );
}

inline void
make_pending_task(
  darma_runtime::abstract::backend::runtime_t::task_unique_ptr&& task,
  int worker_id = Worker::NO_WORKER_ID
) {
  if(worker_id == Worker::NO_WORKER_ID) {
    worker_id = simple_backend::Runtime::this_worker_id;
  }
  auto self_deleting_object = new PendingOperation();
  for(auto dep : task->get_dependencies()) {
    // Dependencies
    if(dep->is_dependency()) {
      // TODO commutative
      //if(dep->immediate_permissions() == use_t::Commutative) {
      //  commutative_locks_to_obtain.emplace_back(std::ref(dep->get_in_flow()->commutative_mtx));
      //  commutative_in_flows.emplace_back(dep->get_in_flow());
      //}
      self_deleting_object->add_dependency_on(dep->get_in_flow()->get_ready_trigger());
    }

    // Antidependencies
    if(dep->is_anti_dependency()) {
      assert(dep->get_anti_in_flow());
      self_deleting_object->add_dependency_on(dep->get_anti_in_flow()->get_ready_trigger());
    }
  }

  // Decrement the "setup dependency" of the pending operation object
  self_deleting_object->make_ready();

  // for now, data parallel tasks can't run directly on the stack
  bool allow_on_stack = not task->is_data_parallel_task();

  self_deleting_object->enqueue_or_run(
    std::make_unique<ReadyTaskOperation>(std::move(task)),
    worker_id,
    allow_on_stack
  );

};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_PENDING_OPERATION_HPP
