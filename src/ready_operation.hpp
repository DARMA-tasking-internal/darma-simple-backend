/*
//@HEADER
// ************************************************************************
//
//                      ready_operation.hpp
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

#ifndef DARMASIMPLEBACKEND_READY_OPERATION_HPP
#define DARMASIMPLEBACKEND_READY_OPERATION_HPP

#include <cassert>
#include <darma/interface/backend/runtime.h>
#include "util.hpp"

namespace simple_backend {

struct ReadyOperation {

  typedef enum MessageKind {
    NoMessage,
    AllTasksDone
    #if SIMPLE_BACKEND_USE_KOKKOS
    , NeededForKokkosWork
    #endif
  } message_kind_t;

  virtual message_kind_t
  get_message_kind() const {
    return MessageKind::NoMessage;
  }

  virtual void run() {
    assert(false); // not runnable
  }

  virtual bool is_runnable() const =0;

  virtual bool is_stealable() const =0;

  virtual bool has_message() const =0;

  virtual ~ReadyOperation() noexcept = default;

};

struct SpecialMessage
  : ReadyOperation
{
  private:

    message_kind_t message_kind_;

  public:

    explicit
    SpecialMessage(message_kind_t kind) : message_kind_(kind) { }

    bool
    is_runnable() const override {
      return false;
    }

    bool
    is_stealable() const override {
      return false;
    }

    bool
    has_message() const override {
      return true;
    }

    message_kind_t
    get_message_kind() const override {
      return message_kind_;
    }

    ~SpecialMessage() noexcept override = default;

};

struct ReadyTaskOperation
  : ReadyOperation
{

  private:

    using task_unique_ptr = darma_runtime::abstract::backend::Runtime::task_unique_ptr;
    task_unique_ptr task_;

  public:

    explicit
    ReadyTaskOperation(task_unique_ptr&& task) : task_(std::move(task)) { }

    bool
    is_runnable() const override {
      return true;
    }

    bool
    is_stealable() const override {
      return true;
    }

    bool
    has_message() const override {
      return false;
    }

    void run() override;


    ~ReadyTaskOperation() noexcept override = default;

};

template <
  typename Callable,
  bool Stealable = true
>
struct CallableOperation
  : ReadyOperation
{

  private:

    Callable callable_;

  public:

    template <typename CallableDeduced>
    explicit
    CallableOperation(
      safe_template_ctor_tag_t,
      CallableDeduced&& callable
    ) : callable_(std::forward<CallableDeduced>(callable))
    { }

    bool
    is_runnable() const override {
      return true;
    }

    bool
    is_stealable() const override {
      return Stealable;
    }

    bool
    has_message() const override {
      return false;
    }

    void run() override {
      callable_();
    }

    ~CallableOperation() noexcept override = default;

};

template <typename Callable>
auto
make_callable_operation(Callable&& callable) {
  return std::make_unique<
    CallableOperation<std::decay_t<Callable>, true>
  >(
    safe_template_ctor_tag,
    std::forward<Callable>(callable)
  );
}

template <typename Callable>
auto
make_nonstealable_callable_operation(Callable&& callable) {
  return std::make_unique<
    CallableOperation<std::decay_t<Callable>, false>
  >(
    safe_template_ctor_tag,
    std::forward<Callable>(callable)
  );
}

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_READY_OPERATION_HPP
