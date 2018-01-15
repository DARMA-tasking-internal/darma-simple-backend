/*
//@HEADER
// ************************************************************************
//
//                      work_stealing_random.cpp
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


#include "worker.hpp"
#include <runtime/runtime.hpp>

using namespace simple_backend;

#if SIMPLE_BACKEND_ENABLE_WORK_STEALING

void Worker::setup_work_stealing() {

  std::random_device rd;
  std::seed_seq seed{ rd(), rd(),rd(), rd(), rd(), rd(), rd(), rd(), rd() };
  steal_generator = std::mt19937(seed);
  std::uniform_int_distribution<> steal_dis(n_threads - 1);

}

bool Worker::try_to_steal_work() {

  auto steal_from = (steal_distribution(steal_generator) + id) % n_threads;

  ready_operation_ptr ready_op = nullptr;

  auto has_ready_op = Runtime::instance->workers[steal_from].ready_tasks.peak_and_pop_if(
    ready_op, [](auto&& to_be_stolen) {
      return (*to_be_stolen)->is_stealable();
    }
  );

  if(has_ready_op) {
    --Runtime::instance->dorment_workers;

    run_operation(ready_op);

    ++Runtime::instance->dorment_workers;

    return true;
  }
  else {
    return false;
  }
}


#endif // SIMPLE_BACKEND_ENABLE_WORK_STEALING
