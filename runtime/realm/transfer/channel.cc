/* Copyright 2016 Stanford University
 * Copyright 2016 Los Alamos National Laboratory
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "channel.h"
#include "channel_disk.h"
#include <realm/transfer/transfer.h>

TYPE_IS_SERIALIZABLE(LegionRuntime::LowLevel::XferOrder::Type);
TYPE_IS_SERIALIZABLE(LegionRuntime::LowLevel::XferDes::XferKind);

namespace LegionRuntime {
  namespace LowLevel {
    Logger::Category log_new_dma("new_dma");
    Logger::Category log_request("request");
    Logger::Category log_xd("xd");

      // TODO: currently we use dma_all_gpus to track the set of GPU* created
#ifdef USE_CUDA
      std::vector<GPU*> dma_all_gpus;
#endif
      // we use a single queue for all xferDes
      static XferDesQueue *xferDes_queue = 0;

      // we use a single manager to organize all channels
      static ChannelManager *channel_manager = 0;

#if 0
      static inline bool cross_ib(off_t start, size_t nbytes, size_t buf_size)
      {
        return (nbytes > 0) && (start / buf_size < (start + nbytes - 1) / buf_size);
      }
#endif
    SequenceAssembler::SequenceAssembler(void)
      : contig_amount(0)
      , first_noncontig((size_t)-1)
    {}

    SequenceAssembler::SequenceAssembler(const SequenceAssembler& copy_from)
      : contig_amount(copy_from.contig_amount)
      , first_noncontig(copy_from.first_noncontig)
      , spans(copy_from.spans)
    {}

    SequenceAssembler::~SequenceAssembler(void)
    {}

    void SequenceAssembler::swap(SequenceAssembler& other)
    {
      // need both locks
      AutoHSLLock al1(mutex);
      AutoHSLLock al2(other.mutex);
      std::swap(contig_amount, other.contig_amount);
      std::swap(first_noncontig, other.first_noncontig);
      spans.swap(other.spans);
    }

    // asks if a span exists - return value is number of bytes from the
    //  start that do
    size_t SequenceAssembler::span_exists(size_t start, size_t count)
    {
      // lock-free case 1: start < contig_amount
      size_t contig_sample = __sync_fetch_and_add(&contig_amount, 0);
      if(start < contig_sample) {
	size_t max_avail = contig_sample - start;
	if(count < max_avail)
	  return count;
	else
	  return max_avail;
      }

      // lock-free case 2: contig_amount <= start < first_noncontig
      size_t noncontig_sample = __sync_fetch_and_add(&first_noncontig, 0);
      if(start < noncontig_sample)
	return 0;

      // general case 3: take the lock and look through spans/etc.
      {
	AutoHSLLock al(mutex);

	// first, recheck the contig_amount, in case both it and the noncontig
	//  counters were bumped in between looking at the two of them
	if(start < contig_amount) {
	  size_t max_avail = contig_amount - start;
	  if(count < max_avail)
	    return count;
	  else
	    return max_avail;
	}

	// otherwise find the first span after us and then back up one to find
	//  the one that might contain our 'start'
	std::map<size_t, size_t>::const_iterator it = spans.upper_bound(start);
	// this should never be the first span
	assert(it != spans.begin());
	--it;
	assert(it->first <= start);
	// does this span overlap us?
	if((it->first + it->second) > start) {
	  size_t max_avail = it->first + it->second - start;
	  while(max_avail < count) {
	    // try to get more - return the current 'max_avail' if we fail
	    if(++it == spans.end())
	      return max_avail; // no more
	    if(it->first > (start + max_avail))
	      return max_avail; // not contiguous
	    max_avail += it->second;
	  }
	  // got at least as much as we wanted
	  return count;
	} else
	  return 0;
      }
    }

    // returns the amount by which the contiguous range has been increased
    //  (i.e. from [pos, pos+retval) )
    size_t SequenceAssembler::add_span(size_t pos, size_t count)
    {
      // first try to bump the contiguous amount without a lock
      size_t span_end = pos + count;
      if(__sync_bool_compare_and_swap(&contig_amount, pos, span_end)) {
	// success: check to see if there are any spans we might need to 
	//  tack on
	if(span_end == __sync_fetch_and_add(&first_noncontig, 0)) {
	  AutoHSLLock al(mutex);
	  while(!spans.empty()) {
	    std::map<size_t, size_t>::iterator it = spans.begin();
	    if(it->first == span_end) {
	      bool ok = __sync_bool_compare_and_swap(&contig_amount,
						     span_end,
						     span_end + it->second);
	      assert(ok);
	      span_end += it->second;
	      spans.erase(it);
	    } else {
	      // this is the new first noncontig
	      first_noncontig = it->first;
	      break;
	    }
	  }
	  if(spans.empty())
	    first_noncontig = (size_t)-1;
	}

	// return total change to contig_amount
	return (span_end - pos);
      } else {
	// failure: have to add ourselves to the span list and possibly update
	//  the 'first_noncontig', all while holding the lock
	{
	  AutoHSLLock al(mutex);

	  if(pos < first_noncontig)
	    first_noncontig = pos;

	  spans[pos] = count;
	}
	
	return 0; // no change to contig_amount
      }
    }



      XferDes::XferDes(DmaRequest* _dma_request, gasnet_node_t _launch_node,
              XferDesID _guid, XferDesID _pre_xd_guid, XferDesID _next_xd_guid,
		       uint64_t _next_max_rw_gap, size_t _src_ib_offset, size_t _src_ib_size,
              bool _mark_start,
	      Memory _src_mem, Memory _dst_mem,
	      TransferIterator *_src_iter, TransferIterator *_dst_iter,
              uint64_t _max_req_size, int _priority,
              XferOrder::Type _order, XferKind _kind, XferDesFence* _complete_fence)
        : dma_request(_dma_request), mark_start(_mark_start), launch_node(_launch_node),
	  iteration_completed(false),
	  bytes_total(0),
          pre_bytes_total((size_t)-1),
	  src_iter(_src_iter), dst_iter(_dst_iter),
          src_ib_offset(_src_ib_offset), src_ib_size(_src_ib_size),
          max_req_size(_max_req_size), priority(_priority),
          guid(_guid), pre_xd_guid(_pre_xd_guid), next_xd_guid(_next_xd_guid),
          kind (_kind), order(_order), channel(NULL), complete_fence(_complete_fence)
      {
        // size_t total_field_size = 0;
        // for (unsigned i = 0; i < oas_vec.size(); i++) {
        //   total_field_size += oas_vec[i].size;
        // }
        //bytes_total = total_field_size * domain.get_volume();
        //pre_bytes_write = 0; //(pre_xd_guid == XFERDES_NO_GUID) ? bytes_total : 0;
	//iteration_completed = (pre_xd_guid != XFERDES_NO_GUID) && (next_xd_guid != XFERDES_NO_GUID);
	src_mem = get_runtime()->get_memory_impl(_src_mem);
	dst_mem = get_runtime()->get_memory_impl(_dst_mem);
	// if we're writing into an IB, the first 'next_max_rw_gap' byte
	//  locations can be freely written
	if(next_xd_guid != XFERDES_NO_GUID)
	  seq_next_read.add_span(0, _next_max_rw_gap);
#if 0
        if (domain.get_dim() == 0) {
          li = NULL;
          // index space instances use 1D linearizations for translation
          me = new MaskEnumerator(domain.get_index_space(),
                                  src_buf.linearization.get_mapping<1>(),
                                  dst_buf.linearization.get_mapping<1>(),
                                  order, src_buf.is_ib, dst_buf.is_ib);
        } else {
          li = new LayoutIterator(
                       domain,
                       src_buf.linearization,
                       dst_buf.linearization,
                       order);
          me = NULL;
        }
#endif
        offset_idx = 0;
        pthread_mutex_init(&xd_lock, NULL);
        pthread_mutex_init(&update_read_lock, NULL);
        pthread_mutex_init(&update_write_lock, NULL);
      }

      XferDes::~XferDes() {
        // clear available_reqs
        while (!available_reqs.empty()) {
          available_reqs.pop();
        }
	delete src_iter;
	delete dst_iter;
	//delete me;
	//delete li;
        // If src_buf is intermediate buffer,
        // we need to free the buffer
        //if (src_buf.is_ib) {
        //  get_runtime()->get_memory_impl(src_buf.memory)->free_bytes(
        //      src_buf.alloc_offset, src_buf.buf_size);
        //}
        pthread_mutex_destroy(&xd_lock);
        pthread_mutex_destroy(&update_read_lock);
        pthread_mutex_destroy(&update_write_lock);
      };

      void XferDes::mark_completed() {
	if(src_ib_size > 0)
	  free_intermediate_buffer(dma_request,
				   src_mem->me,
				   src_ib_offset,
				   src_ib_size);

        // notify owning DmaRequest upon completion of this XferDes
        //printf("complete XD = %lu\n", guid);
        if (launch_node == gasnet_mynode()) {
          complete_fence->mark_finished(true/*successful*/);
        } else {
          NotifyXferDesCompleteMessage::send_request(launch_node, complete_fence);
        }
      }

#if 0
      static inline off_t calc_mem_loc_ib(off_t alloc_offset,
                                          off_t field_start,
                                          int field_size,
                                          size_t elmt_size,
                                          size_t block_size,
                                          size_t buf_size,
                                          size_t domain_size,
                                          off_t index)
      {
        off_t idx2 = domain_size / block_size * block_size;
        off_t offset;
        if (index < idx2) {
          offset = Realm::calc_mem_loc(alloc_offset, field_start, field_size, elmt_size, block_size, index);
        } else {
          offset = (alloc_offset + field_start * domain_size + (elmt_size - field_start) * idx2 + (index - idx2) * field_size);
        }
        return offset % buf_size;
      }
#endif

