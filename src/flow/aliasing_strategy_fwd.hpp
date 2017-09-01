/*
//@HEADER
// ************************************************************************
//
//                      aliasing_strategy_fwd.hpp
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

#ifndef DARMASIMPLEBACKEND_ALIASING_STRATEGY_FWD_HPP
#define DARMASIMPLEBACKEND_ALIASING_STRATEGY_FWD_HPP

namespace simple_backend {
namespace aliasing {


/** @brief CRTP base for aliasing strategies.
 *  Mostly used for specifying the interface.
 */
template <typename ConcreteT>
struct AliasingStrategy;


/**
 *  Old strategy of adding a decrement action directly to the flow's triggered
 *  action list.  This can quite easily blow the stack in recursive applications
 */
struct ActionListAppendAliasingStrategy;


/**
 *  Takes advantage of a layer of indirection to perform aliasing.
 *  Currently buggy (probably).
 */
struct MergeableTriggerAliasingStrategy;

/**
 *  Uses the task queue to avoid blowing the stack, but otherwise does basically
 *  the same thing as ActionListAppendAliasingStrategy
 */
struct WorkQueueAppendAliasingStrategy;

} // end namespace aliasing
} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_ALIASING_STRATEGY_FWD_HPP
