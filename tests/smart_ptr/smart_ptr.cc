/*
//@HEADER
// ************************************************************************
//
//                        fib.cc
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
#include <assert.h>
#include <memory>

using namespace darma_runtime;


namespace darma_runtime {
namespace serialization {

template <typename T>
struct Serializer<std::shared_ptr<T>> {

  static_assert(std::is_move_constructible<T>::value,
    "can't serialize a shared_ptr to a non-move constructible type"
  );
  static_assert(std::is_default_constructible<T>::value,
    "can't serialize a shared_ptr to a non-default constructible type"
  );

  template <typename ArchiveT>
  void serialize(std::shared_ptr<T>& val_ptr, ArchiveT& ar) {
    if(ar.is_unpacking()) {
      T val;
      ar >> val;
      val_ptr = std::make_shared<T>(std::move(val));
    }
    else {
      ar | (*val_ptr);
    }
  }

};

} // end namespace serialization
} // end namespace darma_runtime


void darma_main_task(std::vector<std::string> args)
{
  auto myData = initial_access<std::shared_ptr<int>>();

  create_work([=]{
    *myData = std::make_shared<int>(42);
  });

  create_work(reads(myData), [=]{
      std::cout << (*(*myData).get()) << std::endl;
  });
}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
