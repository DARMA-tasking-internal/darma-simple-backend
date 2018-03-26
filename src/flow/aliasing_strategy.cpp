/*
//@HEADER
// ************************************************************************
//
//                      aliasing_strategy.cpp
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

#include "aliasing_strategy.hpp"

#include <pending_operation.hpp>

using namespace simple_backend::aliasing;

void
WorkQueueAppendAliasingStrategy::handle_aliasing_for_released_use(
  use_pending_release_t* use
) const {
  {
    // Increment, and enqueue the decrement
    auto& out_flow = use->get_out_flow();
    out_flow->get_ready_trigger()->increment_count();
    when_ready_do(use->get_in_flow()->get_ready_trigger(),
      [out_flow]{
        out_flow->get_ready_trigger()->decrement_count();
      }
    );
  } // end out_flow scope

  if(use->get_anti_in_flow()) {
    assert(use->get_anti_out_flow());

    // Increment, and enqueue the decrement
    auto& anti_in_flow = use->get_anti_in_flow();
    anti_in_flow->get_ready_trigger()->increment_count();
    when_ready_do(use->get_anti_out_flow()->get_ready_trigger(),
      [anti_in_flow]{
        anti_in_flow->get_ready_trigger()->decrement_count();
      }
    );
  }
}


