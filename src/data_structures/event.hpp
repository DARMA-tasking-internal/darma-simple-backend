/*
//@HEADER
// ************************************************************************
//
//                      event.hpp
//                         DARMA
//              Copyright (C) 2017 Sandia Corporation
//
// Under the terms of Contract DE-NA-0003525 with NTESS, LLC,
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

#ifndef DARMASIMPLEBACKEND_EVENT_HPP
#define DARMASIMPLEBACKEND_EVENT_HPP

#include <config.hpp>

#include <data_structures/pointers.hpp>
#include <data_structures/queue.hpp>
#include <atomic>

namespace simple_backend {

struct ActionBase {
  virtual void run() = 0;
  virtual ~ActionBase(){}
};

class Event {

  private:

    using action_queue_t = types::thread_safe_queue_t<std::unique_ptr<ActionBase>>;
    using action_queue_ptr_t = types::thread_safe_unique_ptr_t<action_queue_t>;

    action_queue_ptr_t action_queue_ptr_;
    std::atomic_bool triggered_ = { false };

    void _do_actions() {
      // Exchange with an empty queue
      auto actions = action_queue_ptr_.exchange(
        std::make_unique<action_queue_t>()
      );
      actions->consume_all([&](std::unique_ptr<ActionBase>&& action) {
        action->run();
      });
    }

  public:

    template <typename Callable, typename... Args>
    std::shared_ptr<Event>
    attach_action(
      Callable&& callable,
      Args&&... args
    );

    template <typename AttachCallable, typename DoCallable, typename... Args>
    std::shared_ptr<Event>
    attach_or_do_action(
      AttachCallable&& attach_callable,
      DoCallable&& do_callable,
      Args&&... args
    );

    bool is_triggered(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) const {
      return triggered_.load(mem_order);
    }

    Event(Event const&) = delete;
    // Doesn't even need to be movable for now:
    Event(Event&&) = delete;

    ~Event() {
      if(triggered_.load()) {
        _do_actions();
      }
    }

  protected:

    Event()
      : action_queue_ptr_(
          std::make_unique<action_queue_t>()
        )
    { }

    void make_ready(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      triggered_.store(true, mem_order);
      _do_actions();
    }


};

class ActionCompletedEvent
  : public Event
{
  private:

    template <typename, typename...>
    friend class Action;

    friend class Event;

    void _action_finished() {
      this->make_ready();
    }

  public:

    ActionCompletedEvent() = default;

};

template <typename Callable, typename... Args>
class Action : public ActionBase {
  private:

    std::shared_ptr<ActionCompletedEvent> completion_event_;
    Callable callable_;
    std::tuple<Args...> args_;

    template <size_t... Idxs>
    void _run_impl(
      std::integer_sequence<size_t, Idxs...> /*unused*/
    ) {
      callable_(std::get<Idxs>(std::move(args_))...);
    }

  public:

    template <typename... ArgsDeduced>
    explicit
    Action(
      std::shared_ptr<ActionCompletedEvent> const& done_event,
      Callable&& callable,
      ArgsDeduced&&... args
    ) : completion_event_(done_event),
        callable_(std::move(callable)),
        args_(std::make_tuple(std::forward<ArgsDeduced>(args)...))
    { }

    Action(Action&&) = default;

    void run() override {
      _run_impl(std::index_sequence_for<Args...>{});
      completion_event_->_action_finished();
    }
};


template <typename Callable, typename... Args>
std::shared_ptr<Event>
Event::attach_action(
  Callable&& callable, Args&& ... args
) {
  auto done_event = std::make_shared<ActionCompletedEvent>();
  if(triggered_.load()) {
    _do_actions(); // just in case there are other actions that need to be flushed
    std::forward<Callable>(callable)(std::forward<Args&&>(args)...);
    done_event->_action_finished();
    // Since the attached action could delete this it is unsafe to do *anything*
    // here except use stack values and/or return
  }
  else {
    action_queue_ptr_.with_shared_access(
      [&](action_queue_t* queue) {
        queue->emplace(std::make_unique<Action<Callable, std::decay_t<Args>...>>(
          done_event,
          std::forward<Callable>(callable),
          std::forward<Args>(args)...
        ));
      }
    );
    // Since the attached action could delete this, and since a decrement could
    // race with anything we put here, it is unsafe to do *anything* here except
    // use stack values and/or return.  Any extra _do_actions() calls that need
    // to be made can be done in the destructor or upon any other calls to attach
  }
  return done_event;
}


template <typename AttachCallable, typename DoCallable, typename... Args>
std::shared_ptr<Event>
Event::attach_or_do_action(
  AttachCallable&& attach_callable, DoCallable&& do_callable, Args&& ... args
) {
  auto done_event = std::make_shared<ActionCompletedEvent>();
  if(triggered_.load()) {
    _do_actions(); // just in case there are other actions that need to be flushed
    std::forward<DoCallable>(do_callable)(std::forward<Args&&>(args)...);
    done_event->_action_finished();
    // Since the attached action could delete this it is unsafe to do *anything*
    // here except use stack values and/or return
  }
  else {
    action_queue_ptr_.with_shared_access(
      [&](action_queue_t* queue) {
        queue->emplace(std::make_unique<Action<AttachCallable, std::decay_t<Args>...>>(
          done_event,
          std::forward<AttachCallable>(attach_callable),
          std::forward<Args>(args)...
        ));
      }
    );
    // Since the attached action could delete this, and since a decrement could
    // race with anything we put here, it is unsafe to do *anything* here except
    // use stack values and/or return.  Any extra _do_actions() calls that need
    // to be made can be done in the destructor or upon any other calls to attach
  }
  return done_event;
}


class ManuallyTriggeredEvent
  : public Event
{
  public:

    void trigger_event(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      this->make_ready(mem_order);
    }

};


} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_EVENT_HPP
