/*
//@HEADER
// ************************************************************************
//
//                      control_block.hpp
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

#ifndef DARMASIMPLEBACKEND_CONTROL_BLOCK_HPP
#define DARMASIMPLEBACKEND_CONTROL_BLOCK_HPP

#include <memory>

#include "simple_backend_fwd.hpp"

#include "publish.hpp"

namespace simple_backend {

// Forward declaration

struct ControlBlock {
  protected:

    // Called from CollectionControlBlock
    ControlBlock(
      std::shared_ptr<darma_runtime::abstract::frontend::Handle const> in_handle,
      std::size_t /* ignored */
    ) : handle(in_handle), owns_data(true)
    { }

  public:

    struct copy_underlying_data_tag_t { };

    ControlBlock() : handle(nullptr), owns_data(false) { }
    explicit ControlBlock(
      std::shared_ptr<darma_runtime::abstract::frontend::Handle const> in_handle
    ) : handle(in_handle) {
      if(handle) {
        data = ::operator new(in_handle->get_serialization_manager()->get_metadata_size());
        // TODO we could delay this
        handle->get_serialization_manager()->default_construct(data);
      }
      else {
        owns_data = false;
      }
    }
    // For fetching
    ControlBlock(
      std::shared_ptr<darma_runtime::abstract::frontend::Handle const> in_handle,
      void* in_data
    ) : handle(in_handle), data(in_data), owns_data(false)
    { }
    // Copy for fetching
    explicit ControlBlock(
      copy_underlying_data_tag_t,
      std::shared_ptr<ControlBlock> other_block
    );

    ControlBlock(void* in_data, std::shared_ptr<CollectionControlBlock> parent_coll, std::size_t collection_index)
      : handle(nullptr), data(in_data), owns_data(false),
        parent_collection(parent_coll), collection_index(collection_index)
    { }

    virtual ~ControlBlock() {
      if(owns_data) {
        handle->get_serialization_manager()->destroy(data);
        ::operator delete(data);
      }
    }

    std::shared_ptr<darma_runtime::abstract::frontend::Handle const> handle;
    void* data = nullptr;
    bool owns_data = true;
    // TODO make sure this is safe
    std::shared_ptr<CollectionControlBlock> parent_collection = nullptr;
    std::size_t collection_index = 0;
};

struct CollectionControlBlock : ControlBlock {
  CollectionControlBlock(
    std::shared_ptr<darma_runtime::abstract::frontend::Handle const> in_handle,
    std::size_t n_idxs
  ) : ControlBlock(in_handle, n_idxs),
      n_indices(n_idxs)
  {
    auto ser_man = in_handle->get_serialization_manager();
    auto md_size = ser_man->get_metadata_size();
    data = ::operator new(n_indices * md_size);

    // TODO don't construct everything here
    for(int i = 0; i < n_indices; ++i) {
      ser_man->default_construct(static_cast<char*>(data) + i*md_size);
    }
  }

  CollectionControlBlock(
    std::piecewise_construct_t,
    std::shared_ptr<darma_runtime::abstract::frontend::Handle const> in_handle,
    std::size_t n_idxs
  ) : ControlBlock(in_handle, n_idxs),
      n_indices(n_idxs),
      is_piecewise_collection(true)
  {
    piecewise_collection_addresses.resize(n_idxs);
    // We never even need to invoke the callbacks, because we're never going
    // to move the data
    owns_data = false;
  }

  void* data_for_index(size_t index) {
    if(not is_piecewise_collection) {
      return static_cast<char*>(data)
        + index * handle->get_serialization_manager()->get_metadata_size();
    }
    else {
      return piecewise_collection_addresses[index];
    }
  }

  virtual ~CollectionControlBlock() {
    if(owns_data) {
      auto ser_man = handle->get_serialization_manager();
      auto md_size = ser_man->get_metadata_size();
      for(int i = 0; i < n_indices; ++i) {
        ser_man->destroy(static_cast<char*>(data) + i*md_size);
      }
      ::operator delete(data);
      owns_data = false; // make sure the base class doesn't double-delete the data
    }
  }

  size_t n_indices;
  bool is_piecewise_collection = false;
  std::vector<char*> piecewise_collection_addresses;

};

inline void
copy_data(
  std::shared_ptr<darma_runtime::abstract::frontend::Handle const> handle,
  void const* source,
  void*& dest
) {

  if(dest == nullptr) {
    dest = ::operator new(handle->get_serialization_manager()->get_metadata_size());
  }

  auto ser_pol = darma_runtime::abstract::backend::SerializationPolicy();

  size_t packed_size = handle->get_serialization_manager()->get_packed_data_size(
    source, &ser_pol
  );
  char* packed_buffer = new char[packed_size];

  handle->get_serialization_manager()->pack_data(source, packed_buffer, &ser_pol);

  handle->get_serialization_manager()->unpack_data(dest, packed_buffer, &ser_pol);

  delete[] packed_buffer;

}

inline
ControlBlock::ControlBlock(
  copy_underlying_data_tag_t,
  std::shared_ptr<ControlBlock> other_block
) : handle(other_block->handle), data(nullptr), owns_data(true)
{
  if(!handle) {
    assert(other_block->parent_collection);
    handle = other_block->parent_collection->handle;
  }
  data = ::operator new(handle->get_serialization_manager()->get_metadata_size());
  copy_data(handle, other_block->data, data);
}

} // end namespace simple_backend

#endif //DARMASIMPLEBACKEND_CONTROL_BLOCK_HPP
