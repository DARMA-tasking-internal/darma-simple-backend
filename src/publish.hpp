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
#include "simple_backend_fwd.hpp"
#include "trigger.hpp"

#include <darma/impl/key/SSO_key.h>

namespace std {

template <>
struct hash<darma_runtime::types::key_t> {
  std::size_t operator()(darma_runtime::types::key_t const& key) const {
    return typename darma_runtime::detail::key_traits<darma_runtime::types::key_t>::hasher{}(key);
  }
};

} // end namespace std


namespace simple_backend {

template <typename Key, typename Value,
  typename Hasher = std::hash<Key>,
  typename Equal = std::equal_to<Key>
>
class ConcurrentMap {

  private:

    // Just put locks around everything for now.  We'll do something faster
    // some other time
    std::mutex mutex_;

    std::unordered_map<Key, Value, Hasher, Equal> data_;

  public:

    template <typename Callable, typename... Args>
    void evaluate_or_emplace(Key const& key,
      Callable&& callable,
      Args&&... args
    ) {
      std::lock_guard<std::mutex> lg(mutex_);
      auto found = data_.find(key);
      if(found != data_.end()) {
        std::forward<Callable>(callable)(found->second);
      }
      else {
        data_.emplace_hint(
          found,
          std::piecewise_construct,
          std::forward_as_tuple(key),
          std::forward_as_tuple(std::forward<Args>(args)...)
        );
      }
    }

    template <typename Callable>
    void evaluate_at(
      Key const& key,
      Callable&& callable
    ) {
      std::lock_guard<std::mutex> lg(mutex_);
      std::forward<Callable>(callable)(data_[key]);
    }

    void erase(Key const& key) {
      std::lock_guard<std::mutex> lg(mutex_);
      data_.erase(key);
    }
};


struct PublicationTableEntry {
  struct Impl {
    std::shared_ptr<std::shared_ptr<Flow>> source_flow =
      std::make_shared<std::shared_ptr<Flow>>(nullptr);
    CountdownTrigger<MultiActionList> fetching_trigger = { 1 };
    CountdownTrigger<MultiActionList> release_trigger = { 0 };
  };

  std::shared_ptr<Impl> entry = { nullptr };
};

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_PUBLISH_HPP
