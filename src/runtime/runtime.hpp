/*
//@HEADER
// ************************************************************************
//
//                      runtime.hpp
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
// Questions? Contact darma@sandia.gov
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

#include <worker/worker.hpp>
#include <flow/flow.hpp>
#include <flow/aliasing_strategy.hpp>

#include "parse_arguments.hpp"

#include <config.hpp>

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

#if SIMPLE_BACKEND_USE_KOKKOS
    size_t n_kokkos_partitions;
#endif

    size_t lookahead_;

    types::aliasing_strategy_t aliasing_strategy_;


    static SimpleBackendOptions default_options_;

    void _init_shutdown_counter();

    void _create_workers();

  public:


    static void set_default_options(SimpleBackendOptions const& options) {
      default_options_ = options;
    }

    Runtime(
      task_unique_ptr&& top_level_task,
      SimpleBackendOptions const& options
    );

    // DARMA region version
    Runtime();

    void register_quiescence_callback(std::function<void()> const& callback) {
      instance_quiescence_callbacks_.push_back(callback);
    }

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
      size_t n_bytes
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

    void
    reregister_migrated_use(
      darma_runtime::abstract::frontend::RegisteredUse* u
    ) override
    {
      assert(false); // No migration calls should ever be made.
    }

    size_t
    get_packed_flow_size(
      darma_runtime::types::flow_t const& f
    ) override
    {
      assert(false); // No migration calls should ever be made.
      return 0;
    }

    void
    pack_flow(
      darma_runtime::types::flow_t& f,
      void*& buffer
    ) override
    {
      assert(false); // No migration calls should ever be made.
    }

    darma_runtime::types::flow_t
    make_unpacked_flow(
      void const*& buffer
    ) override
    {
      assert(false);
      return {};
    }

    size_t
    get_packed_anti_flow_size(
      darma_runtime::types::anti_flow_t const& f
    ) override
    {
      assert(false); // No migration calls should ever be made.
      return 0;
    }

    void
    pack_anti_flow(
      darma_runtime::types::anti_flow_t& f,
      void*& buffer
    ) override
    {
      assert(false); // No migration calls should ever be made.
    }

    darma_runtime::types::anti_flow_t
    make_unpacked_anti_flow(
      void const*& buffer
    ) override
    {
      assert(false);
      return {};
    }
    // </editor-fold> end all migration-related functions are implemented, but they just assert }}}2
    //--------------------------------------------------------------------------

    std::vector<std::function<void()>> instance_quiescence_callbacks_;

    // Maximum concurrency
    size_t nthreads_;

    static std::shared_ptr<Runtime> instance;

    static thread_local darma_runtime::abstract::frontend::Task* running_task;
    static thread_local int this_worker_id;
    static thread_local int thread_stack_depth;
    // TODO expose this as a command line option
    size_t max_task_depth = 0;
    std::atomic<size_t> pending_tasks = { 0 };

    JoinCounter shutdown_counter;
    std::atomic<size_t> dorment_workers = { 0 };

    using piecewise_handle_map_t = std::unordered_map<
      darma_runtime::types::key_t,
      darma_runtime::types::piecewise_collection_token_t
    >;
    using persistent_handle_map_t = std::unordered_map<
      darma_runtime::types::key_t,
      darma_runtime::types::persistent_collection_token_t
    >;
    piecewise_handle_map_t piecewise_handles;
    persistent_handle_map_t persistent_handles;
    std::unique_ptr<darma_runtime::abstract::frontend::Task> distributed_region_running_task = nullptr;


    #if SIMPLE_BACKEND_USE_KOKKOS
    std::vector<std::unique_ptr<types::thread_safe_queue_t<ReadyOperation*>>> ready_kokkos_tasks;
    #endif

    #if SIMPLE_BACKEND_USE_FCONTEXT
    static std::vector<boost::context::fcontext_t> darma_contexts;
    static std::vector<boost::context::fcontext_t> kokkos_contexts;
    #endif

    #ifdef DARMA_SIMPLE_BACKEND_HAS_LIBCDS
    static std::unique_ptr<cds::gc::DHP> gc_instance_;
    #endif

    std::vector<Worker> workers;

    void spin_up_worker_threads();

  public:

    static void initialize_top_level_instance(int argc, char** argv);

    static void wait_for_top_level_instance_to_shut_down();

};



} // end namespace simple_backend



#endif //DARMASIMPLECVBACKEND_RUNTIME_HPP
