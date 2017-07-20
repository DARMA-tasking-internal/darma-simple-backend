/*
//@HEADER
// ************************************************************************
//
//                      task_collection_token.hpp
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

#ifndef DARMASIMPLEBACKEND_TASK_COLLECTION_TOKEN_HPP
#define DARMASIMPLEBACKEND_TASK_COLLECTION_TOKEN_HPP

#include <memory>

#include <darma/interface/frontend/use.h>

#include "data_structures/trigger.hpp"
#include "control_block.hpp"

namespace simple_backend {

struct TaskCollectionToken {

  static std::atomic<std::size_t> next_sequence_identifier;
  std::size_t sequence_identifier;

  struct CollectiveInvocation {
    CollectiveInvocation(size_t n_contribs)
      : input_uses(),
        ready_trigger(n_contribs) // in and anti-in for each contribution
    {
      input_uses.reserve(n_contribs);
    }
    CountdownTrigger<SingleAction> ready_trigger;
    std::atomic<void*> output_data_ptr = { nullptr }; // only used for the two use case
    std::vector<std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>> input_uses;
    std::vector<std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>> output_uses;
  };

  explicit
  TaskCollectionToken(size_t in_size)
    : size(in_size),
      sequence_identifier(next_sequence_identifier++)
  { }
  size_t size;

  ConcurrentMap<
    darma_runtime::types::key_t,
    std::shared_ptr<CollectiveInvocation> // could probably be a unique_ptr
  > collectives;

  ConcurrentMap<
    std::tuple<darma_runtime::types::key_t, darma_runtime::types::key_t, std::size_t>,
    PublicationTableEntry
  > current_published_entries;

};


} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_TASK_COLLECTION_TOKEN_HPP
