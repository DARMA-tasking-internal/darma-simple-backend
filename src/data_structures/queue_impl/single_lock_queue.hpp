/*
//@HEADER
// ************************************************************************
//
//                      single_lock.hpp
//                         DARMA
//              Copyright (C) 2018 Sandia Corporation
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

#ifndef DARMASIMPLEBACKEND_SINGLE_LOCK_QUEUE_HPP
#define DARMASIMPLEBACKEND_SINGLE_LOCK_QUEUE_HPP

#include <data_structures/queue.hpp>

#include <memory>
#include <mutex>
#include <cassert>


namespace simple_backend {
namespace data_structures {

template <
  typename T, typename Mutex, typename Allocator
>
class SingleLockThreadSafeQueue
  : public ThreadSafeQueue<SingleLockThreadSafeQueue<T, Mutex, Allocator>>
{
  public:

    using value_type = T;

    template <typename ConcreteT>
    friend class ThreadSafeQueue;

  private:

    struct Node {
      std::unique_ptr<Node> next = nullptr;
      std::unique_ptr<T> value;
      template <typename... Args>
      explicit
      Node(
        Args&&... args
      ) : next(nullptr),
          value(std::make_unique<T>(
            std::forward<Args>(args)...
          ))
      { }
    };

    std::unique_ptr<Node> head_ = nullptr;
    Node* tail_ = nullptr;

    std::unique_ptr<Mutex> mutex_ = nullptr;

    using lock_guard_t = std::lock_guard<Mutex>;


  public:

    // TODO allocator support

    SingleLockThreadSafeQueue()
      : mutex_(std::make_unique<Mutex>())
    { }

    explicit
    SingleLockThreadSafeQueue(size_t to_reserve)
      : mutex_(std::make_unique<Mutex>())
    { }

    SingleLockThreadSafeQueue(SingleLockThreadSafeQueue&& other)
      : mutex_(std::move(other.mutex_)),
        head_(std::move(other.head_)),
        tail_(other.tail_)
    {
      other.head_ = nullptr;
      other.tail_ = nullptr;
      other.mutex_ = nullptr;
    }

    ~SingleLockThreadSafeQueue() {
      if(mutex_) {
        lock_guard_t _lg(*mutex_);
        // Should trigger all destructors:
        head_ = nullptr;
      }
      else {
        assert(!head_);
      }
    }

  private:

    template <typename... Args>
    bool
    _emplace(Args&&... args) {
      lock_guard_t _lg(*mutex_);
      if(head_ == nullptr) {
        head_ = std::make_unique<Node>(std::forward<Args>(args)...);
        tail_ = head_.get();
      }
      else {
        assert(tail_->next == nullptr);
        tail_->next = std::make_unique<Node>(std::forward<Args>(args)...);
        tail_ = tail_->next.get();
      }
      return true;
    }

    bool
    _push(value_type const& to_push) {
      return emplace(to_push);
    }

    template <typename Callable>
    bool
    _consume_one(Callable&& callable) {
      lock_guard_t _lg(*mutex_);
      if(head_ == nullptr) {
        return false;
      }

      std::unique_ptr<value_type> value = nullptr;
      std::swap(value, head_->value);
      if(tail_ == head_.get()) {
        tail_ = nullptr;
      }
      std::swap(head_, head_->next);

      std::forward<Callable>(callable)(
        std::move(*value.get())
      );

      return true;
    }

    template <typename Callable>
    size_t
    _consume_all(Callable&& callable) {
      size_t consumed = 0;
      while(this->consume_one(std::forward<Callable>(callable))) {
        ++consumed;
      }
      return consumed;
    }

    template <typename U>
    inline bool
    _pop(U& popped) {
      return this->consume_one([&](value_type&& val) {
        popped = std::move(val);
      });
    }
};

} // end namespace data_structures

} // end namespace simple_backend


#endif //DARMASIMPLEBACKEND_SINGLE_LOCK_QUEUE_HPP
