/*
//@HEADER
// ************************************************************************
//
//                      worker_openmp_fcontext.cpp
//                         DARMA
//              Copyright (C) 2017 Sandia Corporation
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
// Questions? Contact the DARMA developers (darma-admins@sandia.gov)
//
// ************************************************************************
//@HEADER
*/


#if SIMPLE_BACKEND_USE_KOKKOS && SIMPLE_BACKEND_USE_FCONTEXT

#include <Kokkos_Core.hpp>
#include <boost/context/fcontext.hpp>

// Use a relatively large default stack size for now
#ifndef FCONTEXT_STACK_SIZE
#  define FCONTEXT_STACK_SIZE 8 * 1024 * 1024
#endif

#include <random>

#include "runtime/runtime.hpp"
#include "worker.hpp"

#include "debug.hpp"

using namespace simple_backend;


Worker::Worker(Worker&& other) noexcept
  : ready_tasks(std::move(other.ready_tasks)),
    id(other.id),
    n_threads(other.n_threads)
{ }


void Worker::spawn_work_loop(int threads_per_partition) {
  // Should never be called:
  assert(false);
}

void Worker::join() {
  // Should do nothing in this implementation
}

void Worker::run_work_loop(int threads_per_partition) {


  Runtime::this_worker_id = id;

  // TODO reinstate tracking of "dorment" workers with Kokkow
  // until we get a real task, we're considered "dorment"
  ++Runtime::instance->dorment_workers;

  while(true) {

    auto ready = ready_tasks.get_and_pop_front();

    // If there are any tasks on the front of our queue, get them
    if(ready) {
      // pop_front was successful, run the task

      // We're not dorment any more
      --Runtime::instance->dorment_workers;

      // if it's null, this is the signal to stop the workers
      if(ready->task.get() == nullptr) {
        // It's a special message; currently both mean "break", so do that.
        if(ready->message == ReadyTaskHolder::NeededForKokkosWork) {
          boost::context::jump_fcontext(
            &Runtime::darma_contexts[id], Runtime::kokkos_contexts[id], 0
          );
        }
        else {
          assert(ready->message == ReadyTaskHolder::AllTasksDone);
          break;
        }
      }
      else if(ready->task->is_data_parallel_task()) {
        int partition = id / threads_per_partition;
        int master = partition * threads_per_partition;
        Runtime::instance->ready_kokkos_tasks[partition].emplace_back(
          std::move(*ready)
        );
        // tell everyone in my partition to jump contexts
        for(size_t iworker = master; iworker < master + threads_per_partition; ++iworker) {
          // except don't send a message to ourselves...
          if(iworker != id) {
            Runtime::instance->workers[iworker].ready_tasks.emplace_front(
              ReadyTaskHolder::NeededForKokkosWork
            );
          }
        }
        // Jump back to the context where parallel tasks can be run
        boost::context::jump_fcontext(
          &Runtime::darma_contexts[id], Runtime::kokkos_contexts[id], 0
        );
      }
      else {
        // Run the task as usual
        run_task(std::move(ready->task));
      }

      ++Runtime::instance->dorment_workers;

    } // end if any ready tasks exist
    else {
      try_to_steal_work();
    } // end else

  } // end while true loop

}


#endif // SIMPLE_BACKEND_USE_KOKKOS && SIMPLE_BACKEND_USE_FCONTEXT

// TODO decrementing here is probably not the best idea (could cause livelock).  We should use a lock or something to accomplish the same effect
//      else if(Runtime::instance->dorment_workers-- == n_threads_total) {
//
//        // We need to break a publish antidependency via copy.
//        // We might not actually need to do so, but as long as our overheads are
//        // low, we shouldn't "accidentally" end up here very often
//        // We *should* be the only ones able to get here at any given time.
//
//        //std::printf(
//        //  "Reached state where all threads are dormant; may have"
//        //  "publish-fetch anti-dependency-induced deadlock\n"
//        //);
//
//        // General strategy:
//        //   * grab an anti-in flow that began life as an indexed_fetching anti-out flow
//        //   * increment the ready trigger so that the publication entry becoming ready
//        //       and decrement it doesn't cause it to become ready (we'll have to be careful with this!!!)
//        //   * get the control block of the related in flow.  (check if it's ready;
//        //     if not, undo all of the above stuff and pick another one)
//        //   * copy the data in that control block to a new control block.
//        //   * point the in flow to that control block
//        //   * mark a flag or something so that the release by the publication entry
//        //     won't make the anti-out flow ready trigger freak out about going below 0
//        //   * release the anti-in flow
//
//        ++Runtime::instance->dorment_workers;
//
//      }
//      else {
//        // Re-increment to account for the atomic fetch-decrement in the previous if statement
//        ++Runtime::instance->dorment_workers;
//      }

