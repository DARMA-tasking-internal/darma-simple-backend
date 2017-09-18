/*
//@HEADER
// ************************************************************************
//
//                      join_counter.hpp
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

#ifndef DARMASIMPLEBACKEND_JOIN_COUNTER_HPP
#define DARMASIMPLEBACKEND_JOIN_COUNTER_HPP

#include "event.hpp"

namespace simple_backend {

class JoinCounter
  : public Event
{
  private:

    std::atomic<std::uint64_t> count_ = { 1 };

  public:

    explicit
    JoinCounter(uint64_t initial_count = 1)
      : count_(initial_count)
    {
      //if(count_.load() == 0) {
      //  this->make_ready();
      //}
    }

    inline void
    increment_count(std::memory_order mem_order = std::memory_order_seq_cst) {
      advance_count(1);
    }

    inline void
    advance_count(uint64_t amount,
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      assert(not this->is_triggered(mem_order));
      count_.fetch_add(amount, mem_order);
    }

    inline void
    decrement_count(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      assert(not this->is_triggered(mem_order));
      if(count_.fetch_sub(1) == 1) {
        this->make_ready(mem_order);
      }
    }

    inline void
    attach_decrement_of(
      std::shared_ptr<JoinCounter> const& to_decrement,
      std::memory_order decrement_mem_order = std::memory_order_seq_cst
    ) {
      this->attach_action([to_decrement, decrement_mem_order] {
        to_decrement->decrement_count(decrement_mem_order);
      });

    }

    uint64_t
    get_count(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      return count_.load(mem_order);
    }
};


class ManuallyTriggeredEvent
  : public Event
{
  public:

    void trigger_event(
      std::memory_order mem_order = std::memory_order_seq_cst
    ) {
      this->make_ready(mem_order);
    }

};

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_JOIN_COUNTER_HPP
