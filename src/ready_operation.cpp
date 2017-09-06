/*
//@HEADER
// ************************************************************************
//
//                      ready_operation.cpp
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

#include <darma.h>
#include <runtime/runtime.hpp>
#include "ready_operation.hpp"
#include "debug.hpp"

using namespace simple_backend;

void
ReadyTaskOperation::run()
{
  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.insert(task_.get());
    state.pending_tasks.erase(task_.get());
  });

  // setup data
  for(auto&& dep : task_->get_dependencies()) {
    if(dep->immediate_permissions() != darma_runtime::frontend::Permissions::None) {
      dep->get_data_pointer_reference() = dep->get_in_flow()->control_block->data;
    }
  }
  // set the running task
  auto* old_running_task = Runtime::running_task;
  Runtime::running_task = task_.get();

  task_->run();

  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.erase(task_.get());
  });

  // Delete the task object
  task_ = nullptr;

  // Reset the running task ptr
  Runtime::running_task = old_running_task;

  Runtime::instance->shutdown_trigger.decrement_count();

}
