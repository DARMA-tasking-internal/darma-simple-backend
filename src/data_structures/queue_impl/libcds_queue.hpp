/*
//@HEADER
// ************************************************************************
//
//                      libcds_queue.hpp
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

#ifndef DARMASIMPLEBACKEND_DATA_STRUCTURES_QUEUE_IMPL_LIBCDS_QUEUE_HPP
#define DARMASIMPLEBACKEND_DATA_STRUCTURES_QUEUE_IMPL_LIBCDS_QUEUE_HPP

#include <config.hpp>

#ifdef DARMA_SIMPLE_BACKEND_HAS_LIBCDS

#include <data_structures/queue.hpp>

#include <cds/gc/dhp.h>

#ifdef DARMA_SIMPLE_BACKEND_LIBCDS_USE_MSQUEUE
#  include <cds/container/msqueue.h>
#endif

#if defined(DARMA_SIMPLE_BACKEND_LIBCDS_USE_OPTIMISTIC_QUEUE)
#  include <cds/container/optimistic_queue.h>
#endif

namespace simple_backend {
namespace data_structures {
namespace libcds_interface {

template <typename T, typename QueueImplT>
struct BasicLibCDSQueue
  : ThreadSafeQueue<BasicLibCDSQueue<T, QueueImplT>>
{
  private:

    std::unique_ptr<QueueImplT> impl_;

  public:

    using value_type = T;

    BasicLibCDSQueue()
      : impl_(std::make_unique<QueueImplT>())
    { }
    BasicLibCDSQueue(BasicLibCDSQueue const&) = delete;
    BasicLibCDSQueue(BasicLibCDSQueue&& other) noexcept
      : impl_(std::move(other.impl_))
    { }
    ~BasicLibCDSQueue() = default;

  private:

    template <typename... Args>
    bool
    _emplace(Args&&... args) {
      return impl_->emplace(std::forward<Args>(args)...);
    }

    bool
    _push(value_type const& to_push) {
      return impl_->push(to_push);
    }

    template <typename Callable>
    bool
    _consume_one(Callable&& c) {
      return impl_->dequeue_with([callable=std::forward<Callable>(c)](value_type& v) mutable {
        std::forward<Callable>(callable)(std::move(v));
      });
    }

    template <typename Callable>
    size_t
    _consume_all(Callable&& c) {
      size_t consumed = 0;
      while(this->consume_one(std::forward<Callable>(c))) {
        ++consumed;
      }
      return consumed;
    }

    template <typename U>
    bool
    _pop(U& dest) {
      return this->consume_one([&](value_type&& val) {
        dest = std::move(val);
      });
    }

    bool
    _pop(value_type& dest) {
      return impl_->pop(dest);
    }

    template <typename ConcreteT>
    friend class simple_backend::data_structures::ThreadSafeQueue;
};


#if defined(DARMA_SIMPLE_BACKEND_LIBCDS_USE_MSQUEUE)
template <typename T>
using MSQueue = BasicLibCDSQueue<T, cds::container::MSQueue<cds::gc::DHP, T>>;
#endif

#if defined(DARMA_SIMPLE_BACKEND_LIBCDS_USE_OPTIMISTIC_QUEUE)
template <typename T>
using OptimisticQueue = BasicLibCDSQueue<T, cds::container::OptimisticQueue<cds::gc::DHP, T>>;
#endif


} // end namespace libcds_interface
} // end namespace data_structures
} // end namespace simple_backend

#endif // DARMA_SIMPLE_BACKEND_HAS_LIBCDS

#endif //DARMASIMPLEBACKEND_DATA_STRUCTURES_QUEUE_IMPL_LIBCDS_QUEUE_HPP