#define MAX_GEN_REQS 3

      bool support_2d_xfers(XferDes::XferKind kind)
      {
        return (kind == XferDes::XFER_GPU_TO_FB)
               || (kind == XferDes::XFER_GPU_FROM_FB)
               || (kind == XferDes::XFER_GPU_IN_FB)
               || (kind == XferDes::XFER_GPU_PEER_FB)
               || (kind == XferDes::XFER_REMOTE_WRITE)
               || (kind == XferDes::XFER_MEM_CPY);
      }
      void print_request_info(Request* req)
      {
        printf("request(%dD): src_off(%zd) dst_off(%zd) src_str(%zd)"
               " dst_str(%zd) nbytes(%zu) nlines(%zu)\n",
               req->dim + 1, (ssize_t)req->src_off, (ssize_t)req->dst_off,
               (ssize_t)req->src_str, (ssize_t)req->dst_str,
               req->nbytes, req->nlines);
      }

      long XferDes::default_get_requests(Request** reqs, long nr)
      {
        long idx = 0;
	
	while((idx < nr) && (available_reqs.size() > 0)) {
	  // TODO: we really shouldn't even be trying if the iteration
	  //   is already done
	  if(iteration_completed) break;

	  // handle special case of empty transfers by generating a 0-byte
	  //  request
	  if((bytes_total == 0) &&
	     ((pre_xd_guid == XFERDES_NO_GUID) ? src_iter->done() :
	                                         (pre_bytes_total == 0))) {
	    log_request.info() << "empty xferdes: " << guid;
	    assert((next_xd_guid != XFERDES_NO_GUID) || dst_iter->done());

	    iteration_completed = true;

	    Request* new_req = dequeue_request();
	    new_req->seq_pos = 0;
	    new_req->seq_count = 0;
	    new_req->dim = Request::DIM_1D;
	    new_req->src_off = 0;
	    new_req->dst_off = 0;
	    new_req->nbytes = 0;
	    new_req->nlines = 1;
	    reqs[idx++] = new_req;
	    break;
	  }

	  // some sort of per-channel max request size?
	  size_t max_bytes = 1 << 20;

	  // if we're not the first in the chain, and we know the total bytes
	  //  written by the predecessor, don't exceed that
	  if(pre_xd_guid != XFERDES_NO_GUID) {
	    size_t pre_max = pre_bytes_total - bytes_total;
	    if(pre_max == 0) {
	      // due to unsynchronized updates to pre_bytes_total, this path
	      //  can happen for an empty transfer reading from an intermediate
	      //  buffer - handle it by looping around and letting the check
	      //  at the top of the loop notice it the second time around
	      if(bytes_total == 0)
		continue;
	      // otherwise, this shouldn't happen - we should detect this case
	      //  on the the transfer of those last bytes
	      assert(0);
	      iteration_completed = true;
	      break;
	    }
	    if(pre_max < max_bytes) {
	      log_request.info() << "pred limits xfer: " << max_bytes << " -> " << pre_max;
	      max_bytes = pre_max;
	    }
	  }

	  TransferIterator::AddressInfo src_info, dst_info;
	  size_t src_bytes = src_iter->step(max_bytes, src_info,
					    true /*tentative*/);
	  size_t src_bytes_avail;
	  if(pre_xd_guid == XFERDES_NO_GUID) {
	    src_bytes_avail = src_bytes;
	  } else {
	    // if we're reading from an intermediate buffer, make sure we
	    //  have enough data from the predecessor
	    assert((src_info.num_lines == 1) && (src_info.num_planes == 1));
	    src_bytes_avail = seq_pre_write.span_exists(bytes_total, src_bytes);
	    if(src_bytes_avail == 0) {
	      // TODO: put this XD to sleep until we do have data
	      src_iter->cancel_step();
	      break;
	    }

	    // if src_bytes_avail < src_bytes, we'll need to redo the src_iter
	    //  step, but wait until we see if we need to shrink even more due
	    //  to the destination side
	  }

	  bool dst_step_tentative = (next_xd_guid != XFERDES_NO_GUID);
	  size_t dst_bytes = dst_iter->step(src_bytes_avail, dst_info,
					    dst_step_tentative);
	  if(next_xd_guid != XFERDES_NO_GUID) {
	    // if we're writing to an intermediate buffer, make sure the
	    //  next XD has read the data we want to overwrite
	    assert((dst_info.num_lines == 1) && (dst_info.num_planes == 1));
	    size_t dst_bytes_avail = seq_next_read.span_exists(bytes_total,
							       dst_bytes);
	    if(dst_bytes_avail == 0) {
	      // TODO: put this XD to sleep until we do have data
	      dst_iter->cancel_step();
	      src_iter->cancel_step();
	      break;
	    }

	    // if dst_bytes_avail < dst_bytes, we'll need to redo the dst_iter
	    // step
	    if(dst_bytes_avail == dst_bytes) {
	      dst_iter->confirm_step();
	    } else {
	      // cancel and request what we have room to write
	      dst_iter->cancel_step();
	      dst_bytes = dst_iter->step(dst_bytes_avail, dst_info);
	      assert(dst_bytes == dst_bytes_avail);
	    }
	  }

	  // this check can fail either if the destination step size is smaller
	  //  or if one/both of the intermediate buffers were near capacity
	  if(dst_bytes == src_bytes) {
	    // looks good - confirm the src step
	    src_iter->confirm_step();
	  } else {
	    // cancel the src step and try to just step by dst_bytes
	    assert(dst_bytes < src_bytes);  // should never be larger
	    src_iter->cancel_step();
	    src_bytes = src_iter->step(dst_bytes, src_info);
	    // now must match
	    assert(src_bytes == dst_bytes);
	  }

	  Request* new_req = dequeue_request();
	  new_req->seq_pos = bytes_total;
	  new_req->seq_count = src_bytes;
	  new_req->dim = Request::DIM_1D;
	  new_req->src_off = src_info.base_offset;
	  new_req->dst_off = dst_info.base_offset;
	  new_req->nbytes = src_info.bytes_per_chunk;
	  new_req->nlines = 1;

	  bytes_total += src_bytes;

	  // is our iterator done?
	  if(src_iter->done() || dst_iter->done() ||
	     (bytes_total == pre_bytes_total)) {
	    assert(!iteration_completed);
	    iteration_completed = true;
	    
	    // non-ib iterators should end at the same time
	    assert((pre_xd_guid != XFERDES_NO_GUID) || src_iter->done());
	    assert((next_xd_guid != XFERDES_NO_GUID) || dst_iter->done());

	    assert((pre_xd_guid == XFERDES_NO_GUID) || (pre_bytes_total == bytes_total));
	  }

	  log_request.info() << "[1D] guid(" << guid
			     << ") src_off(" << new_req->src_off
			     << ") dst_off(" << new_req->dst_off
			     << ") nbytes(" << new_req->nbytes << ")";
	  reqs[idx++] = new_req;
	}
#if 0
        coord_t src_idx, dst_idx, todo, src_str, dst_str;
        size_t nitems, nlines;
        while (idx + MAX_GEN_REQS <= nr && offset_idx < oas_vec.size()
        && MAX_GEN_REQS <= available_reqs.size()) {
          if (DIM == 0) {
            todo = std::min((coord_t)(max_req_size / oas_vec[offset_idx].size),
                       me->continuous_steps(src_idx, dst_idx));
            nitems = src_str = dst_str = todo;
            nlines = 1;
          }
          else
            todo = std::min((coord_t)(max_req_size / oas_vec[offset_idx].size),
                       li->continuous_steps(src_idx, dst_idx,
                                            src_str, dst_str,
                                            nitems, nlines));
          coord_t src_in_block = src_buf.block_size
                               - src_idx % src_buf.block_size;
          coord_t dst_in_block = dst_buf.block_size
                               - dst_idx % dst_buf.block_size;
          todo = std::min(todo, std::min(src_in_block, dst_in_block));
          if (todo == 0)
            break;
          coord_t src_start, dst_start;
          if (src_buf.is_ib) {
            src_start = calc_mem_loc_ib(0,
                                        oas_vec[offset_idx].src_offset,
                                        oas_vec[offset_idx].size,
                                        src_buf.elmt_size,
                                        src_buf.block_size,
                                        src_buf.buf_size,
                                        domain.get_volume(), src_idx);
            todo = std::min(todo, std::max((coord_t)0,
					   (coord_t)(pre_bytes_write - src_start))
                                    / oas_vec[offset_idx].size);
          } else {
            src_start = Realm::calc_mem_loc(0,
                                     oas_vec[offset_idx].src_offset,
                                     oas_vec[offset_idx].size,
                                     src_buf.elmt_size,
                                     src_buf.block_size, src_idx);
          }
          if (dst_buf.is_ib) {
            dst_start = calc_mem_loc_ib(0,
                                        oas_vec[offset_idx].dst_offset,
                                        oas_vec[offset_idx].size,
                                        dst_buf.elmt_size,
                                        dst_buf.block_size,
                                        dst_buf.buf_size,
                                        domain.get_volume(), dst_idx);
            todo = std::min(todo, std::max((coord_t)0,
					   (coord_t)(next_bytes_read + dst_buf.buf_size - dst_start))
                                    / oas_vec[offset_idx].size);
          } else {
            dst_start = Realm::calc_mem_loc(0,
                                     oas_vec[offset_idx].dst_offset,
                                     oas_vec[offset_idx].size,
                                     dst_buf.elmt_size,
                                     dst_buf.block_size, dst_idx);
          }
          if (todo == 0)
            break;
          bool cross_src_ib = false, cross_dst_ib = false;
          if (src_buf.is_ib)
            cross_src_ib = cross_ib(src_start,
                                    todo * oas_vec[offset_idx].size,
                                    src_buf.buf_size);
          if (dst_buf.is_ib)
            cross_dst_ib = cross_ib(dst_start,
                                    todo * oas_vec[offset_idx].size,
                                    dst_buf.buf_size);
          // We are crossing ib, fallback to 1d case
          // We don't support 2D, fallback to 1d case
          if (cross_src_ib || cross_dst_ib || !support_2d_xfers(kind))
            todo = std::min(todo, (coord_t)nitems);
          if ((size_t)todo <= nitems) {
            // fallback to 1d case
            nitems = (size_t)todo;
            nlines = 1;
          } else {
            nlines = todo / nitems;
            todo = nlines * nitems;
          }
          if (nlines == 1) {
            // 1D case
            size_t nbytes = todo * oas_vec[offset_idx].size;
            while (nbytes > 0) {
              size_t req_size = nbytes;
              Request* new_req = dequeue_request();
              new_req->dim = Request::DIM_1D;
              if (src_buf.is_ib) {
                src_start = src_start % src_buf.buf_size;
                req_size = std::min(req_size, (size_t)(src_buf.buf_size - src_start));
              }
              if (dst_buf.is_ib) {
                dst_start = dst_start % dst_buf.buf_size;
                req_size = std::min(req_size, (size_t)(dst_buf.buf_size - dst_start));
              }
              new_req->src_off = src_start;
              new_req->dst_off = dst_start;
              new_req->nbytes = req_size;
              new_req->nlines = 1;
              log_request.info("[1D] guid(%llx) src_off(%lld) dst_off(%lld)"
                               " nbytes(%zu) offset_idx(%u)",
                               guid, src_start, dst_start, req_size, offset_idx);
              reqs[idx++] = new_req;
              nbytes -= req_size;
              src_start += req_size;
              dst_start += req_size;
            }
          } else {
            // 2D case
            Request* new_req = dequeue_request();
            new_req->dim = Request::DIM_2D;
            new_req->src_off = src_start;
            new_req->dst_off = dst_start;
            new_req->src_str = src_str * oas_vec[offset_idx].size;
            new_req->dst_str = dst_str * oas_vec[offset_idx].size;
            new_req->nbytes = nitems * oas_vec[offset_idx].size;
            new_req->nlines = nlines;
            reqs[idx++] = new_req;
          }
          if (DIM == 0) {
            me->move(todo);
            if (!me->any_left()) {
              me->reset();
              offset_idx ++;
            }
          } else {
            li->move(todo);
            if (!li->any_left()) {
              li->reset();
              offset_idx ++;
            }
          }
        } // while
#endif
        return idx;
      }


    bool XferDes::is_completed(void)
    {
      // to be complete, we need to have finished iterating (which may have been
      //  achieved by getting a pre_bytes_total update) and finished all of our
      //  writes
      return (iteration_completed &&
	      (seq_write.span_exists(0, bytes_total) == bytes_total));
    }

      void XferDes::update_bytes_read(size_t offset, size_t size)
      {
	size_t inc_amt = seq_read.add_span(offset, size);
	log_xd.info() << "bytes_read: " << guid << " " << offset << "+" << size << " -> " << inc_amt;
	if(pre_xd_guid != XFERDES_NO_GUID) {
	  if(inc_amt > 0) {
	    // we're actually telling the previous XD which offsets are ok to
	    //  overwrite, so adjust our offset by our (circular) IB size
            xferDes_queue->update_next_bytes_read(pre_xd_guid,
						  offset + src_ib_size,
						  inc_amt);
	  } else {
	    // TODO: mode to send non-contiguous updates?
	  }
	}
      }

#if 0
      inline void XferDes::simple_update_bytes_read(int64_t offset, uint64_t size)
        //printf("update_read[%lx]: offset = %ld, size = %lu, pre = %lx, next = %lx\n", guid, offset, size, pre_xd_guid, next_xd_guid);
        if (pre_xd_guid != XFERDES_NO_GUID) {
          bool update = false;
          if ((int64_t)(bytes_read % src_buf.buf_size) == offset) {
            bytes_read += size;
            update = true;
          }
          else {
            //printf("[%lx] insert: key = %ld, value = %lu\n", guid, offset, size);
            segments_read[offset] = size;
          }
          std::map<int64_t, uint64_t>::iterator it;
          while (true) {
            it = segments_read.find(bytes_read % src_buf.buf_size);
            if (it == segments_read.end())
              break;
            bytes_read += it->second;
            update = true;
            //printf("[%lx] erase: key = %ld, value = %lu\n", guid, it->first, it->second);
            segments_read.erase(it);
          }
          if (update) {
            xferDes_queue->update_next_bytes_read(pre_xd_guid, bytes_read);
          }
        }
        else {
          bytes_read += size;
        }
      }
#endif

      void XferDes::update_bytes_write(size_t offset, size_t size)
      {
	size_t inc_amt = seq_write.add_span(offset, size);
	log_xd.info() << "bytes_write: " << guid << " " << offset << "+" << size << " -> " << inc_amt;
	if(next_xd_guid != XFERDES_NO_GUID) {
	  if(inc_amt > 0) {
	    // this update carries our bytes_total amount, if we know it
	    //  to be final
            xferDes_queue->update_pre_bytes_write(next_xd_guid,
						  offset,
						  inc_amt,
						  (iteration_completed ?
						     bytes_total :
						     (size_t)-1));
	  } else {
	    // TODO: mode to send non-contiguous updates?
	  }
	}
      }

#if 0
      inline void XferDes::simple_update_bytes_write(int64_t offset, uint64_t size)
      {
        log_request.info(
            "update_write: guid(%llx) off(%zd) size(%zu) pre(%llx) next(%llx)",
            guid, (ssize_t)offset, (size_t)size, pre_xd_guid, next_xd_guid);

	
        if (next_xd_guid != XFERDES_NO_GUID) {
          bool update = false;
          if ((int64_t)(bytes_write % dst_buf.buf_size) == offset) {
            bytes_write += size;
            update = true;
          } else {
            segments_write[offset] = size;
          }
          std::map<int64_t, uint64_t>::iterator it;
          while (true) {
            it = segments_write.find(bytes_write % dst_buf.buf_size);
            if (it == segments_write.end())
              break;
            bytes_write += it->second;
            update = true;
            segments_write.erase(it);
          }
          if (update) {
            xferDes_queue->update_pre_bytes_write(next_xd_guid, bytes_write);
          }
        }
        else {
          bytes_write += size;
        }
        //printf("[%d] offset(%ld), size(%lu), bytes_writes(%lx): %ld\n", gasnet_mynode(), offset, size, guid, bytes_write);
      }
