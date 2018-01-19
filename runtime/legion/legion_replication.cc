/* Copyright 2018 Stanford University, NVIDIA Corporation
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

#include "legion/legion_trace.h"
#include "legion/legion_views.h"
#include "legion/legion_context.h"
#include "legion/legion_replication.h"

namespace Legion {
  namespace Internal {

    LEGION_EXTERN_LOGGER_DECLARATIONS

#ifdef DEBUG_LEGION_COLLECTIVES
    /////////////////////////////////////////////////////////////
    // Collective Check Reduction
    /////////////////////////////////////////////////////////////
    
    /*static*/ const long CollectiveCheckReduction::IDENTITY = -1;
    /*static*/ const long CollectiveCheckReduction::identity = IDENTITY;
    /*static*/ const long CollectiveCheckReduction::BAD = -2;
    /*static*/ const ReductionOpID CollectiveCheckReduction::REDOP = 
                                                MAX_APPLICATION_REDUCTION_ID;

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CollectiveCheckReduction::apply<true>(LHS &lhs, RHS rhs)
    //--------------------------------------------------------------------------
    {
      assert(rhs > IDENTITY);
      if (lhs != IDENTITY)
      {
        if (lhs != rhs)
          lhs = BAD;
      }
      else
        lhs = rhs;
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CollectiveCheckReduction::apply<false>(LHS &lhs, RHS rhs)
    //--------------------------------------------------------------------------
    {
      volatile LHS *ptr = &lhs;
      LHS temp = *ptr;
      while ((temp != BAD) && (temp != rhs))
      {
        if (temp != IDENTITY)
          temp = __sync_val_compare_and_swap(ptr, temp, BAD);
        else
          temp = __sync_val_compare_and_swap(ptr, temp, rhs); 
      }
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CollectiveCheckReduction::fold<true>(RHS &rhs1, RHS rhs2)
    //--------------------------------------------------------------------------
    {
      assert(rhs2 > IDENTITY);
      if (rhs1 != IDENTITY)
      {
        if (rhs1 != rhs2)
          rhs1 = BAD;
      }
      else
        rhs1 = rhs2;
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CollectiveCheckReduction::fold<false>(RHS &rhs1, RHS rhs2)
    //--------------------------------------------------------------------------
    {
      volatile RHS *ptr = &rhs1;
      RHS temp = *ptr;
      while ((temp != BAD) && (temp != rhs2))
      {
        if (temp != IDENTITY)
          temp = __sync_val_compare_and_swap(ptr, temp, BAD);
        else
          temp = __sync_val_compare_and_swap(ptr, temp, rhs2);
      }
    }

    /////////////////////////////////////////////////////////////
    // Check Reduction
    /////////////////////////////////////////////////////////////
    
    /*static*/ const CloseCheckReduction::CloseCheckValue 
      CloseCheckReduction::IDENTITY = CloseCheckReduction::CloseCheckValue();
    /*static*/ const CloseCheckReduction::CloseCheckValue
      CloseCheckReduction::identity = IDENTITY;
    /*static*/ const ReductionOpID CloseCheckReduction::REDOP = 
                                              MAX_APPLICATION_REDUCTION_ID + 1;

    //--------------------------------------------------------------------------
    CloseCheckReduction::CloseCheckValue::CloseCheckValue(void)
      : operation_index(0), region_requirement_index(0),
        barrier(RtBarrier::NO_RT_BARRIER), region(LogicalRegion::NO_REGION), 
        partition(LogicalPartition::NO_PART), is_region(true), read_only(false)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    CloseCheckReduction::CloseCheckValue::CloseCheckValue(
        const LogicalUser &user, RtBarrier bar, RegionTreeNode *node, bool read)
      : operation_index(user.op->get_ctx_index()), 
        region_requirement_index(user.idx), barrier(bar),
        is_region(node->is_region()), read_only(read)
    //--------------------------------------------------------------------------
    {
      if (is_region)
        region = node->as_region_node()->handle;
      else
        partition = node->as_partition_node()->handle;
    }

    //--------------------------------------------------------------------------
    bool CloseCheckReduction::CloseCheckValue::operator==(const
                                                     CloseCheckValue &rhs) const
    //--------------------------------------------------------------------------
    {
      if (operation_index != rhs.operation_index)
        return false;
      if (region_requirement_index != rhs.region_requirement_index)
        return false;
      if (barrier != rhs.barrier)
        return false;
      if (read_only != rhs.read_only)
        return false;
      if (is_region != rhs.is_region)
        return false;
      if (is_region)
      {
        if (region != rhs.region)
          return false;
      }
      else
      {
        if (partition != rhs.partition)
          return false;
      }
      return true;
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CloseCheckReduction::apply<true>(LHS &lhs, RHS rhs)
    //--------------------------------------------------------------------------
    {
      // Only copy over if LHS is the identity
      // This will effectively do a broadcast of one value
      if (lhs == IDENTITY)
        lhs = rhs;
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CloseCheckReduction::apply<false>(LHS &lhs, RHS rhs)
    //--------------------------------------------------------------------------
    {
      // Not supported at the moment
      assert(false);
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CloseCheckReduction::fold<true>(RHS &rhs1, RHS rhs2)
    //--------------------------------------------------------------------------
    {
      // Only copy over if RHS1 is the identity
      // This will effectively do a broadcast of one value
      if (rhs1 == IDENTITY)
        rhs1 = rhs2;
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void CloseCheckReduction::fold<false>(RHS &rhs1, RHS rhs2)
    //--------------------------------------------------------------------------
    {
      // Not supported at the moment
      assert(false);
    }
#endif // DEBUG_LEGION_COLLECTIVES

    /////////////////////////////////////////////////////////////
    // Repl Individual Task 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplIndividualTask::ReplIndividualTask(Runtime *rt)
      : IndividualTask(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndividualTask::ReplIndividualTask(const ReplIndividualTask &rhs)
      : IndividualTask(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplIndividualTask::~ReplIndividualTask(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndividualTask& ReplIndividualTask::operator=(
                                                  const ReplIndividualTask &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_individual_task();
      owner_shard = 0;
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      launch_space = IndexSpace::NO_SPACE;
      versioning_collective_id = UINT_MAX;
      future_collective_id = UINT_MAX;
      version_broadcast_collective = NULL;
#ifdef DEBUG_LEGION
      sharding_collective = NULL; 
#endif
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::deactivate(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      if (version_broadcast_collective != NULL)
        delete version_broadcast_collective;
      deactivate_individual_task();
      projection_infos.clear();
      runtime->free_repl_individual_task(this);
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // We might be able to skip this if the sharding function was already
      // picked for us which occurs when we're part of a must-epoch launch
      if (sharding_function == NULL)
      {
        // Do the mapper call to get the sharding function to use
        if (mapper == NULL)
          mapper = runtime->find_mapper(current_proc, map_id); 
        Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
        Mapper::SelectShardingFunctorOutput output;
        output.chosen_functor = UINT_MAX;
        mapper->invoke_task_select_sharding_functor(this, input, &output);
        if (output.chosen_functor == UINT_MAX)
          REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                        "Mapper %s failed to pick a valid sharding functor for "
                        "task %s (UID %lld)", mapper->get_mapper_name(),
                        get_task_name(), get_unique_id())
        this->sharding_functor = output.chosen_functor;
        sharding_function = 
          repl_ctx->shard_manager->find_sharding_function(sharding_functor);
      }
#ifdef DEBUG_LEGION
      assert(sharding_function != NULL);
      // In debug mode we check to make sure that all the mappers
      // picked the same sharding function
      assert(sharding_collective != NULL);
      // Contribute the result
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() && 
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for individual task %s (UID %lld) in %s "
                      "(UID %lld)", mapper->get_mapper_name(), get_task_name(), 
                      get_unique_id(), parent_ctx->get_task_name(), 
                      parent_ctx->get_unique_id())
#endif
      // Now we can do the normal prepipeline stage
      IndividualTask::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(sharding_function != NULL);
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Figure out whether this shard owns this point
      owner_shard = sharding_function->find_owner(index_point, index_domain);
      if (Runtime::legion_spy_enabled)
        LegionSpy::log_owner_shard(get_unique_id(), owner_shard);
      // If we own it we go on the queue, otherwise we complete early
      if (owner_shard != repl_ctx->owner_shard->shard_id)
        perform_unowned_shard(repl_ctx);
      else // We own it, so it goes on the ready queue
        IndividualTask::trigger_ready();
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::perform_unowned_shard(ReplicateContext *repl_ctx)
    //--------------------------------------------------------------------------
    {
      // Since we're not going to run this task, we need to tell the
      // profiler about its task ID so it can give it a name
      // We use the same method as we do normally for multi tasks
      if (runtime->profiler != NULL)
        runtime->profiler->register_multi_task(this, task_id);
#ifdef DEBUG_LEGION
      assert(version_broadcast_collective == NULL);
#endif
      // We don't own it, so we can pretend like we
      // mapped and executed this task already
      // Before we can do that though we have to get the version state
      // names for any writes so we can update our local state
      version_broadcast_collective = new VersioningInfoBroadcast(repl_ctx,
                                      versioning_collective_id, owner_shard);
      RtEvent versions_ready = 
        version_broadcast_collective->perform_collective_wait(false/*block*/);
      if (versions_ready.exists() && !versions_ready.has_triggered())
      {
        // Defer completion until the versions are ready
        DeferredExecuteArgs deferred_execute_args;
        deferred_execute_args.proxy_this = this;
        runtime->issue_runtime_meta_task(deferred_execute_args,
                                         LG_THROUGHPUT_DEFERRED_PRIORITY,
                                         this, versions_ready);
      }
      else
        deferred_execute();
      trigger_children_complete();
      trigger_children_committed();
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::deferred_execute(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(version_broadcast_collective != NULL);
#endif
      version_broadcast_collective->wait_for_states(map_applied_conditions);
      const UniqueID logical_context_uid = parent_ctx->get_context_uid();
      for (unsigned idx = 0; idx < regions.size(); idx++)
      {
        if (IS_WRITE(regions[idx]))
        {
          const VersioningSet<> &remote_advance_states = 
            version_broadcast_collective->find_advance_states(idx);
          const RegionRequirement &req = regions[idx];
          const bool parent_is_upper_bound = (req.region == req.parent);
          runtime->forest->advance_remote_versions(this, idx, req,
              parent_is_upper_bound, logical_context_uid, 
              remote_advance_states, map_applied_conditions);
        }
      }
      if (!map_applied_conditions.empty())
      {
        RtEvent map_applied = Runtime::merge_events(map_applied_conditions);
        complete_mapping(map_applied);
        // Record the map applied precondition in the versioning
        // broadcast as well so we know when it is safe to remove
        // our valid references
        version_broadcast_collective->record_precondition(map_applied);
      }
      else
        complete_mapping();
      complete_execution();
    }

    //--------------------------------------------------------------------------
    RtEvent ReplIndividualTask::perform_mapping(
                                         MustEpochOp *must_epoch_owner/*=NULL*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(sharding_function != NULL);
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // If we're part of a must epoch operation see if we're the owner
      // and if not then just do the normal broadcast receive
      if (must_epoch_owner != NULL)
      {
#ifdef DEBUG_LEGION
        ReplMustEpochOp *repl_epoch_owner = 
          dynamic_cast<ReplMustEpochOp*>(must_epoch_owner);
        assert(repl_epoch_owner != NULL);
#else
        ReplMustEpochOp *repl_epoch_owner = 
          static_cast<ReplMustEpochOp*>(must_epoch_owner);
#endif
        owner_shard = sharding_function->find_owner(index_point, 
                          repl_epoch_owner->get_index_domain());
        if (owner_shard != repl_ctx->owner_shard->shard_id)
        {
          perform_unowned_shard(repl_ctx); 
          return RtEvent::NO_RT_EVENT;
        }
      }
      // See if we need to do any versioning computations first
      RtEvent version_ready_event = perform_versioning_analysis();
      if (version_ready_event.exists() && !version_ready_event.has_triggered())
        return defer_perform_mapping(version_ready_event, must_epoch_owner); 
      // Grab the mapped event so we can know when to do the broadcast
      RtEvent map_wait = get_mapped_event();
      // Do the base call  
#ifdef DEBUG_LEGION
#ifndef NDEBUG
      RtEvent result = 
#endif
#endif
        IndividualTask::perform_mapping(must_epoch_owner);
#ifdef DEBUG_LEGION
      assert(!result.exists());
#endif
      // Then broadcast the versioning results for any region requirements
      // that are writes which are going to advance the version numbers
      // Have to new this on the heap in case we have to defer it
      VersioningInfoBroadcast *version_broadcast = new VersioningInfoBroadcast(
                               repl_ctx, versioning_collective_id, owner_shard);
#ifdef DEBUG_LEGION
      assert(regions.size() == version_infos.size());
#endif
      for (unsigned idx = 0; idx < regions.size(); idx++)
      {
        if (IS_WRITE(regions[idx]))
          version_broadcast->pack_advance_states(idx, version_infos[idx]);
      }
      // Have to wait for the mapping to be complete before sending to 
      // guarantee correctness of mapping dependences on remote nodes
      // Must epoch launches don't need to wait as their mapping dependences
      // are handled by a different mechanism
      if (must_epoch_owner != NULL)
      {
        version_broadcast->perform_collective_async();
#ifdef DEBUG_LEGION
        assert(version_broadcast_collective == NULL);
#endif
        // Copy it over so we will own and delete as necessary
        version_broadcast_collective = version_broadcast;
      }
      else // Will take ownership of deleting the collective
        version_broadcast->defer_perform_collective(this, map_wait);
      return RtEvent::NO_RT_EVENT;
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::handle_future(const void *res, 
                                           size_t res_size, bool owned)
    //--------------------------------------------------------------------------
    {
      // If we're not remote then we have to save the future locally 
      // for when we go to broadcast it
      if (!is_remote())
      {
        if (owned)
        {
          future_store = const_cast<void*>(res);
          future_size = res_size;
        }
        else
        {
          future_size = res_size;
          future_store = legion_malloc(FUTURE_RESULT_ALLOC, future_size);
          memcpy(future_store, res, future_size);
        }
      }
      IndividualTask::handle_future(future_store, future_size, false/*owned*/);
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::trigger_task_complete(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Before doing the normal thing we have to exchange broadcast/receive
      // the future result, can skip this though if we're part of a must epoch
      if (must_epoch == NULL)
      {
        if (owner_shard == repl_ctx->owner_shard->shard_id)
        {
          FutureBroadcast future_collective(repl_ctx, 
                                            future_collective_id, owner_shard);
          future_collective.broadcast_future(future_store, future_size);
        }
        else
        {
          FutureBroadcast future_collective(repl_ctx, 
                                            future_collective_id, owner_shard);
          future_collective.receive_future(result.impl);
        }
      }
      IndividualTask::trigger_task_complete();
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::unpack_remote_versions(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
      assert(version_broadcast_collective == NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Then broadcast the versioning results for any region requirements
      // that are writes which are going to advance the version numbers
      // We put this one on the heap because we don't want to end up blocking
      // the virtual channel on which the message was sent
      version_broadcast_collective = new VersioningInfoBroadcast(repl_ctx,
                                     versioning_collective_id, owner_shard);
      // Explicitly unpack into the data structure
      version_broadcast_collective->explicit_unpack(derez);
      // Now do the broadcast
      version_broadcast_collective->perform_collective_async();
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::initialize_replication(ReplicateContext *ctx)
    //--------------------------------------------------------------------------
    {
      versioning_collective_id = 
        ctx->get_next_collective_index(COLLECTIVE_LOC_0);
      future_collective_id = 
        ctx->get_next_collective_index(COLLECTIVE_LOC_1);
    }

    //--------------------------------------------------------------------------
    void ReplIndividualTask::set_sharding_function(ShardingID functor,
                                                   ShardingFunction *function)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(must_epoch != NULL);
      assert(sharding_function == NULL);
#endif
      sharding_functor = functor;
      sharding_function = function;
    }

    /////////////////////////////////////////////////////////////
    // Repl Index Task 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplIndexTask::ReplIndexTask(Runtime *rt)
      : IndexTask(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexTask::ReplIndexTask(const ReplIndexTask &rhs)
      : IndexTask(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplIndexTask::~ReplIndexTask(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexTask& ReplIndexTask::operator=(const ReplIndexTask &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_index_task();
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      reduction_collective = NULL;
      launch_space = IndexSpace::NO_SPACE;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_index_task();
      if (reduction_collective != NULL)
      {
        delete reduction_collective;
        reduction_collective = NULL;
      }
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      runtime->free_repl_index_task(this);
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // We might be able to skip this if the sharding function was already
      // picked for us which occurs when we're part of a must-epoch launch
      if (sharding_function == NULL)
      {
        // Do the mapper call to get the sharding function to use
        if (mapper == NULL)
          mapper = runtime->find_mapper(current_proc, map_id); 
        Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
        Mapper::SelectShardingFunctorOutput output;
        output.chosen_functor = UINT_MAX;
        mapper->invoke_task_select_sharding_functor(this, input, &output);
        if (output.chosen_functor == UINT_MAX)
          REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                        "Mapper %s failed to pick a valid sharding functor for "
                        "task %s (UID %lld)", mapper->get_mapper_name(),
                        get_task_name(), get_unique_id())
        this->sharding_functor = output.chosen_functor;
        sharding_function = 
          repl_ctx->shard_manager->find_sharding_function(sharding_functor);
      }
#ifdef DEBUG_LEGION
      assert(sharding_function != NULL);
      assert(sharding_collective != NULL);
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() &&
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for index task %s (UID %lld) in %s (UID %lld)", 
                      mapper->get_mapper_name(), get_task_name(), 
                      get_unique_id(), parent_ctx->get_task_name(), 
                      parent_ctx->get_unique_id())
#endif
      // If we have a future map then set the sharding function
      if (redop == 0)
      {
#ifdef DEBUG_LEGION
        assert(future_map.impl != NULL);
        ReplFutureMapImpl *impl = 
          dynamic_cast<ReplFutureMapImpl*>(future_map.impl);
        assert(impl != NULL);
#else
        ReplFutureMapImpl *impl = 
          static_cast<ReplFutureMapImpl*>(future_map.impl);
#endif
        impl->set_sharding_function(sharding_function);
      }
      // Now we can do the normal prepipeline stage
      IndexTask::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Compute the local index space of points for this shard
      internal_space =
        sharding_function->find_shard_space(repl_ctx->owner_shard->shard_id,
                                            launch_space);
      // If it's empty we're done, otherwise we go back on the queue
      if (!internal_space.exists())
      {
        // We have no local points, so we can just trigger
        complete_mapping();
        complete_execution();
        trigger_children_complete();
        trigger_children_committed();
      }
      else // We have valid points, so it goes on the ready queue
        enqueue_ready_operation();
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::trigger_dependence_analysis(void)
    //--------------------------------------------------------------------------
    {
      perform_base_dependence_analysis();
      for (unsigned idx = 0; idx < regions.size(); idx++)
      {
        projection_infos[idx] = 
         ProjectionInfo(runtime, regions[idx], launch_space, sharding_function);
        runtime->forest->perform_dependence_analysis(this, idx, regions[idx], 
                                                     restrict_infos[idx],
                                                     version_infos[idx],
                                                     projection_infos[idx],
                                                     privilege_paths[idx]);
      }
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::trigger_task_complete(void)
    //--------------------------------------------------------------------------
    {
      // If we have a reduction operator, exchange the future results
      if (redop > 0)
      {
#ifdef DEBUG_LEGION
        assert(reduction_collective != NULL);
#endif
        // Grab the reduction state buffer and then reinitialize it so
        // that all the shards can be applied to it in the same order 
        // so that we have bit equivalence across the shards
        void *shard_buffer = reduction_state;
        reduction_state = NULL;
        initialize_reduction_state();
        // The collective takes ownership of the buffer here
        reduction_collective->reduce_futures(shard_buffer, this);
      }
      // Then we do the base class thing
      IndexTask::trigger_task_complete();
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::resolve_false(bool speculated, bool launched)
    //--------------------------------------------------------------------------
    {
      // If we already launched then we can just return
      if (launched)
        return;
      // Otherwise, we need to update the internal space so we only set
      // our local points with the predicate false result
      if (redop == 0)
      {
#ifdef DEBUG_LEGION
        ReplicateContext *repl_ctx = 
          dynamic_cast<ReplicateContext*>(parent_ctx);
        assert(repl_ctx != NULL);
#else
        ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
        // Compute the local index space of points for this shard
        internal_space =
          sharding_function->find_shard_space(repl_ctx->owner_shard->shard_id,
                                              launch_space);
      }
      // Now continue through and do the base case
      IndexTask::resolve_false(speculated, launched);
    }

    //--------------------------------------------------------------------------
    void ReplIndexTask::initialize_replication(ReplicateContext *ctx,
                                               IndexSpace launch_sp)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(reduction_collective == NULL);
      // Check for any non-functional projection functions
      for (unsigned idx = 0; idx < regions.size(); idx++)
      {
        if (regions[idx].handle_type == SINGULAR)
          continue;
        ProjectionFunction *function = 
          runtime->find_projection_function(regions[idx].projection);
        if (!function->is_functional)
        {
          log_run.error("Region requirement %d of task %s (UID %lld) in "
                        "parent task %s (UID %lld) has non-functional "
                        "projection function. All projection functions "
                        "for control replication must be functional.",
                        idx, get_task_name(), get_unique_id(),
                        parent_ctx->get_task_name(), 
                        parent_ctx->get_unique_id());
          assert(false);
        }
      }
#endif
      // If we have a reduction op then we need an exchange
      if (redop > 0)
        reduction_collective = 
          new FutureExchange(ctx, reduction_state_size, COLLECTIVE_LOC_53);
      launch_space = launch_sp;
    } 

    //--------------------------------------------------------------------------
    void ReplIndexTask::set_sharding_function(ShardingID functor,
                                              ShardingFunction *function)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(must_epoch != NULL);
      assert(sharding_function == NULL);
#endif
      sharding_functor = functor;
      sharding_function = function;
    }

    //--------------------------------------------------------------------------
    FutureMapImpl* ReplIndexTask::create_future_map(TaskContext *ctx)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(ctx);
#endif
      // Make a replicate future map 
      return new ReplFutureMapImpl(repl_ctx, this, index_domain,runtime,
          runtime->get_available_distributed_id(true/*need continuation*/),
          runtime->address_space);
    }

    /////////////////////////////////////////////////////////////
    // Repl Read Close Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplReadCloseOp::ReplReadCloseOp(Runtime *rt)
      : ReadCloseOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplReadCloseOp::ReplReadCloseOp(const ReplReadCloseOp &rhs)
      : ReadCloseOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplReadCloseOp::~ReplReadCloseOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplReadCloseOp& ReplReadCloseOp::operator=(const ReplReadCloseOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplReadCloseOp::activate(void)
    //--------------------------------------------------------------------------
    {
      ReadCloseOp::activate();
      mapped_barrier = RtBarrier::NO_RT_BARRIER;
    }

    //--------------------------------------------------------------------------
    void ReplReadCloseOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_read_only();
      runtime->free_repl_read_close_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplReadCloseOp::set_mapped_barrier(RtBarrier mapped)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!mapped_barrier.exists());
#endif
      mapped_barrier = mapped;
    }

    //--------------------------------------------------------------------------
    void ReplReadCloseOp::trigger_mapping(void)
    //--------------------------------------------------------------------------
    {
      // Now trigger our phase barrier contingent on the precondition and then
      // complete the operation contingent on the phase barrier triggering
      Runtime::phase_barrier_arrive(mapped_barrier, 1/*count*/);
      complete_mapping(mapped_barrier);
      // Then we can do the normal execution
      complete_execution();
    }

    /////////////////////////////////////////////////////////////
    // Repl Inter Close Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplInterCloseOp::ReplInterCloseOp(Runtime *rt)
      : InterCloseOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplInterCloseOp::ReplInterCloseOp(const ReplInterCloseOp &rhs)
      : InterCloseOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplInterCloseOp::~ReplInterCloseOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplInterCloseOp& ReplInterCloseOp::operator=(const ReplInterCloseOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_inter_close();
      mapped_barrier = RtBarrier::NO_RT_BARRIER;
      view_barrier = RtBarrier::NO_RT_BARRIER;
      close_index = 0;
      clone_index = 0;
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_inter_close();
      runtime->free_repl_inter_close_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::set_repl_close_info(unsigned index,
                                               RtBarrier mapped, RtBarrier view)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!mapped_barrier.exists());
      assert(!view_barrier.exists());
#endif
      mapped_barrier = mapped;
      view_barrier = view;
      close_index = index;
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::trigger_dependence_analysis(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mapped_barrier.exists());
#endif
      // All we have to do is add our map precondition to the tracker
      // so we know we are mapping in order with respect to other
      // repl close operations that use the same close index
      dependence_tracker.mapping->add_mapping_dependence(
                      mapped_barrier.get_previous_phase());
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::invoke_mapper(const InstanceSet &valid_instances)
    //--------------------------------------------------------------------------
    {
      // Currently we do nothing for this as it is very difficult to shard
      // a close operation and distributed any instances we want to update
    }

    //--------------------------------------------------------------------------
    void ReplInterCloseOp::complete_close_mapping(CompositeView *view,
                                                  RtEvent precondition)
    //--------------------------------------------------------------------------
    {
      // Arrive on our barrier with the precondition
      Runtime::phase_barrier_arrive(mapped_barrier, 1/*count*/, precondition);
      // Then complete the mapping once the barrier has triggered
      complete_mapping(mapped_barrier);
    }

    /////////////////////////////////////////////////////////////
    // Repl Index Fill Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplIndexFillOp::ReplIndexFillOp(Runtime *rt)
      : IndexFillOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexFillOp::ReplIndexFillOp(const ReplIndexFillOp &rhs)
      : IndexFillOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplIndexFillOp::~ReplIndexFillOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexFillOp& ReplIndexFillOp::operator=(const ReplIndexFillOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplIndexFillOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_index_fill();
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      launch_space = IndexSpace::NO_SPACE;
      mapper = NULL;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
    }

    //--------------------------------------------------------------------------
    void ReplIndexFillOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      deactivate_index_fill();
      runtime->free_repl_index_fill_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplIndexFillOp::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Do the mapper call to get the sharding function to use
      if (mapper == NULL)
        mapper = runtime->find_mapper(
            parent_ctx->get_executing_processor(), map_id);
      Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
      Mapper::SelectShardingFunctorOutput output;
      output.chosen_functor = UINT_MAX;
      mapper->invoke_fill_select_sharding_functor(this, input, &output);
      if (output.chosen_functor == UINT_MAX)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s failed to pick a valid sharding functor for "
                      "index fill in task %s (UID %lld)", 
                      mapper->get_mapper_name(),
                      parent_ctx->get_task_name(), parent_ctx->get_unique_id())
      this->sharding_functor = output.chosen_functor;
      sharding_function = 
        repl_ctx->shard_manager->find_sharding_function(sharding_functor);
#ifdef DEBUG_LEGION
      assert(sharding_collective != NULL);
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() &&
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for index fill in task %s (UID %lld)", 
                      mapper->get_mapper_name(), parent_ctx->get_task_name(),
                      parent_ctx->get_unique_id())
#endif
      // Now we can do the normal prepipeline stage
      IndexFillOp::trigger_prepipeline_stage();
    }
    
    //--------------------------------------------------------------------------
    void ReplIndexFillOp::trigger_dependence_analysis(void)
    //--------------------------------------------------------------------------
    {
      perform_base_dependence_analysis();
      projection_info = ProjectionInfo(runtime, requirement, 
                                       launch_space, sharding_function);
      runtime->forest->perform_dependence_analysis(this, 0/*idx*/,
                                                   requirement,
                                                   restrict_info,
                                                   version_info,
                                                   projection_info,
                                                   privilege_path);
    }

    //--------------------------------------------------------------------------
    void ReplIndexFillOp::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Compute the local index space of points for this shard
      launch_space =
        sharding_function->find_shard_space(repl_ctx->owner_shard->shard_id, 
                                            launch_space);
      // If it's empty we're done, otherwise we go back on the queue
      if (!launch_space.exists())
      {
        // We have no local points, so we can just trigger
        complete_mapping();
        complete_execution();
      }
      else // We have valid points, so it goes on the ready queue
        IndexFillOp::trigger_ready();
    }

    //--------------------------------------------------------------------------
    void ReplIndexFillOp::initialize_replication(ReplicateContext *ctx,
                                                 IndexSpace launch_sp)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // Check for any non-functional projection functions
      if (requirement.handle_type != SINGULAR)
      {
        ProjectionFunction *function = 
          runtime->find_projection_function(requirement.projection);
        if (!function->is_functional)
        {
          log_run.error("Region requirement of index fill op (UID %lld) in "
                        "parent task %s (UID %lld) has non-functional "
                        "projection function. All projection functions "
                        "for control replication must be functional.",
                        get_unique_id(), parent_ctx->get_task_name(), 
                        parent_ctx->get_unique_id());
          assert(false);
        }
      }
#endif
      launch_space = launch_sp;
    }

    /////////////////////////////////////////////////////////////
    // Repl Copy Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplCopyOp::ReplCopyOp(Runtime *rt)
      : CopyOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplCopyOp::ReplCopyOp(const ReplCopyOp &rhs)
      : CopyOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplCopyOp::~ReplCopyOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplCopyOp& ReplCopyOp::operator=(const ReplCopyOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::initialize_replication(ReplicateContext *ctx)
    //--------------------------------------------------------------------------
    {
      versioning_collective_id = 
        ctx->get_next_collective_index(COLLECTIVE_LOC_2);
      // Initialize our index domain of a single point
      index_domain = Domain(index_point, index_point);
      launch_space = ctx->find_index_launch_space(index_domain);
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_copy();
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      launch_space = IndexSpace::NO_SPACE;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
      versioning_collective_id = UINT_MAX;
      version_broadcast_collective = NULL;
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      if (version_broadcast_collective != NULL)
        delete version_broadcast_collective;
      deactivate_copy();
      src_projection_infos.clear();
      dst_projection_infos.clear();
      runtime->free_repl_copy_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Do the mapper call to get the sharding function to use
      if (mapper == NULL)
        mapper = runtime->find_mapper(
            parent_ctx->get_executing_processor(), map_id); 
      Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
      Mapper::SelectShardingFunctorOutput output;
      output.chosen_functor = UINT_MAX; 
      mapper->invoke_copy_select_sharding_functor(this, input, &output);
      if (output.chosen_functor == UINT_MAX)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s failed to pick a valid sharding functor for "
                      "copy in task %s (UID %lld)", mapper->get_mapper_name(),
                      parent_ctx->get_task_name(), parent_ctx->get_unique_id())
      this->sharding_functor = output.chosen_functor;
      sharding_function = 
        repl_ctx->shard_manager->find_sharding_function(sharding_functor);
#ifdef DEBUG_LEGION
      assert(sharding_collective != NULL);
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() &&
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for copy in task %s (UID %lld)", 
                      mapper->get_mapper_name(), parent_ctx->get_task_name(), 
                      parent_ctx->get_unique_id())
#endif
      // Now we can do the normal prepipeline stage
      CopyOp::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Figure out whether this shard owns this point
      const ShardID owner_shard = 
        sharding_function->find_owner(index_point, index_domain); 
      if (Runtime::legion_spy_enabled)
        LegionSpy::log_owner_shard(get_unique_id(), owner_shard);
      // If we own it we go on the queue, otherwise we complete early
      if (owner_shard != repl_ctx->owner_shard->shard_id)
      {
        // We don't own it, so we can pretend like we
        // mapped and executed this copy already
        // Before we do this though we have to get the version state
        // names for any writes so we can update our local state
#ifdef DEBUG_LEGION
        assert(version_broadcast_collective == NULL);
#endif
        version_broadcast_collective = new VersioningInfoBroadcast(repl_ctx, 
                                        versioning_collective_id, owner_shard);
        RtEvent versions_ready = 
          version_broadcast_collective->perform_collective_wait(false/*block*/);
        if (versions_ready.exists() && !versions_ready.has_triggered())
        {
          // Defer completion until the versions are ready
          DeferredExecuteArgs deferred_execute_args;
          deferred_execute_args.proxy_this = this;
          runtime->issue_runtime_meta_task(deferred_execute_args,
                                           LG_THROUGHPUT_DEFERRED_PRIORITY,
                                           this, versions_ready);
        }
        else
          deferred_execute();
      }
      else // We own it, so do the base call
      {
        // Do the versioning analysis
        RtEvent ready = perform_local_versioning_analysis();
        // Then we can do the enqueue
        enqueue_ready_operation(ready);
      }
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::deferred_execute(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(version_broadcast_collective != NULL);
#endif
      version_broadcast_collective->wait_for_states(map_applied_conditions);
      const UniqueID logical_context_uid = parent_ctx->get_context_uid();
      for (unsigned idx = 0; idx < dst_requirements.size(); idx++)
      {
        const VersioningSet<> &remote_advance_states = 
          version_broadcast_collective->find_advance_states(idx);
        RegionRequirement &req = dst_requirements[idx];
        // Switch the privileges to read-write if necessary
        const bool is_reduce_req = IS_REDUCE(dst_requirements[idx]);
        if (is_reduce_req)
          req.privilege = READ_WRITE;
        const bool parent_is_upper_bound = (req.region == req.parent);
        runtime->forest->advance_remote_versions(this, 
            src_requirements.size() + idx, req,
            parent_is_upper_bound, logical_context_uid, 
            remote_advance_states, map_applied_conditions);
        // Switch the privileges back when we are done
        if (is_reduce_req)
          req.privilege = REDUCE;
      }
      if (!map_applied_conditions.empty())
      {
        RtEvent map_applied = Runtime::merge_events(map_applied_conditions);
        complete_mapping(map_applied);
        // Also record a precondition for our versioning info being done
        version_broadcast_collective->record_precondition(map_applied);
      }
      else
        complete_mapping();
      complete_execution();
    }

    //--------------------------------------------------------------------------
    void ReplCopyOp::trigger_mapping(void)
    //--------------------------------------------------------------------------
    {
      // A little trick to avoid the completion race for once we call
      // the base CopyOp trigger_mapping call: we add a user event to the
      // map applied events which means the operation isn't mapped until 
      // we actually trigger this event, which we can do after we gather
      // the data that we need
      RtUserEvent prevent_completion_race = Runtime::create_rt_user_event();
      map_applied_conditions.insert(prevent_completion_race);
      // Do the base trigger mapping
      CopyOp::trigger_mapping();
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      const ShardID owner_shard = 
        sharding_function->find_owner(index_point, index_domain);
      // Have to new this on the heap in case we end up needing to defer it
      VersioningInfoBroadcast *version_broadcast = new VersioningInfoBroadcast(
                               repl_ctx, versioning_collective_id, owner_shard);
#ifdef DEBUG_LEGION
      assert(dst_requirements.size() == dst_versions.size());
#endif
      for (unsigned idx = 0; idx < dst_versions.size(); idx++)
        version_broadcast->pack_advance_states(idx, dst_versions[idx]);
      // Have to make a copy to avoid completion race
      RtEvent map_wait = get_mapped_event();
      // Now that we have all our information, we can trigger our map user event
      Runtime::trigger_event(prevent_completion_race);
      // See if we can send the result now or need to defer it
      if (map_wait.has_triggered())
      {
        // We can do it now
        version_broadcast->perform_collective_async();
#ifdef DEBUG_LEGION
        assert(version_broadcast_collective == NULL);
#endif
        // By copying this here we take ownership of it and will clean it up
        version_broadcast_collective = version_broadcast;
      }
      else // Will take ownership of deleting the collective
        version_broadcast->defer_perform_collective(this, map_wait);
    }

    /////////////////////////////////////////////////////////////
    // Repl Index Copy Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplIndexCopyOp::ReplIndexCopyOp(Runtime *rt)
      : IndexCopyOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexCopyOp::ReplIndexCopyOp(const ReplIndexCopyOp &rhs)
      : IndexCopyOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplIndexCopyOp::~ReplIndexCopyOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplIndexCopyOp& ReplIndexCopyOp::operator=(const ReplIndexCopyOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_index_copy();
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      launch_space = IndexSpace::NO_SPACE;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      deactivate_index_copy();
      runtime->free_repl_index_copy_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Do the mapper call to get the sharding function to use
      if (mapper == NULL)
        mapper = runtime->find_mapper(
            parent_ctx->get_executing_processor(), map_id); 
      Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
      Mapper::SelectShardingFunctorOutput output;
      output.chosen_functor = UINT_MAX;
      mapper->invoke_copy_select_sharding_functor(this, input, &output);
      if (output.chosen_functor == UINT_MAX)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s failed to pick a valid sharding functor for "
                      "index copy in task %s (UID %lld)", 
                      mapper->get_mapper_name(),
                      parent_ctx->get_task_name(), parent_ctx->get_unique_id())
      this->sharding_functor = output.chosen_functor;
      sharding_function = 
        repl_ctx->shard_manager->find_sharding_function(sharding_functor); 
#ifdef DEBUG_LEGION
      assert(sharding_collective != NULL);
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() &&
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for index copy in task %s (UID %lld)", 
                      mapper->get_mapper_name(), parent_ctx->get_task_name(),
                      parent_ctx->get_unique_id())
#endif
      // Now we can do the normal prepipeline stage
      IndexCopyOp::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::trigger_dependence_analysis(void)
    //--------------------------------------------------------------------------
    {
      perform_base_dependence_analysis();
      for (unsigned idx = 0; idx < src_requirements.size(); idx++)
      {
        src_projection_infos[idx] = 
          ProjectionInfo(runtime, src_requirements[idx], 
                         launch_space, sharding_function);
        runtime->forest->perform_dependence_analysis(this, idx, 
                                                     src_requirements[idx],
                                                     src_restrict_infos[idx],
                                                     src_versions[idx],
                                                     src_projection_infos[idx],
                                                     src_privilege_paths[idx]);
      }
      for (unsigned idx = 0; idx < dst_requirements.size(); idx++)
      {
        dst_projection_infos[idx] = 
          ProjectionInfo(runtime, dst_requirements[idx], 
                         launch_space, sharding_function);
        unsigned index = src_requirements.size()+idx;
        // Perform this dependence analysis as if it was READ_WRITE
        // so that we can get the version numbers correct
        const bool is_reduce_req = IS_REDUCE(dst_requirements[idx]);
        if (is_reduce_req)
          dst_requirements[idx].privilege = READ_WRITE;
        runtime->forest->perform_dependence_analysis(this, index, 
                                                     dst_requirements[idx],
                                                     dst_restrict_infos[idx],
                                                     dst_versions[idx],
                                                     dst_projection_infos[idx],
                                                     dst_privilege_paths[idx]);
        // Switch the privileges back when we are done
        if (is_reduce_req)
          dst_requirements[idx].privilege = REDUCE;
      }
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Compute the local index space of points for this shard
      launch_space =
        sharding_function->find_shard_space(repl_ctx->owner_shard->shard_id,
                                            launch_space);
      // If it's empty we're done, otherwise we go back on the queue
      if (!launch_space.exists())
      {
        // We have no local points, so we can just trigger
        complete_mapping();
        complete_execution();
      }
      else // If we have any valid points do the base call
        IndexCopyOp::trigger_ready();
    }

    //--------------------------------------------------------------------------
    void ReplIndexCopyOp::initialize_replication(ReplicateContext *ctx,
                                                 IndexSpace launch_sp)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      for (unsigned idx = 0; idx < dst_requirements.size(); idx++)
      {
        if (dst_requirements[idx].handle_type == SINGULAR)
          continue;
        ProjectionFunction *function = 
          runtime->find_projection_function(dst_requirements[idx].projection);
        if (!function->is_functional)
        {
          log_run.error("Destination region requirement %d of index copy "
                        "(UID %lld) in parent task %s (UID %lld) has "
                        "non-functional projection function. All projection "
                        "functions for control replication must be functional.",
                        idx, get_unique_id(), parent_ctx->get_task_name(), 
                        parent_ctx->get_unique_id());
          assert(false);
        }
      }
#endif
      launch_space = launch_sp;
    }

    /////////////////////////////////////////////////////////////
    // Repl Deletion Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplDeletionOp::ReplDeletionOp(Runtime *rt)
      : DeletionOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplDeletionOp::ReplDeletionOp(const ReplDeletionOp &rhs)
      : DeletionOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplDeletionOp::~ReplDeletionOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplDeletionOp& ReplDeletionOp::operator=(const ReplDeletionOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplDeletionOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_deletion();
      mapped_barrier = RtBarrier::NO_RT_BARRIER;
    }

    //--------------------------------------------------------------------------
    void ReplDeletionOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_deletion();
      runtime->free_repl_deletion_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplDeletionOp::trigger_mapping(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mapped_barrier.exists());
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Shard 0 will handle all the deletions
      if (repl_ctx->owner_shard->shard_id != 0)
      {
        // Everyone else can arrive on the barrier and map like normal
        // since they are not going to do anything
        Runtime::phase_barrier_arrive(mapped_barrier, 1/*count*/);
        complete_mapping();
      }
      else // shard 0 maps when everyone has mapped
        complete_mapping(mapped_barrier);
      // We don't do anything for execution so we are executed
      complete_execution();
    }

    //--------------------------------------------------------------------------
    void ReplDeletionOp::trigger_complete(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Shard 0 will handle all the deletions
      if (repl_ctx->owner_shard->shard_id > 0)
      {
        // The other shards still have to tell the parent context that it
        // has actually been deleted
        switch (kind)
        {
          case INDEX_SPACE_DELETION:
            {
              // Only need to tell our parent if it is a top-level index space
              if (runtime->forest->is_top_level_index_space(index_space))
                parent_ctx->register_index_space_deletion(index_space,
                                                          false/*finalize*/);
              break;
            }
          case INDEX_PARTITION_DELETION:
            {
              parent_ctx->register_index_partition_deletion(index_part,
                                                            false/*finalize*/);
              break;
            }
          case FIELD_SPACE_DELETION:
            {
              parent_ctx->register_field_space_deletion(field_space,
                                                        false/*finalize*/);
              break;
            }
          case FIELD_DELETION:
            {
              parent_ctx->register_field_deletions(field_space, free_fields,
                                                   false/*finalize*/);
              break;
            }
          case LOGICAL_REGION_DELETION:
            {
              // Only need to tell our parent if it is a top-level region
              if (runtime->forest->is_top_level_region(logical_region))
                parent_ctx->register_region_deletion(logical_region,
                                                    false/*finalize*/);
              break;
            }
          case LOGICAL_PARTITION_DELETION:
            {
              // We don't need to register partition deletions explicitly
              break;
            }
          default:
            assert(false); // should never get here
        }
        // We still need to 
        complete_operation();
      }
      else // Shard 0 does the actual deletion
        DeletionOp::trigger_complete();
    }

    //--------------------------------------------------------------------------
    void ReplDeletionOp::set_mapped_barrier(RtBarrier mapped)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!mapped_barrier.exists());
#endif
      mapped_barrier = mapped;
    }

    /////////////////////////////////////////////////////////////
    // Repl Pending Partition Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplPendingPartitionOp::ReplPendingPartitionOp(Runtime *rt)
      : PendingPartitionOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplPendingPartitionOp::ReplPendingPartitionOp(
                                              const ReplPendingPartitionOp &rhs)
      : PendingPartitionOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplPendingPartitionOp::~ReplPendingPartitionOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplPendingPartitionOp& ReplPendingPartitionOp::operator=(
                                              const ReplPendingPartitionOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplPendingPartitionOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_pending();
    }

    //--------------------------------------------------------------------------
    void ReplPendingPartitionOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_pending();
      runtime->free_repl_pending_partition_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplPendingPartitionOp::trigger_mapping(void)
    //--------------------------------------------------------------------------
    {
      // We know we are in a replicate context
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Perform the partitioning operation
      ApEvent ready_event = thunk->perform_shard(this, runtime->forest,
        repl_ctx->owner_shard->shard_id, repl_ctx->shard_manager->total_shards);
      complete_mapping();
      Runtime::trigger_event(completion_event, ready_event);
      need_completion_trigger = false;
      complete_execution(Runtime::protect_event(ready_event));
    }

    /////////////////////////////////////////////////////////////
    // Repl Dependent Partition Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplDependentPartitionOp(Runtime *rt)
      : DependentPartitionOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplDependentPartitionOp(
                                            const ReplDependentPartitionOp &rhs)
      : DependentPartitionOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::~ReplDependentPartitionOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp& ReplDependentPartitionOp::operator=(
                                            const ReplDependentPartitionOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::initialize_by_field(ReplicateContext *ctx, 
                                                       ApEvent ready_event,
                                                       IndexPartition pid,
                                                       LogicalRegion handle, 
                                                       LogicalRegion parent,
                                                       FieldID fid,
                                                       MapperID id, 
                                                       MappingTagID t,
                                                       ShardID shard,
                                                       size_t total_shards)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (!runtime->forest->check_partition_by_field_size(pid, 
            handle.get_field_space(), fid, false/*range*/, 
            true/*use color space*/))
      {
        log_run.error("ERROR: Field size of field %d does not match the size "
                      "of the color space elements for 'partition_by_field' "
                      "call in task %s (UID %lld)", fid, ctx->get_task_name(),
                      ctx->get_unique_id());
        assert(false);
      }
#endif
      parent_task = ctx->get_task();
      initialize_operation(ctx, true/*track*/); 
      // Start without the projection requirement, we'll ask
      // the mapper later if it wants to turn this into an index launch
      requirement = RegionRequirement(handle, READ_ONLY, EXCLUSIVE, parent);
      requirement.add_field(fid);
      map_id = id;
      tag = t;
#ifdef DEBUG_LEGION
      assert(thunk == NULL);
#endif
      thunk = new ReplByFieldThunk(ctx, pid, shard, total_shards);
      partition_ready = ready_event;
      if (Runtime::legion_spy_enabled)
        perform_logging();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::initialize_by_image(ReplicateContext *ctx, 
                                                       ShardID target_shard,
                                                       ApEvent ready_event,
                                                       IndexPartition pid,
                                                   LogicalPartition projection,
                                             LogicalRegion parent, FieldID fid,
                                                   MapperID id, MappingTagID t) 
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (!runtime->forest->check_partition_by_field_size(pid, 
            projection.get_field_space(), fid, false/*range*/))
      {
        log_run.error("ERROR: Field size of field %d does not match the size "
                      "of the destination index space elements for "
                      "'partition_by_image' call in task %s (UID %lld)",
                      fid, ctx->get_task_name(), ctx->get_unique_id());
        assert(false);
      }
#endif
      parent_task = ctx->get_task();
      initialize_operation(ctx, true/*track*/);
      // Start without the projection requirement, we'll ask
      // the mapper later if it wants to turn this into an index launch
      LogicalRegion proj_parent = 
        runtime->forest->get_parent_logical_region(projection);
      requirement = RegionRequirement(proj_parent, READ_ONLY, EXCLUSIVE,parent);
      requirement.add_field(fid);
      map_id = id;
      tag = t;
#ifdef DEBUG_LEGION
      assert(thunk == NULL);
#endif
      thunk = new ReplByImageThunk(ctx, target_shard, 
                                   pid, projection.get_index_partition());
      partition_ready = ready_event;
      if (Runtime::legion_spy_enabled)
        perform_logging();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::initialize_by_image_range(
                                                         ReplicateContext *ctx, 
                                                         ShardID target_shard,
                                                         ApEvent ready_event,
                                                         IndexPartition pid,
                                                LogicalPartition projection,
                                                LogicalRegion parent,
                                                FieldID fid, MapperID id,
                                                MappingTagID t) 
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (!runtime->forest->check_partition_by_field_size(pid, 
            projection.get_field_space(), fid, true/*range*/))
      {
        log_run.error("ERROR: Field size of field %d does not match the size "
                      "of the destination index space elements for "
                      "'partition_by_image_range' call in task %s (UID %lld)",
                      fid, ctx->get_task_name(), ctx->get_unique_id());
        assert(false);
      }
#endif
      parent_task = ctx->get_task();
      initialize_operation(ctx, true/*track*/);
      // Start without the projection requirement, we'll ask
      // the mapper later if it wants to turn this into an index launch
      LogicalRegion proj_parent = 
        runtime->forest->get_parent_logical_region(projection);
      requirement = RegionRequirement(proj_parent, READ_ONLY, EXCLUSIVE,parent);
      requirement.add_field(fid);
      map_id = id;
      tag = t;
#ifdef DEBUG_LEGION
      assert(thunk == NULL);
#endif
      thunk = new ReplByImageRangeThunk(ctx, target_shard, 
                                        pid, projection.get_index_partition());
      partition_ready = ready_event;
      if (Runtime::legion_spy_enabled)
        perform_logging();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::initialize_by_preimage(ReplicateContext *ctx,
                                    ShardID target_shard, ApEvent ready_event,
                                    IndexPartition pid, IndexPartition proj,
                                    LogicalRegion handle, LogicalRegion parent,
                                    FieldID fid, MapperID id, MappingTagID t)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (!runtime->forest->check_partition_by_field_size(pid,
            handle.get_field_space(), fid, false/*range*/))
      {
        log_run.error("ERROR: Field size of field %d does not match the size "
                      "of the range index space elements for "
                      "'partition_by_preimage' call in task %s (UID %lld)",
                      fid, ctx->get_task_name(), ctx->get_unique_id());
        assert(false);
      }
#endif
      parent_task = ctx->get_task();
      initialize_operation(ctx, true/*track*/);
      // Start without the projection requirement, we'll ask
      // the mapper later if it wants to turn this into an index launch
      requirement = RegionRequirement(handle, READ_ONLY, EXCLUSIVE, parent);
      requirement.add_field(fid);
      map_id = id;
      tag = t;
#ifdef DEBUG_LEGION
      assert(thunk == NULL);
#endif
      thunk = new ReplByPreimageThunk(ctx, target_shard, pid, proj);
      partition_ready = ready_event;
      if (Runtime::legion_spy_enabled)
        perform_logging();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::initialize_by_preimage_range(
                                    ReplicateContext *ctx, ShardID target_shard,
                                    ApEvent ready_event,
                                    IndexPartition pid, IndexPartition proj,
                                    LogicalRegion handle, LogicalRegion parent,
                                    FieldID fid, MapperID id, MappingTagID t)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (!runtime->forest->check_partition_by_field_size(pid,
            handle.get_field_space(), fid, true/*range*/))
      {
        log_run.error("ERROR: Field size of field %d does not match the size "
                     "of the range index space elements for "
                     "'partition_by_preimage_range' call in task %s (UID %lld)",
                     fid, ctx->get_task_name(), ctx->get_unique_id());
        assert(false);
      }
#endif
      parent_task = ctx->get_task();
      initialize_operation(ctx, true/*track*/);
      // Start without the projection requirement, we'll ask
      // the mapper later if it wants to turn this into an index launch
      requirement = RegionRequirement(handle, READ_ONLY, EXCLUSIVE, parent);
      requirement.add_field(fid);
      map_id = id;
      tag = t;
#ifdef DEBUG_LEGION
      assert(thunk == NULL);
#endif
      thunk = new ReplByPreimageRangeThunk(ctx, target_shard, pid, proj);
      partition_ready = ready_event;
      if (Runtime::legion_spy_enabled)
        perform_logging();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_dependent_op();
      sharding_functor = UINT_MAX;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_dependent_op();
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      runtime->free_repl_dependent_partition_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Do the mapper call to get the sharding function to use
      if (mapper == NULL)
        mapper = runtime->find_mapper(
            parent_ctx->get_executing_processor(), map_id);
      Mapper::SelectShardingFunctorInput* input = repl_ctx->shard_manager;
      Mapper::SelectShardingFunctorOutput output;
      output.chosen_functor = UINT_MAX;
      mapper->invoke_partition_select_sharding_functor(this, input, &output);
      if (output.chosen_functor == UINT_MAX)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s failed to pick a valid sharding functor for "
                      "dependent partition in task %s (UID %lld)", 
                      mapper->get_mapper_name(),
                      parent_ctx->get_task_name(), parent_ctx->get_unique_id())
      this->sharding_functor = output.chosen_functor;
#ifdef DEBUG_LEGION
      assert(sharding_collective != NULL);
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() &&
          !sharding_collective->validate(this->sharding_functor))
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Mapper %s chose different sharding functions "
                      "for dependent partition op in task %s (UID %lld)", 
                      mapper->get_mapper_name(), parent_ctx->get_task_name(),
                      parent_ctx->get_unique_id())
#endif
      // Now we can do the normal prepipeline stage
      DependentPartitionOp::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplDependentPartitionOp::trigger_ready(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Get the sharding function implementation to use from our context
      ShardingFunction *function = 
        repl_ctx->shard_manager->find_sharding_function(sharding_functor);
      // Do different things if this is an index space point or a single point
      if (is_index_space)
      {
        // Compute the local index space of points for this shard
        launch_space =
          function->find_shard_space(repl_ctx->owner_shard->shard_id,
                                     launch_space);
        // If it's empty we're done, otherwise we go back on the queue
        if (!launch_space.exists())
        {
          // We aren't participating directly, but we still have to 
          // participate in the collective operations
          thunk->perform(this, runtime->forest, ApEvent::NO_AP_EVENT,instances);
          // We have no local points, so we can just trigger
          complete_mapping();
          complete_execution();
        }
        else // If we have valid points then we do the base call
        {
          // Update the index domain to match the launch space
          runtime->forest->find_launch_space_domain(launch_space, index_domain);
          DependentPartitionOp::trigger_ready();
        }
      }
      else
      {
        // Shard 0 always owns dependent partition operations
        // If we own it we go on the queue, otherwise we complete early
        if (repl_ctx->owner_shard->shard_id != 0)
        {
          // We don't own it, so we can pretend like we
          // mapped and executed this task already
          complete_mapping();
          complete_execution();
        }
        else // If we're the shard then we do the base call
          DependentPartitionOp::trigger_ready();
      }
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplByFieldThunk::ReplByFieldThunk(
        ReplicateContext *ctx, IndexPartition p, ShardID s, size_t t)
      : ByFieldThunk(p), 
        collective(FieldDescriptorExchange(ctx, COLLECTIVE_LOC_54)),
        shard_id(s), total_shards(t)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ApEvent ReplDependentPartitionOp::ReplByFieldThunk::perform(
                              DependentPartitionOp *op,
                              RegionTreeForest *forest, ApEvent instances_ready,
                              const std::vector<FieldDataDescriptor> &instances)
    //--------------------------------------------------------------------------
    {
      if (op->is_index_space)
      {
        // Do the all-to-all gather of the field data descriptors
        ApEvent all_ready = collective.exchange_descriptors(instances_ready,
                                                            instances);
        return forest->create_partition_by_field(op, pid, 
                collective.descriptors, all_ready, shard_id, total_shards);
      }
      else // singular so just do the normal thing
        return forest->create_partition_by_field(op, pid, 
                        instances, instances_ready, shard_id, total_shards);
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplByImageThunk::ReplByImageThunk(
                                          ReplicateContext *ctx, ShardID target,
                                          IndexPartition p, IndexPartition proj)
      : ByImageThunk(p, proj), 
        gather_collective(FieldDescriptorGather(ctx, target, COLLECTIVE_LOC_55))
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ApEvent ReplDependentPartitionOp::ReplByImageThunk::perform(
                              DependentPartitionOp *op,
                              RegionTreeForest *forest, ApEvent instances_ready,
                              const std::vector<FieldDataDescriptor> &instances)
    //--------------------------------------------------------------------------
    {
      if (op->is_index_space)
      {
        gather_collective.contribute(instances_ready, instances);
        if (gather_collective.is_target())
        {
          ApEvent all_ready;
          const std::vector<FieldDataDescriptor> &full_descriptors =
            gather_collective.get_full_descriptors(all_ready);
          // Perform the operation
          return forest->create_partition_by_image(op, pid, projection, 
                                          full_descriptors, all_ready);
        }
        else // nothing else for us to do
          return ApEvent::NO_AP_EVENT;
      }
      else // singular so just do the normal thing
        return forest->create_partition_by_image(op, pid, projection, 
                                                 instances, instances_ready);
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplByImageRangeThunk::ReplByImageRangeThunk(
                                          ReplicateContext *ctx, ShardID target,
                                          IndexPartition p, IndexPartition proj)
      : ByImageRangeThunk(p, proj), 
        gather_collective(FieldDescriptorGather(ctx, target, COLLECTIVE_LOC_60))
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ApEvent ReplDependentPartitionOp::ReplByImageRangeThunk::perform(
                              DependentPartitionOp *op,
                              RegionTreeForest *forest, ApEvent instances_ready,
                              const std::vector<FieldDataDescriptor> &instances)
    //--------------------------------------------------------------------------
    {
      if (op->is_index_space)
      {
        gather_collective.contribute(instances_ready, instances);
        if (gather_collective.is_target())
        {
          ApEvent all_ready;
          const std::vector<FieldDataDescriptor> &full_descriptors =
            gather_collective.get_full_descriptors(all_ready);
          // Perform the operation
          return forest->create_partition_by_image_range(op, pid, projection,
                                                full_descriptors, all_ready);
        }
        else // nothing else for us to do
          return ApEvent::NO_AP_EVENT;
      }
      else // singular so just do the normal thing
        return forest->create_partition_by_image_range(op, pid, projection, 
                                                 instances, instances_ready);
    }

    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplByPreimageThunk::ReplByPreimageThunk(
                                          ReplicateContext *ctx, ShardID target,
                                          IndexPartition p, IndexPartition proj)
      : ByPreimageThunk(p, proj), 
        gather_collective(FieldDescriptorGather(ctx, target, COLLECTIVE_LOC_56))
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ApEvent ReplDependentPartitionOp::ReplByPreimageThunk::perform(
                              DependentPartitionOp *op,
                              RegionTreeForest *forest, ApEvent instances_ready,
                              const std::vector<FieldDataDescriptor> &instances)
    //--------------------------------------------------------------------------
    {
      if (op->is_index_space)
      {
        gather_collective.contribute(instances_ready, instances);
        if (gather_collective.is_target())
        {
          ApEvent all_ready;
          const std::vector<FieldDataDescriptor> &full_descriptors =
            gather_collective.get_full_descriptors(all_ready);
          // Perform the operation
          return forest->create_partition_by_preimage(op, pid, projection,
                                              full_descriptors, all_ready);
        }
        else // nothing else for us to do
          return ApEvent::NO_AP_EVENT;
      }
      else // singular so just do the normal thing
        return forest->create_partition_by_preimage(op, pid, projection, 
                                                 instances, instances_ready);
    }
    
    //--------------------------------------------------------------------------
    ReplDependentPartitionOp::ReplByPreimageRangeThunk::
                 ReplByPreimageRangeThunk(ReplicateContext *ctx, ShardID target,
                                          IndexPartition p, IndexPartition proj)
      : ByPreimageRangeThunk(p, proj), 
        gather_collective(FieldDescriptorGather(ctx, target, COLLECTIVE_LOC_57))
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ApEvent ReplDependentPartitionOp::ReplByPreimageRangeThunk::perform(
                              DependentPartitionOp *op,
                              RegionTreeForest *forest, ApEvent instances_ready,
                              const std::vector<FieldDataDescriptor> &instances)
    //--------------------------------------------------------------------------
    {
      if (op->is_index_space)
      {
        gather_collective.contribute(instances_ready, instances);
        if (gather_collective.is_target())
        {
          ApEvent all_ready;
          const std::vector<FieldDataDescriptor> &full_descriptors =
            gather_collective.get_full_descriptors(all_ready);
          // Perform the operation
          return forest->create_partition_by_preimage_range(op, pid, projection,
                                                   full_descriptors, all_ready);
        }
        else // nothing else for us to do
          return ApEvent::NO_AP_EVENT;
      }
      else // singular so just do the normal thing
        return forest->create_partition_by_preimage_range(op, pid, projection, 
                                                 instances, instances_ready);
    }

    /////////////////////////////////////////////////////////////
    // Repl Must Epoch Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplMustEpochOp::ReplMustEpochOp(Runtime *rt)
      : MustEpochOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplMustEpochOp::ReplMustEpochOp(const ReplMustEpochOp &rhs)
      : MustEpochOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplMustEpochOp::~ReplMustEpochOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplMustEpochOp& ReplMustEpochOp::operator=(const ReplMustEpochOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_must_epoch_op();
      sharding_functor = UINT_MAX;
      sharding_function = NULL;
      index_domain = Domain::NO_DOMAIN;
      mapping_collective_id = 0;
      collective_map_must_epoch_call = false;
      mapping_broadcast = NULL;
      mapping_exchange = NULL;
      dependence_exchange = NULL;
      completion_exchange = NULL;
#ifdef DEBUG_LEGION
      sharding_collective = NULL;
#endif
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      deactivate_must_epoch_op(); 
      shard_single_tasks.clear();
      runtime->free_repl_epoch_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::instantiate_tasks(TaskContext *ctx, 
                       bool check_privileges, const MustEpochLauncher &launcher)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(ctx);
#endif
      // Initialize operations for everything in the launcher
      // Note that we do not track these operations as we want them all to
      // appear as a single operation to the parent context in order to
      // avoid deadlock with the maximum window size.
      indiv_tasks.resize(launcher.single_tasks.size());
      for (unsigned idx = 0; idx < launcher.single_tasks.size(); idx++)
      {
        ReplIndividualTask *task = 
          runtime->get_available_repl_individual_task(true);
        task->initialize_task(ctx, launcher.single_tasks[idx],
                              check_privileges, false/*track*/);
        task->set_must_epoch(this, idx, true/*register*/);
        // If we have a trace, set it for this operation as well
        if (trace != NULL)
          task->set_trace(trace, !trace->is_fixed(), NULL);
        task->must_epoch_task = true;
        task->initialize_replication(repl_ctx);
#ifdef DEBUG_LEGION
        task->set_sharding_collective(new ShardingGatherCollective(repl_ctx,
                                      0/*owner shard*/, COLLECTIVE_LOC_59));
#endif
        indiv_tasks[idx] = task;
      }
      indiv_triggered.resize(indiv_tasks.size(), false);
      index_tasks.resize(launcher.index_tasks.size());
      for (unsigned idx = 0; idx < launcher.index_tasks.size(); idx++)
      {
        IndexSpace launch_space = launcher.index_tasks[idx].launch_space;
        if (!launch_space.exists())
          launch_space = runtime->find_or_create_index_launch_space(
                      launcher.index_tasks[idx].launch_domain);
        ReplIndexTask *task = runtime->get_available_repl_index_task(true);
        task->initialize_task(ctx, launcher.index_tasks[idx],
                          launch_space, check_privileges, false/*track*/);
        task->set_must_epoch(this, indiv_tasks.size()+idx, 
                                         true/*register*/);
        if (trace != NULL)
          task->set_trace(trace, !trace->is_fixed(), NULL);
        task->must_epoch_task = true;
        task->initialize_replication(repl_ctx, launch_space);
#ifdef DEBUG_LEGION
        task->set_sharding_collective(new ShardingGatherCollective(repl_ctx,
                                      0/*owner shard*/, COLLECTIVE_LOC_59));
#endif
        index_tasks[idx] = task;
      }
      index_triggered.resize(index_tasks.size(), false);
    }

    //--------------------------------------------------------------------------
    FutureMapImpl* ReplMustEpochOp::create_future_map(TaskContext *ctx,
                                                        IndexSpace launch_space)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(launch_space.exists());
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(ctx);
#endif
      runtime->forest->find_launch_space_domain(launch_space, index_domain);
      return new ReplFutureMapImpl(repl_ctx, this, index_domain,runtime,
          runtime->get_available_distributed_id(true/*need continuation*/),
          runtime->address_space);
    }

    //--------------------------------------------------------------------------
    MapperManager* ReplMustEpochOp::invoke_mapper(void)
    //--------------------------------------------------------------------------
    {
      Processor mapper_proc = parent_ctx->get_executing_processor();
      MapperManager *mapper = runtime->find_mapper(mapper_proc, map_id);
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // We want to do the map must epoch call
      // First find all the tasks that we own on this shard
      for (std::vector<SingleTask*>::const_iterator it = 
            single_tasks.begin(); it != single_tasks.end(); it++)
      {
        ShardID shard = 
          sharding_function->find_owner((*it)->index_point, index_domain);
        // If it is not our shard then we don't own it
        if (shard != repl_ctx->owner_shard->shard_id)
          continue;
        shard_single_tasks.insert(*it);
      }
      // Find the set of constraints that apply to our local set of tasks
      std::vector<Mapper::MappingConstraint> local_constraints;
      std::vector<unsigned> original_constraint_indexes;
      for (unsigned idx = 0; idx < input.constraints.size(); idx++)
      {
        bool is_local = false;
        for (std::vector<const Task*>::const_iterator it = 
              input.constraints[idx].constrained_tasks.begin(); it !=
              input.constraints[idx].constrained_tasks.end(); it++)
        {
          SingleTask *single = static_cast<SingleTask*>(const_cast<Task*>(*it));
          if (shard_single_tasks.find(single) == shard_single_tasks.end())
            continue;
          is_local = true;
          break;
        }
        if (is_local)
        {
          local_constraints.push_back(input.constraints[idx]);
          original_constraint_indexes.push_back(idx);
        }
      }
      if (collective_map_must_epoch_call)
      {
        // Update the input tasks for our subset
        std::vector<const Task*> all_tasks(shard_single_tasks.begin(),
                                           shard_single_tasks.end());
        input.tasks.swap(all_tasks);
        // Sort them again by their index points to for determinism
        std::sort(input.tasks.begin(), input.tasks.end(), single_task_sorter);
        // Update the constraints to contain just our subset
        const size_t total_constraints = input.constraints.size();
        input.constraints.swap(local_constraints);
        // Fill in our shard mapping and local shard info
        input.shard_mapping = repl_ctx->shard_manager->shard_mapping;
        input.local_shard = repl_ctx->owner_shard->shard_id;
        // Update the outputs
        output.task_processors.resize(input.tasks.size());
        output.constraint_mappings.resize(input.constraints.size());
        output.weights.resize(input.constraints.size());
        // Now we can do the mapper call
        mapper->invoke_map_must_epoch(this, &input, &output);
        // Now we need to exchange our mapping decisions between all the shards
#ifdef DEBUG_LEGION
        assert(mapping_exchange == NULL);
        assert(mapping_collective_id > 0);
#endif
        mapping_exchange = 
          new MustEpochMappingExchange(repl_ctx, mapping_collective_id);
        mapping_exchange->exchange_must_epoch_mappings(
                  repl_ctx->owner_shard->shard_id,
                  repl_ctx->shard_manager->total_shards, total_constraints,
                  input.tasks, all_tasks, output.task_processors,
                  original_constraint_indexes, output.constraint_mappings,
                  output.weights, *get_acquired_instances_ref());
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(mapping_broadcast == NULL);
        assert(mapping_collective_id > 0);
#endif
        mapping_broadcast = new MustEpochMappingBroadcast(repl_ctx, 
                                  0/*owner shard*/, mapping_collective_id);
        // Do the mapper call on shard 0 and then broadcast the results
        if (repl_ctx->owner_shard->shard_id == 0)
        {
          mapper->invoke_map_must_epoch(this, &input, &output);
          mapping_broadcast->broadcast(output.task_processors,
                                       output.constraint_mappings);
        }
        else
          mapping_broadcast->receive_results(output.task_processors,
              original_constraint_indexes, output.constraint_mappings,
              *get_acquired_instances_ref());
      }
      // No need to do any checks, the base class handles that
      return mapper;
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::map_and_distribute(std::set<RtEvent> &tasks_mapped,
                                             std::set<ApEvent> &tasks_complete)
    //--------------------------------------------------------------------------
    {
      // Perform the mapping
      map_replicate_tasks();
      mapping_dependences.clear();
      // We have to exchange mapping and completion events with all the
      // other shards as well
      std::set<RtEvent> local_tasks_mapped;
      std::set<ApEvent> local_tasks_complete;
      for (std::vector<IndividualTask*>::const_iterator it = 
            indiv_tasks.begin(); it != indiv_tasks.end(); it++)
      {
        local_tasks_mapped.insert((*it)->get_mapped_event());
        local_tasks_complete.insert((*it)->get_completion_event());
      }
      for (std::vector<IndexTask*>::const_iterator it = 
            index_tasks.begin(); it != index_tasks.end(); it++)
      {
        local_tasks_mapped.insert((*it)->get_mapped_event());
        local_tasks_complete.insert((*it)->get_completion_event());
      }
      RtEvent local_mapped = Runtime::merge_events(local_tasks_mapped);
      tasks_mapped.insert(local_mapped);
      ApEvent local_complete = Runtime::merge_events(local_tasks_complete);
      tasks_complete.insert(local_complete);
#ifdef DEBUG_LEGION
      assert(completion_exchange != NULL);
#endif
      completion_exchange->exchange_must_epoch_completion(
          local_mapped, local_complete, tasks_mapped, tasks_complete);
      // Then we can distribute the tasks
      distribute_replicate_tasks();
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::trigger_prepipeline_stage(void)
    //--------------------------------------------------------------------------
    {
      Processor mapper_proc = parent_ctx->get_executing_processor();
      MapperManager *mapper = runtime->find_mapper(mapper_proc, map_id);
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Select our sharding functor and then do the base call
      this->individual_tasks.resize(indiv_tasks.size());
      for (unsigned idx = 0; idx < indiv_tasks.size(); idx++)
        this->individual_tasks[idx] = indiv_tasks[idx];
      this->index_space_tasks.resize(index_tasks.size());
      for (unsigned idx = 0; idx < index_tasks.size(); idx++)
        this->index_space_tasks[idx] = index_tasks[idx];
      Mapper::SelectShardingFunctorInput sharding_input;
      sharding_input.shard_mapping = repl_ctx->shard_manager->shard_mapping;
      Mapper::MustEpochShardingFunctorOutput sharding_output;
      sharding_output.chosen_functor = UINT_MAX;
      sharding_output.collective_map_must_epoch_call = false;
      mapper->invoke_must_epoch_select_sharding_functor(this,
                                    &sharding_input, &sharding_output);
      // We can clear these now that we don't need them anymore
      individual_tasks.clear();
      index_space_tasks.clear();
      // Check that we have a sharding ID
      if (sharding_output.chosen_functor == UINT_MAX)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
            "Invalid mapper output from invocation of "
            "'map_must_epoch' on mapper %s. Mapper failed to specify "
            "a valid sharding ID for a must epoch operation in control "
            "replicated context of task %s (UID %lld).",
            mapper->get_mapper_name(), repl_ctx->get_task_name(),
            repl_ctx->get_unique_id())
      this->sharding_functor = sharding_output.chosen_functor;
      this->collective_map_must_epoch_call = 
        sharding_output.collective_map_must_epoch_call;
#ifdef DEBUG_LEGION
      assert(sharding_function == NULL);
      // Check that the sharding IDs are all the same
      assert(sharding_collective != NULL);
      // Contribute the result
      sharding_collective->contribute(this->sharding_functor);
      if (sharding_collective->is_target() && 
          !sharding_collective->validate(this->sharding_functor))
      {
        log_run.error("ERROR: Mapper %s chose different sharding functions "
                      "for must epoch launch in %s (UID %lld)", 
                      mapper->get_mapper_name(), parent_ctx->get_task_name(),
                      parent_ctx->get_unique_id());
        assert(false); 
      }
      ReplFutureMapImpl *impl = 
          dynamic_cast<ReplFutureMapImpl*>(result_map.impl);
      assert(impl != NULL);
#else
      ReplFutureMapImpl *impl = 
          static_cast<ReplFutureMapImpl*>(result_map.impl);
#endif
      // Set the future map sharding functor
      sharding_function = 
        repl_ctx->shard_manager->find_sharding_function(sharding_functor);
      impl->set_sharding_function(sharding_function);
      // Set the sharding functor for all the point and index tasks too
      for (unsigned idx = 0; idx < indiv_tasks.size(); idx++)
      {
        ReplIndividualTask *task = 
          static_cast<ReplIndividualTask*>(indiv_tasks[idx]);
        task->set_sharding_function(sharding_functor, sharding_function);
      }
      for (unsigned idx = 0; idx < index_tasks.size(); idx++)
      {
        ReplIndexTask *task = static_cast<ReplIndexTask*>(index_tasks[idx]);
        task->set_sharding_function(sharding_functor, sharding_function);
      }
      // Then we can do the normal prepipeline stage
      MustEpochOp::trigger_prepipeline_stage();
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::trigger_commit(void)
    //--------------------------------------------------------------------------
    {
      // We have to delete these here to make sure that they are
      // unregistered with the context before the context is deleted
      if (mapping_broadcast != NULL)
        delete mapping_broadcast;
      if (mapping_exchange != NULL)
        delete mapping_exchange;
      if (dependence_exchange != NULL)
        delete dependence_exchange;
      if (completion_exchange != NULL)
        delete completion_exchange;
#ifdef DEBUG_LEGION
      if (sharding_collective != NULL)
        delete sharding_collective;
#endif
      MustEpochOp::trigger_commit();
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::map_replicate_tasks(void) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(dependence_exchange != NULL);
      assert(single_tasks.size() == mapping_dependences.size());
#endif
      std::map<DomainPoint,RtUserEvent> mapped_events;
      for (std::set<SingleTask*>::const_iterator it = 
            shard_single_tasks.begin(); it != shard_single_tasks.end(); it++)
        mapped_events[(*it)->index_point] = Runtime::create_rt_user_event();
      // Now exchange completion events for the point tasks we own
      // and end up with a set of the completion event for each task
      // First compute the set of mapped events for the points that we own
      dependence_exchange->exchange_must_epoch_dependences(mapped_events);

      MustEpochMapArgs args;
      args.owner = const_cast<ReplMustEpochOp*>(this);
      std::set<RtEvent> local_mapped_events;
      // For correctness we still have to abide by the mapping dependences
      // computed on the individual tasks while we are mapping them
      for (unsigned idx = 0; idx < single_tasks.size(); idx++)
      {
        bool own_point = true;
        // Check to see if it is one of the ones that we own
        if (shard_single_tasks.find(single_tasks[idx]) == 
            shard_single_tasks.end())
        {
          // We don't own this point
          // We still need to do some work for individual tasks
          // to exchange versioning information, but no such 
          // work is necessary for point tasks
          SingleTask *task = single_tasks[idx];
          if (dynamic_cast<ReplIndividualTask*>(single_tasks[idx]) == NULL)
          {
            // Do the stuff to record that this is mapped and executed
            task->complete_mapping(mapped_events[task->index_point]);
            task->complete_execution();
            task->trigger_children_complete();
            task->trigger_children_committed();
            continue;
          }
          else // We're going to fall through, but we don't own this point
            own_point = false;
        }
        // Figure out our preconditions
        std::set<RtEvent> preconditions;
        for (std::set<unsigned>::const_iterator it = 
              mapping_dependences[idx].begin(); it != 
              mapping_dependences[idx].end(); it++)
        {
#ifdef DEBUG_LEGION
          assert((*it) < idx);
#endif
          preconditions.insert(mapped_events[single_tasks[*it]->index_point]);
        }
        args.task = single_tasks[idx];
        RtEvent done;
        if (!preconditions.empty())
        {
          RtEvent precondition = Runtime::merge_events(preconditions);
          done = runtime->issue_runtime_meta_task(args, 
                LG_THROUGHPUT_DEFERRED_PRIORITY, args.owner, precondition); 
        }
        else
          done = runtime->issue_runtime_meta_task(args, 
                LG_THROUGHPUT_DEFERRED_PRIORITY, args.owner);
        local_mapped_events.insert(done);
        if (own_point)
        {
          // We can trigger our completion event once the task is done
          RtUserEvent mapped = mapped_events[single_tasks[idx]->index_point];
          Runtime::trigger_event(mapped, done);
        }
      }
      // Now we have to wait for all our mapping operations to be done
      if (!local_mapped_events.empty())
      {
        RtEvent mapped_event = Runtime::merge_events(local_mapped_events);
        mapped_event.lg_wait();
      }
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::distribute_replicate_tasks(void) const
    //--------------------------------------------------------------------------
    {
      // We only want to distribute the points that are owned by our shard
      MustEpochDistributorArgs dist_args;
      MustEpochLauncherArgs launch_args;
      std::set<RtEvent> wait_events;
      ReplMustEpochOp *owner = const_cast<ReplMustEpochOp*>(this);
      for (std::vector<IndividualTask*>::const_iterator it = 
            indiv_tasks.begin(); it != indiv_tasks.end(); it++)
      {
        // Skip any points that we do not own on this shard
        if (shard_single_tasks.find(*it) == shard_single_tasks.end())
          continue;
        if (!runtime->is_local((*it)->target_proc))
        {
          dist_args.task = *it;
          RtEvent wait = 
            runtime->issue_runtime_meta_task(dist_args, 
                LG_THROUGHPUT_DEFERRED_PRIORITY, owner);
          if (wait.exists())
            wait_events.insert(wait);
        }
        else
        {
          launch_args.task = *it;
          RtEvent wait = 
            runtime->issue_runtime_meta_task(launch_args,
                  LG_THROUGHPUT_DEFERRED_PRIORITY, owner);
          if (wait.exists())
            wait_events.insert(wait);
        }
      }
      for (std::set<SliceTask*>::const_iterator it = 
            slice_tasks.begin(); it != slice_tasks.end(); it++)
      {
        // Check to see if we either do or not own this slice
        // We currently do not support mixed slices for which
        // we only own some of the points
        bool contains_any = false;
        bool contains_all = true;
        for (std::vector<PointTask*>::const_iterator pit = 
              (*it)->points.begin(); pit != (*it)->points.end(); pit++)
        {
          if (shard_single_tasks.find(*pit) != shard_single_tasks.end())
            contains_any = true;
          else if (contains_all)
          {
            contains_all = false;
            if (contains_any) // At this point we have all the answers
              break;
          }
        }
        if (!contains_any)
          continue;
        if (!contains_all)
        {
          Processor mapper_proc = parent_ctx->get_executing_processor();
          MapperManager *mapper = runtime->find_mapper(mapper_proc, map_id);
          REPORT_LEGION_FATAL(ERROR_INVALID_MAPPER_OUTPUT,
                              "Mapper %s specified a slice for a must epoch "
                              "launch in control replicated task %s "
                              "(UID %lld) for which not all the points "
                              "mapped to the same shard. Legion does not "
                              "currently support this use case. Please "
                              "specify slices and a sharding function to "
                              "ensure that all the points in a slice are "
                              "owned by the same shard", 
                              mapper->get_mapper_name(),
                              parent_ctx->get_task_name(),
                              parent_ctx->get_unique_id())
        }
        (*it)->update_target_processor();
        if (!runtime->is_local((*it)->target_proc))
        {
          dist_args.task = *it;
          RtEvent wait = 
            runtime->issue_runtime_meta_task(dist_args, 
                LG_THROUGHPUT_DEFERRED_PRIORITY, owner);
          if (wait.exists())
            wait_events.insert(wait);
        }
        else
        {
          launch_args.task = *it;
          RtEvent wait = 
            runtime->issue_runtime_meta_task(launch_args,
                 LG_THROUGHPUT_DEFERRED_PRIORITY, owner);
          if (wait.exists())
            wait_events.insert(wait);
        }
      }
      if (!wait_events.empty())
      {
        RtEvent dist_event = Runtime::merge_events(wait_events);
        dist_event.lg_wait();
      }
    }

    //--------------------------------------------------------------------------
    void ReplMustEpochOp::initialize_collectives(ReplicateContext *ctx)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mapping_collective_id == 0);
      assert(mapping_broadcast == NULL);
      assert(mapping_exchange == NULL);
      assert(dependence_exchange == NULL);
      assert(completion_exchange == NULL);
#endif
      // We can't actually make a collective for the mapping yet because we 
      // don't know if we are going to broadcast or exchange so we just get
      // a collective ID that we will use later 
      mapping_collective_id = ctx->get_next_collective_index(COLLECTIVE_LOC_58);
      dependence_exchange = 
        new MustEpochDependenceExchange(ctx, COLLECTIVE_LOC_70);
      completion_exchange = 
        new MustEpochCompletionExchange(ctx, COLLECTIVE_LOC_73);
    }

    //--------------------------------------------------------------------------
    /*static*/ IndexSpace ReplMustEpochOp::create_temporary_launch_space(
                                 Runtime *runtime, RegionTreeForest *forest,
                                 Context ctx, const MustEpochLauncher &launcher)
    //--------------------------------------------------------------------------
    {
      size_t dim;
      if (launcher.single_tasks.empty())
      {
        const IndexTaskLauncher &index = launcher.index_tasks[0]; 
        if (index.launch_domain.exists())
          dim = index.launch_domain.get_dim();
        else
          dim = NT_TemplateHelper::get_dim(index.launch_space.get_type_tag());
      }
      else
        dim = launcher.single_tasks[0].point.get_dim();
      IndexSpace result;
      switch (dim)
      {
        case 1:
          {
            std::vector<Realm::Point<1,coord_t> > realm_points;
            for (unsigned idx = 0; idx < launcher.single_tasks.size(); idx++)
            {
              Realm::Point<1,coord_t> p = 
                Point<1,coord_t>(launcher.single_tasks[idx].point); 
              realm_points.push_back(p);
            }
            for (unsigned idx = 0; idx < launcher.index_tasks.size(); idx++)
            {
              const IndexTaskLauncher &index = launcher.index_tasks[idx];
              Domain dom = index.launch_domain;
              if (!dom.exists())
                forest->find_launch_space_domain(index.launch_space, dom);
              for (Domain::DomainPointIterator itr(index.launch_domain);
                    itr; itr++)
              {
                Realm::Point<1,coord_t> p = Point<1,coord_t>(itr.p);
                realm_points.push_back(p);
              }
            }
            DomainT<1,coord_t> realm_is((
                Realm::IndexSpace<1,coord_t>(realm_points)));
            result = runtime->create_index_space(ctx, &realm_is, 
                NT_TemplateHelper::encode_tag<1,coord_t>());
            break;
          }
        case 2:
          {
            std::vector<Realm::Point<2,coord_t> > realm_points;
            for (unsigned idx = 0; idx < launcher.single_tasks.size(); idx++)
            {
              Realm::Point<2,coord_t> p = 
                Point<2,coord_t>(launcher.single_tasks[idx].point); 
              realm_points.push_back(p);
            }
            for (unsigned idx = 0; idx < launcher.index_tasks.size(); idx++)
            {
              const IndexTaskLauncher &index = launcher.index_tasks[idx];
              Domain dom = index.launch_domain;
              if (!dom.exists())
                forest->find_launch_space_domain(index.launch_space, dom);
              for (Domain::DomainPointIterator itr(index.launch_domain);
                    itr; itr++)
              {
                Realm::Point<2,coord_t> p = Point<2,coord_t>(itr.p);
                realm_points.push_back(p);
              }
            }
            DomainT<2,coord_t> realm_is((
                Realm::IndexSpace<2,coord_t>(realm_points)));
            result = runtime->create_index_space(ctx, &realm_is, 
                NT_TemplateHelper::encode_tag<2,coord_t>());
            break;
          }
        case 3:
          {
            std::vector<Realm::Point<3,coord_t> > realm_points;
            for (unsigned idx = 0; idx < launcher.single_tasks.size(); idx++)
            {
              Realm::Point<3,coord_t> p = 
                Point<3,coord_t>(launcher.single_tasks[idx].point); 
              realm_points.push_back(p);
            }
            for (unsigned idx = 0; idx < launcher.index_tasks.size(); idx++)
            {
              const IndexTaskLauncher &index = launcher.index_tasks[idx];
              Domain dom = index.launch_domain;
              if (!dom.exists())
                forest->find_launch_space_domain(index.launch_space, dom);
              for (Domain::DomainPointIterator itr(index.launch_domain);
                    itr; itr++)
              {
                Realm::Point<3,coord_t> p = Point<3,coord_t>(itr.p);
                realm_points.push_back(p);
              }
            }
            DomainT<3,coord_t> realm_is((
                Realm::IndexSpace<3,coord_t>(realm_points)));
            result = runtime->create_index_space(ctx, &realm_is, 
                NT_TemplateHelper::encode_tag<3,coord_t>());
            break;
          }
        default:
          assert(false);
      }
      return result;
    }

    /////////////////////////////////////////////////////////////
    // Repl Timing Op 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplTimingOp::ReplTimingOp(Runtime *rt)
      : TimingOp(rt)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplTimingOp::ReplTimingOp(const ReplTimingOp &rhs)
      : TimingOp(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ReplTimingOp::~ReplTimingOp(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplTimingOp& ReplTimingOp::operator=(const ReplTimingOp &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ReplTimingOp::activate(void)
    //--------------------------------------------------------------------------
    {
      activate_timing();
      timing_collective = NULL;
    }

    //--------------------------------------------------------------------------
    void ReplTimingOp::deactivate(void)
    //--------------------------------------------------------------------------
    {
      if (timing_collective != NULL)
      {
        delete timing_collective;
        timing_collective = NULL;
      }
      deactivate_timing();
      runtime->free_repl_timing_op(this);
    }

    //--------------------------------------------------------------------------
    void ReplTimingOp::trigger_mapping(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Shard 0 will handle the timing operation so do the normal mapping
      if (repl_ctx->owner_shard->shard_id > 0)
      {
        complete_mapping();
        RtEvent result_ready = 
          timing_collective->perform_collective_wait(false/*block*/);
        if (result_ready.exists() && !result_ready.has_triggered())
        {
          // Defer completion until the value is ready
          DeferredExecuteArgs deferred_execute_args;
          deferred_execute_args.proxy_this = this;
          runtime->issue_runtime_meta_task(deferred_execute_args,
                                           LG_THROUGHPUT_DEFERRED_PRIORITY,
                                           this, result_ready);
        }
        else
          deferred_execute();
      }
      else // Shard 0 does the normal timing operation
        TimingOp::trigger_mapping();
    } 

    //--------------------------------------------------------------------------
    void ReplTimingOp::deferred_execute(void)
    //--------------------------------------------------------------------------
    {
 #ifdef DEBUG_LEGION
      ReplicateContext *repl_ctx = dynamic_cast<ReplicateContext*>(parent_ctx);
      assert(repl_ctx != NULL);
#else
      ReplicateContext *repl_ctx = static_cast<ReplicateContext*>(parent_ctx);
#endif
      // Shard 0 will handle the timing operation
      if (repl_ctx->owner_shard->shard_id > 0)     
      {
        long long value = timing_collective->get_value(false/*already waited*/);
        result.impl->set_result(&value, sizeof(value), false);
      }
      else
      {
        // Perform the measurement and then arrive on the barrier
        // with the result to broadcast it to the other shards
        switch (measurement)
        {
          case MEASURE_SECONDS:
            {
              double value = Realm::Clock::current_time();
              result.impl->set_result(&value, sizeof(value), false);
              long long *ptr = reinterpret_cast<long long*>(&value);
              timing_collective->broadcast(*ptr);
              break;
            }
          case MEASURE_MICRO_SECONDS:
            {
              long long value = Realm::Clock::current_time_in_microseconds();
              result.impl->set_result(&value, sizeof(value), false);
              timing_collective->broadcast(value);
              break;
            }
          case MEASURE_NANO_SECONDS:
            {
              long long value = Realm::Clock::current_time_in_nanoseconds();
              result.impl->set_result(&value, sizeof(value), false);
              timing_collective->broadcast(value);
              break;
            }
          default:
            assert(false); // should never get here
        }
      }
      complete_execution();
    }

    /////////////////////////////////////////////////////////////
    // Shard Manager 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ShardMapping::ShardMapping(void)
      : Collectable()
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ShardMapping::ShardMapping(const ShardMapping &rhs)
      : Collectable()
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ShardMapping::ShardMapping(const std::vector<AddressSpaceID> &spaces)
      : Collectable(), address_spaces(spaces)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ShardMapping::~ShardMapping(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ShardMapping& ShardMapping::operator=(const ShardMapping &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    AddressSpaceID ShardMapping::operator[](unsigned idx) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(idx < address_spaces.size());
#endif
      return address_spaces[idx];
    }

    //--------------------------------------------------------------------------
    AddressSpaceID& ShardMapping::operator[](unsigned idx)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(idx < address_spaces.size());
#endif
      return address_spaces[idx];
    }

    //--------------------------------------------------------------------------
    void ShardMapping::pack_mapping(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(address_spaces.size());
      for (std::vector<AddressSpaceID>::const_iterator it = 
            address_spaces.begin(); it != address_spaces.end(); it++)
        rez.serialize(*it);
    }

    //--------------------------------------------------------------------------
    void ShardMapping::unpack_mapping(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      size_t num_spaces;
      derez.deserialize(num_spaces);
      address_spaces.resize(num_spaces);
      for (unsigned idx = 0; idx < num_spaces; idx++)
        derez.deserialize(address_spaces[idx]);
    }

    /////////////////////////////////////////////////////////////
    // Shard Manager 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ShardManager::ShardManager(Runtime *rt, ReplicationID id, bool control, 
                               bool top, size_t total, AddressSpaceID owner, 
                               SingleTask *original/*= NULL*/, RtBarrier bar)
      : runtime(rt), repl_id(id), owner_space(owner), total_shards(total),
        original_task(original),control_replicated(control),top_level_task(top),
        manager_lock(Reservation::create_reservation()), address_spaces(NULL),
        local_mapping_complete(0), remote_mapping_complete(0),
        trigger_local_complete(0), trigger_remote_complete(0),
        trigger_local_commit(0), trigger_remote_commit(0), 
        remote_constituents(0), first_future(true), startup_barrier(bar) 
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(total_shards > 0);
#endif
      runtime->register_shard_manager(repl_id, this);
      if (control_replicated && (owner_space == runtime->address_space))
      {
#ifdef DEBUG_LEGION
        assert(!startup_barrier.exists());
#endif
        startup_barrier = 
          RtBarrier(Realm::Barrier::create_barrier(total_shards));
        pending_partition_barrier = 
          ApBarrier(Realm::Barrier::create_barrier(total_shards));
        future_map_barrier = 
          ApBarrier(Realm::Barrier::create_barrier(total_shards));
        // Only need shards-1 for arrivals here since it is used
        // to signal from all the non-creator shards to the creator shard
        creation_barrier = 
          RtBarrier(Realm::Barrier::create_barrier(total_shards-1));
        // Same thing as above for deletion barriers
        deletion_barrier = 
          RtBarrier(Realm::Barrier::create_barrier(total_shards-1));
#ifdef DEBUG_LEGION_COLLECTIVES
        collective_check_barrier = 
          RtBarrier(Realm::Barrier::create_barrier(total_shards,
                CollectiveCheckReduction::REDOP,
                &CollectiveCheckReduction::IDENTITY, 
                sizeof(CollectiveCheckReduction::IDENTITY)));
        close_check_barrier = 
          RtBarrier(Realm::Barrier::create_barrier(total_shards,
                CloseCheckReduction::REDOP,
                &CloseCheckReduction::IDENTITY,
                sizeof(CloseCheckReduction::IDENTITY)));
#endif
      }
#ifdef DEBUG_LEGION
      else if (control_replicated)
        assert(startup_barrier.exists());
#endif
    }

    //--------------------------------------------------------------------------
    ShardManager::ShardManager(const ShardManager &rhs)
      : runtime(NULL), repl_id(0), owner_space(0), total_shards(0),
        original_task(NULL), control_replicated(false), top_level_task(false)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }
    
    //--------------------------------------------------------------------------
    ShardManager::~ShardManager(void)
    //--------------------------------------------------------------------------
    { 
      // We can delete our shard tasks
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
        delete (*it);
      local_shards.clear();
      // Finally unregister ourselves with the runtime
      const bool owner_manager = (owner_space == runtime->address_space);
      runtime->unregister_shard_manager(repl_id, owner_manager);
      manager_lock.destroy_reservation();
      manager_lock = Reservation::NO_RESERVATION;
      if (owner_manager)
      {
        if (control_replicated)
        {
          startup_barrier.destroy_barrier();
          pending_partition_barrier.destroy_barrier();
          future_map_barrier.destroy_barrier();
          creation_barrier.destroy_barrier();
          deletion_barrier.destroy_barrier();
#ifdef DEBUG_LEGION_COLLECTIVES
          collective_check_barrier.destroy_barrier();
          close_check_barrier.destroy_barrier();
#endif
        }
        // Send messages to all the remote spaces to remove the manager
        std::set<AddressSpaceID> sent_spaces;
        for (unsigned idx = 0; idx < address_spaces->size(); idx++)
        {
          AddressSpaceID target = (*address_spaces)[idx];
          if (sent_spaces.find(target) != sent_spaces.end())
            continue;
          if (target == runtime->address_space)
            continue;
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(repl_id);
          }
          runtime->send_replicate_delete(target, rez);
          sent_spaces.insert(target);
        }
      }
      if ((address_spaces != NULL) && address_spaces->remove_reference())
        delete address_spaces;
    }

    //--------------------------------------------------------------------------
    ShardManager& ShardManager::operator=(const ShardManager &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ShardManager::set_shard_mapping(const std::vector<Processor> &mapping)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mapping.size() == total_shards);
#endif
      shard_mapping = mapping;
    }

    //--------------------------------------------------------------------------
    ShardTask* ShardManager::create_shard(ShardID id, Processor target)
    //--------------------------------------------------------------------------
    {
      ShardTask *shard = new ShardTask(runtime, this, id, target);
      local_shards.push_back(shard);
      return shard;
    }

    //--------------------------------------------------------------------------
    void ShardManager::extract_event_preconditions(
                                       const std::deque<InstanceSet> &instances)
    //--------------------------------------------------------------------------
    {
      // Iterate through all the shards and have them extract 
      // their event preconditions
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
        (*it)->extract_event_preconditions(instances);
    }

    //--------------------------------------------------------------------------
    void ShardManager::launch(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_shards.empty());
      assert(address_spaces == NULL);
#endif
      address_spaces = new ShardMapping();
      address_spaces->add_reference();
      address_spaces->resize(local_shards.size());
      // Sort the shards into their target address space
      std::map<AddressSpaceID,std::vector<ShardTask*> > shard_groups;
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
      {
        const AddressSpaceID target = 
          runtime->find_address_space((*it)->target_proc);
        shard_groups[target].push_back(*it); 
#ifdef DEBUG_LEGION
        assert((*it)->shard_id < address_spaces->size());
#endif
        (*address_spaces)[(*it)->shard_id] = target;
      }
      local_shards.clear();
      // Now either send the shards to the remote nodes or record them locally
      for (std::map<AddressSpaceID,std::vector<ShardTask*> >::const_iterator 
            it = shard_groups.begin(); it != shard_groups.end(); it++)
      {
        if (it->first != runtime->address_space)
        {
          distribute_shards(it->first, it->second);
          // Update the remote constituents count
          remote_constituents++;
          // Clean up the shards that are now sent remotely
          for (unsigned idx = 0; idx < it->second.size(); idx++)
            delete it->second[idx];
        }
        else
          local_shards = it->second;
      }
      if (!local_shards.empty())
      {
        for (std::vector<ShardTask*>::const_iterator it = 
              local_shards.begin(); it != local_shards.end(); it++)
          launch_shard(*it);
      }
    }

    //--------------------------------------------------------------------------
    void ShardManager::distribute_shards(AddressSpaceID target,
                                         const std::vector<ShardTask*> &shards)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!shards.empty());
      assert(address_spaces != NULL);
