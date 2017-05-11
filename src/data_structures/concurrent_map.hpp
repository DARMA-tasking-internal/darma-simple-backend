/*
//@HEADER
// ************************************************************************
//
//                      concurrent_map.hpp
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

#ifndef DARMASIMPLEBACKEND_CONCURRENT_MAP_HPP
#define DARMASIMPLEBACKEND_CONCURRENT_MAP_HPP

namespace simple_backend {

template <typename Key, typename Value,
  typename Hasher = std::hash<Key>,
  typename Equal = std::equal_to<Key>
>
class ConcurrentMap {

  private:

    // Just put locks around everything for now.  We'll do something faster
    // some other time
    std::recursive_mutex mutex_;

    std::unordered_map<Key, Value, Hasher, Equal> data_;

  public:

    template <typename Callable, typename... Args>
    void evaluate_or_emplace(Key const& key,
      Callable&& callable,
      Args&&... args
    ) {
      std::lock_guard<std::recursive_mutex> lg(mutex_);
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
      std::lock_guard<std::recursive_mutex> lg(mutex_);
      std::forward<Callable>(callable)(data_[key]);
    }

    template <typename Callable, typename FirstCallable, typename EmplaceArgsFwdTuple, typename... Args>
    void evaluate_or_evaluate_first(
      Key const& key,
      Callable&& callable,
      FirstCallable&& first_callable,
      EmplaceArgsFwdTuple&& emplace_args,
      Args&&... args
    ) {
      std::lock_guard<std::recursive_mutex> lg(mutex_);
      auto found = data_.find(key);
      if(found != data_.end()) {
        assert(data_.size() > 0);
        std::forward<Callable>(callable)(
          found->second,
          std::forward<Args>(args)...
        );
      }
      else {
        auto spot = data_.emplace_hint(
          found,
          std::piecewise_construct,
          std::forward_as_tuple(key),
          std::forward<EmplaceArgsFwdTuple>(emplace_args)
        );
        std::forward<FirstCallable>(first_callable)(
          spot->second,
          std::forward<Args>(args)...
        );
      }
    }

    void erase(Key const& key) {
      std::lock_guard<std::recursive_mutex> lg(mutex_);
      data_.erase(key);
    }
};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_CONCURRENT_MAP_HPP