#endif

      void XferDes::update_pre_bytes_write(size_t offset, size_t size, size_t pre_bytes_total)
      {
	// do this before we add the span
	if(pre_bytes_total != (size_t)-1) {
	  if(this->pre_bytes_total == (size_t)-1)
	    this->pre_bytes_total = pre_bytes_total;
	  else
	    assert(this->pre_bytes_total == pre_bytes_total);
	}

	size_t inc_amt = seq_pre_write.add_span(offset, size);
	log_xd.info() << "pre_write: " << guid << " " << offset << "+" << size << " -> " << inc_amt << " (" << pre_bytes_total << ")";
      }

      void XferDes::update_next_bytes_read(size_t offset, size_t size)
      {
	size_t inc_amt = seq_next_read.add_span(offset, size);
	log_xd.info() << "next_read: " << guid << " " << offset << "+" << size << " -> " << inc_amt;
      }

      void XferDes::default_notify_request_read_done(Request* req)
      {  
        req->is_read_done = true;
	update_bytes_read(req->seq_pos, req->seq_count);
#if 0
        if (req->dim == Request::DIM_1D)
          simple_update_bytes_read(req->src_off, req->nbytes);
        else
          simple_update_bytes_read(req->src_off, req->nbytes * req->nlines);
#endif
      }

      void XferDes::default_notify_request_write_done(Request* req)
      {
        req->is_write_done = true;
	update_bytes_write(req->seq_pos, req->seq_count);
#if 0
        if (req->dim == Request::DIM_1D)
          simple_update_bytes_write(req->dst_off, req->nbytes);
        else
          simple_update_bytes_write(req->dst_off, req->nbytes * req->nlines);
#endif
        enqueue_request(req);
      }

      MemcpyXferDes::MemcpyXferDes(DmaRequest* _dma_request,
                                        gasnet_node_t _launch_node,
                                        XferDesID _guid,
                                        XferDesID _pre_xd_guid,
                                        XferDesID _next_xd_guid,
				   uint64_t _next_max_rw_gap,
				   size_t src_ib_offset,
				   size_t src_ib_size,
                                        bool mark_started,
				   Memory _src_mem, Memory _dst_mem,
				   TransferIterator *_src_iter, TransferIterator *_dst_iter,
                                        uint64_t _max_req_size,
                                        long max_nr,
                                        int _priority,
                                        XferOrder::Type _order,
                                        XferDesFence* _complete_fence)
        : XferDes(_dma_request, _launch_node, _guid, _pre_xd_guid,
                  _next_xd_guid, _next_max_rw_gap, src_ib_offset, src_ib_size,
		  mark_started,
		  //_src_buf, _dst_buf, _domain, _oas_vec,
		  _src_mem, _dst_mem, _src_iter, _dst_iter,
		  _max_req_size, _priority, _order,
                  XferDes::XFER_MEM_CPY, _complete_fence)
      {
        //MemoryImpl* src_mem_impl = get_runtime()->get_memory_impl(_src_buf.memory);
        //MemoryImpl* dst_mem_impl = get_runtime()->get_memory_impl(_dst_buf.memory);
        channel = channel_manager->get_memcpy_channel();
        //src_buf_base = (char*) src_mem_impl->get_direct_ptr(_src_buf.alloc_offset, 0);
        //dst_buf_base = (char*) dst_mem_impl->get_direct_ptr(_dst_buf.alloc_offset, 0);
        memcpy_reqs = (MemcpyRequest*) calloc(max_nr, sizeof(MemcpyRequest));
        for (int i = 0; i < max_nr; i++) {
          memcpy_reqs[i].xd = this;
          enqueue_request(&memcpy_reqs[i]);
        }
      }

      long MemcpyXferDes::get_requests(Request** requests, long nr)
      {
        MemcpyRequest** reqs = (MemcpyRequest**) requests;
        long new_nr = default_get_requests(requests, nr);
        for (long i = 0; i < new_nr; i++)
        {
          //reqs[i]->src_base = (char*)(src_buf_base + reqs[i]->src_off);
          //reqs[i]->dst_base = (char*)(dst_buf_base + reqs[i]->dst_off);
	  reqs[i]->src_base = src_mem->get_direct_ptr(reqs[i]->src_off,
						      reqs[i]->nbytes);
	  reqs[i]->dst_base = dst_mem->get_direct_ptr(reqs[i]->dst_off,
						      reqs[i]->nbytes);
	  assert(reqs[i]->src_base != 0);
	  assert(reqs[i]->dst_base != 0);
        }
        return new_nr;

#ifdef TO_BE_DELETE
        long idx = 0;
        while (idx < nr && !available_reqs.empty() && offset_idx < oas_vec.size()) {
          off_t src_start, dst_start;
          size_t nbytes;
          if (DIM == 0) {
            simple_get_mask_request(src_start, dst_start, nbytes, me, offset_idx, min(available_reqs.size(), nr - idx));
          } else {
            simple_get_request<DIM>(src_start, dst_start, nbytes, li, offset_idx, min(available_reqs.size(), nr - idx));
          }
          if (nbytes == 0)
            break;
          //printf("[MemcpyXferDes] guid = %lx, offset_idx = %lld, oas_vec.size() = %lu, nbytes = %lu\n", guid, offset_idx, oas_vec.size(), nbytes);
          while (nbytes > 0) {
            size_t req_size = nbytes;
            if (src_buf.is_ib) {
              src_start = src_start % src_buf.buf_size;
              req_size = std::min(req_size, (size_t)(src_buf.buf_size - src_start));
            }
            if (dst_buf.is_ib) {
              dst_start = dst_start % dst_buf.buf_size;
              req_size = std::min(req_size, (size_t)(dst_buf.buf_size - dst_start));
            }
            mem_cpy_reqs[idx] = (MemcpyRequest*) available_reqs.front();
            available_reqs.pop();
            //printf("[MemcpyXferDes] src_start = %ld, dst_start = %ld, nbytes = %lu\n", src_start, dst_start, nbytes);
            mem_cpy_reqs[idx]->is_read_done = false;
            mem_cpy_reqs[idx]->is_write_done = false;
            mem_cpy_reqs[idx]->src_buf = (char*)(src_buf_base + src_start);
            mem_cpy_reqs[idx]->dst_buf = (char*)(dst_buf_base + dst_start);
            mem_cpy_reqs[idx]->nbytes = req_size;
            src_start += req_size; // here we don't have to mod src_buf.buf_size since it will be performed in next loop
            dst_start += req_size; //
            nbytes -= req_size;
            idx++;
          }
        }
        return idx;
#endif
      }

      void MemcpyXferDes::notify_request_read_done(Request* req)
      {
        default_notify_request_read_done(req);
      }

      void MemcpyXferDes::notify_request_write_done(Request* req)
      {
        default_notify_request_write_done(req);
      }

      void MemcpyXferDes::flush()
      {
      }

      GASNetXferDes::GASNetXferDes(DmaRequest* _dma_request,
				   gasnet_node_t _launch_node,
				   XferDesID _guid,
				   XferDesID _pre_xd_guid,
				   XferDesID _next_xd_guid,
				   uint64_t _next_max_rw_gap,
				   size_t src_ib_offset,
				   size_t src_ib_size,
				   bool mark_started,
				   Memory _src_mem, Memory _dst_mem,
				   TransferIterator *_src_iter, TransferIterator *_dst_iter,
				   uint64_t _max_req_size,
				   long max_nr,
				   int _priority,
				   XferOrder::Type _order,
				   XferKind _kind,
				   XferDesFence* _complete_fence)
        : XferDes(_dma_request, _launch_node, _guid, _pre_xd_guid,
                  _next_xd_guid, _next_max_rw_gap, src_ib_offset, src_ib_size,
		  mark_started,
		  //_src_buf, _dst_buf, _domain, _oas_vec,
		  _src_mem, _dst_mem, _src_iter, _dst_iter,
		  _max_req_size, _priority, _order,
                  _kind, _complete_fence)
      {
        //MemoryImpl* src_mem_impl = get_runtime()->get_memory_impl(_src_buf.memory);
        //MemoryImpl* dst_mem_impl = get_runtime()->get_memory_impl(_dst_buf.memory);
        gasnet_reqs = (GASNetRequest*) calloc(max_nr, sizeof(GASNetRequest));
        for (int i = 0; i < max_nr; i++) {
          gasnet_reqs[i].xd = this;
          enqueue_request(&gasnet_reqs[i]);
        }
        switch (kind) {
          case XferDes::XFER_GASNET_READ:
          {
            channel = channel_manager->get_gasnet_read_channel();
            //buf_base = (const char*) dst_mem_impl->get_direct_ptr(_dst_buf.alloc_offset, 0);
            break;
          }
          case XferDes::XFER_GASNET_WRITE:
          {
            channel = channel_manager->get_gasnet_write_channel();
            //buf_base = (const char*) src_mem_impl->get_direct_ptr(_src_buf.alloc_offset, 0);
            break;
          }
          default:
            assert(false);
        }
      }

      long GASNetXferDes::get_requests(Request** requests, long nr)
      {
        GASNetRequest** reqs = (GASNetRequest**) requests;
        long new_nr = default_get_requests(requests, nr);
        switch (kind) {
          case XferDes::XFER_GASNET_READ:
          {
            for (long i = 0; i < new_nr; i++) {
              reqs[i]->gas_off = /*src_buf.alloc_offset +*/ reqs[i]->src_off;
              //reqs[i]->mem_base = (char*)(buf_base + reqs[i]->dst_off);
	      reqs[i]->mem_base = dst_mem->get_direct_ptr(reqs[i]->dst_off,
							  reqs[i]->nbytes);
	      assert(reqs[i]->mem_base != 0);
            }
            break;
          }
          case XferDes::XFER_GASNET_WRITE:
          {
            for (long i = 0; i < new_nr; i++) {
              //reqs[i]->mem_base = (char*)(buf_base + reqs[i]->src_off);
	      reqs[i]->mem_base = src_mem->get_direct_ptr(reqs[i]->src_off,
							  reqs[i]->nbytes);
	      assert(reqs[i]->mem_base != 0);
              reqs[i]->gas_off = /*dst_buf.alloc_offset +*/ reqs[i]->dst_off;
            }
            break;
          }
          default:
            assert(0);
        }
        return new_nr;
      }

      void GASNetXferDes::notify_request_read_done(Request* req)
      {
        default_notify_request_read_done(req);
      }

      void GASNetXferDes::notify_request_write_done(Request* req)
      {
        default_notify_request_write_done(req);
      }

      void GASNetXferDes::flush()
      {
      }

      RemoteWriteXferDes::RemoteWriteXferDes(DmaRequest* _dma_request,
					     gasnet_node_t _launch_node,
					     XferDesID _guid,
					     XferDesID _pre_xd_guid,
					     XferDesID _next_xd_guid,
					     uint64_t _next_max_rw_gap,
					     size_t src_ib_offset,
					     size_t src_ib_size,
					     bool mark_started,
					     Memory _src_mem, Memory _dst_mem,
					     TransferIterator *_src_iter, TransferIterator *_dst_iter,
					     uint64_t _max_req_size,
					     long max_nr,
					     int _priority,
					     XferOrder::Type _order,
					     XferDesFence* _complete_fence)
        : XferDes(_dma_request, _launch_node, _guid, _pre_xd_guid,
                  _next_xd_guid, _next_max_rw_gap, src_ib_offset, src_ib_size,
		  mark_started,
		  //_src_buf, _dst_buf,_domain, _oas_vec,
		  _src_mem, _dst_mem, _src_iter, _dst_iter,
		  _max_req_size, _priority, _order,
                  XferDes::XFER_REMOTE_WRITE, _complete_fence)
      {
        //MemoryImpl* src_mem_impl = get_runtime()->get_memory_impl(_src_buf.memory);
        //dst_mem_impl = get_runtime()->get_memory_impl(_dst_buf.memory);
        // make sure dst buffer is registered memory
        assert(dst_mem->kind == MemoryImpl::MKIND_RDMA);
        channel = channel_manager->get_remote_write_channel();
        //src_buf_base = (const char*) src_mem_impl->get_direct_ptr(_src_buf.alloc_offset, 0);
        // Note that we cannot use get_direct_ptr to get dst_buf_base, since it always returns 0
        dst_buf_base = ((char *)((Realm::RemoteMemory*)dst_mem)->regbase) /*+ dst_buf.alloc_offset*/;
        requests = (RemoteWriteRequest*) calloc(max_nr, sizeof(RemoteWriteRequest));
        for (int i = 0; i < max_nr; i++) {
          requests[i].xd = this;
          requests[i].dst_node = ID(dst_mem->me).memory.owner_node;
          enqueue_request(&requests[i]);
        }
      }

      long RemoteWriteXferDes::get_requests(Request** requests, long nr)
      {
        pthread_mutex_lock(&xd_lock);
        RemoteWriteRequest** reqs = (RemoteWriteRequest**) requests;
        long new_nr = default_get_requests(requests, nr);
        for (long i = 0; i < new_nr; i++)
        {
          //reqs[i]->src_base = (char*)(src_buf_base + reqs[i]->src_off);
	  reqs[i]->src_base = src_mem->get_direct_ptr(reqs[i]->src_off,
						      reqs[i]->nbytes);
	  assert(reqs[i]->src_base != 0);
          reqs[i]->dst_base = dst_buf_base + reqs[i]->dst_off;
        }
        pthread_mutex_unlock(&xd_lock);
        return new_nr;
      }

      void RemoteWriteXferDes::notify_request_read_done(Request* req)
      {
        pthread_mutex_lock(&xd_lock);
        default_notify_request_read_done(req);
        pthread_mutex_unlock(&xd_lock);
      }

      void RemoteWriteXferDes::notify_request_write_done(Request* req)
      {
        pthread_mutex_lock(&xd_lock);
        default_notify_request_write_done(req);
        pthread_mutex_unlock(&xd_lock);
      }

      void RemoteWriteXferDes::flush()
      {
        //pthread_mutex_lock(&xd_lock);
        //pthread_mutex_unlock(&xd_lock);
      }

      // doesn't do pre_bytes_write updates, since the remote write message
      //  takes care of it with lower latency
      void RemoteWriteXferDes::update_bytes_write(size_t offset, size_t size)
      {
	seq_write.add_span(offset, size);
      }