#endif
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(repl_id);
        rez.serialize(total_shards);
        rez.serialize(control_replicated);
        rez.serialize(top_level_task);
        rez.serialize(startup_barrier);
        address_spaces->pack_mapping(rez);
        if (control_replicated)
        {
#ifdef DEBUG_LEGION
          assert(pending_partition_barrier.exists());
          assert(future_map_barrier.exists());
          assert(creation_barrier.exists());
          assert(deletion_barrier.exists());
          assert(shard_mapping.size() == total_shards);
#endif
          rez.serialize(pending_partition_barrier);
          rez.serialize(future_map_barrier);
          rez.serialize(creation_barrier);
          rez.serialize(deletion_barrier);
#ifdef DEBUG_LEGION_COLLECTIVES
          assert(collective_check_barrier.exists());
          rez.serialize(collective_check_barrier);
          assert(close_check_barrier.exists());
          rez.serialize(close_check_barrier);
#endif
          for (std::vector<Processor>::const_iterator it = 
                shard_mapping.begin(); it != shard_mapping.end(); it++)
            rez.serialize(*it);
        }
        rez.serialize<size_t>(shards.size());
        for (std::vector<ShardTask*>::const_iterator it = 
              shards.begin(); it != shards.end(); it++)
        {
          rez.serialize((*it)->shard_id);
          rez.serialize((*it)->target_proc);
          (*it)->pack_task(rez, (*it)->target_proc);
        }
      }
      runtime->send_replicate_launch(target, rez);
    }

    //--------------------------------------------------------------------------
    void ShardManager::unpack_shards_and_launch(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(owner_space != runtime->address_space);
      assert(local_shards.empty());
      assert(address_spaces == NULL);
#endif
      address_spaces = new ShardMapping();
      address_spaces->add_reference();
      address_spaces->unpack_mapping(derez);
      if (control_replicated)
      {
        derez.deserialize(pending_partition_barrier);
        derez.deserialize(future_map_barrier);
        derez.deserialize(creation_barrier);
        derez.deserialize(deletion_barrier);
#ifdef DEBUG_LEGION_COLLECTIVES
        derez.deserialize(collective_check_barrier);
        derez.deserialize(close_check_barrier);
#endif
        shard_mapping.resize(total_shards);
        for (unsigned idx = 0; idx < total_shards; idx++)
          derez.deserialize(shard_mapping[idx]);
      }
      size_t num_shards;
      derez.deserialize(num_shards);
      local_shards.resize(num_shards);
      for (unsigned idx = 0; idx < num_shards; idx++)
      {
        ShardID shard_id;
        derez.deserialize(shard_id);
        Processor target;
        derez.deserialize(target);
        ShardTask *shard = new ShardTask(runtime, this, shard_id, target);
        std::set<RtEvent> ready_preconditions;
        shard->unpack_task(derez, target, ready_preconditions);
        local_shards[idx] = shard;
        if (!ready_preconditions.empty())
          launch_shard(shard, Runtime::merge_events(ready_preconditions));
        else
          launch_shard(shard);
      }
    }

    //--------------------------------------------------------------------------
    void ShardManager::launch_shard(ShardTask *task, RtEvent precondition) const
    //--------------------------------------------------------------------------
    {
      ShardManagerLaunchArgs args;
      args.shard = task;
      runtime->issue_runtime_meta_task(args, LG_LATENCY_WORK_PRIORITY, 
                                       original_task, precondition);
    }

    //--------------------------------------------------------------------------
    void ShardManager::complete_startup_initialization(void) const
    //--------------------------------------------------------------------------
    {
      // Do our arrival
      Runtime::phase_barrier_arrive(startup_barrier, 1/*count*/);
      // Then wait for everyone else to be ready
      startup_barrier.lg_wait();
    }

    //--------------------------------------------------------------------------
    void ShardManager::handle_post_mapped(bool local)
    //--------------------------------------------------------------------------
    {
      bool notify = false;   
      {
        AutoLock m_lock(manager_lock);
        if (local)
        {
          local_mapping_complete++;
#ifdef DEBUG_LEGION
          assert(local_mapping_complete <= local_shards.size());
#endif
        }
        else
        {
          remote_mapping_complete++;
#ifdef DEBUG_LEGION
          assert(remote_mapping_complete <= remote_constituents);
#endif
        }
        notify = (local_mapping_complete == local_shards.size()) &&
                 (remote_mapping_complete == remote_constituents);
      }
      if (notify)
      {
        if (original_task == NULL)
        {
          Serializer rez;
          rez.serialize(repl_id);
          runtime->send_replicate_post_mapped(owner_space, rez);
        }
        else
          original_task->handle_post_mapped(RtEvent::NO_RT_EVENT);
      }
    }

    //--------------------------------------------------------------------------
    void ShardManager::handle_future(const void *res,size_t res_size,bool owned)
    //--------------------------------------------------------------------------
    {
      bool notify = false;
      {
        AutoLock m_lock(manager_lock);
        notify = first_future;
        first_future = false;
      }
      if (notify && (original_task != NULL))
        original_task->handle_future(res, res_size, owned);
      else if (owned) // if we own it and don't use it we need to free it
        free(const_cast<void*>(res));
    }

    //--------------------------------------------------------------------------
    void ShardManager::trigger_task_complete(bool local)
    //--------------------------------------------------------------------------
    {
      bool notify = false;
      {
        AutoLock m_lock(manager_lock);
        if (local)
        {
          trigger_local_complete++;
#ifdef DEBUG_LEGION
          assert(trigger_local_complete <= local_shards.size());
#endif
        }
        else
        {
          trigger_remote_complete++;
#ifdef DEBUG_LEGION
          assert(trigger_remote_complete <= remote_constituents);
#endif
        }
        notify = (trigger_local_complete == local_shards.size()) &&
                 (trigger_remote_complete == remote_constituents);
      }
      if (notify)
      {
        if (original_task == NULL)
        {
          Serializer rez;
          rez.serialize(repl_id);
          runtime->send_replicate_trigger_complete(owner_space, rez);
        }
        else
        {
          // Return the privileges first if this isn't the top-level task
          if (!original_task->is_top_level_task())
            local_shards[0]->return_privilege_state(
                              original_task->get_context());
          original_task->trigger_children_complete();
        }
      }
    }

    //--------------------------------------------------------------------------
    void ShardManager::trigger_task_commit(bool local)
    //--------------------------------------------------------------------------
    {
      bool notify = false;
      {
        AutoLock m_lock(manager_lock);
        if (local)
        {
          trigger_local_commit++;
#ifdef DEBUG_LEGION
          assert(trigger_local_commit <= local_shards.size());
#endif
        }
        else
        {
          trigger_remote_commit++;
#ifdef DEBUG_LEGION
          assert(trigger_remote_commit <= remote_constituents);
#endif
        }
        notify = (trigger_local_commit == local_shards.size()) &&
                 (trigger_remote_commit == remote_constituents);
      }
      if (notify)
      {
        if (original_task == NULL)
        {
          Serializer rez;
          rez.serialize(repl_id);
          runtime->send_replicate_trigger_commit(owner_space, rez);
        }
        else
          original_task->trigger_children_committed();
      }
    }

    //--------------------------------------------------------------------------
    void ShardManager::send_collective_message(ShardID target, Serializer &rez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target < address_spaces->size());
