/*
//@HEADER
// ************************************************************************
//
//                      worker_openmp.cpp
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

#if SIMPLE_BACKEND_USE_KOKKOS && !SIMPLE_BACKEND_USE_FCONTEXT

#include <Kokkos_Core.hpp>

#include <runtime/runtime.hpp>
#include "worker.hpp"

using namespace simple_backend;

void
Runtime::spin_up_worker_threads()
{
  const int threads_per_partition = nthreads_ / n_kokkos_partitions;

  Runtime::instance->ready_kokkos_tasks.resize(n_kokkos_partitions);

  Kokkos::OpenMP::partition_master([&](int partition_id, int n_partitions) {
    while(not Runtime::instance->shutdown_trigger.get_triggered()) {
      //#pragma omp parallel num_threads(nthreads_ / n_kokkos_partitions)
      Kokkos::parallel_for(threads_per_partition, [=](int i){

        workers[
          partition_id * threads_per_partition + i // omp_get_thread_num()
        ].run_work_loop(threads_per_partition);

      }); // end partition parallel

      if (Runtime::instance->shutdown_trigger.get_triggered()) {
        break;
      } else {
        assert(Kokkos::Impl::t_openmp_instance);
        // Exited to run a Kokkos task, so run it
        auto ktask =
          Runtime::instance->ready_kokkos_tasks[partition_id].get_and_pop_front();
        assert(Kokkos::Impl::t_openmp_instance);
        assert(ktask);
        assert(ktask->task->is_data_parallel_task());
        workers[partition_id
          * threads_per_partition].run_task(std::move(ktask->task));

      }
    }

  }, n_kokkos_partitions, nthreads_ / n_kokkos_partitions);

}

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
        break;
      }
      else if(ready->task->is_data_parallel_task()) {
        size_t partition = id / threads_per_partition;
        size_t master = partition * threads_per_partition;
        Runtime::instance->ready_kokkos_tasks[partition].emplace_back(
          std::move(*ready)
        );

        // tell everyone in my partition to break
        for(size_t iworker = master; iworker < master + threads_per_partition; ++iworker) {
          if(iworker != id) {
            Runtime::instance->workers[iworker].ready_tasks.emplace_front(
              ReadyTaskHolder::NeededForKokkosWork
            );
          }
        }

        // We also need to break out of the omp parallel region that we're running in
        break;
      }
      else {
        run_task(std::move(ready->task));
      }

      ++Runtime::instance->dorment_workers;

    } // end if any ready tasks exist
    else {
      try_to_steal_work();
    } // end else

  } // end while true loop

}


#endif
