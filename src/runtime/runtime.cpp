/*
//@HEADER
// ************************************************************************
//
//                      runtime.cpp
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

#if SIMPLE_BACKEND_USE_OPENMP
#include <omp.h>
#endif

#if SIMPLE_BACKEND_USE_KOKKOS
#  include <Kokkos_Core.hpp>
#  if SIMPLE_BACKEND_USE_FCONTEXT
#    include <boost/context/fcontext.hpp>
#    define FCONTEXT_STACK_SIZE 8 * 1024 * 1024
#  endif
#endif

#include "runtime.hpp"
#include "flow/flow.hpp"
#include "worker/worker.hpp"
#include "util.hpp"
#include "task_collection_token.hpp"

#include "debug.hpp"

#include <darma/interface/frontend/top_level.h>

#include <darma.h>


using namespace simple_backend;

std::unique_ptr<Runtime> Runtime::instance = nullptr;

thread_local
darma_runtime::abstract::frontend::Task* Runtime::running_task = nullptr;

thread_local
int Runtime::this_worker_id = 0;

thread_local
int Runtime::thread_stack_depth = 0;

std::atomic<std::size_t>
TaskCollectionToken::next_sequence_identifier = { 0 };

#if SIMPLE_BACKEND_USE_FCONTEXT
std::vector<boost::context::fcontext_t> Runtime::darma_contexts = {};
std::vector<boost::context::fcontext_t> Runtime::kokkos_contexts = {};
#endif

//==============================================================================

void
Runtime::initialize_top_level_instance(int argc, char** argv) {

  SimpleBackendOptions options;

  auto top_level_task = darma_runtime::frontend::darma_top_level_setup(
    options.parse_args(argc, argv)
  );

  instance = std::make_unique<Runtime>(std::move(top_level_task), options);
}

//==============================================================================

void
Runtime::wait_for_top_level_instance_to_shut_down() {
  for(int i = 1; i < instance->nthreads_; ++i) {
    instance->workers[i].join();
  }
}

//==============================================================================

// Construct from a task that is ready to run
Runtime::Runtime(task_unique_ptr&& top_level_task, SimpleBackendOptions const& options)
  : nthreads_(options.n_threads), shutdown_trigger(1), lookahead_(options.lookahead)
#if SIMPLE_BACKEND_USE_KOKKOS
    , n_kokkos_partitions(options.kokkos_partitions)
#endif
{
  shutdown_trigger.add_action([this]{
    for(int i = 0; i < nthreads_; ++i) {
      workers[i].ready_tasks.emplace_back(ReadyTaskHolder::AllTasksDone);
    }
  });

  // TODO in openmp mode, we may want to do this initialization on the thread that will own the worker (for locality purposes)
  // Create the workers
  for(size_t i = 0; i < nthreads_; ++i) {
    workers.emplace_back(i, nthreads_);
  }
  workers[0].ready_tasks.emplace_back(std::move(top_level_task));
}

//==============================================================================

Runtime::task_t*
Runtime::get_running_task() const { return running_task; }

//==============================================================================

void
Runtime::register_task(task_unique_ptr&& task) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  // PendingTaskHolder deletes itself, so this isn't a memory leak
  auto* holder = new PendingTaskHolder(std::move(task));
  holder->enqueue_or_run(
    // only run on the stack if the number of pending tasks is greater than
    // or equal to the lookahead
    pending_tasks_.load() >= lookahead_
    and thread_stack_depth < max_task_depth_
  );
}

//==============================================================================

void
Runtime::register_task_collection(task_collection_unique_ptr&& tc) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  auto tc_token = std::make_shared<TaskCollectionToken>(tc->size());
  tc->set_task_collection_token(tc_token);

  for(auto&& dep : tc->get_dependencies()) {
    if(dep->manages_collection()) {
      // currently only implemented for modify/modify task collections
      // TODO do this for read only and other stuff.  (probably need something like a UseCollectionToken...)
      assert(dep->is_anti_dependency());

      dep->get_in_flow()->parent_collection_token = tc_token;

    }
  }

  for(size_t i = 0; i < tc->size(); ++i) {
    // Enqueueing another task, so increment shutdown ready_trigger
    shutdown_trigger.increment_count();

    // PendingTaskHolder deletes itself, so this isn't a memory leak
    auto* holder = new PendingTaskHolder(tc->create_task_for_index(i));


    if(
      not darma_runtime::detail::key_traits<darma_runtime::types::key_t>::key_equal{}(
        tc->get_name(),
        darma_runtime::make_key()
      )
    ) {
      // TODO do this faster
      holder->task_->set_name(
        darma_runtime::make_key(
          std::string("backend index ") + std::to_string(i) + std::string(" of "),
          tc->get_name()
        )
      );
    }

    assert(this_worker_id < nthreads_ and this_worker_id >= 0);

    holder->enqueue_or_run((this_worker_id + i) % nthreads_,
      // Prevent immediate execution so that all tc indices get spawned
      // and have a chance to run concurrently
      // TODO this should be dependent on some lookahead variable
      /* allow_run_on_stack = */ false
    );
  }

  tc = nullptr;
  shutdown_trigger.decrement_count();
}