#endif
      AddressSpaceID target_space = (*address_spaces)[target];
      // Check to see if this is a local shard
      if (target_space == runtime->address_space)
      {
        Deserializer derez(rez.get_buffer(), rez.get_used_bytes());
        // Have to unpack the preample we already know
        ReplicationID local_repl;
        derez.deserialize(local_repl);
        handle_collective_message(derez);
      }
      else
        runtime->send_control_replicate_collective_message(target_space, rez);
    }

    //--------------------------------------------------------------------------
    void ShardManager::handle_collective_message(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      // Figure out which shard we are going to
      ShardID target;
      derez.deserialize(target);
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
      {
        if ((*it)->shard_id == target)
        {
          (*it)->handle_collective_message(derez);
          return;
        }
      }
      // Should never get here
      assert(false);
    }

    //--------------------------------------------------------------------------
    void ShardManager::send_future_map_request(ShardID target, Serializer &rez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target < address_spaces->size());
#endif
      AddressSpaceID target_space = (*address_spaces)[target];
      // Check to see if this is a local shard
      if (target_space == runtime->address_space)
      {
        Deserializer derez(rez.get_buffer(), rez.get_used_bytes());
        // Have to unpack the preample we already know
        ReplicationID local_repl;
        derez.deserialize(local_repl);     
        handle_future_map_request(derez);
      }
      else
        runtime->send_control_replicate_future_map_request(target_space, rez);
    }

    //--------------------------------------------------------------------------
    void ShardManager::handle_future_map_request(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      // Figure out which shard we are going to
      ShardID target;
      derez.deserialize(target);
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
      {
        if ((*it)->shard_id == target)
        {
          (*it)->handle_future_map_request(derez);
          return;
        }
      }
      // Should never get here
      assert(false);
    }

    //--------------------------------------------------------------------------
    void ShardManager::send_composite_view_request(ShardID target, 
                                                   Serializer &rez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target < address_spaces->size());
