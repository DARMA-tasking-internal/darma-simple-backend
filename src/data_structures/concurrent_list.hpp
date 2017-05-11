/*
//@HEADER
// ************************************************************************
//
//                      concurrent_list.hpp
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

#ifndef DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP
#define DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP

#include <mutex>
#include <deque>

namespace simple_backend {

// A simple, stand-in concurrent list implementation, for now
template <typename T>
class ConcurrentDeque {

  private:

    // Just put locks around everything for now.  We'll do something faster
    // some other time
    std::unique_ptr<std::mutex> mutex_;

    std::deque<T> data_;

  public:

    ConcurrentDeque()
      : mutex_(std::make_unique<std::mutex>())
    { }

    ConcurrentDeque(ConcurrentDeque&& other)
      : mutex_(std::move(other.mutex_)),
        data_(std::move(other.data_))
    { }

    template <typename... Args>
    void emplace_front(Args&&... args) {
      std::lock_guard<std::mutex> lg(*mutex_);
      data_.emplace_front(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void
    emplace_back(Args&&... args) {
      std::lock_guard<std::mutex> lg(*mutex_);
      data_.emplace_back(std::forward<Args>(args)...);
    }

    std::unique_ptr<T>
    get_and_pop_front() {
      std::lock_guard<std::mutex> lg(*mutex_);
      if(data_.empty()) return nullptr;
      else {
        auto rv = std::make_unique<T>(std::move(data_.front()));
        data_.pop_front();
        return std::move(rv);
      }
    }

    std::unique_ptr<T>
    get_and_pop_back() {
      std::lock_guard<std::mutex> lg(*mutex_);
      if(data_.empty()) return nullptr;
      else {
        auto rv = std::make_unique<T>(std::move(data_.back()));
        data_.pop_back();
        return std::move(rv);
      }
    }
};

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_CONCURRENT_LIST_HPP
