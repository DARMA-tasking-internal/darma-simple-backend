/*
//@HEADER
// ************************************************************************
//
//                      concurrent_list.hpp
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

#ifndef DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP
#define DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP

#include <mutex>
#include <deque>
#include <list>

namespace simple_backend {

// A simple, stand-in concurrent list implementation, for now
template <typename T>
class ConcurrentDeque {

  private:

    // Just put locks around everything for now.  We'll do something faster
    // some other time
    std::unique_ptr<std::mutex> mutex_;

    std::list<T> data_;

  public:

    ConcurrentDeque()
      : mutex_(std::make_unique<std::mutex>())
    { }

    ConcurrentDeque(ConcurrentDeque&& other)
      : mutex_(std::move(other.mutex_)),
        data_(std::move(other.data_))
    { }

    template <typename... Args>
    void emplace_front(Args&&... args) {
      std::lock_guard<std::mutex> lg(*mutex_);
      data_.emplace_front(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void
    emplace_back(Args&&... args) {
      std::lock_guard<std::mutex> lg(*mutex_);
      data_.emplace_back(std::forward<Args>(args)...);
    }

    void splice_back(ConcurrentDeque<T>& other_list) {
      std::unique_lock<std::mutex> my_lock_(*mutex_.get(), std::defer_lock);
      std::unique_lock<std::mutex> other_lock_(*other_list.mutex_.get(), std::defer_lock);
      std::lock(my_lock_, other_lock_);
      data_.splice(data_.end(), other_list.data_);
    }

    std::unique_ptr<T>
    get_and_pop_front() {
      std::lock_guard<std::mutex> lg(*mutex_);
      if(data_.empty()) return nullptr;
      else {
        auto rv = std::make_unique<T>(std::move(data_.front()));
        data_.pop_front();
        return std::move(rv);
      }
    }

    std::size_t
    size() const {
      std::lock_guard<std::mutex> lg(*mutex_);
      return data_.size();
    }

    std::unique_ptr<T>
    get_and_pop_back() {
      std::lock_guard<std::mutex> lg(*mutex_);
      if(data_.empty()) return nullptr;
      else {
        auto rv = std::make_unique<T>(std::move(data_.back()));
        data_.pop_back();
        return std::move(rv);
      }
    }
};



// Taken from "Simple, fast, and practical non-blocking and blocking concurrent
// queue algorithms" (doi: 10.1145/248052.248106)
//template <typename T>
//class MichaelScottQueue {
//
//  private:
//
//    struct pointer_t;
//
//    struct node_t {
//      std::unique_ptr<T> value = nullptr;
//      pointer_t next = nullptr;
//    };
//
//    struct pointer_t {
//      node_t* ptr = nullptr;
//      size_t count = 0;
//
//      pointer_t() = default;
//      pointer_t(nullptr_t) : ptr(nullptr), count(0) { }
//      pointer_t(node_t* node) : ptr(node), count(0) { }
//      pointer_t(pointer_t const& other) : ptr(other.ptr), count(other.count) { }
//      pointer_t(node_t* node, size_t count)
//        : ptr(node), count(count)
//      { }
//
//
//      bool operator==(pointer_t const& other) const {
//        return other.ptr == ptr and other.count == count;
//      }
//    };
//
//    pointer_t head;
//    pointer_t tail;
//
//  public:
//
//    MichaelScottQueue() {
//      node_t* first_node = new node_t;
//      head = tail = first_node;
//    }
//
//    template <typename... Args>
//    void emplace_back(
//      Args&&... args
//    ) {
//      auto* to_enqueue = new node_t;
//      to_enqueue->value = std::make_unique<T>(std::forward<Args>(args)...);
//
//      while(true) {
//
//        auto itail = tail;
//        auto next = itail.ptr->next;
//        if(itail == tail) {
//
//          if(next.ptr == nullptr) {
//
//            if(std::atomic_compare_exchange_strong(
//              &tail.ptr->next, next, pointer_t(to_enqueue, next.count+1)
//            )) {
//              break;
//            } // end if CAS successful
//
//          } // end if next.pointer == null
//          else {
//
//            // Try to swap tail to be the next node
//            std::atomic_compare_exchange_strong(
//              &tail, itail, pointer_t(next.ptr, itail.count+1)
//            );
//
//          } // end if tail is pointing to the last node
//
//        } // end if itail and next are in a consistent state
//
//      } // end loop until successful CAS
//
//
//
//    }
//
//
//};


} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP
