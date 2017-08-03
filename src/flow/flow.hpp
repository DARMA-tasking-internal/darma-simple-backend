/*
//@HEADER
// ************************************************************************
//
//                      flow.hpp
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

#ifndef DARMASIMPLECVBACKEND_FLOW_HPP
#define DARMASIMPLECVBACKEND_FLOW_HPP

#include <list>

#include "data_structures/trigger.hpp"
#include "publish.hpp"
#include "control_block.hpp"
#include "task_collection_token.hpp"
#include "mergeable_flow.hpp"

namespace simple_backend {

struct AliasableBasicFlow {
  private:

    std::shared_ptr<MergeableTrigger> ready_trigger_;

  public:

    explicit
    AliasableBasicFlow(size_t initial_count)
      : ready_trigger_(std::make_shared<MergeableTrigger>(initial_count))
    { }

    // always copy to prevent races
    auto get_ready_trigger() { return ready_trigger_; }

    void alias_to(std::shared_ptr<AliasableBasicFlow> const& other) {
      // Note: other cannot be ready upon invocation

      // Redirect anyone who still has access to us to instead use other:
      auto my_trigger = std::atomic_exchange(&ready_trigger_, other->ready_trigger_);

      // Now that the swap has happened, the only calls that will change the count
      // or action list are ones that happen through existing aliases to my_trigger
      // (or any actions that started before the swap happened and might still be running...)
      auto actually_aliased_to = my_trigger->alias_to(std::atomic_load(&other->ready_trigger_));

      // and redirect everyone to the new
      std::atomic_exchange(&ready_trigger_, actually_aliased_to);
    }

};

struct Flow : AliasableBasicFlow {

  public:
    explicit
    Flow(std::shared_ptr<ControlBlock> cblk)
      : AliasableBasicFlow(0),
        control_block(cblk)
    { }

    Flow( std::shared_ptr<ControlBlock> cblk, size_t initial_count)
      : AliasableBasicFlow(initial_count),
        control_block(cblk)
    { }

    std::shared_ptr<ControlBlock> control_block;

    // Currently only used with commutative:
    std::mutex commutative_mtx;
    ResettableBooleanTrigger<MultiActionList> comm_in_flow_release_trigger;

    // Currently only for flow collections, and only valid after the task
    // collection is registered and isn't read-only
    std::shared_ptr<TaskCollectionToken> parent_collection_token = nullptr;

};

struct AntiFlow : AliasableBasicFlow {
  AntiFlow()
    : AliasableBasicFlow(0)
  { }

  explicit
  AntiFlow(size_t initial_count)
    : AliasableBasicFlow(initial_count)
  { }

  bool is_index_fetching_antiflow = false;
};

} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_FLOW_HPP