#ifdef USE_CUDA
      GPUXferDes::GPUXferDes(DmaRequest* _dma_request,
			     gasnet_node_t _launch_node,
			     XferDesID _guid,
			     XferDesID _pre_xd_guid,
			     XferDesID _next_xd_guid,
			     uint64_t _next_max_rw_gap,
			     size_t src_ib_offset,
			     size_t src_ib_size,
			     bool mark_started,
			     Memory _src_mem, Memory _dst_mem,
			     TransferIterator *_src_iter, TransferIterator *_dst_iter,
			     uint64_t _max_req_size,
			     long max_nr,
			     int _priority,
			     XferOrder::Type _order,
			     XferKind _kind,
			     XferDesFence* _complete_fence)
      : XferDes(_dma_request, _launch_node, _guid, _pre_xd_guid,
                _next_xd_guid, _next_max_rw_gap, src_ib_offset, src_ib_size,
		mark_started,
		//_src_buf, _dst_buf, _domain, _oas_vec,
		_src_mem, _dst_mem, _src_iter, _dst_iter,
		_max_req_size, _priority,
                _order, _kind, _complete_fence)
      {
        //gpu_reqs = (GPURequest*) calloc(max_nr, sizeof(GPURequest));
        for (int i = 0; i < max_nr; i++) {
          GPURequest* gpu_req = new GPURequest;
          gpu_req->xd = this;
          enqueue_request(gpu_req);
        }
 
        switch (kind) {
          case XferDes::XFER_GPU_TO_FB:
          {
            src_gpu = NULL;
            dst_gpu = ((GPUFBMemory*)dst_mem)->gpu;
            channel = channel_manager->get_gpu_to_fb_channel(dst_gpu);
            //src_buf_base = (char*) src_mem_impl->get_direct_ptr(_src_buf.alloc_offset, 0);
            //dst_buf_base = NULL;
            assert(dst_mem->kind == MemoryImpl::MKIND_GPUFB);
            break;
          }
          case XferDes::XFER_GPU_FROM_FB:
          {
            src_gpu = ((GPUFBMemory*)src_mem)->gpu;
            dst_gpu = NULL;
            channel = channel_manager->get_gpu_from_fb_channel(src_gpu);
            //src_buf_base = NULL;
            //dst_buf_base = (char*) dst_mem_impl->get_direct_ptr(_dst_buf.alloc_offset, 0);
            assert(src_mem->kind == MemoryImpl::MKIND_GPUFB);
            break;
          }
          case XferDes::XFER_GPU_IN_FB:
          {
            src_gpu = ((GPUFBMemory*)src_mem)->gpu;
            dst_gpu = ((GPUFBMemory*)dst_mem)->gpu;
            channel = channel_manager->get_gpu_in_fb_channel(src_gpu);
            //src_buf_base = dst_buf_base = NULL;
            assert(src_mem->kind == MemoryImpl::MKIND_GPUFB);
            assert(src_mem->kind == MemoryImpl::MKIND_GPUFB);
            assert(src_gpu == dst_gpu);
            break;
          }
          case XferDes::XFER_GPU_PEER_FB:
          {
            src_gpu = ((GPUFBMemory*)src_mem)->gpu;
            dst_gpu = ((GPUFBMemory*)dst_mem)->gpu;
            channel = channel_manager->get_gpu_peer_fb_channel(src_gpu);
            //src_buf_base = dst_buf_base = NULL;
            assert(src_mem->kind == MemoryImpl::MKIND_GPUFB);
            assert(src_mem->kind == MemoryImpl::MKIND_GPUFB);
            assert(src_gpu != dst_gpu);
            break;
          }
          default:
            assert(0);
        }
      }

      long GPUXferDes::get_requests(Request** requests, long nr)
      {
        GPURequest** reqs = (GPURequest**) requests;
        long new_nr = default_get_requests(requests, nr);
        for (long i = 0; i < new_nr; i++) {
          reqs[i]->event.reset();
          switch (kind) {
            case XferDes::XFER_GPU_TO_FB:
            {
              //reqs[i]->src_base = src_buf_base + reqs[i]->src_off;
	      reqs[i]->src_base = src_mem->get_direct_ptr(reqs[i]->src_off,
							  reqs[i]->nbytes);
	      assert(reqs[i]->src_base != 0);
              reqs[i]->dst_gpu_off = /*dst_buf.alloc_offset +*/ reqs[i]->dst_off;
              break;
            }
            case XferDes::XFER_GPU_FROM_FB:
            {
              reqs[i]->src_gpu_off = /*src_buf.alloc_offset +*/ reqs[i]->src_off;
              //reqs[i]->dst_base = dst_buf_base + reqs[i]->dst_off;
	      reqs[i]->dst_base = dst_mem->get_direct_ptr(reqs[i]->dst_off,
							  reqs[i]->nbytes);
	      assert(reqs[i]->dst_base != 0);
              break;
            }
            case XferDes::XFER_GPU_IN_FB:
            {
              reqs[i]->src_gpu_off = /*src_buf.alloc_offset*/ + reqs[i]->src_off;
              reqs[i]->dst_gpu_off = /*dst_buf.alloc_offset*/ + reqs[i]->dst_off;
              break;
            }
            case XferDes::XFER_GPU_PEER_FB:
            {
              reqs[i]->src_gpu_off = /*src_buf.alloc_offset +*/ reqs[i]->src_off;
              reqs[i]->dst_gpu_off = /*dst_buf.alloc_offset +*/ reqs[i]->dst_off;
              // also need to set dst_gpu for peer xfer
              reqs[i]->dst_gpu = dst_gpu;
              break;
            }
            default:
              assert(0);
          }
        }
        return new_nr;
      }

      void GPUXferDes::notify_request_read_done(Request* req)
      {
        default_notify_request_read_done(req);
      }

      void GPUXferDes::notify_request_write_done(Request* req)
      {
        default_notify_request_write_done(req);
      }

      void GPUXferDes::flush()
      {
      }
#endif

