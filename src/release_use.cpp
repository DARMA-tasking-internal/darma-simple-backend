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

#include "runtime/runtime.hpp"
#include "debug.hpp"

using namespace simple_backend;

#define ALIAS_HANDLING_METHOD 2

//==============================================================================

void
Runtime::release_use(use_pending_release_t* use) {

  _SIMPLE_DBG_DO([use](auto& state) { state.remove_registered_use(use); });

  if(use->establishes_alias()) {

    assert(use->get_in_flow() and use->get_out_flow());

    // THIS IS INCORRECT
    // This is an ugly hack, and it doesn't even fix the cost of alias resolution,
    // but at least it fixes the stack overflow problem
    //  {
    //    auto& in_flow = use->get_in_flow();
    //    auto& out_flow = use->get_out_flow();
    //    std::lock_guard<std::recursive_mutex> in_flow_alias_lock(
    //      in_flow->alias_resolution_mutex
    //    );
    //
    //    in_flow->aliased_to.push_back(out_flow);
    //
    //    for(auto ialias = in_flow->aliased_to.begin(); ialias != in_flow->aliased_to.end(); ++ialias) {
    //      std::lock_guard<std::recursive_mutex> lg(ialias->get()->alias_resolution_mutex);
    //      in_flow->aliased_to.splice(in_flow->aliased_to.end(), ialias->get()->aliased_to);
    //    }
    //
    //    // Increment the count to indicate responsibility for all of the
    //    // producers of the in flow
    //    out_flow->ready_trigger.increment_count();
    //    // Then decrement that count when the in flow becomes ready
    //
    //    in_flow->ready_trigger.add_action([in_flow] {
    //      std::lock_guard<std::recursive_mutex> inner_in_flow_alias_lock(
    //        in_flow->alias_resolution_mutex
    //      );
    //      for(auto&& alflow : in_flow->aliased_to) {
    //        alflow->ready_trigger.decrement_count();
    //      }
    //      in_flow->aliased_to.clear();
    //    });
    //
    //    in_flow->is_aliased.store(true);
    //
    //  } // release in_flow_alias_lock

#if ALIAS_HANDLING_METHOD == 1
    auto& in_flow = use->get_in_flow();
    auto& out_flow = use->get_out_flow();
    out_flow->ready_trigger.increment_count();
    use->get_in_flow()->ready_trigger.add_action([in_flow, out_flow]{
      // ugly hack to avoid stack overflow:
      auto pair = out_flow->ready_trigger.decrement_count_and_return_actions_if_ready();
      if(pair.first) {
        in_flow->ready_trigger.extend_action_list_from_within_action(pair.second);
      }
    });
#elif ALIAS_HANDLING_METHOD == 2
    //auto& out_flow = use->get_out_flow();
    //out_flow->ready_trigger.increment_count();
    //use->get_in_flow()->ready_trigger.add_action([out_flow]{
    //  out_flow->ready_trigger.decrement_count();
    //});
    use->get_in_flow()->alias_to(use->get_out_flow());
#endif

    // Sometime a use can establish an alias but have an insignificant
    // anti-in flow, so we need to protect the anti-flow alias with an if
    if(use->get_anti_in_flow()) {

      // THIS IS INCORRECT
      //    // This is an ugly hack, and it doesn't even fix the cost of alias resolution,
      //    // but at least it fixes the stack overflow problem.
      //    // It's the same ugly hack from above...
      //    {
      //      auto& anti_out_flow = use->get_anti_out_flow();
      //      std::lock_guard<std::mutex> anti_out_flow_alias_lock(
      //        anti_out_flow->alias_resolution_mutex
      //      );
      //
      //      anti_out_flow->aliased_to.push_back(use->get_anti_in_flow());
      //
      //      for(auto ialias = anti_out_flow->aliased_to.begin(); ialias != anti_out_flow->aliased_to.end(); ++ialias) {
      //        std::lock_guard<std::mutex> lg(ialias->get()->alias_resolution_mutex);
      //        anti_out_flow->aliased_to.splice(
      //          anti_out_flow->aliased_to.end(),
      //          ialias->get()->aliased_to
      //        );
      //      }
      //
      //      // Increment the count to indicate responsibility for all of the
      //      // producers of the in flow
      //      use->get_anti_in_flow()->ready_trigger.increment_count();
      //      // Then decrement that count when the in flow becomes ready
      //      anti_out_flow->ready_trigger.add_action([anti_out_flow] {
      //        std::lock_guard<std::mutex> inner_anti_out_flow_alias_lock(
      //          anti_out_flow->alias_resolution_mutex
      //        );
      //        for(auto&& alflow : anti_out_flow->aliased_to) {
      //          alflow->ready_trigger.decrement_count();
      //        }
      //        anti_out_flow->aliased_to.clear();
      //      });
      //
      //      anti_out_flow->is_aliased.store(true);
      //
      //    } // release anti_out_flow_alias_lock

#if ALIAS_HANDLING_METHOD == 1
      auto& anti_in_flow = use->get_anti_in_flow();
      auto& anti_out_flow = use->get_anti_out_flow();
      anti_in_flow->ready_trigger.increment_count();
      use->get_anti_out_flow()->ready_trigger.add_action([anti_in_flow, anti_out_flow]{
        // ugly hack to avoid stack overflow:
        auto pair = anti_in_flow->ready_trigger.decrement_count_and_return_actions_if_ready();
        if(pair.first) {
          anti_out_flow->ready_trigger.extend_action_list_from_within_action(pair.second);
        }
      });
#elif ALIAS_HANDLING_METHOD == 2
      //auto& anti_in_flow = use->get_anti_in_flow();
      //anti_in_flow->ready_trigger.increment_count();
      //use->get_anti_out_flow()->ready_trigger.add_action([anti_in_flow]{
      //  anti_in_flow->ready_trigger.decrement_count();
      //});
      use->get_anti_out_flow()->alias_to(use->get_anti_in_flow());
#endif

    }

  }

  if(not use->is_anti_dependency() and use->get_anti_in_flow()) {
    use->get_anti_in_flow()->get_ready_trigger()->decrement_count();
  }

  if(use->get_out_flow()) {
    use->get_out_flow()->get_ready_trigger()->decrement_count();
  }

  if(use->get_anti_out_flow()) {
    use->get_anti_out_flow()->get_ready_trigger()->decrement_count();
  }

  if(use->immediate_permissions() == use_t::Commutative) {
    use->get_in_flow()->comm_in_flow_release_trigger.activate();
  }

}

