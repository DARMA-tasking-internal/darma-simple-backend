/*
//@HEADER
// ************************************************************************
//
//                      debug.hpp
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


#ifndef DARMASIMPLECVBACKEND_DEBUG_HPP
#define DARMASIMPLECVBACKEND_DEBUG_HPP

#ifdef SIMPLE_BACKEND_DEBUG

#include <utility>
#include <set>
#include <memory>

#include <darma/interface/frontend/use.h>

#include "concurrent_list.hpp"

namespace simple_backend {

struct DebugState {

  std::set<darma_runtime::abstract::frontend::Use*> registered_uses;

  template <typename UseT>
  void add_registered_use(UseT* use) {
    registered_uses.insert(
      darma_runtime::abstract::frontend::use_cast<
        darma_runtime::abstract::frontend::Use*
      >(use)
    );
  }

  template <typename UseT>
  void remove_registered_use(UseT* use) {
    registered_uses.erase(
      darma_runtime::abstract::frontend::use_cast<
        darma_runtime::abstract::frontend::Use*
      >(use)
    );
  }

  static void print_use(
    darma_runtime::abstract::frontend::Use* use,
    std::string indent = ""
  ) {
    std::cerr << indent << std::hex << "Use object at: 0x" << std::intptr_t(use);
    auto* reg_use = darma_runtime::abstract::frontend::use_cast<
      darma_runtime::abstract::frontend::RegisteredUse*
    >(use);

    std::cerr << std::endl << indent << "  ";
    if(reg_use->get_in_flow()) {
      auto& in_flow = reg_use->get_in_flow();
      std::cerr << "in_flow at: " << std::hex << std::intptr_t(in_flow.get()) << std::endl;
      std::cerr << indent << "    ready: " << std::boolalpha << in_flow->ready_trigger.get_triggered() << std::endl;
      std::cerr << indent << "    ready trigger count: " << std::boolalpha << in_flow->ready_trigger.get_count() << std::endl;
    }
    else {
      std::cerr << "in_flow is nullptr" << std::endl;
    }

    std::cerr << indent << "  ";
    if(reg_use->get_out_flow()) {
      auto& out_flow = reg_use->get_out_flow();
      std::cerr << "out_flow at: " << std::hex << std::intptr_t(out_flow.get()) << std::endl;
      std::cerr << indent << "    ready: " << std::boolalpha << out_flow->ready_trigger.get_triggered() << std::endl;
      std::cerr << indent << "    ready trigger count: " << std::boolalpha << out_flow->ready_trigger.get_count() << std::endl;
    }
    else {
      std::cerr << "out_flow is nullptr" << std::endl;
    }

    std::cerr << indent << "  ";
    if(reg_use->get_anti_in_flow()) {
      auto& anti_in_flow = reg_use->get_anti_in_flow();
      std::cerr << "anti_in_flow at: " << std::hex << std::intptr_t(anti_in_flow.get()) << std::endl;
      std::cerr << indent << "    ready: " << std::boolalpha << anti_in_flow->ready_trigger.get_triggered() << std::endl;
      std::cerr << indent << "    ready trigger count: " << std::boolalpha << anti_in_flow->ready_trigger.get_count() << std::endl;
    }
    else {
      std::cerr << "anti_in_flow is nullptr" << std::endl;
    }

    std::cerr << indent << "  ";
    if(reg_use->get_anti_out_flow()) {
      auto& anti_out_flow = reg_use->get_anti_out_flow();
      std::cerr << "anti_out_flow at: " << std::hex << std::intptr_t(anti_out_flow.get()) << std::endl;
      std::cerr << indent << "    ready: " << std::boolalpha << anti_out_flow->ready_trigger.get_triggered() << std::endl;
      std::cerr << indent << "    ready trigger count: " << std::boolalpha << anti_out_flow->ready_trigger.get_count() << std::endl;
    }
    else {
      std::cerr << "anti_out_flow is nullptr" << std::endl;
    }
  }

  static void print_state(DebugState& state) {
    std::cerr << "Registered Uses:" << std::endl;
    for(auto* use : state.registered_uses) {
      print_use(use, "  ");
      std::cerr << std::endl;
    }
  }

};

struct DebugActionBase {
  virtual void run(DebugState& state) =0;
  virtual ~DebugActionBase() = default;
};




template <typename Callable>
struct DebugAction : DebugActionBase {
  private:

    Callable callable_;

  public:

    DebugAction(Callable&& callable)
      : callable_(std::move(callable))
    { }

    void run(DebugState& state) override {
      callable_(state);
    }

};

template <typename Callable>
inline auto
make_debug_action_ptr(Callable&& callable) {
  return std::make_unique<DebugAction<std::decay_t<Callable>>>(
    std::forward<Callable>(callable)
  );
}


struct DebugWorker {

  std::unique_ptr<std::thread> worker_thread = nullptr;
  ConcurrentDeque<std::unique_ptr<DebugActionBase>> actions;
  ConcurrentDeque<std::unique_ptr<DebugActionBase>> empty_queue_actions;

  DebugState current_state;

  static std::unique_ptr<DebugWorker> instance;

  void spawn_work_loop() {
    worker_thread = std::make_unique<std::thread>([this]{
      run_work_loop();
    });
  }

  void run_work_loop() {

    while(true) {
      auto dbg_action = actions.get_and_pop_front();

      if(dbg_action) {
        if(dbg_action->get() == nullptr) { break; }
        else { (*dbg_action)->run(current_state); }
      }
      else {
        // Run all of the empty queue actions before handling any more debug actions
        // (This allows print actions to run on some semblance of a consistent snapshot)
        while(true) {
          auto empty_dbg_action = empty_queue_actions.get_and_pop_front();
          if(empty_dbg_action) {
            (*empty_dbg_action)->run(current_state);
          }
          else {
            break;
          }
        }
      }

    } // end main work loop


    // Run through any remaining output actions...
    while(true) {
      auto empty_dbg_action = empty_queue_actions.get_and_pop_front();
      if(empty_dbg_action) {
        (*empty_dbg_action)->run(current_state);
      }
      else {
        break;
      }
    }

  }

};


} // end namespace simple_backend

#define _SIMPLE_DBG_DO(action...) ::simple_backend::DebugWorker::instance->actions.emplace_back( \
  ::simple_backend::make_debug_action_ptr(action) \
);


#else

#define _SIMPLE_DBG_DO(action...)

#endif // SIMPLE_BACKEND_DEBUG


#endif //DARMASIMPLECVBACKEND_DEBUG_HPP
