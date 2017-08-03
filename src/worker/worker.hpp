/*
//@HEADER
// ************************************************************************
//
//                      worker.hpp
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

#ifndef DARMASIMPLECVBACKEND_WORKER_HPP
#define DARMASIMPLECVBACKEND_WORKER_HPP

#include <thread>
#include <cassert>

#include <darma/interface/backend/runtime.h>

#include "data_structures/concurrent_list.hpp"
#include "ready_task_holder.hpp"

#ifndef SIMPLE_BACKEND_ENABLE_WORK_STEALING
#  if !SIMPLE_BACKEND_DISABLE_WORK_STEALING
#    define SIMPLE_BACKEND_ENABLE_WORK_STEALING 1
#  else
#    define SIMPLE_BACKEND_ENABLE_WORK_STEALING 0
#  endif
#endif

#if SIMPLE_BACKEND_ENABLE_WORK_STEALING
#  include <random>
#endif

namespace simple_backend {

struct Worker {
  private:

#if !SIMPLE_BACKEND_USE_KOKKOS
    std::unique_ptr<std::thread> thread_;
#endif

#if SIMPLE_BACKEND_ENABLE_WORK_STEALING
    std::random_device work_stealing_random_device;
    std::seed_seq work_stealing_seed_sequence;
    std::mt19937 steal_generator;
    std::uniform_int_distribution<> steal_distribution;
#endif

    void setup_work_stealing();

    bool try_to_steal_work();

  public:

    using task_unique_ptr = darma_runtime::abstract::backend::Runtime::task_unique_ptr;

    ConcurrentDeque<ReadyTaskHolder> ready_tasks;

    int id = -1;
    int n_threads;

    explicit
    Worker(int id, int n_threads) : id(id), n_threads(n_threads) {
      setup_work_stealing();
    }

    Worker(Worker&& other) noexcept;

    Worker(Worker const&) = delete;
    Worker& operator=(Worker const&) = delete;
    Worker& operator=(Worker&&) = default;

    void spawn_work_loop(int threads_per_partition);

    void run_work_loop(int threads_per_partition);

    void run_task(task_unique_ptr&& task);

    void join();

    ~Worker() = default;

};


} // end namespace simple_backend

#endif //DARMASIMPLECVBACKEND_WORKER_HPP
