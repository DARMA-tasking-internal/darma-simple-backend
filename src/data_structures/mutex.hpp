/*
//@HEADER
// ************************************************************************
//
//                      mutex.hpp
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

#ifndef DARMASIMPLEBACKEND_MUTEX_HPP
#define DARMASIMPLEBACKEND_MUTEX_HPP

#include "config.hpp"

#include <mutex>

#ifdef DARMA_SIMPLE_BACKEND_HAS_SHARED_MUTEX
#  include <shared_mutex>
#endif

namespace simple_backend {

inline auto shared_lock_in_scope(std::mutex& mtx) {
  return std::unique_lock<std::mutex>(mtx);
}

inline auto unique_lock_in_scope(std::mutex& mtx) {
  return std::unique_lock<std::mutex>(mtx);
}

#ifdef DARMA_SIMPLE_BACKEND_HAS_SHARED_MUTEX

inline auto shared_lock_in_scope(std::shared_timed_mutex& mtx) {
  return std::shared_lock<std::shared_timed_mutex>(mtx);
}

inline auto unique_lock_in_scope(std::shared_timed_mutex& mtx) {
  return std::unique_lock<std::shared_timed_mutex>(mtx);
}

#endif

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_MUTEX_HPP
