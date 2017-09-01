/*
//@HEADER
// ************************************************************************
//
//                      mergeable_flow.hpp
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

#ifndef DARMASIMPLEBACKEND_FLOW_MERGEABLE_FLOW_HPP
#define DARMASIMPLEBACKEND_FLOW_MERGEABLE_FLOW_HPP

#include <shared_mutex>
#include <atomic>

#include <boost/lockfree/queue.hpp>
#include <data_structures/trigger.hpp>
#include <config.hpp>

namespace simple_backend {

// The object underlying both Flows and AntiFlows which is one layer of
// abstraction (via pointers) removed from the flows themselves, allowing
// for efficient implementation of aliasing
class MergeableTrigger {

  private:

    using shared_mutex_t = std::shared_timed_mutex;
    using shared_lock_t = std::shared_lock<shared_mutex_t>;
    using unique_lock_t = std::unique_lock<shared_mutex_t>;
    // items are deleted via a SelfDeleting action
    using action_queue_t = types::thread_safe_queue_t<TriggeredActionBase*>;

    // We need a shared pointer so that _do_actions() can race with the
    // destructor (or so that actions in the list can delete the MergeableTrigger),
    // and we need an atomic so that _do_actions() can race with add_action()
    // and with the aliasing operation.
    std::shared_ptr<std::atomic<action_queue_t*>> actions_ = { nullptr };

    shared_mutex_t aliasing_mutex_;
    // Semantics: once aliased_to_ becomes non-null, it doesn't change for the
    // rest of this object's lifetime
    std::shared_ptr<MergeableTrigger> aliased_to_ = nullptr;

    std::atomic<int> counter_ = { 0 };
    std::atomic<bool> triggered_ = { false };

    void _do_actions(shared_lock_t aliasing_lock) {
      // make a copy of the shared ptr, so that we can race with the destructor
      auto actions = actions_;
      if(actions == nullptr) return; // unlock happens via aliasing_lock destructor
      // get a view of some queue, either the one we were called with or the
      // one someone else swapped in (do an exchange so that we don't race with
      // the destructor
      auto* my_queue = new action_queue_t(1);
      my_queue = actions->exchange(my_queue);

      // as soon as we've exchanged with the action queue, we can release the
      // aliasing lock because it's safe for aliasing to happen
      aliasing_lock.unlock();

      // just start with consumed at some number larger than 0
      size_t consumed = 1;

      while(my_queue != nullptr and consumed > 0) {
        consumed = my_queue->consume_all([](TriggeredActionBase* action) {
          action->run();
        });
        my_queue = actions->exchange(my_queue);
      }

      if(my_queue != nullptr) {
        delete my_queue;
      }
    }

  public:

    explicit
    MergeableTrigger(int initial_count)
      : counter_(initial_count), actions_(
          std::make_shared<std::atomic<action_queue_t*>>(new action_queue_t(1))
        )
    { }

    // Note: add action *cannot* be called at any time after the destructor
    // could have been called
    template <typename Callable>
    void add_action(Callable&& callable) {
      // We can check if it's triggered first and just run on the stack if so
      if(triggered_.load()) {
        callable();
      }
      else {
        std::shared_lock<shared_mutex_t> aliasing_lock(aliasing_mutex_);
        // This only applies if the trigger was aliased while we were waiting to
        // obtain the aliasing_lock
        if(aliased_to_) {
          aliased_to_->add_action(std::forward<Callable>(callable));
        }
        else {
          actions_->load()->push(make_new_self_deleting_action(
            std::forward<Callable>(callable)
          ));

          if(triggered_.load()) { _do_actions(std::move(aliasing_lock)); }
        }

      } // release the aliasing_lock here if not already released
    }


    void increment_count() {
      // prevent aliasing while we increment (do we need this?)
      std::shared_lock<shared_mutex_t> aliasing_lock(aliasing_mutex_);
      // semantic assertion: triggers not allowed to increment after going to
      // zero once.  This isn't a bulletproof assertion, since setting triggered
      // and the last decrement of counter are separate operations
      assert(not triggered_.load());
      if(aliased_to_) {
        aliased_to_->increment_count();
      }
      else {
        ++counter_;
      }
    }

    void advance_count(std::size_t size) {
      // prevent aliasing while we increment
      shared_lock_t aliasing_lock(aliasing_mutex_);
      // semantic assertion: triggers not allowed to increment after going to
      // zero once.  This isn't a bulletproof assertion, since setting triggered
      // and the last decrement of counter are separate operations
      assert(not triggered_.load());
      if(aliased_to_) {
        aliased_to_->advance_count(size);
      }
      else {
        counter_ += size;
      }
    }

    void decrement_count() {
      // prevent aliasing while we decrement
      shared_lock_t aliasing_lock(aliasing_mutex_);
      // semantic assertion: triggers not allowed to decrement after going to
      // zero once.  This isn't a bulletproof assertion, since setting triggered
      // and the last decrement of counter are separate operations
      assert(not triggered_.load());

      if(aliased_to_) {
        aliased_to_->decrement_count();
      }
      else if(--counter_ == 0) {
        triggered_.store(true);
        _do_actions(std::move(aliasing_lock));
      }
    }

    // returns the pointer that it's actually aliased to
    std::shared_ptr<MergeableTrigger>
    alias_to(std::shared_ptr<MergeableTrigger> other) {
      unique_lock_t my_aliasing_lock(aliasing_mutex_, std::defer_lock);
      unique_lock_t other_aliasing_lock(other->aliasing_mutex_, std::defer_lock);
      std::lock(my_aliasing_lock, other_aliasing_lock);

      assert(!other->triggered_.load());

      // Make sure another aliasing operation didn't take place while we were
      // waiting for the lock (if it did, alias to that one instead)
      auto to_alias_to = other;
      while(to_alias_to->aliased_to_ != nullptr) {
        to_alias_to = to_alias_to->aliased_to_;
        // Release the old other aliasing lock (at the end of this scope) and
        // grab the new one
        unique_lock_t new_other_aliasing_lock(to_alias_to->aliasing_mutex_);
        std::swap(other_aliasing_lock, new_other_aliasing_lock);
      }

      // both are locked (so their counts can't change until we release the
      // locks), so we can add their counts together here
      to_alias_to->counter_ += counter_.exchange(0);

      auto* my_actions = actions_->exchange(new action_queue_t(1));
      assert(my_actions);
      assert(to_alias_to->actions_);
      assert(to_alias_to->actions_->load());

      to_alias_to->actions_->load()->push(make_new_self_deleting_action([my_actions]{
        my_actions->consume_all([](TriggeredActionBase* action){
          action->run();
        });
        delete my_actions;
      }));

      return to_alias_to;
      // Unique locks released at the end of this scope
    }


    // For approximate debugging purposes only
    std::size_t get_count() const { return counter_.load(); }

    // For approximate debugging purposes only
    bool get_triggered() const { return triggered_.load(); }


    ~MergeableTrigger() {
      auto actions = std::atomic_exchange(&actions_, std::make_shared<std::atomic<action_queue_t*>>());
      delete actions->load();
    }


};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_FLOW_MERGEABLE_FLOW_HPP