#ifdef USE_HDF
      HDFXferDes::HDFXferDes(DmaRequest* _dma_request,
			     gasnet_node_t _launch_node,
			     XferDesID _guid,
			     XferDesID _pre_xd_guid,
			     XferDesID _next_xd_guid,
			     uint64_t _next_max_rw_gap,
			     size_t src_ib_offset,
			     size_t src_ib_size,
			     bool mark_started,
			     RegionInstance inst,
			     Memory _src_mem, Memory _dst_mem,
			     TransferIterator *_src_iter, TransferIterator *_dst_iter,
			     uint64_t _max_req_size,
			     long max_nr,
			     int _priority,
			     XferOrder::Type _order,
			     XferKind _kind,
			     XferDesFence* _complete_fence)
        : XferDes(_dma_request, _launch_node, _guid, _pre_xd_guid,
                  _next_xd_guid, _next_max_rw_gap, src_ib_offset, src_ib_size,
		  mark_started,
		  //_src_buf, _dst_buf, _domain, _oas_vec,
		  _src_mem, _dst_mem, _src_iter, _dst_iter,
		  _max_req_size, _priority,
                  _order, _kind, _complete_fence)
      {
        HDF5Memory* hdf_mem;
        switch (kind) {
          case XferDes::XFER_HDF_READ:
          {
	    assert(src_mem->kind == MemoryImpl::MKIND_HDF);
            hdf_mem = (HDF5Memory*) src_mem;
            //pthread_rwlock_rdlock(&hdf_mem->rwlock);
            // std::map<RegionInstance, HDFMetadata*>::iterator it;
            // it = hdf_mem->hdf_metadata.find(inst);
            // assert(it != hdf_mem->hdf_metadata.end());
            // hdf_metadata = it->second;
            //pthread_rwlock_unlock(&hdf_mem->rwlock);
            channel = channel_manager->get_hdf_read_channel();
            //buf_base = (char*) dst_impl->get_direct_ptr(_dst_buf.alloc_offset, 0);
            //lsi = new GenericLinearSubrectIterator<Mapping<DIM, 1> >(domain.get_rect<DIM>(), (*dst_buf.linearization.get_mapping<DIM>()));
            //fit = oas_vec.begin();
            break;
          }
          case XferDes::XFER_HDF_WRITE:
          {
            assert(dst_mem->kind == MemoryImpl::MKIND_HDF);
            hdf_mem = (HDF5Memory*) dst_mem;
            //pthread_rwlock_rdlock(&hdf_mem->rwlock);
            // std::map<RegionInstance, HDFMetadata*>::iterator it;
            // it = hdf_mem->hdf_metadata.find(inst);
            // assert(it != hdf_mem->hdf_metadata.end());
            // hdf_metadata = it->second;
            //pthread_rwlock_unlock(&hdf_mem->rwlock);
            channel = channel_manager->get_hdf_write_channel();
            //buf_base = (char*) src_impl->get_direct_ptr(_src_buf.alloc_offset, 0);
            //lsi = new GenericLinearSubrectIterator<Mapping<DIM, 1> >(domain.get_rect<DIM>(), (*src_buf.linearization.get_mapping<DIM>()));
            //fit = oas_vec.begin();
            break;
          }
          default:
            assert(0);
        }

	std::map<RegionInstance, HDFMetadata*>::iterator it;
	it = hdf_mem->hdf_metadata.find(inst);
	assert(it != hdf_mem->hdf_metadata.end());
	hdf_metadata = it->second;

        hdf_reqs = (HDFRequest*) calloc(max_nr, sizeof(HDFRequest));
        for (int i = 0; i < max_nr; i++) {
          hdf_reqs[i].xd = this;
          //hdf_reqs[i].hdf_memory = hdf_mem;
          enqueue_request(&hdf_reqs[i]);
        }
     }

      long HDFXferDes::get_requests(Request** requests, long nr)
      {
        long idx = 0;
	
	while((idx < nr) && (available_reqs.size() > 0)) {	  
	  // is our iterator done?
	  if(src_iter->done() || dst_iter->done()) {
	    // non-ib iterators should end at the same time
	    assert((pre_xd_guid != XFERDES_NO_GUID) || src_iter->done());
	    assert((next_xd_guid != XFERDES_NO_GUID) || dst_iter->done());
	    iteration_completed = true;
	    break;
	  }

	  // some sort of per-channel max request size?
	  size_t max_bytes = 1 << 20;
	  assert(pre_xd_guid == XFERDES_NO_GUID);
	  assert(next_xd_guid == XFERDES_NO_GUID);

	  // HDF5 uses its own address info, instead of src/dst, we
	  //  distinguish between hdf5 and mem
	  TransferIterator *hdf5_iter = ((kind == XferDes::XFER_HDF_READ) ?
					   src_iter :
					   dst_iter);
	  TransferIterator *mem_iter = ((kind == XferDes::XFER_HDF_READ) ?
					  dst_iter :
					  src_iter);

	  TransferIterator::AddressInfo mem_info;
	  TransferIterator::AddressInfoHDF5 hdf5_info;

	  // always ask the HDF5 size for a step first
	  size_t hdf5_bytes = hdf5_iter->step(max_bytes, hdf5_info,
					      true /*tentative*/);
	  size_t mem_bytes = mem_iter->step(hdf5_bytes, mem_info);
	  if(mem_bytes == hdf5_bytes) {
	    // looks good - confirm the hdf5 step
	    hdf5_iter->confirm_step();
	  } else {
	    // cancel the hdf5 step and try to just step by mem_bytes
	    assert(mem_bytes < hdf5_bytes);  // should never be larger
	    hdf5_iter->cancel_step();
	    hdf5_bytes = hdf5_iter->step(mem_bytes, hdf5_info);
	    // now must match
	    assert(hdf5_bytes == mem_bytes);
	  }
	  bytes_total += hdf5_bytes;

	  HDFRequest* new_req = (HDFRequest *)(dequeue_request());
	  new_req->dim = Request::DIM_1D;
	  new_req->mem_base = ((kind == XferDes::XFER_HDF_READ) ?
			         dst_mem :
			         src_mem)->get_direct_ptr(mem_info.base_offset,
							  mem_info.bytes_per_chunk);
	  new_req->dataset_id = hdf5_info.dset_id;
	  new_req->datatype_id = hdf5_info.dtype_id;

	  // TODO: this should be based on analysis of memory strides
	  std::vector<hsize_t> mem_dims = hdf5_info.extent;
	  CHECK_HDF5( new_req->mem_space_id = H5Screate_simple(mem_dims.size(), mem_dims.data(), NULL) );
	  //std::vector<hsize_t> mem_start(DIM, 0);
	  //CHECK_HDF5( H5Sselect_hyperslab(new_req->mem_space_id, H5S_SELECT_SET, ms_start, NULL, count, NULL) );

	  CHECK_HDF5( new_req->file_space_id = H5Screate_simple(hdf5_info.dset_bounds.size(), hdf5_info.dset_bounds.data(), 0) );
	  CHECK_HDF5( H5Sselect_hyperslab(new_req->file_space_id, H5S_SELECT_SET, hdf5_info.offset.data(), 0, hdf5_info.extent.data(), 0) );

	  new_req->nbytes = hdf5_bytes;
	  requests[idx++] = new_req;
	}

	return idx;
#if 0
        long ns = 0;
        while (ns < nr && !available_reqs.empty() && fit != oas_vec.end()) {
          requests[ns] = dequeue_request();
          switch (kind) {
            case XferDes::XFER_HDF_READ:
            {
              off_t hdf_ofs = fit->src_offset;
              assert(hdf_metadata->dataset_ids.count(hdf_ofs) > 0);
              //pthread_rwlock_rdlock(&hdf_metadata->hdf_memory->rwlock);
              size_t elemnt_size = H5Tget_size(hdf_metadata->datatype_ids[hdf_ofs]);
              HDFRequest* hdf_req = (HDFRequest*) requests[ns];
              hdf_req->dataset_id = hdf_metadata->dataset_ids[hdf_ofs];
              //hdf_req->rwlock = &hdf_metadata->dataset_rwlocks[hdf_idx];
              hdf_req->mem_type_id = hdf_metadata->datatype_ids[hdf_ofs];
              hsize_t count[DIM], ms_start[DIM], ds_start[DIM], ms_dims[DIM];
              // assume SOA for now
              assert(dst_buf.block_size >= lsi->image_lo[0] + domain.get_volume());
              assert(lsi->strides[0][0] == 1);
              ms_dims[DIM - 1] = lsi->strides[1][0];
              for (unsigned i = 1; i < DIM - 1; i++)
                ms_dims[DIM - 1 - i] = lsi->strides[i+1][0] / lsi->strides[i][0];
              ms_dims[0] = lsi->subrect.hi[DIM - 1] - lsi->subrect.lo[DIM - 1] + 1;
              size_t todo = 1;
              for (unsigned i = 0; i < DIM; i++) {
                ms_start[i] = 0;
                count[i] = lsi->subrect.hi[DIM - 1 - i] - lsi->subrect.lo[DIM - 1 - i] + 1;
                todo *= count[i];
                ds_start[i] = lsi->subrect.lo[DIM - 1 - i] - hdf_metadata->lo[DIM - 1 - i];
              }
              hdf_req->file_space_id = H5Dget_space(hdf_metadata->dataset_ids[hdf_ofs]);
              // HDF dimension always start with zero, but Legion::Domain may start with any integer
              // We need to deal with the offset between them here
              herr_t ret = H5Sselect_hyperslab(hdf_req->file_space_id, H5S_SELECT_SET, ds_start, NULL, count, NULL);
              assert(ret >= 0);
              //pthread_rwlock_unlock(&hdf_metadata->hdf_memory->rwlock);
              //pthread_rwlock_wrlock(&hdf_metadata->hdf_memory->rwlock);
              hdf_req->mem_space_id = H5Screate_simple(DIM, ms_dims, NULL);
              ret = H5Sselect_hyperslab(hdf_req->mem_space_id, H5S_SELECT_SET, ms_start, NULL, count, NULL);
              assert(ret >= 0);
              //pthread_rwlock_unlock(&hdf_metadata->hdf_memory->rwlock);
              off_t dst_offset = Realm::calc_mem_loc(0, fit->dst_offset, fit->size,
                                              dst_buf.elmt_size, dst_buf.block_size, lsi->image_lo[0]);
              hdf_req->mem_base = buf_base + dst_offset;
              hdf_req->nbytes = todo * elemnt_size;
              break;
            }
            case XferDes::XFER_HDF_WRITE:
            {
              off_t hdf_ofs = fit->dst_offset;
              assert(hdf_metadata->dataset_ids.count(hdf_ofs) > 0);
              //pthread_rwlock_rdlock(&hdf_metadata->hdf_memory->rwlock);
              size_t elemnt_size = H5Tget_size(hdf_metadata->datatype_ids[hdf_ofs]);
              HDFRequest* hdf_req = (HDFRequest*) requests[ns];
              hdf_req->dataset_id = hdf_metadata->dataset_ids[hdf_ofs];
              //hdf_req->rwlock = &hdf_metadata->dataset_rwlocks[hdf_idx];
              hdf_req->mem_type_id = hdf_metadata->datatype_ids[hdf_ofs];
              hsize_t count[DIM], ms_start[DIM], ds_start[DIM], ms_dims[DIM];
              //assume SOA for now
              assert(src_buf.block_size >= lsi->image_lo[0] + domain.get_volume());
              assert(lsi->strides[0][0] == 1);
              ms_dims[DIM - 1] = lsi->strides[1][0];
              for (unsigned i = 1; i < DIM - 1; i++)
                ms_dims[DIM - 1 - i] = lsi->strides[i+1][0] / lsi->strides[i][0];
              ms_dims[0] = lsi->subrect.hi[DIM - 1] - lsi->subrect.lo[DIM - 1] + 1;
              size_t todo = 1;
              for (unsigned i = 0; i < DIM; i++) {
                ms_start[i] = 0;
                count[i] = lsi->subrect.hi[DIM - 1 - i] - lsi->subrect.lo[DIM - 1 - i] + 1;
                todo *= count[i];
                ds_start[i] = lsi->subrect.lo[DIM - 1 - i] - hdf_metadata->lo[DIM - 1 - i];
              }
              hdf_req->file_space_id = H5Dget_space(hdf_metadata->dataset_ids[hdf_ofs]);
              // HDF dimension always start with zero, but Legion::Domain may start with any integer
              // We need to deal with the offset between them here
              herr_t ret = H5Sselect_hyperslab(hdf_req->file_space_id, H5S_SELECT_SET, ds_start, NULL, count, NULL);
              assert(ret >= 0);
              //pthread_rwlock_unlock(&hdf_metadata->hdf_memory->rwlock);
              //pthread_rwlock_wrlock(&hdf_metadata->hdf_memory->rwlock);
              hdf_req->mem_space_id = H5Screate_simple(DIM, ms_dims, NULL);
              ret = H5Sselect_hyperslab(hdf_req->mem_space_id, H5S_SELECT_SET, ms_start, NULL, count, NULL);
              assert(ret >= 0);
              //pthread_rwlock_unlock(&hdf_metadata->hdf_memory->rwlock);
              off_t src_offset = Realm::calc_mem_loc(0, fit->src_offset, fit->size,
                                              src_buf.elmt_size, src_buf.block_size, lsi->image_lo[0]);
              hdf_req->mem_base = buf_base + src_offset;
              hdf_req->nbytes = todo * elemnt_size;
              break;
            }
            default:
              assert(0);
          }
          lsi->step();
          if (!lsi->any_left) {
            fit++;
            delete lsi;
            if (kind == XferDes::XFER_HDF_READ)
              lsi = new GenericLinearSubrectIterator<Mapping<DIM, 1> >(domain.get_rect<DIM>(), (*dst_buf.linearization.get_mapping<DIM>()));
            else
              lsi = new GenericLinearSubrectIterator<Mapping<DIM, 1> >(domain.get_rect<DIM>(), (*src_buf.linearization.get_mapping<DIM>()));
          }
          ns ++;
        }
        return ns;
#endif
      }

      void HDFXferDes::notify_request_read_done(Request* req)
      {
        req->is_read_done = true;
        // close and release HDF resources
        // currently we don't support ib case
        assert(pre_xd_guid == XFERDES_NO_GUID);
        HDFRequest* hdf_req = (HDFRequest*) req;
        bytes_read += hdf_req->nbytes;
      }

      void HDFXferDes::notify_request_write_done(Request* req)
      {
        req->is_write_done = true;
        // currently we don't support ib case
        assert(next_xd_guid == XFERDES_NO_GUID);
        HDFRequest* hdf_req = (HDFRequest*) req;
        bytes_write += hdf_req->nbytes;
        //pthread_rwlock_wrlock(&hdf_metadata->hdf_memory->rwlock);
        CHECK_HDF5( H5Sclose(hdf_req->mem_space_id) );
        CHECK_HDF5( H5Sclose(hdf_req->file_space_id) );
        //pthread_rwlock_unlock(&hdf_metadata->hdf_memory->rwlock);
        enqueue_request(req);
      }

      void HDFXferDes::flush()
      {
        if (kind == XferDes::XFER_HDF_READ) {
        } else {
          assert(kind == XferDes::XFER_HDF_WRITE);
	  CHECK_HDF5( H5Fflush(hdf_metadata->file_id, H5F_SCOPE_LOCAL) );
          // for (fit = oas_vec.begin(); fit != oas_vec.end(); fit++) {
          //   off_t hdf_idx = fit->dst_offset;
          //   hid_t dataset_id = hdf_metadata->dataset_ids[hdf_idx];
          //   //TODO: I am not sure if we need a lock here to protect HDFflush
          //   H5Fflush(dataset_id, H5F_SCOPE_LOCAL);
          // }
        }
      }
