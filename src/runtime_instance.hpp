/*
//@HEADER
// ************************************************************************
//
//                      runtime_instance.h
//                         DARMA
//              Copyright (C) 2018 Sandia Corporation
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

#ifndef DARMASIMPLEBACKEND_RUNTIME_INSTANCE_HPP
#define DARMASIMPLEBACKEND_RUNTIME_INSTANCE_HPP

#include <darma_types.h>

#include <darma/interface/backend/runtime.h>

#include "runtime/runtime.hpp"

#include <darma/interface/app/darma_region.h>
#include <darma/interface/backend/darma_region.h>

namespace darma_runtime {
namespace backend {

types::runtime_instance_token_t
initialize_runtime_instance() {
  return new simple_backend::Runtime();
}

void
register_runtime_instance_quiescence_callback(
  types::runtime_instance_token_t& token,
  std::function<void()> callback
) {
  token->register_quiescence_callback(callback);
}

void
with_active_runtime_instance(
  types::runtime_instance_token_t& token,
  std::function<void()> callback
) {
  simple_backend::Runtime::instance = std::unique_ptr<simple_backend::Runtime>(token);

  token->shutdown_counter.attach_action([token]{
    token->workers[0].ready_tasks.emplace(
      simple_backend::make_nonstealable_callable_operation([token]{
        for(int i = 1; i < token->nthreads_; ++i) {
          token->workers[i].join();
        }
      })
    );
    for(int i = 0; i < token->nthreads_; ++i) {
      token->workers[i].ready_tasks.emplace(
        std::make_unique<simple_backend::SpecialMessage>(simple_backend::ReadyOperation::AllTasksDone)
      );
    }
    for(auto&& cb : token->instance_quiescence_callbacks_) {
      cb();
    }
  });

  token = nullptr;

  auto top_level_running_task =
    darma_runtime::frontend::make_empty_running_task();
  simple_backend::Runtime::instance->running_task = top_level_running_task.get();

  callback();

  top_level_running_task = nullptr;

  simple_backend::Runtime::instance->running_task = nullptr;

  simple_backend::Runtime::instance->shutdown_counter.decrement_count();

  simple_backend::Runtime::instance->spin_up_worker_threads();

  token = new simple_backend::Runtime();
}

void initialize_runtime_arguments(int& argc, char**& argv) {
  static std::vector<std::string> args;
  simple_backend::SimpleBackendOptions options;
  args = options.parse_args(argc, argv);

  // Update argc and argv
  assert(argc >= args.size());
  argc = args.size();
  for(int i = 0; i < argc; ++i) argv[i] = const_cast<char*>(args[i].c_str());

  simple_backend::Runtime::set_default_options(options);
}

} // end namespace backend
} // end namespace darma

#endif //DARMASIMPLEBACKEND_RUNTIME_INSTANCE_HPP
