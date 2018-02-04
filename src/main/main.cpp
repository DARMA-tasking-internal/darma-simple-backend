/*
//@HEADER
// ************************************************************************
//
//                        main.cpp
//                         DARMA
//              Copyright (C) 2017 NTESS, LLC
//
// Under the terms of Contract DE-NA-0003525 with NTESS, LLC,
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
// Questions? Contact darma@sandia.gov
//
// ************************************************************************
//@HEADER
*/

#if SIMPLE_BACKEND_USE_KOKKOS
#include <Kokkos_Core.hpp>
#endif

#include "runtime/runtime.hpp"
#include "debug.hpp"

#if SIMPLE_BACKEND_DEBUG

#include <signal.h>
#include <csignal>
#include <unistd.h>

extern "C" {
void sig_usr2_handler(int signal) {
  simple_backend::DebugWorker::instance->empty_queue_actions.emplace_back(
    ::simple_backend::make_debug_action_ptr([](auto& state){
      ::simple_backend::DebugState::print_state(state);
    })
  );
}
} // end extern "C"
#endif

int main(int argc, char** argv) {

#if SIMPLE_BACKEND_USE_KOKKOS
  Kokkos::initialize(argc, argv);
#endif

#if SIMPLE_BACKEND_DEBUG

  std::cout << "Running program with simple debug backend enabled.  Send signal SIGUSR2"
    " to process " << std::to_string(getpid()) << " to introspect state." << std::endl;
  std::signal(SIGUSR2, sig_usr2_handler);


  simple_backend::DebugWorker::instance->spawn_work_loop();
#endif

  simple_backend::Runtime::initialize_top_level_instance(argc, argv);
  simple_backend::Runtime::instance->spin_up_worker_threads();
  simple_backend::Runtime::wait_for_top_level_instance_to_shut_down();

#if SIMPLE_BACKEND_DEBUG
  simple_backend::DebugWorker::instance->actions.emplace_back(nullptr);
  simple_backend::DebugWorker::instance->worker_thread->join();
#endif

#if SIMPLE_BACKEND_USE_KOKKOS
  Kokkos::finalize();
#endif

}

namespace darma_runtime {

void
abort(std::string const& abort_str) {
  std::cerr << "Aborting with message: " << abort_str << std::endl;
  std::abort();
}

} // end namespace darma_runtime