/*
//@HEADER
// ************************************************************************
//
//                      util.hpp
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

#ifndef DARMASIMPLECVBACKEND_UTIL_HPP
#define DARMASIMPLECVBACKEND_UTIL_HPP

#include <iterator>
#include <mutex>

#include <tinympl/is_instantiation_of.hpp>

namespace simple_backend {

enum { TryLockSuccess = -1 };

namespace detail {

// TODO non-recursive implementation, for stack overflow concerns?

// More or less copied from the standard library's implementation of the variadic version
// and Boost's implementation of the iterator version.
template <
  typename InputIterator1,
  typename InputIterator2,
  typename UniqueLockT,
  typename ImplSpecialization
>
int _try_lock_impl(
  InputIterator1 lbegin,
  InputIterator2 const& lend,
  UniqueLockT prev_ulock,
  ImplSpecialization&& spec
) {

  if(lbegin == lend) {
    prev_ulock.release();
    return TryLockSuccess;
  }
  else {

    std::unique_lock<typename std::decay_t<ImplSpecialization>::lockable_t>
      this_lock(spec.dereference(lbegin), std::try_to_lock);
    int rv = 0;

    if(this_lock.owns_lock()) {
      rv = spec(++lbegin, lend, std::move(this_lock));
      if(rv == TryLockSuccess) {
        // Success
        // relinquish responsibility for *lbegin of previous recursion, leaving
        // the object locked (see std::unique_lock::release())
        prev_ulock.release();
      }
      else {
        // prev_ulock will go out of scope and unlock at the end of this function
        // the previous *lbegin, so nothing to do here but increment the return value
        ++rv;
      }
    }

    // If the locking failed, then we want to return 0 and have all of the callers
    // above us on the recursive stack add 1 (so we don't need to do anything
    // since rv is already 0)

    return rv;

  }

};


template <typename InputIterator1, typename Enable=void>
struct try_lock_impl {

  using lockable_t = typename std::iterator_traits<InputIterator1>::value_type;

  static decltype(auto) dereference(InputIterator1& iter) {
    return *iter;
  }

  template <typename InputIterator2, typename UniqueLockT>
  int operator()(
    InputIterator1 lbegin,
    InputIterator2 const& lend,
    UniqueLockT prev_ulock
  ) const {
    return detail::_try_lock_impl(lbegin, lend, std::move(prev_ulock), *this);
  }

};

template <typename InputIterator1>
struct try_lock_impl<InputIterator1,
  std::enable_if_t<
    tinympl::is_instantiation_of<std::reference_wrapper,
      typename std::iterator_traits<InputIterator1>::value_type
    >::value
    or tinympl::is_instantiation_of<std::shared_ptr,
      typename std::iterator_traits<InputIterator1>::value_type
    >::value
  >
> {
  //using lockable_holder_t = typename std::iterator_traits<InputIterator1>::value_type;
  //struct _get_element_type { template <typename T> using apply = typename T::element_type; };
  //struct _get_type { template <typename T> using apply = typename T::type; };


  static decltype(auto) dereference(InputIterator1& iter) {
    return iter->get();
  }

  using lockable_t = std::decay_t<decltype(
    dereference(std::declval<InputIterator1&>())
  )>;

  template <typename InputIterator2, typename UniqueLockT>
  int operator()(
    InputIterator1 lbegin,
    InputIterator2 const& lend,
    UniqueLockT prev_ulock
  ) const {
    return detail::_try_lock_impl(lbegin, lend, std::move(prev_ulock), *this);
  }
};

} // end namespace detail


template <typename InputIterator1, typename InputIterator2>
int
try_lock(InputIterator1 begin_lockables, InputIterator2 const& end_lockables) {
  using lockable_t = typename std::iterator_traits<InputIterator1>::value_type;

  using impl_t = detail::try_lock_impl<InputIterator1>;

  if(begin_lockables != end_lockables) {

    std::unique_lock<typename impl_t::lockable_t> first_lock(
      impl_t::dereference(begin_lockables), std::try_to_lock
    );

    if(first_lock.owns_lock()) {
      return impl_t{}(
        ++begin_lockables, end_lockables, std::move(first_lock)
      );
    }
    else {
      return 0;
    }

  }
  else {
    // Nothing actually locked, but we can still say we locked "everything"
    return TryLockSuccess;
  }
}

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_UTIL_HPP
