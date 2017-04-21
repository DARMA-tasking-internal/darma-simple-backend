/*
//@HEADER
// ************************************************************************
//
//                      runtime.hpp
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

#ifndef DARMASIMPLECVBACKEND_RUNTIME_HPP
#define DARMASIMPLECVBACKEND_RUNTIME_HPP

#include <memory>

#include <darma/interface/backend/runtime.h>

#include "concurrent_list.hpp"
#include "trigger.hpp"
#include "worker.hpp"
#include "flow.hpp"

namespace simple_backend {

class Runtime
  : public darma_runtime::abstract::backend::Runtime,
    public darma_runtime::abstract::backend::MemoryManager,
    public darma_runtime::abstract::backend::Context
{
  public:

    using task_t = darma_runtime::abstract::backend::Runtime::task_t;
    using task_unique_ptr = darma_runtime::abstract::backend::Runtime::task_unique_ptr;
    using use_pending_registration_t = darma_runtime::abstract::frontend::UsePendingRegistration;
    using use_pending_release_t = darma_runtime::abstract::backend::Runtime::use_pending_release_t;

  private:

    struct PendingTaskHolder {
      private:
        task_unique_ptr task_;
        CountdownTrigger<SingleAction> trigger_;

      public:
        PendingTaskHolder(task_unique_ptr&& task);
        void enqueue_or_run();
        void enqueue_or_run(size_t worker_id, bool allow_run_on_stack = true);
    };

    // Maximum concurrency
    size_t nthreads_ = 4;

  public:

    Runtime(
      task_unique_ptr&& top_level_task,
      size_t n_threads
    );

    task_t* get_running_task() const override;

    void register_task(task_unique_ptr&& task) override;

    void register_use(use_pending_registration_t*) override;

    void release_use(use_pending_release_t*) override;

    void register_task_collection(task_collection_unique_ptr&& collection) override;

    void publish_use(
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&&,
      darma_runtime::abstract::frontend::PublicationDetails*
    ) override;

    void* allocate(
      size_t n_bytes,
      darma_runtime::abstract::frontend::MemoryRequirementDetails const&
    ) override {
      return ::operator new(n_bytes);
    }

    void deallocate(void* ptr, size_t n_bytes) override {
      ::operator delete(ptr);
    }

    size_t get_execution_resource_count(size_t depth) const override {
      if(depth == 0) return nthreads_;
      else return 1; // for now
    }

    static std::unique_ptr<Runtime> instance;

    CountdownTrigger<SingleAction> shutdown_trigger;

    std::vector<Worker> workers;

    void spin_up_worker_threads();

  public:

    static void initialize_top_level_instance(int argc, char** argv);

    static void wait_for_top_level_instance_to_shut_down();

};

} // end namespace simple_backend



#endif //DARMASIMPLECVBACKEND_RUNTIME_HPP
