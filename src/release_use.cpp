/*
//@HEADER
// ************************************************************************
//
//                      release_use.cpp
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

#include "runtime.hpp"
#include "debug.hpp"

using namespace simple_backend;

//==============================================================================

void
Runtime::release_use(use_pending_release_t* use) {

  _SIMPLE_DBG_DO([use](auto& state) { state.remove_registered_use(use); });

  if(use->establishes_alias()) {

    assert(use->get_in_flow() and use->get_out_flow());

    auto& out_flow = use->get_out_flow();

    // Increment the count to indicate responsibility for all of the
    // producers of the in flow
    out_flow->ready_trigger.increment_count();
    // Then decrement that count when the in flow becomes ready
    use->get_in_flow()->ready_trigger.add_action([out_flow]{
      out_flow->ready_trigger.decrement_count();
    });

    // Sometime a use can establish an alias but have an insignificant
    // anti-in flow, so we need to protect the anti-flow alias with an if
    if(use->get_anti_in_flow()) {
      auto anti_in_flow = use->get_anti_in_flow();
      anti_in_flow->ready_trigger.increment_count();
      use->get_anti_out_flow()->ready_trigger.add_action([anti_in_flow]{
        anti_in_flow->ready_trigger.decrement_count();
      });
    }

  }

  if(not use->is_anti_dependency() and use->get_anti_in_flow()) {
    use->get_anti_in_flow()->ready_trigger.decrement_count();
  }

  if(use->get_out_flow()) {
    use->get_out_flow()->ready_trigger.decrement_count();
  }

  if(use->get_anti_out_flow()) {
    use->get_anti_out_flow()->ready_trigger.decrement_count();
  }

  if(use->immediate_permissions() == use_t::Commutative) {
    use->get_in_flow()->comm_in_flow_release_trigger.activate();
  }

}