#endif

      /*static*/ void* MemcpyThread::start(void* arg)
      {
        MemcpyThread* worker = (MemcpyThread*) arg;
        worker->thread_loop();
        return NULL;
      }

      void MemcpyThread::thread_loop()
      {
        while (!channel->is_stopped) {
          channel->get_request(thread_queue);
          if (channel->is_stopped)
            break;
          std::deque<MemcpyRequest*>::const_iterator it;
          for (it = thread_queue.begin(); it != thread_queue.end(); it++) {
            MemcpyRequest* req = *it;
            //double starttime = Realm::Clock::current_time_in_microseconds();
            if (req->dim == Request::DIM_1D) {
              memcpy(req->dst_base, req->src_base, req->nbytes);
            } else {
              assert(req->dim == Request::DIM_2D);
              const char *src = (const char *)(req->src_base);
	      char *dst = (char *)(req->dst_base);
              for (size_t i = 0; i < req->nlines; i++) {
                memcpy(dst, src, req->nbytes);
                src += req->src_str;
                dst += req->dst_str;
              }
            }
            //double stoptime = Realm::Clock::current_time_in_microseconds();
            //fprintf(stderr, "t = %.2lfus, tp = %.2lfMB/s\n", stoptime - starttime, (req->nbytes / (stoptime - starttime)));
          }
          channel->return_request(thread_queue);
          thread_queue.clear();
        }
      }

      void MemcpyThread::stop()
      {
        channel->stop();
      }

      MemcpyChannel::MemcpyChannel(long max_nr)
      {
        kind = XferDes::XFER_MEM_CPY;
        capacity = max_nr;
        is_stopped = false;
        sleep_threads = false;
        pthread_mutex_init(&pending_lock, NULL);
        pthread_mutex_init(&finished_lock, NULL);
        pthread_cond_init(&pending_cond, NULL);
        //cbs = (MemcpyRequest**) calloc(max_nr, sizeof(MemcpyRequest*));
      }

      MemcpyChannel::~MemcpyChannel()
      {
        pthread_mutex_destroy(&pending_lock);
        pthread_mutex_destroy(&finished_lock);
        pthread_cond_destroy(&pending_cond);
        //free(cbs);
      }

      void MemcpyChannel::stop()
      {
        pthread_mutex_lock(&pending_lock);
        if (!is_stopped)
          pthread_cond_broadcast(&pending_cond);
        is_stopped = true;
        pthread_mutex_unlock(&pending_lock);
      }

      void MemcpyChannel::get_request(std::deque<MemcpyRequest*>& thread_queue)
      {
        pthread_mutex_lock(&pending_lock);
        while (pending_queue.empty() && !is_stopped) {
          sleep_threads = true;
          pthread_cond_wait(&pending_cond, &pending_lock);
        }
        if (!is_stopped) {
          // TODO: enable the following optimization
          //thread_queue.insert(thread_queue.end(), pending_queue.begin(), pending_queue.end());
          thread_queue.push_back(pending_queue.front());
          pending_queue.pop_front();
          //fprintf(stderr, "[%d] thread_queue.size = %lu\n", gettid(), thread_queue.size());
          //pending_queue.clear();
        }
        pthread_mutex_unlock(&pending_lock);
      }

      void MemcpyChannel::return_request(std::deque<MemcpyRequest*>& thread_queue)
      {
        pthread_mutex_lock(&finished_lock);
        finished_queue.insert(finished_queue.end(), thread_queue.begin(), thread_queue.end());
        pthread_mutex_unlock(&finished_lock);
      }

      long MemcpyChannel::submit(Request** requests, long nr)
      {
        MemcpyRequest** mem_cpy_reqs = (MemcpyRequest**) requests;
        for (long i = 0; i < nr; i++) {
          MemcpyRequest* req = mem_cpy_reqs[i];
          if (req->dim == Request::DIM_1D) {
            memcpy(req->dst_base, req->src_base, req->nbytes);
          } else {
            assert(req->dim == Request::DIM_2D);
            const char *src = (const char *)(req->src_base);
	    char *dst = (char *)(req->dst_base);
            for (size_t i = 0; i < req->nlines; i++) {
              memcpy(dst, src, req->nbytes);
              src += req->src_str;
              dst += req->dst_str;
            }
          }
          req->xd->notify_request_read_done(req);
          req->xd->notify_request_write_done(req);
        }
        return nr;
        /*
        pthread_mutex_lock(&pending_lock);
        //if (nr > 0)
          //printf("MemcpyChannel::submit[nr = %ld]\n", nr);
        for (long i = 0; i < nr; i++) {
          pending_queue.push_back(mem_cpy_reqs[i]);
        }
        if (sleep_threads) {
          pthread_cond_broadcast(&pending_cond);
          sleep_threads = false;
        }
        pthread_mutex_unlock(&pending_lock);
        return nr;
        */
        /*
        for (int i = 0; i < nr; i++) {
          push_request(mem_cpy_reqs[i]);
          memcpy(mem_cpy_reqs[i]->dst_buf, mem_cpy_reqs[i]->src_buf, mem_cpy_reqs[i]->nbytes);
          mem_cpy_reqs[i]->xd->notify_request_read_done(mem_cpy_reqs[i]);
          mem_cpy_reqs[i]->xd->notify_request_write_done(mem_cpy_reqs[i]);
        }
        return nr;
        */
      }

      void MemcpyChannel::pull()
      {
        pthread_mutex_lock(&finished_lock);
        while (!finished_queue.empty()) {
          MemcpyRequest* req = finished_queue.front();
          finished_queue.pop_front();
          req->xd->notify_request_read_done(req);
          req->xd->notify_request_write_done(req);
        }
        pthread_mutex_unlock(&finished_lock);
        /*
        while (true) {
          long np = worker->pull(cbs, capacity);
          for (int i = 0; i < np; i++) {
            cbs[i]->xd->notify_request_read_done(cbs[i]);
            cbs[i]->xd->notify_request_write_done(cbs[i]);
          }
          if (np != capacity)
            break;
        }
        */
      }

      long MemcpyChannel::available()
      {
        return capacity;
      }

      GASNetChannel::GASNetChannel(long max_nr, XferDes::XferKind _kind)
      {
        kind = _kind;
        capacity = max_nr;
      }

      GASNetChannel::~GASNetChannel()
      {
      }

      long GASNetChannel::submit(Request** requests, long nr)
      {
        for (long i = 0; i < nr; i++) {
          GASNetRequest* req = (GASNetRequest*) requests[i];
          switch (kind) {
            case XferDes::XFER_GASNET_READ:
            {
              get_runtime()->global_memory->get_bytes(req->gas_off,
                                                      req->mem_base,
                                                      req->nbytes);
              break;
            }
            case XferDes::XFER_GASNET_WRITE:
            {
              get_runtime()->global_memory->put_bytes(req->gas_off,
                                                      req->mem_base,
                                                      req->nbytes);
              break;
            }
            default:
              assert(0);
          }
          req->xd->notify_request_read_done(req);
          req->xd->notify_request_write_done(req);
        }
        return nr;
      }

      void GASNetChannel::pull()
      {
      }

      long GASNetChannel::available()
      {
        return capacity;
      }

      RemoteWriteChannel::RemoteWriteChannel(long max_nr)
      {
        capacity = max_nr;
      }

      RemoteWriteChannel::~RemoteWriteChannel() {}

      long RemoteWriteChannel::submit(Request** requests, long nr)
      {
        assert(nr <= capacity);
        for (long i = 0; i < nr; i ++) {
          RemoteWriteRequest* req = (RemoteWriteRequest*) requests[i];
	  // send a request if there's data or if there's a next XD to update
	  if((req->nbytes > 0) ||
	     (req->xd->next_xd_guid != XferDes::XFERDES_NO_GUID)) {
	    if (req->dim == Request::DIM_1D) {
	      XferDesRemoteWriteMessage::send_request(
                req->dst_node, req->dst_base, req->src_base, req->nbytes, req,
		req->xd->next_xd_guid, req->seq_pos, req->seq_count, 
		(req->xd->iteration_completed ? req->xd->bytes_total : (size_t)-1));
	    } else {
	      assert(req->dim == Request::DIM_2D);
	      // dest MUST be continuous
	      assert(req->nlines <= 1 || ((size_t)req->dst_str) == req->nbytes);
	      XferDesRemoteWriteMessage::send_request(
                req->dst_node, req->dst_base, req->src_base, req->nbytes,
                req->src_str, req->nlines, req,
		req->xd->next_xd_guid, req->seq_pos, req->seq_count, 
		(req->xd->iteration_completed ? req->xd->bytes_total : (size_t)-1));
	    }
	  }
	  // for an empty transfer, we do the local completion ourselves
	  //   instead of waiting for an ack from the other node
	  if(req->nbytes == 0) {
	    req->xd->notify_request_read_done(req);
	    req->xd->notify_request_write_done(req);
	    notify_completion();
	  }
          __sync_fetch_and_sub(&capacity, 1);
        /*RemoteWriteRequest* req = (RemoteWriteRequest*) requests[i];
          req->complete_event = GenEventImpl::create_genevent()->current_event();
          Realm::RemoteWriteMessage::RequestArgs args;
          args.mem = req->dst_mem;
          args.offset = req->dst_offset;
          args.event = req->complete_event;
          args.sender = gasnet_mynode();
          args.sequence_id = 0;

          Realm::RemoteWriteMessage::Message::request(ID(args.mem).node(), args,
                                                      req->src_buf, req->nbytes,
                                                      PAYLOAD_KEEPREG,
                                                      req->dst_buf);*/
        }
        return nr;
      }

      void RemoteWriteChannel::pull()
      {
      }

      long RemoteWriteChannel::available()
      {
        return capacity;
      }
   
#ifdef USE_CUDA
      GPUChannel::GPUChannel(GPU* _src_gpu, long max_nr, XferDes::XferKind _kind)
      {
        src_gpu = _src_gpu;
        kind = _kind;
        capacity = max_nr;
      }

      GPUChannel::~GPUChannel()
      {
      }

      long GPUChannel::submit(Request** requests, long nr)
      {
        for (long i = 0; i < nr; i++) {
          GPURequest* req = (GPURequest*) requests[i];
          if (req->dim == Request::DIM_1D) { 
            switch (kind) {
              case XferDes::XFER_GPU_TO_FB:
                src_gpu->copy_to_fb(req->dst_gpu_off, req->src_base,
                                    req->nbytes, &req->event);
                break;
              case XferDes::XFER_GPU_FROM_FB:
                src_gpu->copy_from_fb(req->dst_base, req->src_gpu_off,
                                      req->nbytes, &req->event);
                break;
              case XferDes::XFER_GPU_IN_FB:
                src_gpu->copy_within_fb(req->dst_gpu_off, req->src_gpu_off,
                                        req->nbytes, &req->event);
                break;
              case XferDes::XFER_GPU_PEER_FB:
                src_gpu->copy_to_peer(req->dst_gpu, req->dst_gpu_off,
                                      req->src_gpu_off, req->nbytes,
                                      &req->event);
                break;
              default:
                assert(0);
            }
          } else {
            assert(req->dim == Request::DIM_2D);
            switch (kind) {
              case XferDes::XFER_GPU_TO_FB:
                src_gpu->copy_to_fb_2d(req->dst_gpu_off, req->src_base,
                                       req->dst_str, req->src_str,
                                       req->nbytes, req->nlines, &req->event);
                break;
              case XferDes::XFER_GPU_FROM_FB:
                src_gpu->copy_from_fb_2d(req->dst_base, req->src_gpu_off,
                                         req->dst_str, req->src_str,
                                         req->nbytes, req->nlines,
                                         &req->event);
                break;
              case XferDes::XFER_GPU_IN_FB:
                src_gpu->copy_within_fb_2d(req->dst_gpu_off, req->src_gpu_off,
                                           req->dst_str, req->src_str,
                                           req->nbytes, req->nlines,
                                           &req->event);
                break;
              case XferDes::XFER_GPU_PEER_FB:
                src_gpu->copy_to_peer_2d(req->dst_gpu, req->dst_gpu_off,
                                         req->src_gpu_off, req->dst_str,
                                         req->src_str, req->nbytes,
                                         req->nlines, &req->event);
                break;
              default:
                assert(0);
            }
          }
          pending_copies.push_back(req);
        }
        return nr;
      }

      void GPUChannel::pull()
      {
        switch (kind) {
          case XferDes::XFER_GPU_TO_FB:
          case XferDes::XFER_GPU_FROM_FB:
          case XferDes::XFER_GPU_IN_FB:
          case XferDes::XFER_GPU_PEER_FB:
            while (!pending_copies.empty()) {
              GPURequest* req = (GPURequest*)pending_copies.front();
              if (req->event.has_triggered()) {
                req->xd->notify_request_read_done(req);
                req->xd->notify_request_write_done(req);
                pending_copies.pop_front();
              }
              else
                break;
            }
            break;
          default:
            assert(0);
        }
      }

      long GPUChannel::available()
      {
        return capacity - pending_copies.size();
      }
#endif

#ifdef USE_HDF
      HDFChannel::HDFChannel(long max_nr, XferDes::XferKind _kind)
      {
        kind = _kind;
        capacity = max_nr;
      }

      HDFChannel::~HDFChannel() {}

      long HDFChannel::submit(Request** requests, long nr)
      {
        HDFRequest** hdf_reqs = (HDFRequest**) requests;
        for (long i = 0; i < nr; i++) {
          HDFRequest* req = hdf_reqs[i];
          //pthread_rwlock_rdlock(req->rwlock);
          if (kind == XferDes::XFER_HDF_READ)
            CHECK_HDF5( H5Dread(req->dataset_id, req->datatype_id,
				req->mem_space_id, req->file_space_id,
				H5P_DEFAULT, req->mem_base) );
          else
            CHECK_HDF5( H5Dwrite(req->dataset_id, req->datatype_id,
				 req->mem_space_id, req->file_space_id,
				 H5P_DEFAULT, req->mem_base) );
          //pthread_rwlock_unlock(req->rwlock);
          req->xd->notify_request_read_done(req);
          req->xd->notify_request_write_done(req);
        }
        return nr;
      }

      void HDFChannel::pull() {}

      long HDFChannel::available()
      {
        return capacity;
      }
#endif

      /*static*/
      void XferDesRemoteWriteMessage::handle_request(RequestArgs args,
                                                     const void *data,
                                                     size_t datalen)
      {
        // assert data copy is in right position
        //assert(data == args.dst_buf);

	// if requested, notify (probably-local) next XD
	if(args.next_xd_guid != XferDes::XFERDES_NO_GUID)
	  xferDes_queue->update_pre_bytes_write(args.next_xd_guid,
						args.span_start,
						args.span_size,
						args.pre_bytes_total);

	// don't ack empty requests
	if(datalen > 0)
	  XferDesRemoteWriteAckMessage::send_request(args.sender, args.req);
      }

      /*static*/
      void XferDesRemoteWriteAckMessage::handle_request(RequestArgs args)
      {
        RemoteWriteRequest* req = args.req;
        req->xd->notify_request_read_done(req);
        req->xd->notify_request_write_done(req);
        channel_manager->get_remote_write_channel()->notify_completion();
      }

      /*static*/
      void XferDesCreateMessage::handle_request(RequestArgs args,
                                                const void *msgdata,
                                                size_t msglen)
      {
#if 0
        const Payload *payload = (const Payload *)msgdata;
        std::vector<OffsetsAndSize> oas_vec(payload->oas_vec_size);
        for(size_t i = 0; i < payload->oas_vec_size; i++)
          oas_vec[i] = payload->oas_vec(i);
        Buffer src_buf, dst_buf;
        src_buf.deserialize(payload->src_buf_bits);
        src_buf.memory = args.src_mem;
        dst_buf.deserialize(payload->dst_buf_bits);
        dst_buf.memory = args.dst_mem;
#else
	DmaRequest* dma_request;
	gasnet_node_t launch_node;
	XferDesID guid, pre_xd_guid, next_xd_guid;
	uint64_t next_max_rw_gap;
	size_t src_ib_offset;
	size_t src_ib_size;
	bool mark_started;
	TransferIterator *src_iter;
	TransferIterator *dst_iter;
	uint64_t max_req_size;
	long max_nr;
	int priority;
	XferOrder::Type order;
	XferDes::XferKind kind;

	Realm::Serialization::FixedBufferDeserializer fbd(msgdata, msglen);

	bool ok = ((fbd >> (intptr_t&)dma_request) &&
		   (fbd >> launch_node) &&
		   (fbd >> guid) &&
		   (fbd >> pre_xd_guid) &&
		   (fbd >> next_xd_guid) &&
		   (fbd >> next_max_rw_gap) &&
		   (fbd >> src_ib_offset) &&
		   (fbd >> src_ib_size) &&
		   (fbd >> mark_started) &&
		   (fbd >> max_req_size) &&
		   (fbd >> max_nr) &&
		   (fbd >> priority) &&
		   (fbd >> order) &&
		   (fbd >> kind));
	assert(ok);
	src_iter = TransferIterator::deserialize_new(fbd);
	dst_iter = TransferIterator::deserialize_new(fbd);
	assert((src_iter != 0) && (dst_iter != 0));
	assert(fbd.bytes_left() == 0);

	create_xfer_des(dma_request, launch_node, gasnet_mynode(),
			guid, pre_xd_guid, next_xd_guid,
			next_max_rw_gap, src_ib_offset, src_ib_size,
			mark_started,
			args.src_mem, args.dst_mem,
			src_iter, dst_iter,
			max_req_size, max_nr, priority,
			order, kind, args.fence, args.inst);
#endif
#if 0
        switch(payload->domain.dim) {
        case 0:
          create_xfer_des<0>(payload->dma_request, payload->launch_node,
                             payload->guid, payload->pre_xd_guid, payload->next_xd_guid,
                             payload->mark_started, src_buf, dst_buf, payload->domain, oas_vec,
                             payload->max_req_size, payload->max_nr, payload->priority,
                             payload->order, payload->kind, args.fence, args.inst);
          break;
        case 1:
          create_xfer_des<1>(payload->dma_request, payload->launch_node,
                             payload->guid, payload->pre_xd_guid, payload->next_xd_guid,
                             payload->mark_started, src_buf, dst_buf, payload->domain, oas_vec,
                             payload->max_req_size, payload->max_nr, payload->priority,
                             payload->order, payload->kind, args.fence, args.inst);
          break;
        case 2:
          create_xfer_des<2>(payload->dma_request, payload->launch_node,
                             payload->guid, payload->pre_xd_guid, payload->next_xd_guid,
                             payload->mark_started, src_buf, dst_buf, payload->domain, oas_vec,
                             payload->max_req_size, payload->max_nr, payload->priority,
                             payload->order, payload->kind, args.fence, args.inst);
          break;
        case 3:
          create_xfer_des<3>(payload->dma_request, payload->launch_node,
                             payload->guid, payload->pre_xd_guid, payload->next_xd_guid,
                             payload->mark_started, src_buf, dst_buf, payload->domain, oas_vec,
                             payload->max_req_size, payload->max_nr, payload->priority,
                             payload->order, payload->kind, args.fence, args.inst);
          break;
        default:
          assert(0);
        }
#endif
      }

      /*static*/
      void XferDesCreateMessage::send_request(gasnet_node_t target, DmaRequest* dma_request, gasnet_node_t launch_node,
                               XferDesID guid, XferDesID pre_xd_guid, XferDesID next_xd_guid,
					      uint64_t next_max_rw_gap,
					      size_t src_ib_offset,
					      size_t src_ib_size,
                               bool mark_started,
			       Memory _src_mem, Memory _dst_mem,
			       TransferIterator *_src_iter, TransferIterator *_dst_iter,
                               uint64_t max_req_size, long max_nr, int priority,
                               XferOrder::Type order, XferDes::XferKind kind,
                               XferDesFence* fence,
			       RegionInstance inst /*= RegionInstance::NO_INST*/)
      {
#if 0
        size_t payload_size = sizeof(Payload);// + sizeof(OffsetsAndSize) * oas_vec.size();
        Payload *payload = (Payload*) malloc(payload_size);
        payload->dma_request = dma_request;
        payload->launch_node = launch_node;
        payload->guid = guid;
        payload->pre_xd_guid = pre_xd_guid;
        payload->next_xd_guid = next_xd_guid;
        payload->mark_started = mark_started;
        payload->max_req_size = max_req_size;
        payload->max_nr = max_nr;
        payload->priority = priority;
        payload->order = order;
        payload->kind = kind;
        //payload->domain = domain;
        //src_buf.serialize(payload->src_buf_bits);
        //dst_buf.serialize(payload->dst_buf_bits);
        //payload->oas_vec_size = oas_vec.size();
        //for (unsigned i = 0; i < oas_vec.size(); i++)
        //  payload->oas_vec(i) = oas_vec[i];
#else
	Realm::Serialization::DynamicBufferSerializer dbs(128);
	bool ok = ((dbs << (intptr_t)dma_request) &&
		   (dbs << launch_node) &&
		   (dbs << guid) &&
		   (dbs << pre_xd_guid) &&
		   (dbs << next_xd_guid) &&
		   (dbs << next_max_rw_gap) &&
		   (dbs << src_ib_offset) &&
		   (dbs << src_ib_size) &&
		   (dbs << mark_started) &&
		   (dbs << max_req_size) &&
		   (dbs << max_nr) &&
		   (dbs << priority) &&
		   (dbs << order) &&
		   (dbs << kind) &&
		   (dbs << *_src_iter) &&
		   (dbs << *_dst_iter));
	assert(ok);
	
	size_t payload_size = dbs.bytes_used();
	void *payload = dbs.detach_buffer(-1 /*no trim*/);
#endif

        RequestArgs args;
        args.inst = inst;
        args.src_mem = _src_mem; //src_buf.memory;
        args.dst_mem = _dst_mem; //dst_buf.memory;
        args.fence = fence;
        Message::request(target, args, payload, payload_size, PAYLOAD_FREE);
      }

      /*static*/ void XferDesDestroyMessage::handle_request(RequestArgs args)
      {
        xferDes_queue->destroy_xferDes(args.guid);
      }

      /*static*/ void UpdateBytesWriteMessage::handle_request(RequestArgs args)
      {
        xferDes_queue->update_pre_bytes_write(args.guid,
					      args.span_start,
					      args.span_size,
					      args.pre_bytes_total);
      }

      /*static*/ void UpdateBytesReadMessage::handle_request(RequestArgs args)
      {
        xferDes_queue->update_next_bytes_read(args.guid,
					      args.span_start,
					      args.span_size);
      }

      void DMAThread::dma_thread_loop()
      {
        log_new_dma.info("start dma thread loop");
        while (!is_stopped) {
          bool is_empty = true;
          std::map<Channel*, PriorityXferDesQueue*>::iterator it;
          for (it = channel_to_xd_pool.begin(); it != channel_to_xd_pool.end(); it++) {
            if(!it->second->empty()) {
              is_empty = false;
              break;
            }
          }
          xd_queue->dequeue_xferDes(this, is_empty);

          for (it = channel_to_xd_pool.begin(); it != channel_to_xd_pool.end(); it++) {
            it->first->pull();
            long nr = it->first->available();
            if (nr == 0)
              continue;
            std::vector<XferDes*> finish_xferdes;
            PriorityXferDesQueue::iterator it2;
            for (it2 = it->second->begin(); it2 != it->second->end(); it2++) {
              assert((*it2)->channel == it->first);
              // If we haven't mark started and we are the first xd, mark start
              if ((*it2)->mark_start) {
                (*it2)->dma_request->mark_started();
                (*it2)->mark_start = false;
              }
              // Do nothing for empty copies
              // if ((*it2)->bytes_total ==0) {
              //   finish_xferdes.push_back(*it2);
              //   continue;
              // }
              long nr_got = (*it2)->get_requests(requests, std::min(nr, max_nr));
              long nr_submitted = it->first->submit(requests, nr_got);
              nr -= nr_submitted;
              assert(nr_got == nr_submitted);
              if ((*it2)->is_completed()) {
                finish_xferdes.push_back(*it2);
                //printf("finish_xferdes.size() = %lu\n", finish_xferdes.size());
		continue;
              }
              if (nr == 0)
                break;
            }
            while(!finish_xferdes.empty()) {
              XferDes *xd = finish_xferdes.back();
              finish_xferdes.pop_back();
              it->second->erase(xd);
              // We flush all changes into destination before mark this XferDes as completed
              xd->flush();
              log_new_dma.info("Finish XferDes : id(" IDFMT ")", xd->guid);
              xd->mark_completed();
              /*bool need_to_delete_dma_request = xd->mark_completed();
              if (need_to_delete_dma_request) {
                DmaRequest* dma_request = xd->dma_request;
                delete dma_request;
              }*/
            }
          }
        }
        log_new_dma.info("finish dma thread loop");
      }

      XferDesQueue* get_xdq_singleton()
      {
        return xferDes_queue;
      }

      ChannelManager* get_channel_manager()
      {
        return channel_manager;
      }

      ChannelManager::~ChannelManager(void) {
        if (memcpy_channel)
          delete memcpy_channel;
        if (gasnet_read_channel)
          delete gasnet_read_channel;
        if (gasnet_write_channel)
          delete gasnet_write_channel;
        if (remote_write_channel)
          delete remote_write_channel;
        if (file_read_channel)
          delete file_read_channel;
        if (file_write_channel)
          delete file_write_channel;
        if (disk_read_channel)
          delete disk_read_channel;
        if (disk_write_channel)
          delete disk_write_channel;
#ifdef USE_CUDA
        std::map<GPU*, GPUChannel*>::iterator it;
        for (it = gpu_to_fb_channels.begin(); it != gpu_to_fb_channels.end(); it++) {
          delete it->second;
        }
        for (it = gpu_from_fb_channels.begin(); it != gpu_from_fb_channels.end(); it++) {
          delete it->second;
        }
        for (it = gpu_in_fb_channels.begin(); it != gpu_in_fb_channels.end(); it++) {
          delete it->second;
        }
        for (it = gpu_peer_fb_channels.begin(); it != gpu_peer_fb_channels.end(); it++) {
          delete it->second;
        }
#endif
      }
#ifdef USE_CUDA
      void register_gpu_in_dma_systems(GPU* gpu)
      {
        dma_all_gpus.push_back(gpu);
      }
