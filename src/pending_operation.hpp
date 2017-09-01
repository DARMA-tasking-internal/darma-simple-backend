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

#include <data_structures/trigger.hpp>
#include <runtime/runtime.hpp>

#include "ready_operation.hpp"

namespace simple_backend {

struct PendingOperation {

  protected:

    CountdownTrigger<SingleAction> trigger_;

    template <typename Trigger>
    int
    _add_dependency_on(Trigger&& prereq) {
      trigger_.increment_count();
      prereq->add_action([this]{
        trigger_.decrement_count();
      });
      return 0; // just for fold expression emulation
    }

  public:

    // TODO technically these constructors should be private and operator new should be public

    template <
      typename... Triggers
    >
    PendingOperation(
      Triggers&&... prerequisites
    ) : trigger_(1)
    {
      std::make_tuple( // just for fold expression emulation
        _add_dependency_on(std::forward<Triggers>(prerequisites))...
      );
      // If there are no dependencies, this should make the operation ready
      // (even though it's not set yet)
      trigger_.decrement_count();
    }

    template <
      typename TriggerContainer
    >
    PendingOperation(
      safe_template_ctor_tag_t,
      TriggerContainer&& prereq_container
    ) : trigger_(1)
    {
      for(auto&& prereq : prereq_container) {
        _add_dependency_on(prereq);
      }
      // If there are no dependencies, this should make the operation ready
      // (even though it's not set yet)
      trigger_.decrement_count();
    }

    template <
      typename ReadyOperationPtr
    >
    void
    enqueue_or_run(
      ReadyOperationPtr&& ready_op,
      bool allow_run_on_stack=true
    ) {
      allow_run_on_stack = allow_run_on_stack && (
        Runtime::thread_stack_depth < Runtime::instance->max_task_depth
      );
      if(allow_run_on_stack) {
        trigger_.add_or_do_action(
          // action to add
          [this](ReadyOperationPtr&& op){
            Runtime::instance->workers[Runtime::this_worker_id].run_operation(
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
        trigger_.add_or_do_action(
          // action to add
          [this](ReadyOperationPtr&& op){
            Runtime::instance->workers[Runtime::this_worker_id].run_operation(
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

template <
  typename TriggerContainer,
  typename Callable
>
void
make_pending_operation(TriggerContainer&& prereqs, Callable&& callable) {
  ++Runtime::instance->pending_tasks;
  auto self_deleting_object = new PendingOperation(
    safe_template_ctor_tag,
    std::forward<TriggerContainer>(prereqs)
  );
  self_deleting_object->enqueue_or_run(
    make_callable_operation(std::forward<Callable>(callable))
  );
}

inline void
make_pending_task(
  darma_runtime::abstract::backend::runtime_t::task_unique_ptr&& task
) {
  using trigger_t = std::decay_t<decltype(darma_runtime::types::flow_t{}->get_ready_trigger())>;
  std::vector<trigger_t> prereqs;
  for(auto dep : task->get_dependencies()) {
    // Dependencies
    if(dep->is_dependency()) {
      // TODO commutative
      //if(dep->immediate_permissions() == use_t::Commutative) {
      //  commutative_locks_to_obtain.emplace_back(std::ref(dep->get_in_flow()->commutative_mtx));
      //  commutative_in_flows.emplace_back(dep->get_in_flow());
      //}
      prereqs.push_back(dep->get_in_flow()->get_ready_trigger());
    }

    // Antidependencies
    if(dep->is_anti_dependency()) {
      assert(dep->get_anti_in_flow());
      prereqs.push_back(dep->get_anti_in_flow()->get_ready_trigger());
    }
  }

  make_pending_operation(
    prereqs, std::make_unique<ReadyTaskOperation>(std::move(task))
  );

};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_PENDING_OPERATION_HPP
