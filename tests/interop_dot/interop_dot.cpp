/*
//@HEADER
// ************************************************************************
//
//                      region_test.cpp
//                         DARMA
//              Copyright (C) 2018 Sandia Corporation
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
// Questions? Contact the DARMA developers (darma-admins@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <darma.h>
#if _darma_has_feature(mpi_interop)
#include <darma/interface/app/darma_region.h> // darma_initialize
#include <darma/impl/mpi/mpi_context.h>
#include <darma/impl/mpi/piecewise_acquired_collection.h>
#include <darma/interface/app/keyword_arguments/all_keyword_arguments.h>

using namespace darma;
using namespace darma::experimental;
using namespace darma::_keyword_arguments_;
namespace kw = darma::keyword_arguments_for_piecewise_handle;
namespace kwm = darma::keyword_arguments_for_mpi_context;

// For mockup purposes
#define MPI_COMM_WORLD 0

struct DotProduct {
  void operator()(
    ConcurrentContext<Index1D<int>> ctxt,
    AccessHandleCollection<std::vector<double>, Range1D<int>> data_c,
    AccessHandleCollection<double, Range1D<int>> result_c
  ) {
    std::printf("running collection index %d\n", ctxt.index().value);
    auto data_h = data_c[ctxt.index()].local_access();
    auto result_h = result_c[ctxt.index()].local_access();
    *result_h = 0;
    for(auto d : *data_h) {
      *result_h += d * d;
    }
  }
};

int main(int argc, char** argv) {

  constexpr int overdecomp = 16;
  constexpr int iters = 25;

  darma_initialize(argc, argv);

  //MPI_Init(&argc, &argv);
  int rank = 0;
  //MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int mpi_size = 1;
  //MPI_Comm_size(MPI_COMM_WORLD, &size);

  std::vector<double> my_data;
  my_data.resize(overdecomp);
  double my_data_sum = 0.0;

  auto darma_context = darma::mpi_context(MPI_COMM_WORLD);

  auto my_data_handle = darma_context.template piecewise_acquired_collection<double>(
    "my_data", kwm::size=mpi_size*overdecomp
  );
  for(int i = 0; i < overdecomp; ++i) {
    my_data_handle.acquire_access(my_data[i], kw::index=rank*overdecomp+i);
  }

  auto darma_data = darma_context.template persistent_collection<std::vector<double>>(
    "darma_data", kwm::size=mpi_size*overdecomp
  );


  for(int i = 0; i < iters; ++i) {
    if(rank == 0) {
      darma_context.run_distributed_region_blocking([&]{

        std::printf("running iter %d\n", i);

        create_concurrent_work<DotProduct>(
          darma_data.collection(),
          my_data_handle.collection(),
          _index_range_=Range1D<int>(mpi_size*overdecomp)
        );

        std::printf("exiting iter %d\n", i);

      });
    }
    else {
      darma_context.run_distributed_region_worker_blocking();
    }

    double my_data_tmp = 0.0;
    for(auto&& d : my_data) my_data_tmp += d;
    //MPI_Allreduce(&my_data_tmp, &my_data_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    for(auto& d : my_data) {
      d = my_data_tmp;
    }

  }

  return 0;

}
#else
int main(int argc, char** argv) {
  return 0;
}
#endif // _darma_has_feature(mpi_interop)