#endif
      void start_channel_manager(int count, bool pinned, int max_nr,
                                 Realm::CoreReservationSet& crs)
      {
        xferDes_queue = new XferDesQueue(count, pinned, crs);
        channel_manager = new ChannelManager;
        xferDes_queue->start_worker(count, max_nr, channel_manager);
      }
      FileChannel* ChannelManager::create_file_read_channel(long max_nr) {
        assert(file_read_channel == NULL);
        file_read_channel = new FileChannel(max_nr, XferDes::XFER_FILE_READ);
        return file_read_channel;
      }
      FileChannel* ChannelManager::create_file_write_channel(long max_nr) {
        assert(file_write_channel == NULL);
        file_write_channel = new FileChannel(max_nr, XferDes::XFER_FILE_WRITE);
        return file_write_channel;
      }
      DiskChannel* ChannelManager::create_disk_read_channel(long max_nr) {
        assert(disk_read_channel == NULL);
        disk_read_channel = new DiskChannel(max_nr, XferDes::XFER_DISK_READ);
        return disk_read_channel;
      }
      DiskChannel* ChannelManager::create_disk_write_channel(long max_nr) {
        assert(disk_write_channel == NULL);
        disk_write_channel = new DiskChannel(max_nr, XferDes::XFER_DISK_WRITE);
        return disk_write_channel;
      }

      void XferDesQueue::start_worker(int count, int max_nr, ChannelManager* channel_manager) 
      {
        log_new_dma.info("XferDesQueue: start_workers");
        // num_memcpy_threads = 0;
#ifdef USE_HDF
        // Need a dedicated thread for handling HDF requests
        // num_threads ++;
#endif
#ifdef USE_CUDA
        // num_threads ++;
#endif
        int idx = 0;
        dma_threads = (DMAThread**) calloc(count, sizeof(DMAThread*));
        // dma thread #1: memcpy
        std::vector<Channel*> channels;
        MemcpyChannel* memcpy_channel = channel_manager->create_memcpy_channel(max_nr);
        channels.push_back(memcpy_channel);
        channels.push_back(channel_manager->create_gasnet_read_channel(max_nr));
        channels.push_back(channel_manager->create_gasnet_write_channel(max_nr));
        if (count > 1) {
          dma_threads[idx++] = new DMAThread(max_nr, xferDes_queue, channels);
          channels.clear();
          count --;
        }
        // dma thread #2: async xfer
        channels.push_back(channel_manager->create_remote_write_channel(max_nr));
        channels.push_back(channel_manager->create_disk_read_channel(max_nr));
        channels.push_back(channel_manager->create_disk_write_channel(max_nr));
        channels.push_back(channel_manager->create_file_read_channel(max_nr));
        channels.push_back(channel_manager->create_file_write_channel(max_nr));
#ifdef USE_HDF
        channels.push_back(channel_manager->create_hdf_read_channel(max_nr));
        channels.push_back(channel_manager->create_hdf_write_channel(max_nr));
#endif
        if (count > 1) {
          dma_threads[idx++] = new DMAThread(max_nr, xferDes_queue, channels);
          channels.clear();
          count --;
        }
#ifdef USE_CUDA
        std::vector<GPU*>::iterator it;
        for (it = dma_all_gpus.begin(); it != dma_all_gpus.end(); it ++) {
          channels.push_back(channel_manager->create_gpu_to_fb_channel(max_nr, *it));
          channels.push_back(channel_manager->create_gpu_from_fb_channel(max_nr, *it));
          channels.push_back(channel_manager->create_gpu_in_fb_channel(max_nr, *it));
          channels.push_back(channel_manager->create_gpu_peer_fb_channel(max_nr, *it));
        }
#endif
        dma_threads[idx++] = new DMAThread(max_nr, xferDes_queue, channels);
        num_threads = idx;
        for (int i = 0; i < num_threads; i++) {
          // register dma thread to XferDesQueue
          register_dma_thread(dma_threads[i]);
        }

        Realm::ThreadLaunchParameters tlp;

        for(int i = 0; i < num_threads; i++) {
          log_new_dma.info("Create a DMA worker thread");
          Realm::Thread *t = Realm::Thread::create_kernel_thread<DMAThread,
                                            &DMAThread::dma_thread_loop>(dma_threads[i],
  						                         tlp,
  					                                 *core_rsrv,
  					                                 0 /* default scheduler*/);
          worker_threads.push_back(t);
        }

#ifdef USE_DEDICATED_MEMCPY_THREADS
        // Next we create memcpy threads
        memcpy_threads =(MemcpyThread**) calloc(num_memcpy_threads, sizeof(MemcpyThread*));
        for (int i = 0; i < num_memcpy_threads; i++) {
          memcpy_threads[i] = new MemcpyThread(memcpy_channel);
          Realm::Thread *t = Realm::Thread::create_kernel_thread<MemcpyThread,
                                            &MemcpyThread::thread_loop>(memcpy_threads[i],
                                                                        tlp,
                                                                        *core_rsrv,
                                                                        0 /*default scheduler*/);
          worker_threads.push_back(t);
        }
#endif
        assert(worker_threads.size() == (size_t)(num_threads));
      }

      void stop_channel_manager()
      {
        xferDes_queue->stop_worker();
        delete xferDes_queue;
        delete channel_manager;
      }

      void XferDesQueue::stop_worker() {
        for (int i = 0; i < num_threads; i++)
          dma_threads[i]->stop();
        for (int i = 0; i < num_memcpy_threads; i++)
          memcpy_threads[i]->stop();
        // reap all the threads
        for(std::vector<Realm::Thread *>::iterator it = worker_threads.begin();
            it != worker_threads.end();
            it++) {
          (*it)->join();
          delete (*it);
        }
        worker_threads.clear();
        for (int i = 0; i < num_threads; i++)
          delete dma_threads[i];
        for (int i = 0; i < num_memcpy_threads; i++)
          delete memcpy_threads[i];
        free(dma_threads);
#ifdef USE_DEDICATED_MEMCPY_THREADS
        free(memcpy_threads);
#endif
      }

      class DeferredXDEnqueue : public Realm::EventWaiter {
      public:
	DeferredXDEnqueue(XferDesQueue *_xferDes_queue,
			  XferDes *_xd)
	  : xferDes_queue(_xferDes_queue), xd(_xd)
	{}

	virtual bool event_triggered(Event e, bool poisoned)
	{
	  // TODO: handle poisoning
	  assert(!poisoned);
	  log_new_dma.info() << "xd metadata ready: xd=" << xd->guid;
	  xferDes_queue->enqueue_xferDes_local(xd);
	  return true; // delete us
	}

	virtual void print(std::ostream& os) const
	{
	  os << "deferred xd enqueue: xd=" << xd->guid;
	}

	virtual Event get_finish_event(void) const
	{
	  // TODO: would be nice to provide dma op's finish event here
	  return Event::NO_EVENT;
	}

      protected:
	XferDesQueue *xferDes_queue;
	XferDes *xd;
      };

      void create_xfer_des(DmaRequest* _dma_request,
                           gasnet_node_t _launch_node,
			   gasnet_node_t _target_node,
                           XferDesID _guid,
                           XferDesID _pre_xd_guid,
                           XferDesID _next_xd_guid,
			   uint64_t _next_max_rw_gap,
			   size_t src_ib_offset,
			   size_t src_ib_size,
                           bool mark_started,
			   Memory _src_mem, Memory _dst_mem,
			   TransferIterator *_src_iter,
			   TransferIterator *_dst_iter,
                           uint64_t _max_req_size,
                           long max_nr,
                           int _priority,
                           XferOrder::Type _order,
                           XferDes::XferKind _kind,
                           XferDesFence* _complete_fence,
                           RegionInstance inst)
      {
	//if (ID(_src_buf.memory).memory.owner_node == gasnet_mynode()) {
	if(_target_node == gasnet_mynode()) {
          // size_t total_field_size = 0;
          // for (unsigned i = 0; i < _oas_vec.size(); i++) {
          //   total_field_size += _oas_vec[i].size;
          // }
          log_new_dma.info("Create local XferDes: id(" IDFMT "), pre(" IDFMT
                           "), next(" IDFMT "), type(%d)",
                           _guid, _pre_xd_guid, _next_xd_guid, _kind);
          XferDes* xd;
          switch (_kind) {
          case XferDes::XFER_MEM_CPY:
            xd = new MemcpyXferDes(_dma_request, _launch_node,
				   _guid, _pre_xd_guid, _next_xd_guid,
				   _next_max_rw_gap,
				   src_ib_offset, src_ib_size,
				   mark_started,
				   //_src_buf, _dst_buf, _domain, _oas_vec,
				   _src_mem, _dst_mem, _src_iter, _dst_iter,
				   _max_req_size, max_nr, _priority,
				   _order, _complete_fence);
            break;
          case XferDes::XFER_GASNET_READ:
          case XferDes::XFER_GASNET_WRITE:
            xd = new GASNetXferDes(_dma_request, _launch_node,
				   _guid, _pre_xd_guid, _next_xd_guid,
				   _next_max_rw_gap,
				   src_ib_offset, src_ib_size,
				   mark_started,
				   //_src_buf, _dst_buf, _domain, _oas_vec,
				   _src_mem, _dst_mem, _src_iter, _dst_iter,
				   _max_req_size, max_nr, _priority,
				   _order, _kind, _complete_fence);
            break;
          case XferDes::XFER_REMOTE_WRITE:
            xd = new RemoteWriteXferDes(_dma_request, _launch_node,
					_guid, _pre_xd_guid, _next_xd_guid,
					_next_max_rw_gap,
					src_ib_offset, src_ib_size,
					mark_started,
					//_src_buf, _dst_buf, _domain, _oas_vec,
					_src_mem, _dst_mem, _src_iter, _dst_iter,
					_max_req_size, max_nr, _priority,
					_order, _complete_fence);
            break;
          case XferDes::XFER_DISK_READ:
          case XferDes::XFER_DISK_WRITE:
            xd = new DiskXferDes(_dma_request, _launch_node,
				 _guid, _pre_xd_guid, _next_xd_guid,
				 _next_max_rw_gap,
				 src_ib_offset, src_ib_size,
				 mark_started,
				 //_src_buf, _dst_buf, _domain, _oas_vec,
				 _src_mem, _dst_mem, _src_iter, _dst_iter,
				 _max_req_size, max_nr, _priority,
				 _order, _kind, _complete_fence);
            break;
          case XferDes::XFER_FILE_READ:
          case XferDes::XFER_FILE_WRITE:
            xd = new FileXferDes(_dma_request, _launch_node,
				 _guid, _pre_xd_guid, _next_xd_guid,
				 _next_max_rw_gap,
				 src_ib_offset, src_ib_size,
				 mark_started,
				 inst,
				 // _src_buf, _dst_buf, _domain, _oas_vec,
				 _src_mem, _dst_mem, _src_iter, _dst_iter,
				 _max_req_size, max_nr, _priority,
				 _order, _kind, _complete_fence);
            break;
#ifdef USE_CUDA
          case XferDes::XFER_GPU_FROM_FB:
          case XferDes::XFER_GPU_TO_FB:
          case XferDes::XFER_GPU_IN_FB:
          case XferDes::XFER_GPU_PEER_FB:
            xd = new GPUXferDes(_dma_request, _launch_node,
				_guid, _pre_xd_guid, _next_xd_guid,
				_next_max_rw_gap,
				src_ib_offset, src_ib_size,
				mark_started,
				//_src_buf, _dst_buf, _domain, _oas_vec,
				_src_mem, _dst_mem, _src_iter, _dst_iter,
				_max_req_size, max_nr, _priority,
				_order, _kind, _complete_fence);
            break;
#endif
#ifdef USE_HDF
          case XferDes::XFER_HDF_READ:
          case XferDes::XFER_HDF_WRITE:
	    xd = new HDFXferDes(_dma_request, _launch_node,
				_guid, _pre_xd_guid, _next_xd_guid,
				_next_max_rw_gap,
				src_ib_offset, src_ib_size,
				mark_started,
				inst,
				//_src_buf, _dst_buf, _domain, _oas_vec,
				_src_mem, _dst_mem, _src_iter, _dst_iter,
				_max_req_size, max_nr, _priority,
				_order, _kind, _complete_fence);
	    break;
#endif
        default:
          printf("_kind = %d\n", _kind);
          assert(false);
	}
	// see if the newly-created xd's iterators needs metadata, and if so,
	//   defer the enqueuing
	Event src_iter_ready = _src_iter->request_metadata();
	Event dst_iter_ready = _dst_iter->request_metadata();
	if(!src_iter_ready.has_triggered() || !dst_iter_ready.has_triggered()) {
	  Event wait_on = Event::merge_events(src_iter_ready, dst_iter_ready);
	  log_new_dma.info() << "xd metadata wait: xd=" << _guid << " ready=" << wait_on;
	  Realm::EventImpl::add_waiter(wait_on, new DeferredXDEnqueue(xferDes_queue,
								      xd));
	} else
	  xferDes_queue->enqueue_xferDes_local(xd);
      } else {
        log_new_dma.info("Create remote XferDes: id(" IDFMT "),"
                         " pre(" IDFMT "), next(" IDFMT "), type(%d)",
                         _guid, _pre_xd_guid, _next_xd_guid, _kind);
        // If the remote XD is the first one on the path, we mark start on the source
        // node. This is sort of a hack, but this case only happens with GASNet Memory
        if (mark_started)
          _dma_request->mark_started();
        XferDesCreateMessage::send_request(_target_node,
                                           _dma_request, _launch_node,
                                           _guid, _pre_xd_guid, _next_xd_guid,
					   _next_max_rw_gap,
					   src_ib_offset, src_ib_size,
                                           false,
                                           //_src_buf, _dst_buf, _domain, _oas_vec,
					   _src_mem, _dst_mem, _src_iter, _dst_iter,
                                           _max_req_size, max_nr, _priority,
                                           _order, _kind, _complete_fence, inst);
      }
    }

    void destroy_xfer_des(XferDesID _guid)
    {
      log_new_dma.info("Destroy XferDes: id(" IDFMT ")", _guid);
      gasnet_node_t execution_node = _guid >> (XferDesQueue::NODE_BITS + XferDesQueue::INDEX_BITS);
      if (execution_node == gasnet_mynode()) {
        xferDes_queue->destroy_xferDes(_guid);
      }
      else {
        XferDesDestroyMessage::send_request(execution_node, _guid);
      }
    }

#if 0
      template class MemcpyXferDes<1>;
      template class MemcpyXferDes<2>;
      template class MemcpyXferDes<3>;
      template class GASNetXferDes<1>;
      template class GASNetXferDes<2>;
      template class GASNetXferDes<3>;
      template class RemoteWriteXferDes<1>;
      template class RemoteWriteXferDes<2>;
      template class RemoteWriteXferDes<3>;
#ifdef USE_CUDA
      template class GPUXferDes<1>;
      template class GPUXferDes<2>;
      template class GPUXferDes<3>;
#endif
#ifdef USE_HDF
      template class HDFXferDes<1>;
      template class HDFXferDes<2>;
      template class HDFXferDes<3>;
#endif
      template long XferDes::default_get_requests<1>(Request**, long);
      template long XferDes::default_get_requests<2>(Request**, long);
      template long XferDes::default_get_requests<3>(Request**, long);
#endif
 } // namespace LowLevel
} // namespace LegionRuntime


