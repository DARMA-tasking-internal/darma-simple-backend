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
#include <Kokkos_Core.hpp>
#endif

#include "runtime.hpp"
#include "flow.hpp"
#include "worker.hpp"
#include "util.hpp"
#include "task_collection_token.hpp"

#include "debug.hpp"

#include <darma/interface/frontend/top_level.h>

#include <darma.h>

#define COPY_ALL_PUBLISHES 1

using namespace simple_backend;

std::unique_ptr<Runtime> Runtime::instance = nullptr;

thread_local
darma_runtime::abstract::frontend::Task* Runtime::running_task = nullptr;

thread_local
std::size_t Runtime::this_worker_id = 0;

//==============================================================================

void
Runtime::initialize_top_level_instance(int argc, char** argv) {

  SimpleBackendOptions options;

  auto top_level_task = darma_runtime::frontend::darma_top_level_setup(
    options.parse_args(argc, argv)
  );

  instance = std::make_unique<Runtime>(std::move(top_level_task),
    options.n_threads,
    options.lookahead
  );
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
Runtime::Runtime(task_unique_ptr&& top_level_task, std::size_t nthreads, std::size_t lookahead)
  : nthreads_(nthreads), shutdown_trigger(1), lookahead_(lookahead)
{
  shutdown_trigger.add_action([this]{
    for(int i = 0; i < nthreads_; ++i) {
      workers[i].ready_tasks.emplace_back(nullptr);
    }
  });

  // TODO in openmp mode, we may want to do this initialization on the thread that will own the worker (for locality purposes)
  // Create the workers
  for(size_t i = 0; i < nthreads_; ++i) {
    workers.emplace_back(i);
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
  );
}

//==============================================================================

void
Runtime::register_task_collection(task_collection_unique_ptr&& tc) {
  // keep the workers from shutting down until this task is done
  shutdown_trigger.increment_count();

  tc->set_task_collection_token(std::make_shared<TaskCollectionToken>(tc->size()));

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
  Kokkos::OpenMP::partition_master([&](int partition_id, int n_partitions) {
    while(not Runtime::instance->shutdown_trigger.get_triggered()) {
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
    }

  }, n_kokkos_partitions, nthreads_ / n_kokkos_partitions);
#else
  // needs to be two seperate loops to make sure ready tasks is initialized on
  // all workers.  Also, only spawn threads on 1-n
  for(size_t i = 1; i < nthreads_; ++i) {
    workers[i].spawn_work_loop(nthreads_);
  }

  workers[0].run_work_loop(nthreads_);
#endif
}


//==============================================================================

void
Runtime::publish_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& pub_use,
  darma_runtime::abstract::frontend::PublicationDetails* details
) {
  assert(pub_use->get_in_flow()->control_block->parent_collection);

  auto parent_cntrl = pub_use->get_in_flow()->control_block->parent_collection;

  parent_cntrl->current_published_entries.evaluate_at(
    std::make_pair(
      details->get_version_name(),
      pub_use->get_in_flow()->control_block->collection_index
    ),
    [this, details, &pub_use] (PublicationTableEntry& entry) {
      // Create the entry if it doesn't already exist
      if(not entry.entry) {
        entry.entry = std::make_shared<PublicationTableEntry::Impl>();
      }

      // Advance the release trigger so that the data isn't released until
      // all of the fetchers have read it
      entry.entry->release_trigger.advance_count(details->get_n_fetchers());
#if COPY_ALL_PUBLISHES
      auto control_blk = pub_use->get_in_flow()->control_block;
      pub_use->get_in_flow()->ready_trigger.add_action([
        this, entry, control_blk, pub_use=std::move(pub_use)
      ]{
          *entry.entry->source_flow = std::make_shared<Flow>(
            std::make_shared<ControlBlock>(
              ControlBlock::copy_underlying_data_tag_t{},
              control_blk
            ),
            1 // so that we can make it ready when the copy completes
          );
          entry.entry->source_flow->get()->ready_trigger.decrement_count();
          release_use(
            darma_runtime::abstract::frontend::use_cast<
              darma_runtime::abstract::frontend::UsePendingRelease*
            >(pub_use.get())
          );
      });
#else
      *entry.entry->source_flow = pub_use->get_in_flow();

      // Increment the anti-out flow of the published entry, then add a decrement
      // to the release trigger
      auto& pub_anti_out = pub_use->get_anti_out_flow();
      pub_anti_out->ready_trigger.increment_count();
      entry.entry->release_trigger.add_action([
        this, pub_anti_out, pub_use=std::move(pub_use)
      ]{
        pub_anti_out->ready_trigger.decrement_count();
        release_use(
          darma_runtime::abstract::frontend::use_cast<
            darma_runtime::abstract::frontend::UsePendingRelease*
          >(pub_use.get())
        );
      });
#endif

      // The fetching trigger count starts at 1 so that it doesn't get triggered
      // while we're setting up the release
      entry.entry->fetching_trigger.decrement_count();
    }
  );

}

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
