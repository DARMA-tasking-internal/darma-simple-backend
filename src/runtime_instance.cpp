/*
//@HEADER
// ************************************************************************
//
//                      runtime_instance.cpp
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

#include <config.hpp>

#include "runtime_instance.hpp"
#include "debug.hpp"

#ifdef DARMA_SIMPLE_BACKEND_HAS_LIBCDS
#  include <cds/init.h>
#  include <cds/gc/dhp.h>
#endif

#ifdef SIMPLE_BACKEND_DEBUG
#  include <csignal>
#  include <unistd.h> // getpid()
#endif

static bool cds_initialized = false;

using namespace simple_backend;
using namespace darma_runtime;

#if SIMPLE_BACKEND_DEBUG
extern "C" {
void sig_usr2_handler(int signal) {
  simple_backend::DebugWorker::instance->empty_queue_actions.emplace(
    ::simple_backend::make_debug_action_ptr([](auto& state){
      ::simple_backend::DebugState::print_state(state);
    })
  );
}
} // end extern "C"
#endif

darma_runtime::types::runtime_instance_token_t
darma_runtime::backend::initialize_runtime_instance() {
  return std::make_shared<simple_backend::Runtime>();
}

void
darma_runtime::backend::register_runtime_instance_quiescence_callback(
  darma_runtime::types::runtime_instance_token_t& token,
  std::function<void()> callback
) {
  token->register_quiescence_callback(callback);
}

void
darma_runtime::backend::with_active_runtime_instance(
  darma_runtime::types::runtime_instance_token_t& token,
  std::function<void()> callback
) {
  simple_backend::Runtime::instance = token;

  token->shutdown_counter.attach_action([token]{
    token->workers[0].enqueue_ready_operation(
      simple_backend::make_nonstealable_callable_operation([token]{
        for(int i = 1; i < token->nthreads_; ++i) {
          token->workers[i].join();
        }
      })
    );
    for(int i = 0; i < token->nthreads_; ++i) {
      token->workers[i].enqueue_ready_operation(
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
  simple_backend::Runtime::wait_for_top_level_instance_to_shut_down();

  simple_backend::Runtime::instance = nullptr;

  token = std::make_shared<simple_backend::Runtime>();
}


void
darma_runtime::backend::initialize_with_arguments(int& argc, char**& argv) {

  #ifdef DARMA_SIMPLE_BACKEND_HAS_LIBCDS
  if(!cds_initialized) {
    cds::Initialize();
    Runtime::gc_instance_ = std::make_unique<cds::gc::DHP>();
    // attach the main thread to the cds threading manager
    cds::threading::Manager::attachThread();
  }
  #endif

  #if SIMPLE_BACKEND_DEBUG
  std::cout << "Running program with simple debug backend enabled.  Send signal SIGUSR2"
               " to process " << std::to_string(getpid()) << " to introspect state." << std::endl;
  std::signal(SIGUSR2, sig_usr2_handler);

  simple_backend::DebugWorker::instance->spawn_work_loop();
  #endif

  static std::vector<std::string> args;
  simple_backend::SimpleBackendOptions options;
  args = options.parse_args(argc, argv);

  // Update argc and argv
  assert(argc >= args.size());
  argc = args.size();
  for(int i = 0; i < argc; ++i) argv[i] = const_cast<char*>(args[i].c_str());

  simple_backend::Runtime::set_default_options(options);
}


void darma_runtime::backend::finalize() {
  #if SIMPLE_BACKEND_DEBUG
  simple_backend::DebugWorker::instance->actions.emplace(nullptr);
  simple_backend::DebugWorker::instance->worker_thread->join();
  simple_backend::DebugWorker::instance = nullptr;
  #endif

  #ifdef DARMA_SIMPLE_BACKEND_HAS_LIBCDS
  if(cds_initialized) {
    Runtime::gc_instance_ = nullptr;
    cds::Terminate();
  }
  #endif
}
