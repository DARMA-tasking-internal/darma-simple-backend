/*
//@HEADER
// ************************************************************************
//
//                      worker.cpp
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

#include <random>

#include "runtime.hpp"
#include "worker.hpp"

#include "debug.hpp"

using namespace simple_backend;

void Worker::run_task(Runtime::task_unique_ptr&& task) {

  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.insert(task.get());
    state.pending_tasks.erase(task.get());
  });

  // setup data
  for(auto&& dep : task->get_dependencies()) {
    if(dep->immediate_permissions() != Runtime::use_t::None) {
      dep->get_data_pointer_reference() = dep->get_in_flow()->control_block->data;
    }
  }
  // set the running task
  auto* old_running_task = Runtime::running_task;
  Runtime::running_task = task.get();

  task->run();

  _SIMPLE_DBG_DO([&](auto& state){
    state.running_tasks.erase(task.get());
  });

  // Delete the task object
  task = nullptr;

  // Reset the running task ptr
  Runtime::running_task = old_running_task;

  Runtime::instance->shutdown_trigger.decrement_count();

}

void Worker::run_work_loop(size_t n_threads_total) {

  std::random_device rd;
  std::seed_seq seed{ rd(), rd(),rd(), rd(), rd(), rd(), rd(), rd(), rd() };
  std::mt19937 steal_gen(seed);
  std::uniform_int_distribution<> steal_dis(n_threads_total - 1);

  Runtime::this_worker_id = id;

  while(true) {
    auto ready = ready_tasks.get_and_pop_front();
    // "Random" version:
    //auto ready = steal_dis(steal_gen) % 2 == 0 ?
    //  ready_tasks.get_and_pop_front() : ready_tasks.get_and_pop_back();

    // If there are any tasks on the front of our queue, get them
    if(ready) {
      // pop_front was successful, run the task

      // if it's null, this is the signal to stop the workers
      if(ready->get() == nullptr) { break; }
      else { run_task(std::move(*ready.get())); }

    } // end if any ready tasks exist
    else {
      // pop_front failed because queue was empty; try to do a steal
      auto steal_from = (steal_dis(steal_gen) + id) % n_threads_total;

      auto new_ready = Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_back();
      // "Random" version:
      //auto new_ready = steal_dis(steal_gen) % 2 == 0 ?
      //  Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_back()
      //    : Runtime::instance->workers[steal_from].ready_tasks.get_and_pop_front();
      if(new_ready) {
        if (new_ready->get() == nullptr) {
          // oops, we stole the termination signal.  Put it back!
          Runtime::instance->workers[steal_from].ready_tasks.emplace_back(
            nullptr
          );
        } else {
          run_task(std::move(*new_ready.get()));
        }
      }
    }
  } // end while true loop

}

void Worker::spawn_work_loop(size_t n_threads_total) {

  thread_ = std::make_unique<std::thread>([this, n_threads_total]{
    run_work_loop(n_threads_total);
  });

}