//==============================================================================

#if SIMPLE_BACKEND_USE_FCONTEXT
void darma_scheduler_context(intptr_t arg) {
  auto& args = *(std::tuple<int, size_t, int>*)arg;
  auto& worker_id = std::get<0>(args);
  auto& nthreads = std::get<1>(args);
  auto& threads_per_partition = std::get<2>(args);

  Runtime::instance->workers[
    worker_id
  ].run_work_loop(threads_per_partition);

}
#endif

// TODO move this to the worker_*.cpp files for all the rest of the cases
#if !SIMPLE_BACKEND_USE_KOKKOS || SIMPLE_BACKEND_USE_FCONTEXT
void
Runtime::spin_up_worker_threads()
{
#if SIMPLE_BACKEND_USE_OPENMP
#pragma omp parallel num_threads(nthreads_) proc_bind(spread)
  {
    workers[omp_get_thread_num()].run_work_loop(nthreads_, 1);
  }
  // End omp region
#elif SIMPLE_BACKEND_USE_KOKKOS
  const int threads_per_partition = nthreads_ / n_kokkos_partitions;
  Runtime::instance->ready_kokkos_tasks.resize(n_kokkos_partitions);
#if SIMPLE_BACKEND_USE_FCONTEXT
  Runtime::kokkos_contexts.resize(nthreads_);
  Runtime::darma_contexts.resize(nthreads_);
#endif

  Kokkos::OpenMP::partition_master([&](int partition_id, int n_partitions) {
#if SIMPLE_BACKEND_USE_FCONTEXT
    std::vector<void*> partition_stacks(threads_per_partition);
#pragma omp parallel num_threads(nthreads_ / n_kokkos_partitions)
    {
      partition_stacks[omp_get_thread_num()] = std::malloc(FCONTEXT_STACK_SIZE);
      auto worker_id = partition_id * threads_per_partition + omp_get_thread_num();
      Runtime::darma_contexts[worker_id] = boost::context::make_fcontext(
        partition_stacks[omp_get_thread_num()], FCONTEXT_STACK_SIZE, darma_scheduler_context
      );
      auto args = std::tuple<int, std::size_t, int>(
        worker_id, nthreads_, threads_per_partition
      );
      boost::context::jump_fcontext(
        &Runtime::kokkos_contexts[worker_id],
        Runtime::darma_contexts[worker_id],
        (std::intptr_t)(&args)
      );
    } // end partition parallel
#endif
    while(not Runtime::instance->shutdown_trigger.get_triggered()) {
#if SIMPLE_BACKEND_USE_FCONTEXT
      // jumped to run a Kokkos task, so run it
      auto ktask = Runtime::instance->ready_kokkos_tasks[partition_id].get_and_pop_front();
      assert(ktask);
      assert(ktask->task->is_data_parallel_task());
      workers[partition_id * threads_per_partition].run_task(std::move(ktask->task));

#pragma omp parallel num_threads(nthreads_ / n_kokkos_partitions)
      {
        auto worker_id = partition_id * threads_per_partition + omp_get_thread_num();
        boost::context::jump_fcontext(&kokkos_contexts[worker_id], darma_contexts[worker_id], 0);
      }
#else
      //#pragma omp parallel num_threads(nthreads_ / n_kokkos_partitions)
      Kokkos::parallel_for(threads_per_partition, [=](int i){

        workers[
          partition_id * threads_per_partition + i // omp_get_thread_num()
        ].run_work_loop(nthreads_, threads_per_partition);

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
#endif
    }

#if SIMPLE_BACKEND_USE_FCONTEXT
    for(auto* part_stack : partition_stacks) {
      std::free(part_stack);
    }
#endif

  }, n_kokkos_partitions, nthreads_ / n_kokkos_partitions);
#else
  // needs to be two seperate loops to make sure ready tasks is initialized on
  // all workers.  Also, only spawn threads on 1-n
  for(size_t i = 1; i < nthreads_; ++i) {
    workers[i].spawn_work_loop(1);
  }

  workers[0].run_work_loop(1);
#endif
}
#endif



//==============================================================================

namespace darma_runtime {
namespace abstract {
namespace backend {

Context* get_backend_context() { return simple_backend::Runtime::instance.get(); }
Runtime* get_backend_runtime() { return simple_backend::Runtime::instance.get(); }
MemoryManager* get_backend_memory_manager() { return simple_backend::Runtime::instance.get(); }

} // end namespace backend
} // end namespace abstract
} // end namespace darma_runtime
