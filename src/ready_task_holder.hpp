/*
//@HEADER
// ************************************************************************
//
//                      ready_task_holder.hpp
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

#ifndef DARMASIMPLEBACKEND_READY_TASK_HOLDER_HPP
#define DARMASIMPLEBACKEND_READY_TASK_HOLDER_HPP

#include <darma/interface/backend/runtime.h>

namespace simple_backend {

struct ReadyTaskHolder {
  using task_unique_ptr = darma_runtime::abstract::backend::Runtime::task_unique_ptr;


  typedef enum special_message {
    NoMessage,
    AllTasksDone
#if SIMPLE_BACKEND_USE_KOKKOS
    , NeededForKokkosWork
#endif
  } special_message_t;

  task_unique_ptr task = nullptr;

  special_message_t message = NoMessage;

  ReadyTaskHolder(task_unique_ptr&& in_task) : task(std::move(in_task)) { }

  ReadyTaskHolder(special_message_t in_message) : task(nullptr), message(in_message) { }

};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_READY_TASK_HOLDER_HPP
