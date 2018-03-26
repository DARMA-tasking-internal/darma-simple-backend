/*
//@HEADER
// ************************************************************************
//
//                      pointers.hpp
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

#ifndef DARMASIMPLEBACKEND_POINTERS_HPP
#define DARMASIMPLEBACKEND_POINTERS_HPP

#include <memory>

#include "data_structures_fwd.hpp"
#include <data_structures/mutex.hpp>
#include <util.hpp>

namespace simple_backend {
namespace data_structures {



template <typename ConcreteT>
class ThreadSafeUniquePtr {

  public:

    using concrete_t = ConcreteT;

    template <typename UniquePtrLike>
    auto exchange(UniquePtrLike&& other) {
      return static_cast<ConcreteT*>(this)->_exchange_impl(
        std::forward<UniquePtrLike>(other)
      );
    }

    template <typename Callable>
    auto with_shared_access(Callable&& callable) {
      return static_cast<ConcreteT*>(this)->_with_shared_access_impl(
        std::forward<Callable>(callable)
      );
    }

    template <typename Callable>
    auto with_exclusive_access(Callable&& callable) {
      return static_cast<ConcreteT*>(this)->_with_exclusive_access_impl(
        std::forward<Callable>(callable)
      );
    }


};


namespace _impl {

struct lock_other_mutex_helper {
  lock_other_mutex_helper() = default;

  template <typename HasMutexT>
  lock_other_mutex_helper(
    safe_template_ctor_tag_t,
    HasMutexT& other
  ) {
    other.mutex_.lock();
  }
};

} // end namespace _impl


template <
  typename T,
  typename Deleter,
  typename Mutex
>
class LockBasedThreadSafeUniquePtr
  : public ThreadSafeUniquePtr<LockBasedThreadSafeUniquePtr<T, Deleter, Mutex>>,
    private _impl::lock_other_mutex_helper
{
  private:

    Mutex mutex_;

    std::unique_ptr<T, Deleter> value_;

  public:

    using value_type = T;
    using unique_ptr_type = std::unique_ptr<T, Deleter>;

    friend class ThreadSafeUniquePtr<LockBasedThreadSafeUniquePtr<T, Deleter, Mutex>>;

  public:

    LockBasedThreadSafeUniquePtr(std::nullptr_t)
      : value_(nullptr)
    { }

    LockBasedThreadSafeUniquePtr()
      : LockBasedThreadSafeUniquePtr(nullptr)
    { /* forwarding ctor, must be empty */ }

    template <typename U, typename Del>
    explicit
    LockBasedThreadSafeUniquePtr(
      std::unique_ptr<U, Del>&& ptr
    ) : value_(std::move(ptr))
    { }

    LockBasedThreadSafeUniquePtr(LockBasedThreadSafeUniquePtr const& other) = delete;
    LockBasedThreadSafeUniquePtr(
      LockBasedThreadSafeUniquePtr&& other
    ) : lock_other_mutex_helper(safe_template_ctor_tag, other),
        value_(std::move(other.value_)),
        // Don't move the mutex; just make another one
        mutex_()
    {
      other.mutex_.unlock();
    }

  private:

    unique_ptr_type
    _exchange_impl(unique_ptr_type&& other) {
      auto _lg = unique_lock_in_scope(mutex_);
      value_.swap(other);
      return std::move(other);
    }

    template <typename Callable>
    auto
    _with_shared_access_impl(Callable&& callable) {
      auto _lg = shared_lock_in_scope(mutex_);
      return std::forward<Callable>(callable)(value_.get());
    }

    template <typename Callable>
    auto
    _with_exclusive_access_impl(Callable&& callable) {
      auto _lg = unique_lock_in_scope(mutex_);
      return std::forward<Callable>(callable)(value_.get());
    }

};



} // end namespace data_structures
} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_POINTERS_HPP
