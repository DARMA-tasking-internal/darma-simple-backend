/*
//@HEADER
// ************************************************************************
//
//                      publish.hpp
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

#ifndef DARMASIMPLECVBACKEND_PUBLISH_HPP
#define DARMASIMPLECVBACKEND_PUBLISH_HPP

#include <unordered_map>
#include <mutex>

#include <darma.h>
#include <data_structures/join_counter.hpp>

#include "data_structures/concurrent_map.hpp"

#include "simple_backend_fwd.hpp"


namespace std {

template <>
struct hash<darma_runtime::types::key_t>
  : darma_runtime::detail::key_traits<darma_runtime::types::key_t>::hasher
{ };

template <>
struct equal_to<darma_runtime::types::key_t>
  :  darma_runtime::detail::key_traits<darma_runtime::types::key_t>::key_equal
{ };

} // end namespace std

namespace simple_backend {

struct PublicationTableEntry {
  struct Impl {
    std::shared_ptr<std::shared_ptr<Flow>> source_flow =
      std::make_shared<std::shared_ptr<Flow>>(nullptr);
    std::shared_ptr<JoinCounter> fetching_join_counter = std::make_shared<JoinCounter>(1);
    std::shared_ptr<JoinCounter> release_event = std::make_shared<JoinCounter>(1);
  };

  std::shared_ptr<Impl> entry = { nullptr };
};

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_PUBLISH_HPP