#endif
      AddressSpaceID target_space = (*address_spaces)[target];
      // Check to see if this is a local shard
      if (target_space == runtime->address_space)
      {
        Deserializer derez(rez.get_buffer(), rez.get_used_bytes());
        // Have to unpack the preample we already know
        ReplicationID local_repl;
        derez.deserialize(local_repl);
        handle_composite_view_request(derez);
      }
      else
        runtime->send_control_replicate_composite_view_request(target_space, 
                                                               rez);
    }

    //--------------------------------------------------------------------------
    void ShardManager::handle_composite_view_request(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      // Figure out which shard we are going to
      ShardID target;
      derez.deserialize(target);
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
      {
        if ((*it)->shard_id == target)
        {
          (*it)->handle_composite_view_request(derez);
          return;
        }
      }
      // Should never get here
      assert(false);
    }

    //--------------------------------------------------------------------------
    void ShardManager::broadcast_clone_barrier(unsigned close_index,
                     unsigned clone_index, RtBarrier bar, AddressSpaceID origin)
    //--------------------------------------------------------------------------
    {
      // For now we will do a dumb broadcast where the owner sends it to
      // everyone since this will be a rare event, if it ever becomes
      // a performance bottleneck we can make this a radix broadcast
      if (origin == runtime->address_space)
      {
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(repl_id);
          rez.serialize(close_index);
          rez.serialize(clone_index);
          rez.serialize(bar);
        }
        for (std::set<AddressSpaceID>::const_iterator it = 
             unique_shard_spaces.begin(); it != unique_shard_spaces.end(); it++)
        {
          if ((*it) == origin)
            continue;
          runtime->send_control_replicate_clone_barrier(*it, rez);
        }
      }
      // Then we can notify our local shards
      for (std::vector<ShardTask*>::const_iterator it = 
            local_shards.begin(); it != local_shards.end(); it++)
        (*it)->handle_clone_barrier_broadcast(close_index, clone_index, bar);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_launch(const void *args)
    //--------------------------------------------------------------------------
    {
      const ShardManagerLaunchArgs *largs = (const ShardManagerLaunchArgs*)args;
      largs->shard->launch_shard();
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_delete(const void *args)
    //--------------------------------------------------------------------------
    {
      const ShardManagerDeleteArgs *dargs = (const ShardManagerDeleteArgs*)args;
      delete dargs->manager;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_launch(Deserializer &derez, 
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      size_t total_shards;
      derez.deserialize(total_shards);
      bool control_repl;
      derez.deserialize(control_repl);
      bool top_level_task;
      derez.deserialize(top_level_task);
      RtBarrier startup_barrier;
      derez.deserialize(startup_barrier);
      ShardManager *manager = 
        new ShardManager(runtime, repl_id, control_repl, top_level_task,
                total_shards, source, NULL/*original*/, startup_barrier);
      manager->unpack_shards_and_launch(derez);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_delete(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      delete manager;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_post_mapped(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->handle_post_mapped(false/*local*/);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_trigger_complete(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->trigger_task_complete(false/*local*/);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_trigger_commit(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->trigger_task_commit(false/*local*/);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_collective_message(Deserializer &derez,
                                                            Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->handle_collective_message(derez);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_future_map_request(Deserializer &derez,
                                                            Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->handle_future_map_request(derez);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_composite_view_request(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->handle_composite_view_request(derez);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_top_view_request(Deserializer &derez,
                                Runtime *runtime, AddressSpaceID request_source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      AddressSpaceID source;
      derez.deserialize(source);
      ReplicateContext *request_context;
      derez.deserialize(request_context);

      RtEvent ready;
      PhysicalManager *physical_manager = 
        runtime->find_or_request_physical_manager(manager_did, ready); 
      ShardManager *manager = runtime->find_shard_manager(repl_id);
      if (!ready.has_triggered())
        ready.lg_wait();
      manager->create_instance_top_view(physical_manager, source, 
                request_context, request_source, true/*handle now*/);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_top_view_response(Deserializer &derez,
                                                           Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID manager_did, view_did;
      derez.deserialize(manager_did);
      derez.deserialize(view_did);
      ReplicateContext *request_context;
      derez.deserialize(request_context);

      RtEvent manager_ready, view_ready;
      PhysicalManager *manager = 
        runtime->find_or_request_physical_manager(manager_did, manager_ready);
      InstanceView *view = static_cast<InstanceView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      if (!manager_ready.has_triggered())
        manager_ready.lg_wait();
      if (!view_ready.has_triggered())
        view_ready.lg_wait();
      request_context->record_replicate_instance_top_view(manager, view);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ShardManager::handle_clone_barrier(Deserializer &derez,
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      ReplicationID repl_id;
      derez.deserialize(repl_id);
      unsigned close_index, clone_index;
      derez.deserialize(close_index);
      derez.deserialize(clone_index);
      RtBarrier bar;
      derez.deserialize(bar);

      ShardManager *manager = runtime->find_shard_manager(repl_id);
      manager->broadcast_clone_barrier(close_index, clone_index, bar, source);
    }

    //--------------------------------------------------------------------------
    ShardingFunction* ShardManager::find_sharding_function(ShardingID sid)
    //--------------------------------------------------------------------------
    {
      // Check to see if it is in the cache
      {
        AutoLock m_lock(manager_lock,1,false/*exclusive*/);
        std::map<ShardingID,ShardingFunction*>::const_iterator finder = 
          sharding_functions.find(sid);
        if (finder != sharding_functions.end())
          return finder->second;
      }
      // Get the functor from the runtime
      ShardingFunctor *functor = runtime->find_sharding_functor(sid);
      // Retake the lock
      AutoLock m_lock(manager_lock);
      // See if we lost the race
      std::map<ShardingID,ShardingFunction*>::const_iterator finder = 
        sharding_functions.find(sid);
      if (finder != sharding_functions.end())
        return finder->second;
      ShardingFunction *result = 
        new ShardingFunction(functor, runtime->forest, sid, total_shards);
      // Save the result for the future
      sharding_functions[sid] = result;
      return result;
    }

    //--------------------------------------------------------------------------
    void ShardManager::create_instance_top_view(PhysicalManager *manager, 
                AddressSpaceID source, ReplicateContext *request_context, 
                AddressSpaceID request_source, bool handle_now/*= false*/)
    //--------------------------------------------------------------------------
    {
      // Easy case if we are not control replicated
      if (!control_replicated)
      {
        InstanceView *result = 
          request_context->create_replicate_instance_top_view(manager, source);
        request_context->record_replicate_instance_top_view(manager, result);
        return;
      }
      // If we're on the owner node of the manager just handle it here
      if (handle_now || (manager->owner_space == runtime->address_space))
      {
#ifdef DEBUG_LEGION
        assert(!local_shards.empty());
#endif
        // Distribute manager requests across local shards
        const unsigned index = manager->did % local_shards.size();
        InstanceView *result = 
          local_shards[index]->create_instance_top_view(manager, source);
        // Now we have to tell the request context about the result
        if (request_source != runtime->address_space)
        {
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(manager->did);
            rez.serialize(result->did);
            rez.serialize(request_context);
          }
          runtime->send_control_replicate_top_view_response(request_source,rez);
        }
        else
          request_context->record_replicate_instance_top_view(manager, result);
      }
      else
      {
        // Check to see if we already have a manager on the owner node
        // if so we can just send a message there and handle it
        // If not, we round robin the distributed ID across the shards to
        // find the shard to handle the request and send it there
        AddressSpaceID target;
        {
          AutoLock m_lock(manager_lock);
          if (unique_shard_spaces.empty())
            for (unsigned shard = 0; shard < total_shards; shard++)
              unique_shard_spaces.insert((*address_spaces)[shard]);
          if (unique_shard_spaces.find(manager->owner_space) == 
              unique_shard_spaces.end())
          {
            // Round-robin accross the shards
            const unsigned index = manager->did % total_shards;
            target = (*address_spaces)[index];
          }
          else
            target = manager->owner_space;
        }
        if (target != runtime->address_space)
        {
          // Now we can send the message to the target
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(repl_id);
            rez.serialize(manager->did);
            rez.serialize(source);
            rez.serialize(request_context);
          }
          runtime->send_control_replicate_top_view_request(target, rez);
        }
        else
          create_instance_top_view(manager, source, request_context, 
                                   request_source, true/*handle now*/);
      }
    }

    /////////////////////////////////////////////////////////////
    // Shard Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ShardCollective::ShardCollective(CollectiveIndexLocation loc,
                                     ReplicateContext *ctx)
      : manager(ctx->shard_manager), context(ctx), 
        local_shard(ctx->owner_shard->shard_id), 
        collective_index(ctx->get_next_collective_index(loc)),
        collective_lock(Reservation::create_reservation())
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ShardCollective::ShardCollective(ReplicateContext *ctx, CollectiveID id)
      : manager(ctx->shard_manager), context(ctx), 
        local_shard(ctx->owner_shard->shard_id), collective_index(id),
        collective_lock(Reservation::create_reservation())
    //--------------------------------------------------------------------------
    { 
    }

    //--------------------------------------------------------------------------
    ShardCollective::~ShardCollective(void)
    //--------------------------------------------------------------------------
    {
      // Unregister this with the context 
      context->unregister_collective(this);
      collective_lock.destroy_reservation();
      collective_lock = Reservation::NO_RESERVATION;
    }

    //--------------------------------------------------------------------------
    int ShardCollective::convert_to_index(ShardID id, ShardID origin) const
    //--------------------------------------------------------------------------
    {
      // shift everything so that the target shard is at index 0
      const int result = 
        ((id + (manager->total_shards - origin)) % manager->total_shards);
      return result;
    }

    //--------------------------------------------------------------------------
    ShardID ShardCollective::convert_to_shard(int index, ShardID origin) const
    //--------------------------------------------------------------------------
    {
      // Add target then take the modulus
      const ShardID result = (index + origin) % manager->total_shards; 
      return result;
    }

    /////////////////////////////////////////////////////////////
    // Gather Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    BroadcastCollective::BroadcastCollective(CollectiveIndexLocation loc,
                                             ReplicateContext *ctx, ShardID o)
      : ShardCollective(loc, ctx), origin(o),
        shard_collective_radix(ctx->get_shard_collective_radix())
    //--------------------------------------------------------------------------
    {
      if (local_shard != origin)
        done_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    BroadcastCollective::BroadcastCollective(ReplicateContext *ctx, 
                                             CollectiveID id, ShardID o)
      : ShardCollective(ctx, id), origin(o),
        shard_collective_radix(ctx->get_shard_collective_radix())
    //--------------------------------------------------------------------------
    {
      if (local_shard != origin)
        done_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    BroadcastCollective::~BroadcastCollective(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    void BroadcastCollective::perform_collective_async(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_shard == origin);
#endif
      // Register this with the context
      context->register_collective(this);
      send_messages(); 
    }

    //--------------------------------------------------------------------------
    RtEvent BroadcastCollective::perform_collective_wait(bool block/*=true*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_shard != origin);
#endif     
      // Register this with the context
      context->register_collective(this);
      if (!done_event.has_triggered())
      {
        if (block)
          done_event.lg_wait();
        else
          return done_event;
      }
      return RtEvent::NO_RT_EVENT;
    }

    //--------------------------------------------------------------------------
    void BroadcastCollective::handle_collective_message(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_shard != origin);
#endif
      // No need for the lock since this is only written to once
      unpack_collective(derez);
      // Send our messages
      send_messages();
      // Then trigger our event to indicate that we are ready
      Runtime::trigger_event(done_event);
    }

    //--------------------------------------------------------------------------
    RtEvent BroadcastCollective::get_done_event(void) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_shard != origin);
#endif
      return done_event;
    }

    //--------------------------------------------------------------------------
    void BroadcastCollective::send_messages(void) const
    //--------------------------------------------------------------------------
    {
      const int local_index = convert_to_index(local_shard, origin);
      for (int idx = 1; idx <= shard_collective_radix; idx++)
      {
        const int target_index = local_index * shard_collective_radix + idx; 
        if (target_index >= int(manager->total_shards))
          break;
        ShardID target = convert_to_shard(target_index, origin);
        Serializer rez;
        {
          rez.serialize(manager->repl_id);
          rez.serialize(target);
          rez.serialize(collective_index);
          pack_collective(rez);
        }
        manager->send_collective_message(target, rez);
      }
    }

    /////////////////////////////////////////////////////////////
    // Gather Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    GatherCollective::GatherCollective(CollectiveIndexLocation loc,
                                       ReplicateContext *ctx, ShardID t)
      : ShardCollective(loc, ctx), target(t), 
        shard_collective_radix(ctx->get_shard_collective_radix()),
        expected_notifications(compute_expected_notifications()),
        received_notifications(0)
    //--------------------------------------------------------------------------
    {
      if (expected_notifications > 1)
        done_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    GatherCollective::~GatherCollective(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    void GatherCollective::perform_collective_async(void)
    //--------------------------------------------------------------------------
    {
      // Register this with the context
      context->register_collective(this);
      bool done = false;
      {
        AutoLock c_lock(collective_lock);
#ifdef DEBUG_LEGION
        assert(received_notifications < expected_notifications);
#endif
        done = (++received_notifications == expected_notifications);
      }
      if (done)
      {
        if (local_shard != target)
          send_message();
        if (done_event.exists())
          Runtime::trigger_event(done_event);
      }
    }

    //--------------------------------------------------------------------------
    RtEvent GatherCollective::perform_collective_wait(bool block/*=true*/)
    //--------------------------------------------------------------------------
    {
      if (done_event.exists() && !done_event.has_triggered())
      {
        if (block)
          done_event.lg_wait();
        else
          return done_event;
      }
      return RtEvent::NO_RT_EVENT;
    }

    //--------------------------------------------------------------------------
    void GatherCollective::handle_collective_message(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      bool done = false;
      {
        // Hold the lock while doing these operations
        AutoLock c_lock(collective_lock);
        // Unpack the result
        unpack_collective(derez);
 #ifdef DEBUG_LEGION
        assert(received_notifications < expected_notifications);
#endif
        done = (++received_notifications == expected_notifications);       
      }
      if (done)
      {
        if (local_shard != target)
          send_message();
        if (done_event.exists())
          Runtime::trigger_event(done_event);
      }
    }

    //--------------------------------------------------------------------------
    void GatherCollective::send_message(void)
    //--------------------------------------------------------------------------
    {
      // Convert to our local index
      const int local_index = convert_to_index(local_shard, target);
#ifdef DEBUG_LEGION
      assert(local_index > 0); // should never be here for zero
#endif
      // Subtract by 1 and then divide to get the target (truncate)
      const int target_index = (local_index - 1) / shard_collective_radix;
      // Then convert back to the target
      ShardID next = convert_to_shard(target_index, target);
      Serializer rez;
      {
        rez.serialize(manager->repl_id);
        rez.serialize(next);
        rez.serialize(collective_index);
        AutoLock c_lock(collective_lock,1,false/*exclusive*/);
        pack_collective(rez);
      }
      manager->send_collective_message(next, rez);
    } 

    //--------------------------------------------------------------------------
    int GatherCollective::compute_expected_notifications(void) const
    //--------------------------------------------------------------------------
    {
      int result = 1; // always have one arriver for ourself
      const int index = convert_to_index(local_shard, target);
      for (int idx = 1; idx <= shard_collective_radix; idx++)
      {
        const int source_index = index * shard_collective_radix + idx;
        if (source_index >= int(manager->total_shards))
          break;
        result++;
      }
      return result;
    }

    /////////////////////////////////////////////////////////////
    // All Gather Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    AllGatherCollective::AllGatherCollective(CollectiveIndexLocation loc,
                                             ReplicateContext *ctx)
      : ShardCollective(loc, ctx),
        shard_collective_radix(ctx->get_shard_collective_radix()),
        shard_collective_log_radix(ctx->get_shard_collective_log_radix()),
        shard_collective_stages(ctx->get_shard_collective_stages()),
        shard_collective_participating_shards(
            ctx->get_shard_collective_participating_shards()),
        shard_collective_last_radix(ctx->get_shard_collective_last_radix()),
        shard_collective_last_log_radix(
            ctx->get_shard_collective_last_log_radix()),
        participating(int(local_shard) < shard_collective_participating_shards),
        current_stage(-1), current_notifications(0),
        prefix_stage_notification(false)
    //--------------------------------------------------------------------------
    { 
#ifdef DEBUG_LEGION
      if (participating)
        assert(shard_collective_stages > 0);
      current_notifications = ((current_stage+1) == shard_collective_stages) ? 
        shard_collective_last_radix : shard_collective_radix;
#endif
      if (manager->total_shards > 1)
        done_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    AllGatherCollective::AllGatherCollective(ReplicateContext *ctx,
                                             CollectiveID id)
      : ShardCollective(ctx, id),
        shard_collective_radix(ctx->get_shard_collective_radix()),
        shard_collective_log_radix(ctx->get_shard_collective_log_radix()),
        shard_collective_stages(ctx->get_shard_collective_stages()),
        shard_collective_participating_shards(
            ctx->get_shard_collective_participating_shards()),
        shard_collective_last_radix(ctx->get_shard_collective_last_radix()),
        shard_collective_last_log_radix(
            ctx->get_shard_collective_last_log_radix()),
        participating(int(local_shard) < shard_collective_participating_shards),
        current_stage(-1), current_notifications(0), 
        prefix_stage_notification(false)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      if (participating)
        assert(shard_collective_stages > 0);
      current_notifications = ((current_stage+1) == shard_collective_stages) ? 
        shard_collective_last_radix : shard_collective_radix;
#endif
      if (manager->total_shards > 1)
        done_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    AllGatherCollective::~AllGatherCollective(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    void AllGatherCollective::perform_collective_sync(void)
    //--------------------------------------------------------------------------
    {
      perform_collective_async(); 
      perform_collective_wait();
    }

    //--------------------------------------------------------------------------
    void AllGatherCollective::perform_collective_async(void)
    //--------------------------------------------------------------------------
    {
      // Register this with the context
      context->register_collective(this);
      if (manager->total_shards <= 1)
        return;
      // See if we are a participating shard or not
      if (participating)
      {
        // We are a participating shard
        // See if we are waiting for an initial notification
        // if not we can just send our message now
        if ((int(manager->total_shards) == 
              shard_collective_participating_shards) ||
            (local_shard >= (manager->total_shards - 
                             shard_collective_participating_shards)))
        {
          int stage = 0;
          while (send_stage(stage++)) { }
        }
        else
        {
          // Have a precondition for stage 0 so start at -1
          int stage = -1;
          bool next_stage = false;
          {
            AutoLock c_lock(collective_lock);
            next_stage = arrive_stage(stage++);
          }
          while (next_stage)
            next_stage = send_stage(stage++);
        }
      }
      else
      {
        // We are not a participating shard
        // so we just have to send a notification to one node
        send_stage(-1);
      }
    }

    //--------------------------------------------------------------------------
    RtEvent AllGatherCollective::perform_collective_wait(bool block/*=true*/)
    //--------------------------------------------------------------------------
    {
      if (manager->total_shards <= 1)
        return RtEvent::NO_RT_EVENT;
      if (!done_event.has_triggered())
      {
        if (block)
          done_event.lg_wait();
        else
          return done_event;
      }
      return RtEvent::NO_RT_EVENT;
    }

    //--------------------------------------------------------------------------
    void AllGatherCollective::handle_collective_message(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      int stage;
      derez.deserialize(stage);
#ifdef DEBUG_LEGION
      assert(participating || (stage == -1));
#endif
      bool send_next = unpack_stage(stage, derez);
      if (participating)
      {
        // Keep doing local arrivals until we are not the last one
        while (send_next)
          send_next = send_stage(++stage);
      }
      else
        complete_exchange();
    }

    //--------------------------------------------------------------------------
    bool AllGatherCollective::send_stage(int stage)
    //--------------------------------------------------------------------------
    {
      // A few special cases here
      if (stage == -1)
      {
        // Single message case
        if (participating)
        {
          // Send back to the nodes that are not participating
          ShardID target = local_shard + shard_collective_participating_shards;
#ifdef DEBUG_LEGION
          assert(target < manager->total_shards);
#endif
          Serializer rez;
          construct_message(target, stage, rez);
          manager->send_collective_message(target, rez);
          AutoLock c_lock(collective_lock);
          return arrive_stage(stage);
        }
        else
        {
          // Send to a node that is participating
          ShardID target = local_shard % shard_collective_participating_shards;
          Serializer rez;
          construct_message(target, stage, rez);
          manager->send_collective_message(target, rez);
          return false;
        }
      }
      else if (stage == shard_collective_stages)
      {
        // Complete the exchange case 
        complete_exchange();
        return false;
      }
      else if (stage == (shard_collective_stages-1))
      {
        for (int r = 1; r < shard_collective_last_radix; r++)
        {
          ShardID target = local_shard ^ 
            (r << (stage * shard_collective_log_radix));
#ifdef DEBUG_LEGION
          assert(int(target) < shard_collective_participating_shards);
#endif
          Serializer rez;
          construct_message(target, stage, rez);
          manager->send_collective_message(target, rez);
        }
        return update_current_stage(stage);
      }
      else
      {
        for (int r = 1; r < shard_collective_radix; r++)
        {
          ShardID target = local_shard ^ 
            (r << (stage * shard_collective_log_radix));
#ifdef DEBUG_LEGION
          assert(int(target) < shard_collective_participating_shards);
#endif
          Serializer rez;
          construct_message(target, stage, rez);
          manager->send_collective_message(target, rez);
        }
        // Once we've sent the messages we can update the current stage
        return update_current_stage(stage);
      }
    }

    //--------------------------------------------------------------------------
    void AllGatherCollective::construct_message(ShardID target, int stage,
                                                Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      rez.serialize(manager->repl_id);
      rez.serialize(target);
      rez.serialize(collective_index);
      rez.serialize(stage);
      AutoLock c_lock(collective_lock, 1, false/*exclusive*/);
      pack_collective_stage(rez, stage);
    }

    //--------------------------------------------------------------------------
    bool AllGatherCollective::arrive_stage(int stage)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(participating);
#endif
      if (stage == -1)
      {
        if (!prefix_stage_notification)
        {
          prefix_stage_notification = true;
          return false;
        }
        else
          return true;
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(stage < shard_collective_stages);
#endif
        // Check to see if we are the current stage or not
        // If not then we just save our notification and we're done
        if (stage != current_stage)
        {
          std::map<int,int>::iterator finder = 
            pending_notifications.find(stage);
          if (finder != pending_notifications.end())
            finder->second++;
          else
            pending_notifications[stage] = 1;
          // Not done yet
          return false;
        }
        else
        {
          // We are the current stage, so update the count
          current_notifications++;
          if (stage < (shard_collective_stages-1))
          {
#ifdef DEBUG_LEGION
            assert(current_notifications <= shard_collective_radix);
#endif
            return (current_notifications == shard_collective_radix);
          }
          else
          {
#ifdef DEBUG_LEGION
            assert(current_notifications <= shard_collective_last_radix);
#endif
            return (current_notifications == shard_collective_last_radix);
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    bool AllGatherCollective::update_current_stage(int stage)
    //--------------------------------------------------------------------------
    {
      AutoLock c_lock(collective_lock);
#ifdef DEBUG_LEGION
      // Stages must trigger in order
      assert((current_stage + 1) == stage);
      assert(stage < shard_collective_stages);
      if ((current_stage+1) == shard_collective_stages)
        assert(current_notifications == shard_collective_last_radix);
      else
        assert(current_notifications == shard_collective_radix);
#endif
      current_stage = stage;
      current_notifications = 0;
      // See if we have any pending notifications for this stage
      if (!pending_notifications.empty())
      {
        std::map<int,int>::iterator next = pending_notifications.begin();
        if (next->first == current_stage)
        {
          current_notifications = next->second;
#ifdef DEBUG_LEGION
          if ((current_stage+1) == shard_collective_stages)
            assert(current_notifications < shard_collective_last_radix);
          else
            assert(current_notifications < shard_collective_radix);
#endif
          pending_notifications.erase(next);
        }
      }
      return arrive_stage(stage);
    }

    //--------------------------------------------------------------------------
    bool AllGatherCollective::unpack_stage(int stage, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      AutoLock c_lock(collective_lock);
      unpack_collective_stage(derez, stage);
      if ((stage < 0) && !participating)
        return false;
      return arrive_stage(stage);
    }

    //--------------------------------------------------------------------------
    void AllGatherCollective::complete_exchange(void)
    //--------------------------------------------------------------------------
    {
      // See if we have to send a message back to a
      // non-participating shard 
      if ((int(manager->total_shards) > shard_collective_participating_shards)
          && (int(local_shard) < int(manager->total_shards - 
                                      shard_collective_participating_shards)))
        send_stage(-1);
      // We are done
      Runtime::trigger_event(done_event);
    }

    /////////////////////////////////////////////////////////////
    // Barrier Exchange Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    BarrierExchangeCollective::BarrierExchangeCollective(ReplicateContext *ctx,
     size_t win_size, std::vector<RtBarrier> &bars, CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx), window_size(win_size), barriers(bars)
    //--------------------------------------------------------------------------
    { 
    }

    //--------------------------------------------------------------------------
    BarrierExchangeCollective::BarrierExchangeCollective(
                                           const BarrierExchangeCollective &rhs)
      : AllGatherCollective(rhs), window_size(0), barriers(rhs.barriers)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    BarrierExchangeCollective::~BarrierExchangeCollective(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    BarrierExchangeCollective& BarrierExchangeCollective::operator=(
                                           const BarrierExchangeCollective &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void BarrierExchangeCollective::exchange_barriers_async(void)
    //--------------------------------------------------------------------------
    {
      // First make our local barriers and put them in the data structure
      {
        AutoLock c_lock(collective_lock);
        for (unsigned index = local_shard; 
              index < window_size; index += manager->total_shards)
        {
#ifdef DEBUG_LEGION
          assert(local_barriers.find(index) == local_barriers.end());
#endif
          local_barriers[index] = 
              RtBarrier(Realm::Barrier::create_barrier(manager->total_shards));
        }
      }
      // Now we can start the exchange from this shard 
      perform_collective_async();
    }

    //--------------------------------------------------------------------------
    void BarrierExchangeCollective::wait_for_barrier_exchange(void)
    //--------------------------------------------------------------------------
    {
      // Wait for everything to be done
      perform_collective_wait();
#ifdef DEBUG_LEGION
      assert(local_barriers.size() == window_size);
#endif
      // Fill in the barrier vector with the barriers we've got from everyone
      barriers.resize(window_size);
      for (std::map<unsigned,RtBarrier>::const_iterator it = 
            local_barriers.begin(); it != local_barriers.end(); it++)
      {
#ifdef DEBUG_LEGION
        assert(it->first < window_size);
#endif
        barriers[it->first] = it->second;
      }
    }

    //--------------------------------------------------------------------------
    void BarrierExchangeCollective::pack_collective_stage(Serializer &rez, 
                                                          int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize(window_size);
      rez.serialize<size_t>(local_barriers.size());
      for (std::map<unsigned,RtBarrier>::const_iterator it = 
            local_barriers.begin(); it != local_barriers.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second);
      }
    }

    //--------------------------------------------------------------------------
    void BarrierExchangeCollective::unpack_collective_stage(Deserializer &derez,
                                                            int stage)
    //--------------------------------------------------------------------------
    {
      size_t other_window_size;
      derez.deserialize(other_window_size);
      if (other_window_size != window_size)
        REPORT_LEGION_ERROR(ERROR_INVALID_MAPPER_OUTPUT,
                      "Context configurations for control replicated "
                      "task %s were assigned different maximum window sizes "
                      "of %zd and %zd by the mapper which is illegal.",
                      context->owner_task->get_task_name(), window_size,
                      other_window_size)
      size_t num_bars;
      derez.deserialize(num_bars);
      for (unsigned idx = 0; idx < num_bars; idx++)
      {
        unsigned index;
        derez.deserialize(index);
        derez.deserialize(local_barriers[index]);
      }
    }

    /////////////////////////////////////////////////////////////
    // Shard Sync Tree 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ShardSyncTree::ShardSyncTree(ReplicateContext *ctx, ShardID origin,
                                 CollectiveIndexLocation loc)
      : BroadcastCollective(loc, ctx, origin), 
        is_origin(origin == ctx->owner_shard->shard_id)
    //--------------------------------------------------------------------------
    {
      if (is_origin)
      {
        // All we need to do is the broadcast and then wait for 
        // everything to be done
        perform_collective_async();
        // Now wait for the result to be ready
        if (!done_preconditions.empty())
        {
          RtEvent ready = Runtime::merge_events(done_preconditions);
          ready.lg_wait();
        }
      }
    }

    //--------------------------------------------------------------------------
    ShardSyncTree::~ShardSyncTree(void)
    //--------------------------------------------------------------------------
    {
      if (!is_origin)
      {
        // Perform the collective wait
        perform_collective_wait();
        // Trigger our done event when all the preconditions are ready
#ifdef DEBUG_LEGION
        assert(done_event.exists());
#endif
        if (!done_preconditions.empty())
          Runtime::trigger_event(done_event,
              Runtime::merge_events(done_preconditions));
        else
          Runtime::trigger_event(done_event);
      }
    }

    //--------------------------------------------------------------------------
    void ShardSyncTree::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      RtUserEvent next = Runtime::create_rt_user_event();
      rez.serialize(next);
      done_preconditions.insert(next);
    }

    //--------------------------------------------------------------------------
    void ShardSyncTree::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      derez.deserialize(done_event);
    }

    /////////////////////////////////////////////////////////////
    // Cross Product Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    CrossProductCollective::CrossProductCollective(ReplicateContext *ctx,
                                                   CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    CrossProductCollective::CrossProductCollective(
                                              const CrossProductCollective &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    CrossProductCollective::~CrossProductCollective(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    CrossProductCollective& CrossProductCollective::operator=(
                                              const CrossProductCollective &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void CrossProductCollective::exchange_partitions(
                                   std::map<IndexSpace,IndexPartition> &handles)
    //--------------------------------------------------------------------------
    {
      // Need the lock in case we are unpacking other things here
      {
        AutoLock c_lock(collective_lock);
        // Only put the non-empty partitions into our local set
        for (std::map<IndexSpace,IndexPartition>::const_iterator it = 
              handles.begin(); it != handles.end(); it++)
        {
          if (!it->second.exists())
            continue;
          non_empty_handles.insert(*it);
        }
      }
      // Now we do the exchange
      perform_collective_sync();
      // When we wake up we should have all the handles and no need the lock
      // to access them
#ifdef DEBUG_LEGION
      assert(handles.size() == non_empty_handles.size());
#endif
      handles = non_empty_handles;
    }

    //--------------------------------------------------------------------------
    void CrossProductCollective::pack_collective_stage(Serializer &rez, 
                                                       int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(non_empty_handles.size());
      for (std::map<IndexSpace,IndexPartition>::const_iterator it = 
            non_empty_handles.begin(); it != non_empty_handles.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second);
      }
    }

    //--------------------------------------------------------------------------
    void CrossProductCollective::unpack_collective_stage(Deserializer &derez,
                                                         int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_handles;
      derez.deserialize(num_handles);
      for (unsigned idx = 0; idx < num_handles; idx++)
      {
        IndexSpace handle;
        derez.deserialize(handle);
        derez.deserialize(non_empty_handles[handle]);
      }
    }

    /////////////////////////////////////////////////////////////
    // Sharding Gather Collective 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ShardingGatherCollective::ShardingGatherCollective(ReplicateContext *ctx,
                                   ShardID target, CollectiveIndexLocation loc)
      : GatherCollective(loc, ctx, target)
    //--------------------------------------------------------------------------
    {
    }
    
    //--------------------------------------------------------------------------
    ShardingGatherCollective::ShardingGatherCollective(
                                            const ShardingGatherCollective &rhs)
      : GatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    ShardingGatherCollective::~ShardingGatherCollective(void)
    //--------------------------------------------------------------------------
    {
      // Make sure that we wait in case we still have messages to pass on
      perform_collective_wait();
    }

    //--------------------------------------------------------------------------
    ShardingGatherCollective& ShardingGatherCollective::operator=(
                                            const ShardingGatherCollective &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void ShardingGatherCollective::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(results.size());
      for (std::map<ShardID,ShardingID>::const_iterator it = 
            results.begin(); it != results.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second);
      }
    }

    //--------------------------------------------------------------------------
    void ShardingGatherCollective::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      size_t num_results;
      derez.deserialize(num_results);
      for (unsigned idx = 0; idx < num_results; idx++)
      {
        ShardID shard;
        derez.deserialize(shard);
        derez.deserialize(results[shard]);
      }
    }

    //--------------------------------------------------------------------------
    void ShardingGatherCollective::contribute(ShardingID value)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
#ifdef DEBUG_LEGION
        assert(results.find(local_shard) == results.end());
#endif
        results[local_shard] = value;
      }
      perform_collective_async();
    }

    //--------------------------------------------------------------------------
    bool ShardingGatherCollective::validate(ShardingID value)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_target());
#endif
      // Wait for the results
      perform_collective_wait();
      for (std::map<ShardID,ShardingID>::const_iterator it = 
            results.begin(); it != results.end(); it++)
      {
        if (it->second != value)
          return false;
      }
      return true;
    }

    /////////////////////////////////////////////////////////////
    // Field Descriptor Exchange 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FieldDescriptorExchange::FieldDescriptorExchange(ReplicateContext *ctx,
                                                   CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FieldDescriptorExchange::FieldDescriptorExchange(
                                             const FieldDescriptorExchange &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    FieldDescriptorExchange::~FieldDescriptorExchange(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FieldDescriptorExchange& FieldDescriptorExchange::operator=(
                                             const FieldDescriptorExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    ApEvent FieldDescriptorExchange::exchange_descriptors(ApEvent ready_event,
                                  const std::vector<FieldDataDescriptor> &descs)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
        ready_events.insert(ready_event);
        descriptors.insert(descriptors.end(), descs.begin(), descs.end());
      }
      perform_collective_sync();
      return Runtime::merge_events(ready_events);
    }

    //--------------------------------------------------------------------------
    void FieldDescriptorExchange::pack_collective_stage(Serializer &rez,
                                                        int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(ready_events.size());
      for (std::set<ApEvent>::const_iterator it = ready_events.begin();
            it != ready_events.end(); it++)
        rez.serialize(*it);
      rez.serialize<size_t>(descriptors.size());
      for (std::vector<FieldDataDescriptor>::const_iterator it = 
            descriptors.begin(); it != descriptors.end(); it++)
        rez.serialize(*it);
    }

    //--------------------------------------------------------------------------
    void FieldDescriptorExchange::unpack_collective_stage(Deserializer &derez,
                                                          int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_events;
      derez.deserialize(num_events);
      for (unsigned idx = 0; idx < num_events; idx++)
      {
        ApEvent ready;
        derez.deserialize(ready);
        ready_events.insert(ready);
      }
      unsigned offset = descriptors.size();
      size_t num_descriptors;
      derez.deserialize(num_descriptors);
      descriptors.resize(offset + num_descriptors);
      for (unsigned idx = 0; idx < num_descriptors; idx++)
        derez.deserialize(descriptors[offset + idx]);
    }

    /////////////////////////////////////////////////////////////
    // Field Descriptor Gather 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FieldDescriptorGather::FieldDescriptorGather(ReplicateContext *ctx,
                             ShardID target, CollectiveIndexLocation loc)
      : GatherCollective(loc, ctx, target), used(false)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FieldDescriptorGather::FieldDescriptorGather(
                                               const FieldDescriptorGather &rhs)
      : GatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    FieldDescriptorGather::~FieldDescriptorGather(void)
    //--------------------------------------------------------------------------
    {
      // Make sure that we wait in case we still have messages to pass on
      if (used)
        perform_collective_wait();
    }

    //--------------------------------------------------------------------------
    FieldDescriptorGather& FieldDescriptorGather::operator=(
                                               const FieldDescriptorGather &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FieldDescriptorGather::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(ready_events.size());
      for (std::set<ApEvent>::const_iterator it = ready_events.begin();
            it != ready_events.end(); it++)
        rez.serialize(*it);
      rez.serialize<size_t>(descriptors.size());
      for (std::vector<FieldDataDescriptor>::const_iterator it = 
            descriptors.begin(); it != descriptors.end(); it++)
        rez.serialize(*it);
    }
    
    //--------------------------------------------------------------------------
    void FieldDescriptorGather::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      size_t num_events;
      derez.deserialize(num_events);
      for (unsigned idx = 0; idx < num_events; idx++)
      {
        ApEvent ready;
        derez.deserialize(ready);
        ready_events.insert(ready);
      }
      unsigned offset = descriptors.size();
      size_t num_descriptors;
      derez.deserialize(num_descriptors);
      descriptors.resize(offset + num_descriptors);
      for (unsigned idx = 0; idx < num_descriptors; idx++)
        derez.deserialize(descriptors[offset + idx]);
    }

    //--------------------------------------------------------------------------
    void FieldDescriptorGather::contribute(ApEvent ready_event,
                                  const std::vector<FieldDataDescriptor> &descs)
    //--------------------------------------------------------------------------
    {
      used = true;
      {
        AutoLock c_lock(collective_lock);
        ready_events.insert(ready_event);
        descriptors.insert(descriptors.end(), descs.begin(), descs.end());
      }
      perform_collective_async();
    }

    //--------------------------------------------------------------------------
    const std::vector<FieldDataDescriptor>& 
                     FieldDescriptorGather::get_full_descriptors(ApEvent &ready)
    //--------------------------------------------------------------------------
    {
      perform_collective_wait();
      ready = Runtime::merge_events(ready_events);
      return descriptors;
    }

    /////////////////////////////////////////////////////////////
    // Future Broadcast 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FutureBroadcast::FutureBroadcast(ReplicateContext *ctx, CollectiveID id,
                                     ShardID source)
      : BroadcastCollective(ctx, id, source), result(NULL), result_size(0)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FutureBroadcast::FutureBroadcast(const FutureBroadcast &rhs)
      : BroadcastCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    FutureBroadcast::~FutureBroadcast(void)
    //--------------------------------------------------------------------------
    {
      if (result != NULL)
        free(result);
    }

    //--------------------------------------------------------------------------
    FutureBroadcast& FutureBroadcast::operator=(const FutureBroadcast &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FutureBroadcast::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      rez.serialize(result_size);
      if (result_size > 0)
        rez.serialize(result, result_size);
    }

    //--------------------------------------------------------------------------
    void FutureBroadcast::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      derez.deserialize(result_size);
      if (result_size > 0)
      {
#ifdef DEBUG_LEGION
        assert(result == NULL);
#endif
        result = malloc(result_size);
        derez.deserialize(result, result_size);
      }
    }

    //--------------------------------------------------------------------------
    void FutureBroadcast::broadcast_future(const void *res, size_t size)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(result == NULL); 
#endif
      result_size = size;
      if (result_size > 0)
      {
        result = malloc(result_size);
        memcpy(result, res, result_size);
      }
      perform_collective_async();
    }

    //--------------------------------------------------------------------------
    void FutureBroadcast::receive_future(FutureImpl *f)
    //--------------------------------------------------------------------------
    {
      perform_collective_wait();
      if (result != NULL)
      {
        f->set_result(result, result_size, true/*own*/);
        result = NULL;
      }
      else
        f->set_result(NULL, 0, false/*own*/);
    }

    /////////////////////////////////////////////////////////////
    // Future Exchange 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FutureExchange::FutureExchange(ReplicateContext *ctx, size_t size,
                                   CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx), future_size(size)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FutureExchange::FutureExchange(const FutureExchange &rhs)
      : AllGatherCollective(rhs), future_size(0)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    FutureExchange::~FutureExchange(void)
    //--------------------------------------------------------------------------
    {
      // Delete all the futures except our local shard one since we know
      // that we don't actually own that memory
      for (std::map<ShardID,void*>::const_iterator it = results.begin();
            it != results.end(); it++)
        free(it->second);
    }

    //--------------------------------------------------------------------------
    FutureExchange& FutureExchange::operator=(const FutureExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FutureExchange::pack_collective_stage(Serializer &rez, int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(results.size());
      for (std::map<ShardID,void*>::const_iterator it = results.begin();
            it != results.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second, future_size);
      }
    }

    //--------------------------------------------------------------------------
    void FutureExchange::unpack_collective_stage(Deserializer &derez, int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_results;
      derez.deserialize(num_results);
      for (unsigned idx = 0; idx < num_results; idx++)
      {
        ShardID shard;
        derez.deserialize(shard);
        if (results.find(shard) != results.end())
        {
          derez.advance_pointer(future_size);
          continue;
        }
        void *buffer = malloc(future_size);
        derez.deserialize(buffer, future_size);
        results[shard] = buffer;
      }
    }

    //--------------------------------------------------------------------------
    void FutureExchange::reduce_futures(void *value, ReplIndexTask *target)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
#ifdef DEBUG_LEGION
        assert(results.find(local_shard) == results.end());
#endif
        results[local_shard] = value;
      }
      perform_collective_sync();
      // Now we apply the shard results in order to ensure that we get
      // the same bitwise order across all the shards
      // No need for the lock anymore since we know we're done
      for (std::map<ShardID,void*>::const_iterator it = results.begin();
            it != results.end(); it++)
        target->fold_reduction_future(it->second, future_size, 
                                      false/*owner*/, true/*exclusive*/);
    }

    /////////////////////////////////////////////////////////////
    // Future Name Exchange 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FutureNameExchange::FutureNameExchange(ReplicateContext *ctx,
                                           CollectiveID id)
      : AllGatherCollective(ctx, id)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FutureNameExchange::FutureNameExchange(const FutureNameExchange &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    FutureNameExchange::~FutureNameExchange(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    FutureNameExchange& FutureNameExchange::operator=(
                                                  const FutureNameExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FutureNameExchange::pack_collective_stage(Serializer &rez, 
                                                   int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(results.size());
      for (std::map<DomainPoint,Future>::const_iterator it = 
            results.begin(); it != results.end(); it++)
      {
        rez.serialize(it->first);
        if (it->second.impl != NULL)
          rez.serialize(it->second.impl->did);
        else
          rez.serialize<DistributedID>(0);
      }
    }

    //--------------------------------------------------------------------------
    void FutureNameExchange::unpack_collective_stage(Deserializer &derez,
                                                     int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_futures;
      derez.deserialize(num_futures);
      for (unsigned idx = 0; idx < num_futures; idx++)
      {
        DomainPoint point;
        derez.deserialize(point);
        DistributedID did;
        derez.deserialize(did);
        if (did > 0)
          results[point] = 
            Future(context->runtime->find_or_create_future(did, &mutator));
        else
          results[point] = Future();
      }
    }

    //--------------------------------------------------------------------------
    void FutureNameExchange::exchange_future_names(
                                          std::map<DomainPoint,Future> &futures)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
        results.insert(futures.begin(), futures.end());
      }
      perform_collective_sync();
      futures = results;
    }

    /////////////////////////////////////////////////////////////
    // Must Epoch Processor Broadcast 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MustEpochMappingBroadcast::MustEpochMappingBroadcast(
            ReplicateContext *ctx, ShardID origin, CollectiveID collective_id)
      : BroadcastCollective(ctx, collective_id, origin)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    MustEpochMappingBroadcast::MustEpochMappingBroadcast(
                                           const MustEpochMappingBroadcast &rhs)
      : BroadcastCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    MustEpochMappingBroadcast::~MustEpochMappingBroadcast(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_done_event.exists());
