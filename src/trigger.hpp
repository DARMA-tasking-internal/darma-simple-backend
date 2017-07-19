/*
//@HEADER
// ************************************************************************
//
//                      trigger.hpp
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

#ifndef DARMASIMPLECVBACKEND_TRIGGER_HPP
#define DARMASIMPLECVBACKEND_TRIGGER_HPP

#include <atomic>
#include <cassert>

#include "data_structures/concurrent_list.hpp"

namespace simple_backend {


struct TriggeredActionBase {
  virtual void run() =0;
  virtual ~TriggeredActionBase() = default;
};

template <typename Callable>
class TriggeredOnceAction
  : public TriggeredActionBase
{
  private:

    std::atomic_flag triggered_ = ATOMIC_FLAG_INIT;
    Callable callable_;

  public:

    explicit
    TriggeredOnceAction(Callable&& callable)
      : callable_(std::move(callable))
    { }

    void run() override {
      // TODO memory order
      if(not triggered_.test_and_set()) {
        callable_();
      }
    }
};

struct MultiActionList {

  std::atomic<ConcurrentDeque<std::unique_ptr<TriggeredActionBase>>*> actions_ = { nullptr };

  MultiActionList() : actions_(new ConcurrentDeque<std::unique_ptr<TriggeredActionBase>>())
  { }

  template <typename ActionUniquePtr>
  void add_action(ActionUniquePtr&& action_ptr) {
    actions_.load()->emplace_back(std::forward<ActionUniquePtr>(action_ptr));
  }

  template <typename ActionUniquePtr>
  void add_priority_action(ActionUniquePtr&& action_ptr) {
    actions_.load()->emplace_front(std::forward<ActionUniquePtr>(action_ptr));
  }

  void add_all_actions_from_list(MultiActionList& other_list) {
    auto* my_actions = actions_.load();
    auto* other_actions = other_list.actions_.load();
    my_actions->splice_back(*other_actions);
  }

  void do_actions() {
    // Allow an action that deletes the action list (or just deletion of this
    // during run) by moving the action list member onto the stack
    auto* action_list = actions_.exchange(
      new ConcurrentDeque<std::unique_ptr<TriggeredActionBase>>()
    );
    auto current_action = action_list->get_and_pop_front();
    while (current_action) {
      current_action->get()->run();
      current_action = action_list->get_and_pop_front();
    }
//
//    while(true) {
//      auto current_action = action_list->get_and_pop_front();
//      while (current_action) {
//        current_action->get()->run();
//        current_action = action_list->get_and_pop_front();
//      }
//      action_list = actions_.exchange(
//        new ConcurrentDeque<std::unique_ptr<TriggeredActionBase>>()
//      );
//      if (action_list->size() == 0) {
//        break;
//      }
//    }
  }

};

struct SingleAction {

  std::atomic<TriggeredActionBase*> action_ = { nullptr };

  template <typename ActionUniquePtr>
  void add_action(ActionUniquePtr&& action_ptr) {
    action_.store(action_ptr.release());
  }

  template <typename ActionUniquePtr>
  void add_priority_action(ActionUniquePtr&& action_ptr) {
    add_action(std::forward<ActionUniquePtr>(action_ptr));
  }

  void add_all_actions_from_list(SingleAction& other_list) {
    assert(false);
  }

  void do_actions() {
    TriggeredActionBase* action_once = action_.exchange(nullptr);
    if(action_once) {
      action_once->run();
      delete action_once;
    }
  }

};

template <typename Action>
struct SingleSpecificAction {

  std::atomic<Action*> action_ = { nullptr };

  template <typename ActionUniquePtr>
  void add_action(ActionUniquePtr&& action_ptr) {
    action_.store(action_ptr.release());
  }

  template <typename ActionUniquePtr>
  void add_priority_action(ActionUniquePtr&& action_ptr) {
    add_action(std::forward<ActionUniquePtr>(action_ptr));
  }

  void add_all_actions_from_list(SingleSpecificAction& other_list) {
    assert(false);
  }

  void do_actions() {
    Action* action_once = action_.exchange(nullptr);
    if(action_once) {
      action_once->run();
      delete action_once;
    }
  }

  ~SingleSpecificAction() {
    Action* action_to_delete = action_.exchange(nullptr);
    if(action_to_delete) {
      delete action_to_delete;
    }
  }

};


template <typename ActionList>
class CountdownTrigger {
  private:

    std::atomic<std::size_t> count_ = { 0 };
    std::atomic<bool> triggered_ = { false };
    ActionList actions_;

  public:

    CountdownTrigger(std::size_t initial_count)
      : count_(initial_count)
    { }

    template <typename Callable>
    void add_action(Callable&& callable) {
      if(triggered_.load()) {
        callable();
      }
      else {
        actions_.add_action(std::make_unique<
          TriggeredOnceAction<std::decay_t<Callable>>
        >(std::forward<Callable>(callable)));
        // If the ready_trigger happened while we were adding the callable, we
        // need to do the actions now (which might include this action)
        // since the whole queue of actions could have completed between
        // our check of triggered_.load() and now
        if(triggered_.load()) { actions_.do_actions(); }
      }
    }

    // Add the first action if the ready_trigger hasn't fired yet,
    // do the second action if it has
    template <typename CallableToAdd, typename CallableToDo>
    void add_or_do_action(
      CallableToAdd&& callable_to_add,
      CallableToDo&& callable_to_do
    ) {
      if(triggered_.load()) {
        callable_to_do();
      }
      else {
        actions_.add_action(std::make_unique<
          TriggeredOnceAction<std::decay_t<CallableToAdd>>
        >(std::forward<CallableToAdd>(callable_to_add)));
        // If the ready_trigger happened while we were adding the callable, we
        // need to do the actions now (which might include this action)
        // since the whole queue of actions could have completed between
        // our check of triggered_.load() and now
        if(triggered_.load()) { actions_.do_actions(); }
      }
    }

    void increment_count() {
      assert(not triggered_.load());
      ++count_;
    }

    void advance_count(std::size_t count) {
      count_.fetch_add(count);
    }

    void decrement_count() {
      assert(not triggered_.load());
      if(--count_ == 0) {
        // Ensures no tasks will be added to queue
        triggered_.store(true);
        actions_.do_actions();
      }
    }

    // YOU'RE RESPONSIBLE for calling do_actions
    std::pair<bool, ActionList&>
    decrement_count_and_return_actions_if_ready() {
      assert(not triggered_.load());
      if(--count_ == 0) {
        // Ensures no tasks will be added to queue
        triggered_.store(true);
        return std::pair<bool, ActionList&>(true, actions_);
      }
      else {
        return std::pair<bool, ActionList&>(false, actions_);
      }
    }

    // ONLY CALL THIS FROM WITHIN AN ACTION LIST ACTION
    void extend_action_list_from_within_action(
      ActionList& other_list
    ) {
      // Note: other_list is empty afterwards
      actions_.add_all_actions_from_list(other_list);
    }



    // For approximate debugging purposes only
    std::size_t get_count() const { return count_.load(); }

    // For approximate debugging purposes only
    bool get_triggered() const { return triggered_.load(); }
};


#if 0
// TODO finish implementing this
namespace detail {

template <typename Action>
struct WhenAllTriggersReadyInvoker {
  CountdownTrigger<SingleSpecificAction<Action>> action_trigger_ = { 0 };

  template <typename... Triggers>
  WhenAllTriggersReadyInvoker(Triggers&&... triggers)
    : action_trigger_(sizeof...(Triggers))
  {
    std::forward_as_tuple( // only for "faking" a fold operation
      triggers.add_action([this] { action_trigger_.decrement_count(); })...
    );
  }

};

} // end namespace detail
#endif


template <typename ActionList>
class ResettableBooleanTrigger {

  private:

    std::atomic<bool> triggered_ = { false };
    std::atomic<ActionList*> actions_ = { nullptr };

  public:

    ResettableBooleanTrigger()
      : actions_(new ActionList())
    { }

    ~ResettableBooleanTrigger() {
      delete actions_.load();
    }

    void activate() {
      triggered_.store(true);
      actions_.load()->do_actions();
    }

    // Note that activate and reset aren't allowed to race!
    void reset() {
      // Any actions that race with the reset will run the next time activate
      // is called
      // Need to do a swap here to avoid racing with action list completion
      // ActionList must be deletable while do_actions is running (potentially
      // from another thread)
      if(triggered_.load()) {
        ActionList* old_actions = actions_.exchange(new ActionList());
        old_actions->do_actions();
        // Safe to delete, since no one can add actions to it
        delete old_actions;
      }
      triggered_.store(false);
    }

    template <typename Callable>
    void add_action(Callable&& callable) {
      if(triggered_.load()) {
        callable();
      }
      else {
        actions_.load()->add_action(std::make_unique<
          TriggeredOnceAction<std::decay_t<Callable>>
        >(std::forward<Callable>(callable)));
        if(triggered_.load()) { actions_.load()->do_actions(); }
      }
    }

    template <typename Callable>
    void add_priority_action(Callable&& callable) {
      if(triggered_.load()) {
        callable();
      }
      else {
        actions_.load()->add_priority_action(std::make_unique<
          TriggeredOnceAction<std::decay_t<Callable>>
        >(std::forward<Callable>(callable)));
        if(triggered_.load()) { actions_.load()->do_actions(); }
      }
    }

};

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_TRIGGER_HPP
