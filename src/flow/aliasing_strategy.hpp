/*
//@HEADER
// ************************************************************************
//
//                      aliasing_strategy.hpp
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

#ifndef DARMASIMPLEBACKEND_ALIASING_STRATEGY_HPP
#define DARMASIMPLEBACKEND_ALIASING_STRATEGY_HPP

#include "aliasing_strategy_fwd.hpp"

#include <darma/interface/backend/runtime.h>

#include <flow/flow.hpp>

namespace simple_backend {
namespace aliasing {


template <typename ConcreteT>
struct AliasingStrategy {
  public:

    using use_t = darma_runtime::abstract::frontend::Use;
    using use_pending_registration_t = darma_runtime::abstract::frontend::UsePendingRegistration;
    using use_pending_release_t = darma_runtime::abstract::backend::Runtime::use_pending_release_t;

    inline void
    handle_aliasing_for_released_use(
      use_pending_release_t* use
    ) {
      assert(use->establishes_alias());
      assert(use->get_in_flow() and use->get_out_flow());
      static_cast<ConcreteT*>(this)->handle_aliasing_for_released_use(use);
    }
};


struct ActionListAppendAliasingStrategy
  : AliasingStrategy<ActionListAppendAliasingStrategy>
{
  void handle_aliasing_for_released_use(use_pending_release_t* use) const;
};

struct MergeableTriggerAliasingStrategy
  : AliasingStrategy<MergeableTriggerAliasingStrategy>
{
  void handle_aliasing_for_released_use(use_pending_release_t* use) const;
};

struct WorkQueueAppendAliasingStrategy
  : AliasingStrategy<WorkQueueAppendAliasingStrategy>
{
  void handle_aliasing_for_released_use(use_pending_release_t* use) const;
};


} // end namespace aliasing
} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_ALIASING_STRATEGY_HPP