#endif
      if (!done_events.empty())
        Runtime::trigger_event(local_done_event,
            Runtime::merge_events(done_events));
      else
        Runtime::trigger_event(local_done_event);
      // This should only happen on the owner node
      if (!held_references.empty())
      {
        // Wait for all the other shards to be done
        local_done_event.lg_wait();
        // Now we can remove our held references
        for (std::set<PhysicalManager*>::const_iterator it = 
              held_references.begin(); it != held_references.end(); it++)
          if ((*it)->remove_base_valid_ref(REPLICATION_REF))
            delete (*it);
      }
    }
    
    //--------------------------------------------------------------------------
    MustEpochMappingBroadcast& MustEpochMappingBroadcast::operator=(
                                           const MustEpochMappingBroadcast &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingBroadcast::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      RtUserEvent next_done = Runtime::create_rt_user_event();
      done_events.insert(next_done);
      rez.serialize(next_done);
      rez.serialize<size_t>(processors.size());
      for (unsigned idx = 0; idx < processors.size(); idx++)
        rez.serialize(processors[idx]);
      rez.serialize<size_t>(instances.size());
      for (unsigned idx = 0; idx < instances.size(); idx++)
      {
        const std::vector<DistributedID> &dids = instances[idx];
        rez.serialize<size_t>(dids.size());
        for (std::vector<DistributedID>::const_iterator it = 
              dids.begin(); it != dids.end(); it++)
          rez.serialize(*it);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingBroadcast::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      derez.deserialize(local_done_event);
      size_t num_procs;
      derez.deserialize(num_procs);
      processors.resize(num_procs);
      for (unsigned idx = 0; idx < num_procs; idx++)
        derez.deserialize(processors[idx]);
      size_t num_constraints;
      derez.deserialize(num_constraints);
      instances.resize(num_constraints);
      for (unsigned idx1 = 0; idx1 < num_constraints; idx1++)
      {
        size_t num_dids;
        derez.deserialize(num_dids);
        std::vector<DistributedID> &dids = instances[idx1];
        dids.resize(num_dids);
        for (unsigned idx2 = 0; idx2 < num_dids; idx2++)
          derez.deserialize(dids[idx2]);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingBroadcast::broadcast(
           const std::vector<Processor> &processor_mapping,
           const std::vector<std::vector<Mapping::PhysicalInstance> > &mappings)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_done_event.exists());
#endif
      local_done_event = Runtime::create_rt_user_event();
      processors = processor_mapping;
      instances.resize(mappings.size());
      // Add valid references to all the physical instances that we will
      // hold until all the must epoch operations are done with the exchange
      WrapperReferenceMutator mutator(done_events);
      for (unsigned idx1 = 0; idx1 < mappings.size(); idx1++)
      {
        std::vector<DistributedID> &dids = instances[idx1];
        dids.resize(mappings[idx1].size());
        for (unsigned idx2 = 0; idx2 < dids.size(); idx2++)
        {
          const Mapping::PhysicalInstance &inst = mappings[idx1][idx2];
          dids[idx2] = inst.impl->did;
          if (held_references.find(inst.impl) != held_references.end())
            continue;
          inst.impl->add_base_valid_ref(REPLICATION_REF, &mutator);
          held_references.insert(inst.impl);
        }
      }
      perform_collective_async();
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingBroadcast::receive_results(
                std::vector<Processor> &processor_mapping,
                const std::vector<unsigned> &constraint_indexes,
                std::vector<std::vector<Mapping::PhysicalInstance> > &mappings,
                std::map<PhysicalManager*,std::pair<unsigned,bool> > &acquired)
    //--------------------------------------------------------------------------
    {
      perform_collective_wait();
      // Just grab all the processors since we still need them
      processor_mapping = processors;
      // We are a little smarter with the mappings since we know exactly
      // which ones we are actually going to need for our local points
      std::set<RtEvent> ready_events;
      Runtime *runtime = manager->runtime;
      for (std::vector<unsigned>::const_iterator it = 
            constraint_indexes.begin(); it != constraint_indexes.end(); it++)
      {
#ifdef DEBUG_LEGION
        assert((*it) < instances.size());
        assert((*it) < mappings.size());
#endif
        const std::vector<DistributedID> &dids = instances[*it];
        std::vector<Mapping::PhysicalInstance> &mapping = mappings[*it];
        mapping.resize(dids.size());
        for (unsigned idx = 0; idx < dids.size(); idx++)
        {
          RtEvent ready;
          mapping[idx].impl = 
            runtime->find_or_request_physical_manager(dids[idx], ready);
          if (!ready.has_triggered())
            ready_events.insert(ready);   
        }
      }
      // Have to wait for the ready events to trigger before we can add
      // our references safely
      if (!ready_events.empty())
      {
        RtEvent ready = Runtime::merge_events(ready_events);
        if (!ready.has_triggered())
          ready.lg_wait();
      }
      // Lastly we need to put acquire references on any of local instances
      WrapperReferenceMutator mutator(done_events);
      for (unsigned idx = 0; idx < constraint_indexes.size(); idx++)
      {
        const unsigned constraint_index = constraint_indexes[idx];
        const std::vector<Mapping::PhysicalInstance> &mapping = 
          mappings[constraint_index];
        // Also grab an acquired reference to these instances
        for (std::vector<Mapping::PhysicalInstance>::const_iterator it = 
              mapping.begin(); it != mapping.end(); it++)
        {
          // If we already had a reference to this instance
          // then we don't need to add any additional ones
          if (acquired.find(it->impl) != acquired.end())
            continue;
          it->impl->add_base_resource_ref(INSTANCE_MAPPER_REF);
          it->impl->add_base_valid_ref(MAPPING_ACQUIRE_REF, &mutator);
          acquired[it->impl] = 
            std::pair<unsigned,bool>(1/*count*/, false/*created*/);
        }
      }
    }

    /////////////////////////////////////////////////////////////
    // Must Epoch Mapping Exchange
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MustEpochMappingExchange::MustEpochMappingExchange(ReplicateContext *ctx,
                                                 CollectiveID collective_id)
      : AllGatherCollective(ctx, collective_id)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    MustEpochMappingExchange::MustEpochMappingExchange(
                                            const MustEpochMappingExchange &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    MustEpochMappingExchange::~MustEpochMappingExchange(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_done_event.exists()); // better have one of these
#endif
      Runtime::trigger_event(local_done_event);
      // See if we need to wait for others to be done before we can
      // remove our valid references
      if (!done_events.empty())
      {
        RtEvent done = Runtime::merge_events(done_events);
        if (!done.has_triggered())
          done.lg_wait();
      }
      // Now we can remove our held references
      for (std::set<PhysicalManager*>::const_iterator it = 
            held_references.begin(); it != held_references.end(); it++)
        if ((*it)->remove_base_valid_ref(REPLICATION_REF))
          delete (*it);
    }
    
    //--------------------------------------------------------------------------
    MustEpochMappingExchange& MustEpochMappingExchange::operator=(
                                            const MustEpochMappingExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingExchange::pack_collective_stage(Serializer &rez,
                                                         int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(processors.size());
      for (std::map<DomainPoint,Processor>::const_iterator it = 
            processors.begin(); it != processors.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second);
      }
      rez.serialize<size_t>(constraints.size());
      for (std::map<unsigned,ConstraintInfo>::const_iterator it = 
            constraints.begin(); it != constraints.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize<size_t>(it->second.instances.size());
        for (unsigned idx = 0; idx < it->second.instances.size(); idx++)
          rez.serialize(it->second.instances[idx]);
        rez.serialize(it->second.origin_shard);
        rez.serialize(it->second.weight);
      }
      rez.serialize<size_t>(done_events.size());
      for (std::set<RtEvent>::const_iterator it = 
            done_events.begin(); it != done_events.end(); it++)
        rez.serialize(*it);
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingExchange::unpack_collective_stage(Deserializer &derez,
                                                           int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_procs;
      derez.deserialize(num_procs);
      for (unsigned idx = 0; idx < num_procs; idx++)
      {
        DomainPoint point;
        derez.deserialize(point);
        derez.deserialize(processors[point]);
      }
      size_t num_mappings;
      derez.deserialize(num_mappings);
      for (unsigned idx1 = 0; idx1 < num_mappings; idx1++)
      {
        unsigned constraint_index;
        derez.deserialize(constraint_index);
        std::map<unsigned,ConstraintInfo>::iterator
          finder = constraints.find(constraint_index);
        if (finder == constraints.end())
        {
          // Can unpack directly since we're first
          ConstraintInfo &info = constraints[constraint_index];
          size_t num_dids;
          derez.deserialize(num_dids);
          info.instances.resize(num_dids);
          for (unsigned idx2 = 0; idx2 < num_dids; idx2++)
            derez.deserialize(info.instances[idx2]);
          derez.deserialize(info.origin_shard);
          derez.deserialize(info.weight);
        }
        else
        {
          // Unpack into a temporary
          ConstraintInfo info;
          size_t num_dids;
          derez.deserialize(num_dids);
          info.instances.resize(num_dids);
          for (unsigned idx2 = 0; idx2 < num_dids; idx2++)
            derez.deserialize(info.instances[idx2]);
          derez.deserialize(info.origin_shard);
          derez.deserialize(info.weight);
          // Only keep the result if we have a larger weight
          // or we have the same weight and a smaller shard
          if ((info.weight > finder->second.weight) ||
              ((info.weight == finder->second.weight) &&
               (info.origin_shard < finder->second.origin_shard)))
            finder->second = info;
        }
      }
      size_t num_done;
      derez.deserialize(num_done);
      for (unsigned idx = 0; idx < num_done; idx++)
      {
        RtEvent done_event;
        derez.deserialize(done_event);
        done_events.insert(done_event);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochMappingExchange::exchange_must_epoch_mappings(
                ShardID shard_id, size_t total_shards, size_t total_constraints,
                const std::vector<const Task*> &local_tasks,
                const std::vector<const Task*> &all_tasks,
                      std::vector<Processor> &processor_mapping,
                const std::vector<unsigned> &constraint_indexes,
                std::vector<std::vector<Mapping::PhysicalInstance> > &mappings,
                const std::vector<int> &mapping_weights,
                std::map<PhysicalManager*,std::pair<unsigned,bool> > &acquired)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_tasks.size() == processor_mapping.size());
      assert(constraint_indexes.size() == mappings.size());
#endif
      // Add valid references to all the physical instances that we will
      // hold until all the must epoch operations are done with the exchange
      WrapperReferenceMutator mutator(done_events);
      for (unsigned idx = 0; idx < mappings.size(); idx++)
      {
        for (std::vector<Mapping::PhysicalInstance>::const_iterator it = 
              mappings[idx].begin(); it != mappings[idx].end(); it++)
        {
          if (held_references.find(it->impl) != held_references.end())
            continue;
          it->impl->add_base_valid_ref(REPLICATION_REF, &mutator);
          held_references.insert(it->impl);
        }
      }
#ifdef DEBUG_LEGION
      assert(!local_done_event.exists());
#endif
      local_done_event = Runtime::create_rt_user_event();
      // Then we can add our instances to the set and do the exchange
      {
        AutoLock c_lock(collective_lock);
        for (unsigned idx = 0; idx < local_tasks.size(); idx++)
        {
          const Task *task = local_tasks[idx];
#ifdef DEBUG_LEGION
          assert(processors.find(task->index_point) == processors.end());
#endif
          processors[task->index_point] = processor_mapping[idx];
        }
        for (unsigned idx1 = 0; idx1 < mappings.size(); idx1++)
        {
          const unsigned constraint_index = constraint_indexes[idx1]; 
#ifdef DEBUG_LEGION
          assert(constraint_index < total_constraints);
#endif
          std::map<unsigned,ConstraintInfo>::iterator
            finder = constraints.find(constraint_index);
          // Only add it if it doesn't exist or it has a lower weight
          // or it has the same weight and is a lower shard
          if ((finder == constraints.end()) || 
              (mapping_weights[idx1] > finder->second.weight) ||
              ((mapping_weights[idx1] == finder->second.weight) &&
               (shard_id < finder->second.origin_shard)))
          {
            ConstraintInfo &info = constraints[constraint_index];
            info.instances.resize(mappings[idx1].size());
            for (unsigned idx2 = 0; idx2 < mappings[idx1].size(); idx2++)
              info.instances[idx2] = mappings[idx1][idx2].impl->did;
            info.origin_shard = shard_id;
            info.weight = mapping_weights[idx1];
          }
        }
        // Also update the local done events
        done_events.insert(local_done_event);
      }
      perform_collective_sync();
      // Start fetching the all the mapping results to get them in flight
      mappings.clear();
      mappings.resize(total_constraints);
      std::set<RtEvent> ready_events;
      Runtime *runtime = manager->runtime;
      // We only need to get the results for local constraints as we 
      // know that we aren't going to care about any of the rest
      for (unsigned idx1 = 0; idx1 < constraint_indexes.size(); idx1++)
      {
        const unsigned constraint_index = constraint_indexes[idx1];
        const std::vector<DistributedID> &dids = 
          constraints[constraint_index].instances;
        std::vector<Mapping::PhysicalInstance> &mapping = 
          mappings[constraint_index];
        mapping.resize(dids.size());
        for (unsigned idx2 = 0; idx2 < dids.size(); idx2++)
        {
          RtEvent ready;
          mapping[idx2].impl = 
            runtime->find_or_request_physical_manager(dids[idx2], ready);
          if (!ready.has_triggered())
            ready_events.insert(ready);   
        }
      }
      // Update the processor mapping
      processor_mapping.resize(all_tasks.size());
      for (unsigned idx = 0; idx < all_tasks.size(); idx++)
      {
        const Task *task = all_tasks[idx];
        std::map<DomainPoint,Processor>::const_iterator finder = 
          processors.find(task->index_point);
#ifdef DEBUG_LEGION
        assert(finder != processors.end());
#endif
        processor_mapping[idx] = finder->second;
      }
      // Wait for all the instances to be ready
      if (!ready_events.empty())
      {
        RtEvent ready = Runtime::merge_events(ready_events);
        if (!ready.has_triggered())
          ready.lg_wait();
      }
      // Lastly we need to put acquire references on any of local instances
      for (unsigned idx = 0; idx < constraint_indexes.size(); idx++)
      {
        const unsigned constraint_index = constraint_indexes[idx];
        const std::vector<Mapping::PhysicalInstance> &mapping = 
          mappings[constraint_index];
        // Also grab an acquired reference to these instances
        for (std::vector<Mapping::PhysicalInstance>::const_iterator it = 
              mapping.begin(); it != mapping.end(); it++)
        {
          // If we already had a reference to this instance
          // then we don't need to add any additional ones
          if (acquired.find(it->impl) != acquired.end())
            continue;
          it->impl->add_base_resource_ref(INSTANCE_MAPPER_REF);
          it->impl->add_base_valid_ref(MAPPING_ACQUIRE_REF, &mutator);
          acquired[it->impl] = 
            std::pair<unsigned,bool>(1/*count*/, false/*created*/);
        }
      }
    }

    /////////////////////////////////////////////////////////////
    // Must Epoch Dependence Exchange
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MustEpochDependenceExchange::MustEpochDependenceExchange(
                             ReplicateContext *ctx, CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    MustEpochDependenceExchange::MustEpochDependenceExchange(
                                         const MustEpochDependenceExchange &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    MustEpochDependenceExchange::~MustEpochDependenceExchange(void)
    //--------------------------------------------------------------------------
    {
    }
    
    //--------------------------------------------------------------------------
    MustEpochDependenceExchange& MustEpochDependenceExchange::operator=(
                                         const MustEpochDependenceExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void MustEpochDependenceExchange::pack_collective_stage(Serializer &rez,
                                                            int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(mapping_dependences.size());
      for (std::map<DomainPoint,RtUserEvent>::const_iterator it = 
            mapping_dependences.begin(); it != mapping_dependences.end(); it++)
      {
        rez.serialize(it->first);
        rez.serialize(it->second);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochDependenceExchange::unpack_collective_stage(
                                                 Deserializer &derez, int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_deps;
      derez.deserialize(num_deps);
      for (unsigned idx = 0; idx < num_deps; idx++)
      {
        DomainPoint point;
        derez.deserialize(point);
        derez.deserialize(mapping_dependences[point]);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochDependenceExchange::exchange_must_epoch_dependences(
                               std::map<DomainPoint,RtUserEvent> &mapped_events)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
        for (std::map<DomainPoint,RtUserEvent>::const_iterator it = 
              mapped_events.begin(); it != mapped_events.end(); it++)
          mapping_dependences.insert(*it);
      }
      perform_collective_sync();
      // No need to hold the lock after the collective is complete
      mapped_events.swap(mapping_dependences);
    }

    /////////////////////////////////////////////////////////////
    // Must Epoch Completion Exchange
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MustEpochCompletionExchange::MustEpochCompletionExchange(
                             ReplicateContext *ctx, CollectiveIndexLocation loc)
      : AllGatherCollective(loc, ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    MustEpochCompletionExchange::MustEpochCompletionExchange(
                                         const MustEpochCompletionExchange &rhs)
      : AllGatherCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    MustEpochCompletionExchange::~MustEpochCompletionExchange(void)
    //--------------------------------------------------------------------------
    {
    }
    
    //--------------------------------------------------------------------------
    MustEpochCompletionExchange& MustEpochCompletionExchange::operator=(
                                         const MustEpochCompletionExchange &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void MustEpochCompletionExchange::pack_collective_stage(Serializer &rez,
                                                            int stage) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(tasks_mapped.size());
      for (std::set<RtEvent>::const_iterator it = 
            tasks_mapped.begin(); it != tasks_mapped.end(); it++)
        rez.serialize(*it);
      rez.serialize<size_t>(tasks_complete.size());
      for (std::set<ApEvent>::const_iterator it = 
            tasks_complete.begin(); it != tasks_complete.end(); it++)
        rez.serialize(*it);
    }

    //--------------------------------------------------------------------------
    void MustEpochCompletionExchange::unpack_collective_stage(
                                                 Deserializer &derez, int stage)
    //--------------------------------------------------------------------------
    {
      size_t num_mapped;
      derez.deserialize(num_mapped);
      for (unsigned idx = 0; idx < num_mapped; idx++)
      {
        RtEvent mapped;
        derez.deserialize(mapped);
        tasks_mapped.insert(mapped);
      }
      size_t num_complete;
      derez.deserialize(num_complete);
      for (unsigned idx = 0; idx < num_complete; idx++)
      {
        ApEvent complete;
        derez.deserialize(complete);
        tasks_complete.insert(complete);
      }
    }

    //--------------------------------------------------------------------------
    void MustEpochCompletionExchange::exchange_must_epoch_completion(
                                RtEvent mapped, ApEvent complete,
                                std::set<RtEvent> &all_mapped,
                                std::set<ApEvent> &all_complete)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock c_lock(collective_lock);
        tasks_mapped.insert(mapped);
        tasks_complete.insert(complete);
      }
      perform_collective_sync();
      // No need to hold the lock after the collective is complete
      all_mapped.swap(tasks_mapped);
      all_complete.swap(tasks_complete);
    }

    /////////////////////////////////////////////////////////////
    // Versioning Info Broadcast 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    VersioningInfoBroadcast::VersioningInfoBroadcast(ReplicateContext *ctx,
                                                   CollectiveID id, ShardID own)
      : BroadcastCollective(ctx, id, own)
    //--------------------------------------------------------------------------
    {
      // If we own it then make our done event
      if (local_shard == origin)
        acknowledge_event = Runtime::create_rt_user_event();
    }

    //--------------------------------------------------------------------------
    VersioningInfoBroadcast::VersioningInfoBroadcast(
                                             const VersioningInfoBroadcast &rhs)
      : BroadcastCollective(rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    VersioningInfoBroadcast::~VersioningInfoBroadcast(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // Everybody should have a done event at this point
      assert(acknowledge_event.exists());
#endif
      if (!ack_preconditions.empty())
        Runtime::trigger_event(acknowledge_event, 
            Runtime::merge_events(ack_preconditions));
      else
        Runtime::trigger_event(acknowledge_event);
      // If we're the owner, we need to wait for all the triggers to 
      // happen and then we can remove our valid references 
      if ((local_shard == origin) && !held_references.empty())
      {
        acknowledge_event.lg_wait();
        for (std::set<VersionState*>::const_iterator it = 
              held_references.begin(); it != held_references.end(); it++)
          (*it)->remove_base_valid_ref(REPLICATION_REF);
      }
    }

    //--------------------------------------------------------------------------
    VersioningInfoBroadcast& VersioningInfoBroadcast::operator=(
                                             const VersioningInfoBroadcast &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::pack_collective(Serializer &rez) const
    //--------------------------------------------------------------------------
    {
      RtUserEvent precondition = Runtime::create_rt_user_event();
      rez.serialize(precondition);
      ack_preconditions.insert(precondition);
      rez.serialize<size_t>(versions.size());
      for (std::map<unsigned,LegionMap<DistributedID,FieldMask>::aligned>::
            const_iterator vit = versions.begin(); vit != versions.end(); vit++)
      {
        rez.serialize(vit->first);
        rez.serialize<size_t>(vit->second.size());
        for (LegionMap<DistributedID,FieldMask>::aligned::const_iterator it = 
              vit->second.begin(); it != vit->second.end(); it++)
        {
          rez.serialize(it->first);
          rez.serialize(it->second);
        }
      }
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::unpack_collective(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!acknowledge_event.exists());
      assert(versions.empty());
#endif
      derez.deserialize(acknowledge_event);
      common_unpack(derez); 
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::explicit_unpack(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // This better happen on the owner
      assert(local_shard == origin);
#endif
      common_unpack(derez);
      // Now record valid references on all our version state objects
      // Record a valid reference to all the version state objects
      // that we will hold until we get acknowledgements from all
      // the other shards that we will broadcast to
      std::set<RtEvent> reference_preconditions;
      for (std::map<unsigned,LegionMap<DistributedID,FieldMask>::aligned>::
            const_iterator vit = versions.begin(); vit != versions.end(); vit++)
      {
        for (LegionMap<DistributedID,FieldMask>::aligned::const_iterator it = 
              vit->second.begin(); it != vit->second.end(); it++)
        {
          RtEvent ready;
          VersionState *state = 
            context->runtime->find_or_request_version_state(it->first, ready);
          if (ready.exists())
            reference_preconditions.insert(ready);
          // Check to see if we already have a reference
          if (held_references.find(state) != held_references.end())
            continue;
          held_references.insert(state);
        }
      }
      if (!reference_preconditions.empty())
      {
        RtEvent wait_for = Runtime::merge_events(reference_preconditions);
        wait_for.lg_wait();
      }
      // Now we can add the references
      WrapperReferenceMutator mutator(ack_preconditions);
      for (std::set<VersionState*>::const_iterator it = 
            held_references.begin(); it != held_references.end(); it++)
        (*it)->add_base_valid_ref(REPLICATION_REF, &mutator);
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::common_unpack(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      size_t num_versions;
      derez.deserialize(num_versions);
      for (unsigned idx1 = 0; idx1 < num_versions; idx1++)
      {
        unsigned index;
        derez.deserialize(index);
        LegionMap<DistributedID,FieldMask>::aligned &target = versions[index];
        size_t num_states;
        derez.deserialize(num_states);
        for (unsigned idx2 = 0; idx2 < num_states; idx2++)
        {
          DistributedID did;
          derez.deserialize(did);
          derez.deserialize(target[did]);
        }
      }
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::pack_advance_states(unsigned index,
                                                const VersionInfo &version_info)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // We should be on the owner
      assert(local_shard == origin);
#endif
      LegionMap<DistributedID,FieldMask>::aligned &dids = versions[index];
      version_info.capture_base_advance_states(dids);
      // Record a valid reference to all the version state objects
      // that we will hold until we get acknowledgements from all
      // the other shards that we will broadcast to
      WrapperReferenceMutator mutator(ack_preconditions);
      for (LegionMap<DistributedID,FieldMask>::aligned::const_iterator it = 
            dids.begin(); it != dids.end(); it++)
      {
        // We know it already exists
#ifdef DEBUG_LEGION
        VersionState *state = dynamic_cast<VersionState*>(
            context->runtime->find_distributed_collectable(it->first));
        assert(state != NULL);
#else
        VersionState *state = static_cast<VersionState*>(
            context->runtime->find_distributed_collectable(it->first));
#endif
        // Check to see if we already have a reference
        if (held_references.find(state) != held_references.end())
          continue;
        state->add_base_valid_ref(REPLICATION_REF, &mutator);
        held_references.insert(state);
      }
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::wait_for_states(
                                              std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(get_done_event().has_triggered());
#endif
      std::set<RtEvent> wait_on;
      Runtime *runtime = context->runtime;
      // Now convert everything over to the results
      for (std::map<unsigned,LegionMap<DistributedID,FieldMask>::aligned>::
            const_iterator vit = versions.begin(); vit != versions.end(); vit++)
      {
        VersioningSet<> &target = results[vit->first];
        for (LegionMap<DistributedID,FieldMask>::aligned::const_iterator it = 
              vit->second.begin(); it != vit->second.end(); it++)
        {
          RtEvent ready;
          VersionState *state = 
            runtime->find_or_request_version_state(it->first, ready);
          ready = target.insert(state, it->second, runtime, ready);
          if (ready.exists() && !ready.has_triggered())
            wait_on.insert(ready);
        }
      }
      if (!wait_on.empty())
      {
        RtEvent wait_for = Runtime::merge_events(wait_on);
        wait_for.lg_wait();
      }
    }

    //--------------------------------------------------------------------------
    const VersioningSet<>& 
              VersioningInfoBroadcast::find_advance_states(unsigned index) const
    //--------------------------------------------------------------------------
    {
      LegionMap<unsigned,VersioningSet<> >::aligned::const_iterator finder = 
        results.find(index);
#ifdef DEBUG_LEGION
      assert(finder != results.end());
#endif
      return finder->second;
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::record_precondition(RtEvent precondition)
    //--------------------------------------------------------------------------
    {
      ack_preconditions.insert(precondition);
    }

    //--------------------------------------------------------------------------
    void VersioningInfoBroadcast::defer_perform_collective(Operation *op,
                                                           RtEvent precondition)
    //--------------------------------------------------------------------------
    {
      DeferVersionBroadcastArgs args;
      args.proxy_this = this;
      context->runtime->issue_runtime_meta_task(args, 
          LG_LATENCY_DEFERRED_PRIORITY, op, precondition);
    }

    //--------------------------------------------------------------------------
    /*static*/ void VersioningInfoBroadcast::handle_deferral(const void *args)
    //--------------------------------------------------------------------------
    {
      const DeferVersionBroadcastArgs *dargs = 
        (const DeferVersionBroadcastArgs*)args;
      dargs->proxy_this->perform_collective_async();
      delete dargs->proxy_this;
    }

  }; // namespace Internal
}; // namespace Legion

