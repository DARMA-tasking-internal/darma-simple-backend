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

#define SIMPLE_BACKEND_DEBUG_USE_FRIENDLY_POINTER_NAMES 1

#include <utility>
#include <set>
#include <memory>

#include <darma/interface/frontend/use.h>
#include <darma/impl/config.h>

#include "flow/flow.hpp"
#include "data_structures/concurrent_list.hpp"

namespace simple_backend {

// Give pointers a "friendly name" for debugging by translating it into a base 26 string
inline std::string
friendly_pointer_name(void const* ptr) {
#if SIMPLE_BACKEND_DEBUG_USE_FRIENDLY_POINTER_NAMES
  // leave out ambiguous characters like l/1 and O
  static const char letters[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHIJKLMNPQRSTUVWXYZ023456789";
  static auto base = std::strlen(letters);
  static constexpr auto addressable_size = 281474976710656; // = 2^48, addressable space on x86_64
  intptr_t value = intptr_t(ptr);
  size_t addr_size_left = addressable_size;
  std::vector<char> rv_vect;
  while(addr_size_left > 0) {
    addr_size_left /= base;
    rv_vect.push_back(letters[value % base]);
    value /= base;
  }
  // it's "backwards", but who cares as long as it's consistent
  return std::string(rv_vect.begin(), rv_vect.end());
#else
  std::stringstream sstr;
  sstr << std::hex << "0x" << std::intptr_t(ptr);
  return sstr.str();
#endif
}

inline std::string
permissions_to_string(darma_runtime::abstract::frontend::Use::permissions_t per) {
  switch(per) {
#define _DARMA__perm_case(val) case darma_runtime::abstract::frontend::Use::Permissions::val: return #val;
    _DARMA__perm_case(None)
    _DARMA__perm_case(Read)
    _DARMA__perm_case(Modify)
    _DARMA__perm_case(Write)
    _DARMA__perm_case(Commutative)
    _DARMA__perm_case(Relaxed)
#undef _DARMA__perm_case
  }
}

struct DebugState {

  std::set<darma_runtime::abstract::frontend::Use*> registered_uses;
  std::set<darma_runtime::abstract::frontend::Task*> pending_tasks;
  std::set<darma_runtime::abstract::frontend::Task*> running_tasks;
  std::mutex state_mutex;

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

  template <typename FlowTPtr>
  static void print_flow(
    FlowTPtr const& flow,
    std::string indent = "",
    std::ostream& o = std::cerr
  ) {
    if(flow) {
      o << indent << "at: " << friendly_pointer_name(&(*flow));
      auto ready = flow->get_ready_trigger()->get_triggered();
      o << " (ready: " << std::boolalpha << ready;
      if(not ready) {
        o << ", ready trigger count: " << flow->get_ready_trigger()->get_count();
      }
      o << ")";
    }
    else {
      o << "is nullptr";
    }
  }

  static void print_use(
    darma_runtime::abstract::frontend::Use* use,
    std::string indent = "",
    std::ostream& o = std::cerr
  ) {
    o << indent << std::hex << "Use object for handle with key " << use->get_handle()->get_key();
    o << "(address: " << friendly_pointer_name(use) << ")";
    auto* reg_use = darma_runtime::abstract::frontend::use_cast<
      darma_runtime::abstract::frontend::RegisteredUse*
    >(use);

    o << std::endl << indent << "  scheduling / immediate permissions: ";
    o << permissions_to_string(use->scheduling_permissions()) << " / ";
    o << permissions_to_string(use->immediate_permissions());

    o << std::endl << indent << "  in_flow ";
    print_flow(reg_use->get_in_flow(), "", o);

    o << std::endl << indent << "  out_flow ";
    print_flow(reg_use->get_out_flow(), "", o);

    o << std::endl << indent << "  anti_in_flow ";
    print_flow(reg_use->get_anti_in_flow(), "", o);

    o << std::endl << indent << "  anti_out_flow ";
    print_flow(reg_use->get_anti_out_flow(), "", o);
  }

  static void print_task(
    darma_runtime::abstract::frontend::Task* task,
    DebugState& state,
    std::string indent = "",
    std::ostream& o = std::cerr
  ) {
    o << indent << std::hex << "Task object";
    if(
      not darma_runtime::detail::key_traits<darma_runtime::types::key_t>::key_equal{}(
        task->get_name(),
        darma_runtime::make_key()
      )
    ) {
      o << " named " << task->get_name();
    }
    else { o << " (unnamed), "; }
    o << "(address: " << friendly_pointer_name(task) << ")";
#if DARMA_CREATE_WORK_RECORD_LINE_NUMBERS
    o << std::dec << std::endl << indent << "  created at: "
      << task->get_calling_filename()
      << ":" << task->get_calling_line_number() << std::endl;
    o << indent << "  (created in function: " << task->get_calling_function_name() << ")" << std::endl;
#endif
    o << indent << "with dependencies:" << std::endl;
    // Technically not allowed after run is called (I think), so we should
    // probably do something different here for running tasks
    for(auto* dep : task->get_dependencies()) {
      auto* dep_use = darma_runtime::abstract::frontend::use_cast<
        darma_runtime::abstract::frontend::Use*
      >(dep);
      if(state.registered_uses.find(dep_use) != state.registered_uses.end()) {
        print_use(dep_use, indent + "  ", o);
      }
      else {
        o << indent << "  " << "Use object at: " << friendly_pointer_name(dep_use);
        o << " (no longer registered)";
      }
      o << std::endl;
    }
  }

  static void print_state(DebugState& state, std::ostream& o = std::cerr) {
    std::cerr << "Printing backend state in response to SIGUSR2" << std::endl;

    o << "============================== Registered Uses ==============================" << std::endl;
    for(auto* use : state.registered_uses) {
      print_use(use, "  ", o);
      o << std::endl;
    }
    o << std::endl;
    o << "============================== Pending Tasks =============================" << std::endl;
    for(auto* task : state.pending_tasks) {
      print_task(task, state, "  ", o);
      o << std::endl;
    }
    o << std::endl;
    o << "============================== Running Tasks =============================" << std::endl;
    for(auto* task : state.running_tasks) {
      print_task(task, state, "  ", o);
      o << std::endl;
    }
    o << std::endl;
    o << "============================= End Program State =============================" << std::endl;
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
      std::lock_guard<std::mutex> lg(state.state_mutex);
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
        else {
          (*dbg_action)->run(current_state);
        }
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

#define _SIMPLE_DBG_ENQUEUE(action...) ::simple_backend::DebugWorker::instance->actions.emplace_back( \
  ::simple_backend::make_debug_action_ptr(action) \
);

#define _SIMPLE_DBG_DO(action...) ::simple_backend::make_debug_action_ptr(action)->run( \
  ::simple_backend::DebugWorker::instance->current_state \
);


#else

#define _SIMPLE_DBG_DO(action...)

#endif // SIMPLE_BACKEND_DEBUG


#endif //DARMASIMPLECVBACKEND_DEBUG_HPP
