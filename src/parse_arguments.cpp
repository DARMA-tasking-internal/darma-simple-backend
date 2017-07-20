/*
//@HEADER
// ************************************************************************
//
//                      parse_arguments.cpp
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

#include <cassert>
#include <cstdlib>
#include "parse_arguments.hpp"


using namespace simple_backend;

static constexpr size_t n_threads_default = 8;
static constexpr size_t lookahead_default = 20;
static constexpr size_t max_stack_depth_default = 20;
#if SIMPLE_BACKEND_USE_KOKKOS
static constexpr size_t kokkos_partitions_default = 4;
#endif

std::vector<std::string> SimpleBackendOptions::parse_args(
  int argc, char** argv
) {
  int spot = 1;

  n_threads = n_threads_default;
  lookahead = lookahead_default;
#if SIMPLE_BACKEND_USE_KOKKOS
  kokkos_partitions = kokkos_partitions_default;
#endif


  auto rv = std::vector<std::string>();
  rv.emplace_back(argv[0]);

  bool app_argv_encountered = false;

  while(spot < argc) {
    std::string arg(argv[spot]);

    // TODO more sophisticated mechanism here
    if(app_argv_encountered) { rv.push_back(arg); }
    else {
      if(
           arg == "--backend-n-workers"
        || arg == "--ranks"
      ) {
        assert(spot + 1 < argc);
        n_threads = std::atoi(argv[++spot]);
      }
#if SIMPLE_BACKEND_USE_KOKKOS
      else if(
        arg == "--backend-kokkos-partitions"
      ) {
        assert(spot + 1 < argc);
        kokkos_partitions = std::atoi(argv[++spot]);
      }
#endif
      else if(arg == "--backend-lookahead") {
        assert(spot + 1 < argc);
        lookahead = std::atoi(argv[++spot]);
      }
      else if(
           arg == "--app-argv"
        || arg == "--"
      ) {
        app_argv_encountered = true;
      }
      // TODO more options
      else {
        rv.push_back(arg);
      }
    }

    ++spot;
  }

#if SIMPLE_BACKEND_USE_KOKKOS
  assert(kokkos_partitions <= n_threads && n_threads % kokkos_partitions == 0);
#endif

  return rv;
}
