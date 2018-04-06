/*
//@HEADER
// ************************************************************************
//
//                      queue.hpp
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

#ifndef DARMASIMPLEBACKEND_QUEUE_HPP
#define DARMASIMPLEBACKEND_QUEUE_HPP

#include <data_structures/data_structures_fwd.hpp>

namespace simple_backend {
namespace data_structures {

template <
  typename ConcreteT
>
class ThreadSafeQueue {
  private:

    using concrete_t = ConcreteT;

  protected:

    // Make sure someone doesn't try to construct/destroy one of these
    // (the implementation type should be used for that)

    ThreadSafeQueue() = default;
    ThreadSafeQueue(ThreadSafeQueue&&) = default;
    ~ThreadSafeQueue() noexcept = default;

  public:

    template <typename Callable>
    inline size_t
    consume_all(Callable&& callable) {
      // TODO check callable validity
      return static_cast<concrete_t*>(this)->_consume_all(
        std::forward<Callable>(callable)
      );
    }

    template <typename Callable>
    inline bool
    consume_one(Callable&& callable) {
      // TODO check callable validity
      return static_cast<concrete_t*>(this)->_consume_one(
        std::forward<Callable>(callable)
      );
    }

    template <typename U>
    inline bool
    push(U const& to_push) {
      return static_cast<concrete_t*>(this)->_push(to_push);
    }

    template <typename... Args>
    inline bool
    emplace(Args&&... args) {
      // TODO check constructibility
      return static_cast<concrete_t*>(this)->_emplace(
        std::forward<Args>(args)...
      );
    }

    template <typename U>
    inline bool
    pop(U& popped) {
      // TODO check assignability
      return static_cast<concrete_t*>(this)->_pop(popped);
    }

};

} // end namespace data_structures
} // end namespace simple_backend

#include "queue_impl/single_lock_queue.hpp"
#include "queue_impl/libcds_queue.hpp"

#endif //DARMASIMPLEBACKEND_QUEUE_HPP
