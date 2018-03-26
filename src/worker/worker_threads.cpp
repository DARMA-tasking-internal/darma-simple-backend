/*
//@HEADER
// ************************************************************************
//
//                      worker_threads.cpp
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

#if !SIMPLE_BACKEND_USE_KOKKOS

#include "worker.hpp"
#include <runtime/runtime.hpp>
#include "ready_operation.hpp"

using namespace simple_backend;

Worker::Worker(Worker&& other) noexcept
  : ready_tasks_(std::move(other.ready_tasks_)),
    ready_tasks_non_stealable_(std::move(other.ready_tasks_non_stealable_)),
    id(other.id),
    thread_(std::move(other.thread_)),
    n_threads(other.n_threads)
{ }

void Worker::spawn_work_loop(int threads_per_partition) {

  thread_ = std::make_unique<std::thread>([this, threads_per_partition]{
    run_work_loop(threads_per_partition);
  });

}

void Worker::run_work_loop(int threads_per_partition) {

  Runtime::this_worker_id = id;

  // TODO reinstate tracking of "dorment" workers with Kokkos
  // until we get a real task, we're considered "dorment"
  ++Runtime::instance->dorment_workers;

  ready_operation_ptr ready_op = nullptr;

  while(true) {

    bool has_ready_op = false;
    // Prioritize non-stealable tasks (for now, at least)
    has_ready_op = ready_tasks_non_stealable_.pop(ready_op);
    if(not has_ready_op) {
      has_ready_op = ready_tasks_.pop(ready_op);
    }

    // If there are any tasks on the front of our queue, get them
    if(has_ready_op) {
      // pop_front was successful, run the task

      // We're not dorment any more
      --Runtime::instance->dorment_workers;

      bool should_break = run_operation(ready_op);

      // We're dorment again
      ++Runtime::instance->dorment_workers;

      if(should_break) break;

    } // end if any ready tasks exist
    else {
      // Stealable work should never tell us to break, so the returned boolean
      // tells us whether or not a steal happened. It's irrelevant, so we can
      // ignore it.
      try_to_steal_work();
    }

  } // end while true loop

}

void Worker::join() {
  assert(thread_);
  thread_->join();
}


#endif
