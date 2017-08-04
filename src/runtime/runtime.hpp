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

#if SIMPLE_BACKEND_USE_FCONTEXT
#  include <boost/context/fcontext.hpp>
#endif

#include <memory>

#include <darma/interface/backend/runtime.h>

#include "data_structures/concurrent_list.hpp"
#include "data_structures/trigger.hpp"
#include "worker/worker.hpp"
#include "flow/flow.hpp"

#include "parse_arguments.hpp"

// TODO this should be included by just including <darma.h> in the future (or there should be a shorter version that can be included...)
#include <darma/impl/serialization/allocator.impl.h>

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

    struct PendingTaskHolder {
      public:

        task_unique_ptr task_;
        CountdownTrigger<SingleAction> trigger_;

        PendingTaskHolder(task_unique_ptr&& task);
        ~PendingTaskHolder();
        void enqueue_or_run(bool allow_run_on_stack);
        void enqueue_or_run(size_t worker_id, bool allow_run_on_stack);
    };


  private:

    // Maximum concurrency
    size_t nthreads_;
#if SIMPLE_BACKEND_USE_KOKKOS
    size_t n_kokkos_partitions;
#endif

    size_t lookahead_;
    // TODO expose this as a command line option
    size_t max_task_depth_ = 10;
    std::atomic<size_t> pending_tasks_ = { 0 };

  public:

    Runtime(
      task_unique_ptr&& top_level_task,
      SimpleBackendOptions const& options
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

    void
    allreduce_use(
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_in_out,
      darma_runtime::abstract::frontend::CollectiveDetails const* details,
      darma_runtime::types::key_t const& tag
    ) override;

    void
    allreduce_use(
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_in,
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_out,
      darma_runtime::abstract::frontend::CollectiveDetails const* details,
      darma_runtime::types::key_t const& tag
    ) override;

    void
    reduce_collection_use(
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_collection_in,
      std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& use_out,
      darma_runtime::abstract::frontend::CollectiveDetails const* details,
      darma_runtime::types::key_t const& tag
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


    //--------------------------------------------------------------------------
    // <editor-fold desc="all migration-related functions are implemented, but they just assert"> {{{2

    virtual void
    reregister_migrated_use(
      darma_runtime::abstract::frontend::RegisteredUse* u
    ) override
    {
      assert(false); // No migration calls should ever be made.
    }

    virtual size_t
    get_packed_flow_size(
      darma_runtime::types::flow_t const& f
    ) override
    {
      assert(false); // No migration calls should ever be made.
      return 0;
    }

    virtual void
    pack_flow(
      darma_runtime::types::flow_t& f,
      void*& buffer
    ) override
    {
      assert(false); // No migration calls should ever be made.
    }

    virtual darma_runtime::types::flow_t
    make_unpacked_flow(
      void const*& buffer
    ) override
    {
      assert(false);
      return {};
    }

    // </editor-fold> end all migration-related functions are implemented, but they just assert }}}2
    //--------------------------------------------------------------------------

    static std::unique_ptr<Runtime> instance;
    static thread_local darma_runtime::abstract::frontend::Task* running_task;
    static thread_local int this_worker_id;
    static thread_local int thread_stack_depth;

    CountdownTrigger<SingleAction> shutdown_trigger;
    std::atomic<size_t> dorment_workers = { 0 };

    #if SIMPLE_BACKEND_USE_KOKKOS
    std::vector<std::unique_ptr<boost::lockfree::queue<ReadyTaskHolder*>>> ready_kokkos_tasks;
    #endif

    #if SIMPLE_BACKEND_USE_FCONTEXT
    static std::vector<boost::context::fcontext_t> darma_contexts;
    static std::vector<boost::context::fcontext_t> kokkos_contexts;
    #endif

    std::vector<Worker> workers;

    void spin_up_worker_threads();

  public:

    static void initialize_top_level_instance(int argc, char** argv);

    static void wait_for_top_level_instance_to_shut_down();

};



} // end namespace simple_backend



#endif //DARMASIMPLECVBACKEND_RUNTIME_HPP
