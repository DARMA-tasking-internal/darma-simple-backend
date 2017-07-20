/*
//@HEADER
// ************************************************************************
//
//                      publish.cpp
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


#include "publish.hpp"
#include "runtime.hpp"

using namespace simple_backend;

#define COPY_ALL_PUBLISHES 1

//==============================================================================

void
Runtime::publish_use(
  std::unique_ptr<darma_runtime::abstract::frontend::DestructibleUse>&& pub_use,
  darma_runtime::abstract::frontend::PublicationDetails* details
) {
  assert(pub_use->get_in_flow()->control_block->parent_collection);

  assert(details->get_task_collection_token());

  auto pub_handle_key = pub_use->get_handle()->get_key();
  auto coll_idx = pub_use->get_in_flow()->control_block->collection_index;

  details->get_task_collection_token()->current_published_entries.evaluate_at(
    std::make_tuple(
      pub_handle_key,
      details->get_version_name(),
      coll_idx
    ),
    [this, details, pub_use=std::move(pub_use)] (PublicationTableEntry& entry)
      mutable /* needs to be mutable to move pub_use into a child lambda */
    {
      // Create the entry if it doesn't already exist
      if(not entry.entry) {
        entry.entry = std::make_shared<PublicationTableEntry::Impl>();
      }

      // Advance the release trigger so that the data isn't released until
      // all of the fetchers have read it
      entry.entry->release_trigger.advance_count(details->get_n_fetchers());
#if COPY_ALL_PUBLISHES
      auto& control_blk = pub_use->get_in_flow()->control_block;

      *entry.entry->source_flow = std::make_shared<Flow>(
        nullptr,
        1 // so that we can make it ready when the copy completes
      );

      auto pub_use_ptr = pub_use.get();
      pub_use_ptr->get_in_flow()->ready_trigger.add_action([
        this, entry, control_blk, pub_use=std::move(pub_use)
      ]{
        // The publish use itself holds the anti-out flow (and thus it's
        // not ready yet), so it's safe to make a copy here as long as we
        // do it before releasing the use
        assert(not pub_use->get_anti_out_flow()->ready_trigger.get_triggered());

        // Make a copy of the underlying data
        entry.entry->source_flow->get()->control_block = std::make_shared<ControlBlock>(
          ControlBlock::copy_underlying_data_tag_t{},
          control_blk
        ),
        entry.entry->source_flow->get()->ready_trigger.decrement_count();

        // The fetching trigger count starts at 1 so that it doesn't get triggered
        // while we're setting up the release; we can safely decrement it now
        // (this may trigger fetching in-flows to reference the underlying data,
        // as is done in release_use(), it is important that we do this *after*
        // the copy completes)
        entry.entry->fetching_trigger.decrement_count();

        // Now we can release the publish use, since we've cleared the
        // antidependency via a copy
        release_use(
          darma_runtime::abstract::frontend::use_cast<
            darma_runtime::abstract::frontend::UsePendingRelease*
          >(pub_use.get())
        );
      });
#else
      *entry.entry->source_flow = pub_use->get_in_flow();

      // Increment the anti-out flow of the published entry, then add a decrement
      // to the release trigger
      auto& pub_anti_out = pub_use->get_anti_out_flow();
      pub_anti_out->ready_trigger.increment_count();
      entry.entry->release_trigger.add_action([
        this, pub_anti_out, pub_use=std::move(pub_use)
      ]{
        pub_anti_out->ready_trigger.decrement_count();
        release_use(
          darma_runtime::abstract::frontend::use_cast<
            darma_runtime::abstract::frontend::UsePendingRelease*
          >(pub_use.get())
        );
      });

      // The fetching trigger count starts at 1 so that it doesn't get triggered
      // while we're setting up the release; we can safely decrement it now
      entry.entry->fetching_trigger.decrement_count();
#endif

    }
  );

}
