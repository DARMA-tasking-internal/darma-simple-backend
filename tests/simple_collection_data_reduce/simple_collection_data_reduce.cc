/*
//@HEADER
// ************************************************************************
//
//              simple_collection_data_reduce.cc
//                         DARMA
//              Copyright (C) 2017 NTESS, LLC
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

#include <darma.h>

using namespace darma_runtime;
using namespace darma::keyword_arguments_for_access_handle_collection;
using namespace darma_runtime::keyword_arguments_for_collectives;

struct MyFunctor {
  void operator()(
    ConcurrentContext<Index1D<int>> context,
    int iter, AccessHandleCollection<int, Range1D<int>> data
  ) const {
    auto const& index = context.index();

    auto handle = data[index].local_access();

    handle.set_value(handle.get_value() + index.value);
  }
};

void darma_main_task(std::vector<std::string> args) {
  using darma_runtime::keyword_arguments_for_create_concurrent_work::index_range;

  assert(args.size() == 3);

  auto const& num_elems = std::atoi(args[1].c_str());
  auto const& num_iter = std::atoi(args[2].c_str());

  auto range = Range1D<int>(num_elems);

  auto data = initial_access_collection<int>(index_range=range);

  for (auto i = 0; i < num_iter; i++) {
    create_concurrent_work<MyFunctor>(0, data, index_range=range);

    auto result = initial_access<int>();

    data.reduce(output=result);

    create_work(reads(result),[=]{
      std::cout <<  "i=" << i
                << ": result = " << result.get_value()
                << std::endl;
    });
  }
}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
