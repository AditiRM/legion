/* Copyright 2022 Stanford University, NVIDIA Corporation
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

#include "legion.h"
#include "legion/runtime.h"
#include "legion/legion_ops.h"
#include "legion/legion_tasks.h"
#include "legion/region_tree.h"
#include "legion/legion_spy.h"
#include "legion/legion_profiling.h"
#include "legion/legion_instances.h"
#include "legion/legion_views.h"
#include "legion/legion_analysis.h"
#include "legion/legion_trace.h"
#include "legion/legion_context.h"
#include "legion/legion_replication.h"

namespace Legion {
  namespace Internal {

    LEGION_EXTERN_LOGGER_DECLARATIONS

    /////////////////////////////////////////////////////////////
    // LogicalView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    LogicalView::LogicalView(RegionTreeForest *ctx, DistributedID did,
                             AddressSpaceID own_addr, bool register_now,
                             CollectiveMapping *map)
      : DistributedCollectable(ctx->runtime, did, own_addr, register_now, map),
        context(ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    LogicalView::~LogicalView(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    /*static*/ void LogicalView::handle_view_request(Deserializer &derez,
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      DistributedCollectable *dc = runtime->find_distributed_collectable(did);
#ifdef DEBUG_LEGION
      LogicalView *view = dynamic_cast<LogicalView*>(dc);
      assert(view != NULL);
#else
      LogicalView *view = static_cast<LogicalView*>(dc);
#endif
      view->send_view(source);
    } 

    /////////////////////////////////////////////////////////////
    // InstanceView 
    ///////////////////////////////////////////////////////////// 

    //--------------------------------------------------------------------------
    InstanceView::InstanceView(RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID owner_sp, UniqueID own_ctx,
                               bool register_now, CollectiveMapping *mapping)
      : LogicalView(ctx, did, owner_sp, register_now, mapping),
        owner_context(own_ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    InstanceView::~InstanceView(void)
    //--------------------------------------------------------------------------
    { 
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void InstanceView::process_replication_request(AddressSpaceID source,
                                                  const FieldMask &request_mask,
                                                  RtUserEvent done_event)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }

    //--------------------------------------------------------------------------
    void InstanceView::process_replication_response(RtUserEvent done_event,
                                                    Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }

    //--------------------------------------------------------------------------
    void InstanceView::process_replication_removal(AddressSpaceID source,
                                                  const FieldMask &removal_mask)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }
#endif // ENABLE_VIEW_REPLICATION 

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_register_user(Deserializer &derez,
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);
      DistributedID target_did;
      derez.deserialize(target_did);
      RtEvent target_ready;
      PhysicalManager *target =
        runtime->find_or_request_instance_manager(target_did, target_ready);

      RegionUsage usage;
      derez.deserialize(usage);
      FieldMask user_mask;
      derez.deserialize(user_mask);
      IndexSpace handle;
      derez.deserialize(handle);
      IndexSpaceNode *user_expr = runtime->forest->get_node(handle);
      UniqueID op_id;
      derez.deserialize(op_id);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      ApEvent term_event;
      derez.deserialize(term_event);
      RtEvent collect_event;
      derez.deserialize(collect_event);
      size_t local_collective_arrivals;
      derez.deserialize(local_collective_arrivals);
      ApUserEvent ready_event;
      derez.deserialize(ready_event);
      RtUserEvent registered_event, applied_event;
      derez.deserialize(registered_event);
      derez.deserialize(applied_event);
      const PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      if (target_ready.exists() && !target_ready.has_triggered())
        target_ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      std::vector<RtEvent> registered_events;
      std::set<RtEvent> applied_events;
      ApEvent pre = inst_view->register_user(usage, user_mask, user_expr,
                                             op_id, op_ctx_index, index,
                                             term_event, collect_event, 
                                             target, NULL/*no mapping*/,
                                             local_collective_arrivals,
                                             registered_events, applied_events,
                                             trace_info, source);
      if (ready_event.exists())
        Runtime::trigger_event(&trace_info, ready_event, pre);
      if (!registered_events.empty())
        Runtime::trigger_event(registered_event,
            Runtime::merge_events(registered_events));
      else
        Runtime::trigger_event(registered_event);
      if (!applied_events.empty())
        Runtime::trigger_event(applied_event, 
            Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied_event);
    } 

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      FieldMask request_mask;
      derez.deserialize(request_mask);
      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_request(source, request_mask, done_event);
    }

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_response(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_response(done_event, derez);
      Runtime::trigger_event(done_event);
    }

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_removal(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      FieldMask removal_mask;
      derez.deserialize(removal_mask);
      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_removal(source, removal_mask);
      // Trigger the done event now that we are done
      Runtime::trigger_event(done_event);
    }
#endif // ENABLE_VIEW_REPLICATION

    /////////////////////////////////////////////////////////////
    // CollectableView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    void CollectableView::defer_collect_user(PhysicalManager *manager,
                 ApEvent term_event, RtEvent collect, ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // The runtime will add the gc reference to this view when necessary
      std::set<ApEvent> to_collect;
      bool add_ref = false;
      bool remove_ref = false;
      manager->defer_collect_user(this, term_event, collect,
                                  to_collect, add_ref, remove_ref);
      if (add_ref)
        add_collectable_reference(mutator);
      if (!to_collect.empty())
        collect_users(to_collect); 
      if (remove_ref && remove_collectable_reference(mutator))
        delete this;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectableView::handle_deferred_collect(
                                            CollectableView *view, 
                                            const std::set<ApEvent> &to_collect)
    //--------------------------------------------------------------------------
    {
      view->collect_users(to_collect);
      // Then remove the gc reference on the object
      if (view->remove_collectable_reference(NULL))
        delete view;
    }

    /////////////////////////////////////////////////////////////
    // ExprView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ExprView::ExprView(RegionTreeForest *ctx, PhysicalManager *man, 
                       MaterializedView *view, IndexSpaceExpression *exp) 
      : context(ctx), manager(man), inst_view(view),
        view_expr(exp), view_volume(SIZE_MAX),
#if defined(DEBUG_LEGION_GC) || defined(LEGION_GC)
        view_did(view->did),
#endif
        invalid_fields(FieldMask(LEGION_FIELD_MASK_FIELD_ALL_ONES))
    //--------------------------------------------------------------------------
    {
      view_expr->add_nested_expression_reference(view->did);
    }

    //--------------------------------------------------------------------------
    ExprView::~ExprView(void)
    //--------------------------------------------------------------------------
    {
#if defined(DEBUG_LEGION_GC) || defined(LEGION_GC)
      if (view_expr->remove_nested_expression_reference(view_did))
        delete view_expr;
#else
      // We can lie about the did here since its not actually used
      if (view_expr->remove_nested_expression_reference(0/*bogus did*/))
        delete view_expr;
#endif
      if (!subviews.empty())
      {
        for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
              it != subviews.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
      }
      // If we have any current or previous users filter them out now
      if (!current_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
        }
        current_epoch_users.clear();
      }
      if (!previous_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
        }
        previous_epoch_users.clear();
      }
    }

    //--------------------------------------------------------------------------
    size_t ExprView::get_view_volume(void)
    //--------------------------------------------------------------------------
    {
      size_t result = view_volume.load();
      if (result != SIZE_MAX)
        return result;
      result = view_expr->get_volume();
#ifdef DEBUG_LEGION
      assert(result != SIZE_MAX);
#endif
      view_volume.store(result);
      return result;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ExprView::verify_current_to_filter(
                 const FieldMask &dominated, EventFieldUsers &current_to_filter)
    //--------------------------------------------------------------------------
    {
      if (!!dominated)
      {
        for (EventFieldUsers::iterator eit = current_to_filter.begin();
              eit != current_to_filter.end(); /*nothing*/)
        {
          const FieldMask non_dominated = 
            eit->second.get_valid_mask() - dominated;
          // If everything was actually dominated we can keep going
          if (!non_dominated)
          {
            eit++;
            continue;
          }
          // If no fields were dominated we can just remove this
          if (non_dominated == eit->second.get_valid_mask())
          {
            EventFieldUsers::iterator to_delete = eit++;
            current_to_filter.erase(to_delete);
            continue;
          }
          // Otherwise do the actuall overlapping test
          std::vector<PhysicalUser*> to_delete; 
          for (FieldMaskSet<PhysicalUser>::iterator it =
                eit->second.begin(); it != eit->second.end(); it++)
          {
            it.filter(non_dominated);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!eit->second.tighten_valid_mask())
          {
            EventFieldUsers::iterator to_delete = eit++;
            current_to_filter.erase(to_delete);
          }
          else
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
              eit->second.erase(*it);
            eit++;
          }
        }
      }
      else
        current_to_filter.clear();
    } 

    //--------------------------------------------------------------------------
    void ExprView::find_user_preconditions(const RegionUsage &usage,
                                           IndexSpaceExpression *user_expr,
                                           const bool user_dominates,
                                           const FieldMask &user_mask,
                                           ApEvent term_event,
                                           UniqueID op_id, unsigned index,
                                           std::set<ApEvent> &preconditions,
                                           const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(Internal::implicit_runtime, 
                        MATERIALIZED_VIEW_FIND_LOCAL_PRECONDITIONS_CALL);
      FieldMask dominated;
      std::set<ApEvent> dead_events; 
      EventFieldUsers current_to_filter, previous_to_filter;
      // Perform the analysis with a read-only lock
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Check to see if we dominate when doing this analysis and
        // can therefore filter or whether we are just intersecting
        // Do the local analysis
        if (user_dominates)
        {
          // We dominate in this case so we can do filtering
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, user_mask, user_expr,
                                       term_event, op_id, index, 
                                       user_dominates, preconditions, 
                                       dead_events, current_to_filter, 
                                       observed, non_dominated,trace_recording);
            if (!!observed)
              dominated = observed - non_dominated;
          }
          if (!previous_epoch_users.empty())
          {
            if (!!dominated)
              find_previous_filter_users(dominated, previous_to_filter);
            const FieldMask previous_mask = user_mask - dominated;
            if (!!previous_mask)
              find_previous_preconditions(usage, previous_mask, user_expr,
                                          term_event, op_id, index,
                                          user_dominates, preconditions,
                                          dead_events, trace_recording);
          }
        }
        else
        {
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, user_mask, user_expr,
                                       term_event, op_id, index, 
                                       user_dominates, preconditions, 
                                       dead_events, current_to_filter, 
                                       observed, non_dominated,trace_recording);
#ifdef DEBUG_LEGION
            assert(!observed);
            assert(current_to_filter.empty());
#endif
          }
          if (!previous_epoch_users.empty())
            find_previous_preconditions(usage, user_mask, user_expr,
                                        term_event, op_id, index,
                                        user_dominates, preconditions,
                                        dead_events, trace_recording);
        }
      } 
      // It's possible that we recorded some users for fields which
      // are not actually fully dominated, if so we need to prune them
      // otherwise we can get into issues of soundness
      if (!current_to_filter.empty())
        verify_current_to_filter(dominated, current_to_filter);
      if (!trace_recording && (!dead_events.empty() || 
           !previous_to_filter.empty() || !current_to_filter.empty()))
      {
        // Need exclusive permissions to modify data structures
        AutoLock v_lock(view_lock);
        if (!dead_events.empty())
          for (std::set<ApEvent>::const_iterator it = dead_events.begin();
                it != dead_events.end(); it++)
            filter_local_users(*it); 
        if (!previous_to_filter.empty())
          filter_previous_users(previous_to_filter);
        if (!current_to_filter.empty())
          filter_current_users(current_to_filter);
      }
      // Then see if there are any users below that we need to traverse
      if (!subviews.empty() && 
          !(subviews.get_valid_mask() * user_mask))
      {
        FieldMaskSet<ExprView> to_traverse;
        std::map<ExprView*,IndexSpaceExpression*> traverse_exprs;
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & user_mask;
          if (!overlap)
            continue;
          // If we've already determined the user dominates
          // then we don't even have to do this test
          if (user_dominates)
          {
            to_traverse.insert(it->first, overlap);
            continue;
          }
          if (it->first->view_expr == user_expr)
          {
            to_traverse.insert(it->first, overlap);
            traverse_exprs[it->first] = user_expr;
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, user_expr);
          if (!expr_overlap->is_empty())
          {
            to_traverse.insert(it->first, overlap);
            traverse_exprs[it->first] = expr_overlap;
          }
        }
        if (!to_traverse.empty())
        {
          if (user_dominates)
          {
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  to_traverse.begin(); it != to_traverse.end(); it++)
              it->first->find_user_preconditions(usage, it->first->view_expr,
                                    true/*dominate*/, it->second, term_event,
                                    op_id, index,preconditions,trace_recording);
          }
          else
          {
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  to_traverse.begin(); it != to_traverse.end(); it++)
            {
              IndexSpaceExpression *intersect = traverse_exprs[it->first];
              const bool user_dominates = 
                (intersect->expr_id == it->first->view_expr->expr_id) ||
                (intersect->get_volume() == it->first->get_view_volume());
              it->first->find_user_preconditions(usage, intersect, 
                            user_dominates, it->second, term_event, 
                            op_id, index, preconditions, trace_recording);
            }
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_copy_preconditions(const RegionUsage &usage,
                                           IndexSpaceExpression *copy_expr,
                                           const bool copy_dominates,
                                           const FieldMask &copy_mask,
                                           UniqueID op_id, unsigned index,
                                           std::set<ApEvent> &preconditions,
                                           const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(Internal::implicit_runtime, 
                        MATERIALIZED_VIEW_FIND_LOCAL_COPY_PRECONDITIONS_CALL);
      FieldMask dominated;
      std::set<ApEvent> dead_events; 
      EventFieldUsers current_to_filter, previous_to_filter;
      // Do the first pass with a read-only lock on the events
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Check to see if we dominate when doing this analysis and
        // can therefore filter or whether we are just intersecting
        // Do the local analysis
        if (copy_dominates)
        {
          // We dominate in this case so we can do filtering
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, copy_mask, copy_expr, 
                                       op_id, index, copy_dominates,
                                       preconditions, dead_events, 
                                       current_to_filter, observed, 
                                       non_dominated, trace_recording);
            if (!!observed)
              dominated = observed - non_dominated;
          }
          if (!previous_epoch_users.empty())
          {
            if (!!dominated)
              find_previous_filter_users(dominated, previous_to_filter);
            const FieldMask previous_mask = copy_mask - dominated;
            if (!!previous_mask)
              find_previous_preconditions(usage, previous_mask,
                                          copy_expr, op_id, index,
                                          copy_dominates, preconditions,
                                          dead_events, trace_recording);
          }
        }
        else
        {
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, copy_mask, copy_expr,
                                       op_id, index, copy_dominates,
                                       preconditions, dead_events, 
                                       current_to_filter, observed, 
                                       non_dominated, trace_recording);
#ifdef DEBUG_LEGION
            assert(!observed);
            assert(current_to_filter.empty());
#endif
          }
          if (!previous_epoch_users.empty())
            find_previous_preconditions(usage, copy_mask, copy_expr,
                                        op_id, index, copy_dominates,
                                        preconditions, dead_events,
                                        trace_recording);
        }
      }
      // It's possible that we recorded some users for fields which
      // are not actually fully dominated, if so we need to prune them
      // otherwise we can get into issues of soundness
      if (!current_to_filter.empty())
        verify_current_to_filter(dominated, current_to_filter);
      if (!trace_recording && (!dead_events.empty() || 
           !previous_to_filter.empty() || !current_to_filter.empty()))
      {
        // Need exclusive permissions to modify data structures
        AutoLock v_lock(view_lock);
        if (!dead_events.empty())
          for (std::set<ApEvent>::const_iterator it = dead_events.begin();
                it != dead_events.end(); it++)
            filter_local_users(*it); 
        if (!previous_to_filter.empty())
          filter_previous_users(previous_to_filter);
        if (!current_to_filter.empty())
          filter_current_users(current_to_filter);
      }
      // Then see if there are any users below that we need to traverse
      if (!subviews.empty() && 
          !(subviews.get_valid_mask() * copy_mask))
      {
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & copy_mask;
          if (!overlap)
            continue;
          // If the copy dominates then we don't even have
          // to do the intersection test
          if (copy_dominates)
          {
            it->first->find_copy_preconditions(usage, it->first->view_expr,
                                    true/*dominate*/, overlap, op_id, index,
                                    preconditions, trace_recording);
            continue;
          }
          if (it->first->view_expr == copy_expr)
          {
            it->first->find_copy_preconditions(usage, copy_expr,
                                    true/*dominate*/, overlap, op_id, index,
                                    preconditions, trace_recording);
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, copy_expr);
          if (!expr_overlap->is_empty())
          {
            const bool copy_dominates = 
              (expr_overlap->expr_id == it->first->view_expr->expr_id) ||
              (expr_overlap->get_volume() == it->first->get_view_volume());
            it->first->find_copy_preconditions(usage, expr_overlap, 
                              copy_dominates, overlap, op_id, 
                              index, preconditions, trace_recording);
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_last_users(const RegionUsage &usage,
                                   IndexSpaceExpression *expr,
                                   const bool expr_dominates,
                                   const FieldMask &mask,
                                   std::set<ApEvent> &last_events) const
    //--------------------------------------------------------------------------
    {
      // See if there are any users below that we need to traverse
      if (!subviews.empty() && !(subviews.get_valid_mask() * mask))
      {
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & mask;
          if (!overlap)
            continue;
          // If the expr dominates then we don't even have
          // to do the intersection test
          if (expr_dominates)
          {
            it->first->find_last_users(usage, it->first->view_expr,
                            true/*dominate*/, overlap, last_events);
            continue;
          }
          if (it->first->view_expr == expr)
          {
            it->first->find_last_users(usage, expr,
                true/*dominate*/, overlap, last_events);
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, expr);
          if (!expr_overlap->is_empty())
          {
            const bool dominates = 
              (expr_overlap->expr_id == it->first->view_expr->expr_id) ||
              (expr_overlap->get_volume() == it->first->get_view_volume());
            it->first->find_last_users(usage, expr_overlap,
                          dominates, overlap, last_events); 
          }
        }
      }
      FieldMask dominated;
      // Now we can traverse at this level
      AutoLock v_lock(view_lock,1,false/*exclusive*/);
      // We dominate in this case so we can do filtering
      if (!current_epoch_users.empty())
      {
        FieldMask observed, non_dominated;
        find_current_preconditions(usage, mask, expr, 
                                   expr_dominates, last_events,
                                   observed, non_dominated);
        if (!!observed)
          dominated = observed - non_dominated;
      }
      if (!previous_epoch_users.empty())
      {
        const FieldMask previous_mask = mask - dominated;
        if (!!previous_mask)
          find_previous_preconditions(usage, previous_mask,
                                      expr, expr_dominates, last_events);
      }
    }

    //--------------------------------------------------------------------------
    ExprView* ExprView::find_congruent_view(IndexSpaceExpression *expr)
    //--------------------------------------------------------------------------
    {
      // Handle the base case first
      if ((expr == view_expr) || (expr->get_volume() == get_view_volume()))
        return const_cast<ExprView*>(this);
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        if (it->first->view_expr == expr)
          return it->first;
        IndexSpaceExpression *overlap =
          context->intersect_index_spaces(expr, it->first->view_expr);
        const size_t overlap_volume = overlap->get_volume();
        if (overlap_volume == 0)
          continue;
        // See if we dominate or just intersect
        if (overlap_volume == expr->get_volume())
        {
          // See if we strictly dominate or whether they are equal
          if (overlap_volume < it->first->get_view_volume())
          {
            ExprView *result = it->first->find_congruent_view(expr);
            if (result != NULL)
              return result;
          }
          else // Otherwise we're the same 
            return it->first;
        }
      }
      return NULL;
    }

    //--------------------------------------------------------------------------
    void ExprView::insert_subview(ExprView *subview, FieldMask &subview_mask)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(this != subview);
#endif
      // Iterate over all subviews and see which ones we dominate and which
      // ones dominate the subview
      if (!subviews.empty() && !(subviews.get_valid_mask() * subview_mask))
      {
        bool need_tighten = true;
        std::vector<ExprView*> to_delete;
        FieldMaskSet<ExprView> dominating_subviews;
        for (FieldMaskSet<ExprView>::iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          // See if we intersect on fields
          FieldMask overlap_mask = it->second & subview_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(subview->view_expr,
                                            it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == 0)
            continue;
          // See if we dominate or just intersect
          if (overlap_volume == subview->get_view_volume())
          {
#ifdef DEBUG_LEGION
            // Should only strictly dominate if they were congruent
            // then we wouldn't be inserting in the first place
            assert(overlap_volume < it->first->get_view_volume());
#endif
            // Dominator so we can just continue traversing
            dominating_subviews.insert(it->first, overlap_mask);
          }
          else if (overlap_volume == it->first->get_view_volume())
          {
#ifdef DEBUG_LEGION
            assert(overlap_mask * dominating_subviews.get_valid_mask());
#endif
            // We dominate this view so we can just pull it 
            // in underneath of us now
            it.filter(overlap_mask);
            subview->insert_subview(it->first, overlap_mask);
            need_tighten = true;
            // See if we need to remove this subview
            if (!it->second)
              to_delete.push_back(it->first);
          }
          // Otherwise it's just a normal intersection
        }
        // See if we had any dominators
        if (!dominating_subviews.empty())
        {
          if (dominating_subviews.size() > 1)
          {
            // We need to deduplicate finding or making the new ExprView
            // First check to see if we have it already in one sub-tree
            // If not, we'll pick the one with the smallest bounding volume
            LegionMap<std::pair<size_t/*volume*/,ExprView*>,FieldMask>
              sorted_subviews;
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  dominating_subviews.begin(); it != 
                  dominating_subviews.end(); it++)
            {
              FieldMask overlap = it->second;
              // Channeling Tuco here
              it->first->find_tightest_subviews(subview->view_expr, overlap,
                                                sorted_subviews);
            }
            for (LegionMap<std::pair<size_t,ExprView*>,FieldMask>::
                  const_iterator it = sorted_subviews.begin(); it !=
                  sorted_subviews.end(); it++)
            {
              FieldMask overlap = it->second & subview_mask;
              if (!overlap)
                continue;
              subview_mask -= overlap;
              it->first.second->insert_subview(subview, overlap);
              if (!subview_mask || 
                  (subview_mask * dominating_subviews.get_valid_mask()))
                break;
            }
#ifdef DEBUG_LEGION
            assert(subview_mask * dominating_subviews.get_valid_mask());
#endif
          }
          else
          {
            FieldMaskSet<ExprView>::const_iterator first = 
              dominating_subviews.begin();
            FieldMask dominated_mask = first->second; 
            subview_mask -= dominated_mask;
            first->first->insert_subview(subview, dominated_mask);
          }
        }
        if (!to_delete.empty())
        {
          for (std::vector<ExprView*>::const_iterator it = 
                to_delete.begin(); it != to_delete.end(); it++)
          {
            subviews.erase(*it);
            if ((*it)->remove_reference())
              delete (*it);
          }
        }
        if (need_tighten)
          subviews.tighten_valid_mask();
      }
      // If we make it here and there are still fields then we need to 
      // add it locally
      if (!!subview_mask && subviews.insert(subview, subview_mask))
        subview->add_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::find_tightest_subviews(IndexSpaceExpression *expr,
                                          FieldMask &expr_mask,
                                          LegionMap<std::pair<size_t,ExprView*>,
                                                     FieldMask> &bounding_views)
    //--------------------------------------------------------------------------
    {
      if (!subviews.empty() && !(expr_mask * subviews.get_valid_mask()))
      {
        FieldMask dominated_mask;
        for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
              it != subviews.end(); it++)
        {
          // See if we intersect on fields
          FieldMask overlap_mask = it->second & expr_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(expr, it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == 0)
            continue;
          // See if we dominate or just intersect
          if (overlap_volume == expr->get_volume())
          {
#ifdef DEBUG_LEGION
            // Should strictly dominate otherwise we'd be congruent
            assert(overlap_volume < it->first->get_view_volume());
#endif
            dominated_mask |= overlap_mask;
            // Continute the traversal
            it->first->find_tightest_subviews(expr,overlap_mask,bounding_views);
          }
        }
        // Remove any dominated fields from below
        if (!!dominated_mask)
          expr_mask -= dominated_mask;
      }
      // If we still have fields then record ourself
      if (!!expr_mask)
      {
        std::pair<size_t,ExprView*> key(get_view_volume(), this);
        bounding_views[key] |= expr_mask;
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::add_partial_user(const RegionUsage &usage,
                                    UniqueID op_id, unsigned index,
                                    FieldMask user_mask,
                                    const ApEvent term_event,
                                    const RtEvent collect_event,
                                    IndexSpaceExpression *user_expr,
                                    const size_t user_volume,
                                    const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // We're going to try to put this user as far down the ExprView tree
      // as we can in order to avoid doing unnecessary intersection tests later
      {
        // Find all the intersecting subviews to see if we can 
        // continue the traversal
        // No need for the view lock anymore since we're protected
        // by the expr_lock at the top of the tree
        //AutoLock v_lock(view_lock,1,false/*exclusive*/); 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          // If the fields don't overlap then we don't care
          const FieldMask overlap_mask = it->second & user_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(user_expr, it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == user_volume)
          {
            // Check for the cases where we dominated perfectly
            if (overlap_volume == it->first->view_volume)
            {
#ifdef ENABLE_VIEW_REPLICATION
              PhysicalUser *dominate_user = new PhysicalUser(usage,
                  it->first->view_expr, op_id, index, collect_event,
                  true/*copy*/, true/*covers*/);
#else
              PhysicalUser *dominate_user = new PhysicalUser(usage,
                  it->first->view_expr,op_id,index,true/*copy*/,true/*covers*/);
#endif
              it->first->add_current_user(dominate_user, term_event, 
                      collect_event, overlap_mask, trace_recording);
            }
            else
            {
              // Continue the traversal on this node
              it->first->add_partial_user(usage, op_id, index, overlap_mask,
                                          term_event, collect_event, user_expr,
                                          user_volume, trace_recording);
            }
            // We only need to record the partial user in one sub-tree
            // where it is dominated in order to be sound
            user_mask -= overlap_mask;
            if (!user_mask)
              break;
          }
          // Otherwise for all other cases we're going to record it here
          // because they don't dominate the user to be recorded
        }
      }
      // If we still have local fields, make a user and record it here
      if (!!user_mask)
      {
#ifdef ENABLE_VIEW_REPLICATION
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index,
                                collect_event, true/*copy*/, false/*covers*/);
#else
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index,
                                              true/*copy*/, false/*covers*/);
#endif
        add_current_user(user, term_event, collect_event, 
                         user_mask, trace_recording);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::add_current_user(PhysicalUser *user,const ApEvent term_event,
                              RtEvent collect_event, const FieldMask &user_mask,
                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      bool issue_collect = true;
      {
        AutoLock v_lock(view_lock);
        EventUsers &event_users = current_epoch_users[term_event];
        if (event_users.insert(user, user_mask))
          user->add_reference();
        else
          issue_collect = false;
      }
      if (issue_collect)
        defer_collect_user(manager, term_event, collect_event);
    }

    //--------------------------------------------------------------------------
    void ExprView::clean_views(FieldMask &valid_mask, 
                               FieldMaskSet<ExprView> &clean_set)
    //--------------------------------------------------------------------------
    {
      // Handle the base case if we already did it
      FieldMaskSet<ExprView>::const_iterator finder = clean_set.find(this);
      if (finder != clean_set.end())
      {
        valid_mask = finder->second;
        return;
      }
      // No need to hold the lock for this part we know that no one
      // is going to be modifying this data structure at the same time
      FieldMaskSet<ExprView> new_subviews;
      std::vector<ExprView*> to_delete;
      for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
            it != subviews.end(); it++)
      {
        FieldMask new_mask;
        it->first->clean_views(new_mask, clean_set);
        // Save this as part of the valid mask without filtering
        valid_mask |= new_mask;
        // Have to make sure to filter this by the previous set of fields 
        // since we could get more than we initially had
        // We also need update the invalid fields if we remove a path
        // to the subview
        if (!!new_mask)
        {
          new_mask &= it->second;
          const FieldMask new_invalid = it->second - new_mask;
          if (!!new_invalid)
          {
#ifdef DEBUG_LEGION
            // Should only have been one path here
            assert(it->first->invalid_fields * new_invalid);
#endif
            it->first->invalid_fields |= new_invalid;
          }
        }
        else
        {
#ifdef DEBUG_LEGION
          // Should only have been one path here
          assert(it->first->invalid_fields * it->second);
#endif
          it->first->invalid_fields |= it->second;
        }
        if (!!new_mask)
          new_subviews.insert(it->first, new_mask);
        else
          to_delete.push_back(it->first);
      }
      subviews.swap(new_subviews);
      if (!to_delete.empty())
      {
        for (std::vector<ExprView*>::const_iterator it = 
              to_delete.begin(); it != to_delete.end(); it++)
          if ((*it)->remove_reference())
            delete (*it);
      }
      AutoLock v_lock(view_lock);
      if (!current_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator it = 
              current_epoch_users.begin(); it != 
              current_epoch_users.end(); it++)
          valid_mask |= it->second.get_valid_mask();
      }
      if (!previous_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator it = 
              previous_epoch_users.begin(); it != 
              previous_epoch_users.end(); it++)
          valid_mask |= it->second.get_valid_mask();
      }
      // Save this for the future so we don't need to compute it again
      if (clean_set.insert(this, valid_mask))
        add_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::pack_replication(Serializer &rez,
                                    std::map<PhysicalUser*,unsigned> &indexes,
                                    const FieldMask &pack_mask,
                                    const AddressSpaceID target) const
    //--------------------------------------------------------------------------
    {
      RezCheck z(rez);
      {
        // Need a read-only lock here to protect against garbage collection
        // tasks coming back through and pruning out current epoch users
        // but we know there are no other modifications happening in parallel
        // because the replicated lock at the top prevents any new users
        // from being added while we're doing this pack
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Pack the current users
        EventFieldUsers needed_current; 
        for (EventFieldUsers::const_iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * pack_mask)
            continue;
          FieldMaskSet<PhysicalUser> &needed = needed_current[eit->first];
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            const FieldMask overlap = it->second & pack_mask;
            if (!overlap)
              continue;
            needed.insert(it->first, overlap);
          }
        }
        rez.serialize<size_t>(needed_current.size());
        for (EventFieldUsers::const_iterator eit = needed_current.begin();
              eit != needed_current.end(); eit++)
        {
          rez.serialize(eit->first);
          rez.serialize<size_t>(eit->second.size());
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            // See if we already packed this before or not
            std::map<PhysicalUser*,unsigned>::const_iterator finder = 
              indexes.find(it->first);
            if (finder == indexes.end())
            {
              const unsigned index = indexes.size();
              rez.serialize(index);
              it->first->pack_user(rez, target);
              indexes[it->first] = index;
            }
            else
              rez.serialize(finder->second);
            rez.serialize(it->second);
          }
        }
        // Pack the previous users
        EventFieldUsers needed_previous; 
        for (EventFieldUsers::const_iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * pack_mask)
            continue;
          FieldMaskSet<PhysicalUser> &needed = needed_previous[eit->first];
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            const FieldMask overlap = it->second & pack_mask;
            if (!overlap)
              continue;
            needed.insert(it->first, overlap);
          }
        }
        rez.serialize<size_t>(needed_previous.size());
        for (EventFieldUsers::const_iterator eit = needed_previous.begin();
              eit != needed_previous.end(); eit++)
        {
          rez.serialize(eit->first);
          rez.serialize<size_t>(eit->second.size());
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            // See if we already packed this before or not
            std::map<PhysicalUser*,unsigned>::const_iterator finder = 
              indexes.find(it->first);
            if (finder == indexes.end())
            {
              const unsigned index = indexes.size();
              rez.serialize(index);
              it->first->pack_user(rez, target);
              indexes[it->first] = index;
            }
            else
              rez.serialize(finder->second);
            rez.serialize(it->second);
          }
        }
      }
      // Pack the needed subviews no need for a lock here
      // since we know that we're protected by the expr_lock
      // at the top of the tree
      FieldMaskSet<ExprView> needed_subviews;
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        const FieldMask overlap = it->second & pack_mask;
        if (!overlap)
          continue;
        needed_subviews.insert(it->first, overlap);
      }
      rez.serialize<size_t>(needed_subviews.size());
      for (FieldMaskSet<ExprView>::const_iterator it = 
            needed_subviews.begin(); it != needed_subviews.end(); it++)
      {
        it->first->view_expr->pack_expression(rez, target);
        rez.serialize(it->second);
        it->first->pack_replication(rez, indexes, it->second, target);
      }
    }
    
    //--------------------------------------------------------------------------
    void ExprView::unpack_replication(Deserializer &derez, ExprView *root,
                              const AddressSpaceID source,
                              std::map<IndexSpaceExprID,ExprView*> &expr_cache,
                              std::vector<PhysicalUser*> &users)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      std::map<ApEvent,RtEvent> to_collect;
      // Need a read-write lock since we're going to be mutating the structures
      {
        AutoLock v_lock(view_lock);
        size_t num_current;
        derez.deserialize(num_current);
        for (unsigned idx1 = 0; idx1 < num_current; idx1++)
        {
          ApEvent user_event;
          derez.deserialize(user_event);
          FieldMaskSet<PhysicalUser> &current_users = 
            current_epoch_users[user_event];
#ifndef ENABLE_VIEW_REPLICATION
          if (current_users.empty())
            to_collect[user_event] = RtEvent::NO_RT_EVENT;
#endif
          size_t num_users;
          derez.deserialize(num_users);
          for (unsigned idx2 = 0; idx2 < num_users; idx2++)
          {
            unsigned user_index;
            derez.deserialize(user_index);
            if (user_index >= users.size())
            {
#ifdef DEBUG_LEGION
              assert(user_index == users.size());
#endif
              users.push_back(PhysicalUser::unpack_user(derez, context,source));
              // Add a reference to prevent this being deleted
              // before we're done unpacking
              users.back()->add_reference();
#ifdef ENABLE_VIEW_REPLICATION
              to_collect[user_event] = users.back()->collect_event;
#endif
            }
            FieldMask user_mask;
            derez.deserialize(user_mask);
            if (current_users.insert(users[user_index], user_mask))
              users[user_index]->add_reference();
          }
        }
        size_t num_previous;
        derez.deserialize(num_previous);
        for (unsigned idx1 = 0; idx1 < num_previous; idx1++)
        {
          ApEvent user_event;
          derez.deserialize(user_event);
          FieldMaskSet<PhysicalUser> &previous_users = 
            previous_epoch_users[user_event];
#ifndef ENABLE_VIEW_REPLICATION
          if (previous_users.empty())
            to_collect[user_event] = RtEvent::NO_RT_EVENT;
#endif
          size_t num_users;
          derez.deserialize(num_users);
          for (unsigned idx2 = 0; idx2 < num_users; idx2++)
          {
            unsigned user_index;
            derez.deserialize(user_index);
            if (user_index >= users.size())
            {
#ifdef DEBUG_LEGION
              assert(user_index == users.size());
#endif
              users.push_back(PhysicalUser::unpack_user(derez, context,source));
              // Add a reference to prevent this being deleted
              // before we're done unpacking
              users.back()->add_reference();
#ifdef ENABLE_VIEW_REPLICATION
              to_collect[user_event] = users.back()->collect_event;
#endif
            }
            FieldMask user_mask;
            derez.deserialize(user_mask);
            if (previous_users.insert(users[user_index], user_mask))
              users[user_index]->add_reference();
          }
        }
      }
      size_t num_subviews;
      derez.deserialize(num_subviews);
      if (num_subviews > 0)
      {
        for (unsigned idx = 0; idx < num_subviews; idx++)
        {
          IndexSpaceExpression *subview_expr = 
            IndexSpaceExpression::unpack_expression(derez, context, source);
          FieldMask subview_mask;
          derez.deserialize(subview_mask);
          // See if we already have it in the cache
          std::map<IndexSpaceExprID,ExprView*>::const_iterator finder = 
            expr_cache.find(subview_expr->expr_id);
          ExprView *subview = NULL;
          if (finder == expr_cache.end())
          {
            // See if we can find this view in the tree before we make it
            subview = root->find_congruent_view(subview_expr);
            // If it's still NULL then we can make it
            if (subview == NULL)
              subview = new ExprView(context, manager, inst_view, subview_expr);
            expr_cache[subview_expr->expr_id] = subview;
          }
          else
            subview = finder->second;
#ifdef DEBUG_LEGION
          assert(subview != NULL);
#endif
          // Check to see if it needs to be inserted
          if (subview != root)
          {
            FieldMask insert_mask = subview->invalid_fields & subview_mask;
            if (!!insert_mask)
            {
              subview->invalid_fields -= insert_mask;
              root->insert_subview(subview, insert_mask);
            }
          }
          // Continue the unpacking
          subview->unpack_replication(derez, root, source, expr_cache, users);
        }
      }
      if (!to_collect.empty())
      {
        for (std::map<ApEvent,RtEvent>::const_iterator it = 
              to_collect.begin(); it != to_collect.end(); it++)
          defer_collect_user(manager, it->first, it->second);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::deactivate_replication(const FieldMask &deactivate_mask)
    //--------------------------------------------------------------------------
    {
      // Traverse any subviews and do the deactivates in those nodes first
      // No need to get the lock here since we're protected by the 
      // exclusive expr_lock at the top of the tree
      // Don't worry about pruning, when we clean the cache after doing
      // this pass then that will also go through and prune out any 
      // expr views which no longer have users in any subtrees
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        const FieldMask overlap = it->second & deactivate_mask;
        if (!overlap)
          continue;
        it->first->deactivate_replication(overlap);
      }
      // Need a read-write lock since we're going to be mutating the structures
      AutoLock v_lock(view_lock);
      // Prune out the current epoch users
      if (!current_epoch_users.empty())
      {
        std::vector<ApEvent> events_to_delete;
        for (EventFieldUsers::iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * deactivate_mask)
            continue;
          bool need_tighten = false;
          std::vector<PhysicalUser*> to_delete;
          for (FieldMaskSet<PhysicalUser>::iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            if (it->second * deactivate_mask)
              continue;
            need_tighten = true;
            it.filter(deactivate_mask);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!to_delete.empty())
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
            {
              eit->second.erase(*it);
              if ((*it)->remove_reference())
                delete (*it);
            }
            if (eit->second.empty())
            {
              events_to_delete.push_back(eit->first);
              continue;
            }
          }
          if (need_tighten)
            eit->second.tighten_valid_mask();
        }
        if (!events_to_delete.empty())
        {
          for (std::vector<ApEvent>::const_iterator it = 
                events_to_delete.begin(); it != events_to_delete.end(); it++)
            current_epoch_users.erase(*it);
        }
      }
      // Prune out the previous epoch users
      if (!previous_epoch_users.empty())
      {
        std::vector<ApEvent> events_to_delete;
        for (EventFieldUsers::iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * deactivate_mask)
            continue;
          bool need_tighten = false;
          std::vector<PhysicalUser*> to_delete;
          for (FieldMaskSet<PhysicalUser>::iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            if (it->second * deactivate_mask)
              continue;
            need_tighten = true;
            it.filter(deactivate_mask);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!to_delete.empty())
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
            {
              eit->second.erase(*it);
              if ((*it)->remove_reference())
                delete (*it);
            }
            if (eit->second.empty())
            {
              events_to_delete.push_back(eit->first);
              continue;
            }
          }
          if (need_tighten)
            eit->second.tighten_valid_mask();
        }
        if (!events_to_delete.empty())
        {
          for (std::vector<ApEvent>::const_iterator it = 
                events_to_delete.begin(); it != events_to_delete.end(); it++)
            previous_epoch_users.erase(*it);
        }
      } 
    }

    //--------------------------------------------------------------------------
    void ExprView::add_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      add_reference();
      // Only the logical owner adds the full GC reference as this is where
      // the actual garbage collection algorithm will take place and we know
      // that we have all the valid gc event users
      if (inst_view->is_logical_owner())
        inst_view->add_base_gc_ref(PENDING_GC_REF, mutator);
      else
        inst_view->add_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    bool ExprView::remove_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (inst_view->is_logical_owner())
      {
        if (inst_view->remove_base_gc_ref(PENDING_GC_REF, mutator))
          delete inst_view;
      }
      else
      {
        if (inst_view->remove_base_resource_ref(PENDING_GC_REF))
          delete inst_view;
      }
      return remove_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::collect_users(const std::set<ApEvent> &to_collect)
    //--------------------------------------------------------------------------
    {
      AutoLock v_lock(view_lock);
      for (std::set<ApEvent>::const_iterator it = 
            to_collect.begin(); it != to_collect.end(); it++)
        filter_local_users(*it);
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_local_users(ApEvent term_event) 
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      DETAILED_PROFILER(context->runtime, 
                        MATERIALIZED_VIEW_FILTER_LOCAL_USERS_CALL);
      // Don't do this if we are in Legion Spy since we want to see
      // all of the dependences on an instance
#ifndef LEGION_DISABLE_EVENT_PRUNING
      EventFieldUsers::iterator current_finder = 
        current_epoch_users.find(term_event);
      if (current_finder != current_epoch_users.end())
      {
        for (EventUsers::const_iterator it = current_finder->second.begin();
              it != current_finder->second.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
        current_epoch_users.erase(current_finder);
      }
      LegionMap<ApEvent,EventUsers>::iterator previous_finder = 
        previous_epoch_users.find(term_event);
      if (previous_finder != previous_epoch_users.end())
      {
        for (EventUsers::const_iterator it = previous_finder->second.begin();
              it != previous_finder->second.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
        previous_epoch_users.erase(previous_finder);
      }
#endif
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_current_users(const EventFieldUsers &to_filter)
    //--------------------------------------------------------------------------
    {
      // Lock needs to be held by caller 
      for (EventFieldUsers::const_iterator fit = to_filter.begin();
            fit != to_filter.end(); fit++)
      {
        EventFieldUsers::iterator event_finder = 
          current_epoch_users.find(fit->first);
        // If it's already been pruned out then either it was filtered
        // because it finished or someone else moved it already, either
        // way we don't need to do anything about it
        if (event_finder == current_epoch_users.end())
          continue;
        EventFieldUsers::iterator target_finder = 
          previous_epoch_users.find(fit->first);
        for (EventUsers::const_iterator it = fit->second.begin();
              it != fit->second.end(); it++)
        {
          EventUsers::iterator finder = event_finder->second.find(it->first);
          // Might already have been pruned out again, either way there is
          // nothing for us to do here if it was already moved
          if (finder == event_finder->second.end())
            continue;
          const FieldMask overlap = finder->second & it->second;
          if (!overlap)
            continue;
          finder.filter(overlap);
          bool needs_reference = true;
          if (!finder->second)
          {
            // Have the reference flow back with the user
            needs_reference = false;
            event_finder->second.erase(finder);
          }
          // Now add the user to the previous set
          if (target_finder == previous_epoch_users.end())
          {
            if (needs_reference)
              it->first->add_reference();
            previous_epoch_users[fit->first].insert(it->first, overlap);
            target_finder = previous_epoch_users.find(fit->first);
          }
          else
          {
            if (target_finder->second.insert(it->first, overlap))
            {
              // Added a new user to the previous users
              if (needs_reference)
                it->first->add_reference();
            }
            else
            {
              // Remove any extra references we might be trying to send back
              if (!needs_reference && it->first->remove_reference())
                delete it->first;
            }
          }
        }
        if (event_finder->second.empty())
          current_epoch_users.erase(event_finder);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_previous_users(const EventFieldUsers &to_filter)
    //--------------------------------------------------------------------------
    {
      // Lock needs to be held by caller
      for (EventFieldUsers::const_iterator fit = to_filter.begin();
            fit != to_filter.end(); fit++)
      {
        EventFieldUsers::iterator event_finder = 
          previous_epoch_users.find(fit->first);
        // Might already have been pruned out
        if (event_finder == previous_epoch_users.end())
          continue;
        for (EventUsers::const_iterator it = fit->second.begin();
              it != fit->second.end(); it++)
        {
          EventUsers::iterator finder = event_finder->second.find(it->first);
          // Might already have been pruned out again
          if (finder == event_finder->second.end())
            continue;
          finder.filter(it->second);
          if (!finder->second)
          {
            if (finder->first->remove_reference())
              delete finder->first;
            event_finder->second.erase(finder);
          }
        }
        if (event_finder->second.empty())
          previous_epoch_users.erase(event_finder);
      }
    } 

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &user_mask,
                                              IndexSpaceExpression *user_expr,
                                              ApEvent term_event,
                                              const UniqueID op_id,
                                              const unsigned index,
                                              const bool user_covers,
                                              std::set<ApEvent> &preconditions,
                                              std::set<ApEvent> &dead_events,
                                              EventFieldUsers &filter_users,
                                              FieldMask &observed,
                                              FieldMask &non_dominated,
                                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
        if (cit->first == term_event)
          continue;
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && cit->first.has_triggered_faultignorant())
        {
          dead_events.insert(cit->first);
          continue;
        }
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording &&
            preconditions.find(cit->first) != preconditions.end())
          continue;
#endif
#endif
        const EventUsers &event_users = cit->second;
        const FieldMask overlap = event_users.get_valid_mask() & user_mask;
        if (!overlap)
          continue;
        EventFieldUsers::iterator to_filter = filter_users.find(cit->first);
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = user_mask & it->second;
          if (!user_overlap)
            continue;
          bool dominates = true;
          if (has_local_precondition<false>(it->first, usage, user_expr,
                                      op_id, index, user_covers, dominates))
          {
            preconditions.insert(cit->first);
            if (dominates)
            {
              observed |= user_overlap;
              if (to_filter == filter_users.end())
              {
                filter_users[cit->first].insert(it->first, user_overlap);
                to_filter = filter_users.find(cit->first);
              }
              else
              {
#ifdef DEBUG_LEGION
                assert(to_filter->second.find(it->first) == 
                        to_filter->second.end());
#endif
                to_filter->second.insert(it->first, user_overlap);
              }
            }
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               ApEvent term_event,
                                               const UniqueID op_id,
                                               const unsigned index,
                                               const bool user_covers,
                                               std::set<ApEvent> &preconditions,
                                               std::set<ApEvent> &dead_events,
                                               const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator pit = previous_epoch_users.begin(); 
            pit != previous_epoch_users.end(); pit++)
      {
        if (pit->first == term_event)
          continue;
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && pit->first.has_triggered_faultignorant())
        {
          dead_events.insert(pit->first);
          continue;
        }
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording &&
            preconditions.find(pit->first) != preconditions.end())
          continue;
#endif
#endif
        const EventUsers &event_users = pit->second;
        if (user_mask * event_users.get_valid_mask())
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          if (user_mask * it->second)
            continue;
          if (has_local_precondition<false>(it->first, usage, user_expr,
                                                op_id, index, user_covers))
          {
            preconditions.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &user_mask,
                                              IndexSpaceExpression *user_expr,
                                              const UniqueID op_id,
                                              const unsigned index,
                                              const bool user_covers,
                                              std::set<ApEvent> &preconditions,
                                              std::set<ApEvent> &dead_events,
                                              EventFieldUsers &filter_events,
                                              FieldMask &observed,
                                              FieldMask &non_dominated,
                                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && cit->first.has_triggered_faultignorant())
        {
          dead_events.insert(cit->first);
          continue;
        }
#endif
        const EventUsers &event_users = cit->second;
        FieldMask overlap = event_users.get_valid_mask() & user_mask;
        if (!overlap)
          continue;
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording && finder != preconditions.end())
        {
          overlap -= finder->second.get_valid_mask();
          if (!overlap)
            continue;
        }
#endif
        EventFieldUsers::iterator to_filter = filter_events.find(cit->first);
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = user_mask & it->second;
          if (!user_overlap)
            continue;
          bool dominated = true;
          if (has_local_precondition<true>(it->first, usage, user_expr,
                                 op_id, index, user_covers, dominated)) 
          {
            preconditions.insert(cit->first);
            if (dominated)
            {
              observed |= user_overlap;
              if (to_filter == filter_events.end())
              {
                filter_events[cit->first].insert(it->first, user_overlap);
                to_filter = filter_events.find(cit->first);
              }
              else
                to_filter->second.insert(it->first, user_overlap);
            }
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               const UniqueID op_id,
                                               const unsigned index,
                                               const bool user_covers,
                                               std::set<ApEvent> &preconditions,
                                               std::set<ApEvent> &dead_events,
                                               const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (LegionMap<ApEvent,EventUsers>::const_iterator pit = 
            previous_epoch_users.begin(); pit != 
            previous_epoch_users.end(); pit++)
      {
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && pit->first.has_triggered_faultignorant())
        {
          dead_events.insert(pit->first);
          continue;
        }
#endif
        const EventUsers &event_users = pit->second;
        FieldMask overlap = user_mask & event_users.get_valid_mask();
        if (!overlap)
          continue;
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording && finder != preconditions.end())
        {
          overlap -= finder->second.get_valid_mask();
          if (!overlap)
            continue;
        }
#endif
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = overlap & it->second;
          if (!user_overlap)
            continue;
          if (has_local_precondition<true>(it->first, usage, user_expr, 
                                           op_id, index, user_covers))
          {
            preconditions.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &mask,
                                              IndexSpaceExpression *expr,
                                              const bool expr_covers,
                                              std::set<ApEvent> &last_events,
                                              FieldMask &observed,
                                              FieldMask &non_dominated) const
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
        const EventUsers &event_users = cit->second;
        FieldMask overlap = event_users.get_valid_mask() & mask;
        if (!overlap)
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = mask & it->second;
          if (!user_overlap)
            continue;
          bool dominated = true;
          // We're just reading these and we want to see all prior
          // dependences so just give dummy opid and index
          if (has_local_precondition<true>(it->first, usage, expr,
                   0/*opid*/, 0/*index*/, expr_covers, dominated)) 
          {
            last_events.insert(cit->first);
            if (dominated)
              observed |= user_overlap;
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                            const FieldMask &mask,
                                            IndexSpaceExpression *expr,
                                            const bool expr_covers,
                                            std::set<ApEvent> &last_users) const
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (LegionMap<ApEvent,EventUsers>::const_iterator pit = 
            previous_epoch_users.begin(); pit != 
            previous_epoch_users.end(); pit++)
      {
        const EventUsers &event_users = pit->second;
        FieldMask overlap = mask & event_users.get_valid_mask();
        if (!overlap)
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = overlap & it->second;
          if (!user_overlap)
            continue;
          // We're just reading these and we want to see all prior
          // dependences so just give dummy opid and index
          if (has_local_precondition<true>(it->first, usage, expr, 
                               0/*opid*/, 0/*index*/, expr_covers))
          {
            last_users.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_filter_users(const FieldMask &dom_mask,
                                            EventFieldUsers &filter_users)
    //--------------------------------------------------------------------------
    {
      // Lock better be held by caller
      for (EventFieldUsers::const_iterator pit = previous_epoch_users.begin(); 
            pit != previous_epoch_users.end(); pit++)
      {
        FieldMask event_overlap = pit->second.get_valid_mask() & dom_mask;
        if (!event_overlap)
          continue;
        EventFieldUsers::iterator to_filter = filter_users.find(pit->first);
        for (EventUsers::const_iterator it = pit->second.begin();
              it != pit->second.end(); it++)
        {
          const FieldMask user_overlap = it->second & event_overlap;
          if (!user_overlap)
            continue;
          if (to_filter == filter_users.end())
          {
            filter_users[pit->first].insert(it->first, user_overlap);
            to_filter = filter_users.find(pit->first);
          }
          else
            to_filter->second.insert(it->first, user_overlap);
        }
      }
    }

    /////////////////////////////////////////////////////////////
    // PendingTaskUser
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PendingTaskUser::PendingTaskUser(const RegionUsage &u, const FieldMask &m,
                                     IndexSpaceNode *expr, const UniqueID id,
                                     const unsigned idx, const ApEvent term,
                                     const RtEvent collect)
      : usage(u), user_mask(m), user_expr(expr), op_id(id), 
        index(idx), term_event(term), collect_event(collect)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    PendingTaskUser::~PendingTaskUser(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    bool PendingTaskUser::apply(MaterializedView *view, const FieldMask &mask)
    //--------------------------------------------------------------------------
    {
      const FieldMask overlap = user_mask & mask;
      if (!overlap)
        return false;
      view->add_internal_task_user(usage, user_expr, overlap, term_event, 
                                   collect_event, op_id,index,false/*tracing*/);
      user_mask -= overlap;
      return !user_mask;
    }

    /////////////////////////////////////////////////////////////
    // PendingCopyUser
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PendingCopyUser::PendingCopyUser(const bool read, const FieldMask &mask,
                                     IndexSpaceExpression *e, const UniqueID id,
                                     const unsigned idx, const ApEvent term,
                                     const RtEvent collect)
      : reading(read), copy_mask(mask), copy_expr(e), op_id(id), 
        index(idx), term_event(term), collect_event(collect)
    //--------------------------------------------------------------------------
    {
    }
    
    //--------------------------------------------------------------------------
    PendingCopyUser::~PendingCopyUser(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    bool PendingCopyUser::apply(MaterializedView *view, const FieldMask &mask)
    //--------------------------------------------------------------------------
    {
      const FieldMask overlap = copy_mask & mask;
      if (!overlap)
        return false;
      const RegionUsage usage(reading ? LEGION_READ_ONLY : LEGION_READ_WRITE, 
                              LEGION_EXCLUSIVE, 0);
      view->add_internal_copy_user(usage, copy_expr, overlap, term_event,
                       collect_event, op_id, index, false/*trace recording*/);
      copy_mask -= overlap;
      return !copy_mask;
    }

    /////////////////////////////////////////////////////////////
    // IndividualView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    IndividualView::IndividualView(RegionTreeForest *ctx, DistributedID did,
                     PhysicalManager *man, AddressSpaceID owner_proc,
                     AddressSpaceID log_owner, UniqueID owner_context,
                     bool register_now, CollectiveMapping *mapping)
      : InstanceView(ctx, did, owner_proc, owner_context, register_now,mapping),
        manager(man), logical_owner(log_owner)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager != NULL);
#endif
      // Keep the manager from being collected
      manager->add_nested_resource_ref(did);
    }

    //--------------------------------------------------------------------------
    IndividualView::~IndividualView(void)
    //--------------------------------------------------------------------------
    {
      if (manager->remove_nested_resource_ref(did))
        delete manager;
      if (is_owner())
      {
        for (std::map<unsigned,Reservation>::iterator it =
              view_reservations.begin(); it != view_reservations.end(); it++)
          it->second.destroy_reservation();
      }
    }

    //--------------------------------------------------------------------------
    AddressSpaceID IndividualView::get_analysis_space(
                                                PhysicalManager *instance) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      return logical_owner;
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      manager->add_nested_gc_ref(did, mutator);
      // If we're the logical owner, but not the original owner
      // then we use a gc reference on the original owner to 
      // keep all the views allive until we're done
      if (is_logical_owner() && !is_owner())
        send_remote_gc_increment(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      manager->remove_nested_gc_ref(did, mutator);
      // If we're the logical owner but not the original owner
      // then we remove the gc reference that we added
      if (is_logical_owner() && !is_owner())
        send_remote_gc_decrement(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // The logical owner is where complete set of users is and is therefore
      // where garbage collection will take place so we need to send our 
      // valid update there if we're not the owner, otherwise we send it 
      // down to the manager
      if (is_logical_owner())
        manager->add_nested_valid_ref(did, mutator);
      else
        send_remote_valid_increment(logical_owner, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_logical_owner())
        // we have a resource reference on the manager so no need to check
        manager->remove_nested_valid_ref(did, mutator);
      else
        send_remote_valid_decrement(logical_owner, mutator);
    }

    //--------------------------------------------------------------------------
    ApEvent IndividualView::fill_from(FillView *fill_view,
                                      ApEvent precondition,
                                      PredEvent predicate_guard,
                                      IndexSpaceExpression *fill_expression,
                                      Operation *op, const unsigned index,
                                      const FieldMask &fill_mask,
                                      const PhysicalTraceInfo &trace_info,
                                      std::set<RtEvent> &recorded_events,
                                      std::set<RtEvent> &applied_events,
                                      CopyAcrossHelper *across_helper,
                                      const bool manage_dst_events,
                                      const bool fill_restricted,
                                      const bool need_valid_return)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert((across_helper == NULL) || !manage_dst_events);
#endif
      // Compute the precondition first
      if (manage_dst_events)
      {
        ApEvent dst_precondition = find_copy_preconditions(
            false/*reading*/, 0/*redop*/, fill_mask, fill_expression,
            op->get_unique_op_id(), index, applied_events, trace_info);
        if (dst_precondition.exists())
        {
          if (dst_precondition.exists())
            precondition =
              Runtime::merge_events(&trace_info,precondition,dst_precondition);
          else
            precondition = dst_precondition;
        }
      }
      std::vector<CopySrcDstField> dst_fields;
      if (across_helper != NULL)
      {
        const FieldMask src_mask = across_helper->convert_dst_to_src(fill_mask);
        across_helper->compute_across_offsets(src_mask, dst_fields);
      }
      else
        manager->compute_copy_offsets(fill_mask, dst_fields); 
      const ApEvent result = fill_expression->issue_fill(op, trace_info,
                                             dst_fields,
                                             fill_view->value->value,
                                             fill_view->value->value_size,
#ifdef LEGION_SPY
                                             fill_view->fill_op_uid,
                                             manager->field_space_node->handle,
                                             manager->tree_id,
#endif
                                             precondition, predicate_guard);
      // Save the result
      if (manage_dst_events && result.exists())
      {
        const RtEvent collect_event = trace_info.get_collect_event();
        add_copy_user(false/*reading*/, 0/*redop*/, result, 
          collect_event, fill_mask, fill_expression, op->get_unique_op_id(),
          index, recorded_events, trace_info.recording, runtime->address_space);
      }
      if (trace_info.recording)
      {
        const UniqueInst dst_inst(this);
        trace_info.record_fill_inst(result, fill_expression, dst_inst,
                        fill_mask, applied_events, (get_redop() > 0));
      }
      return result;
    }

    //--------------------------------------------------------------------------
    ApEvent IndividualView::copy_from(InstanceView *src_view,
                                      ApEvent precondition,
                                      PredEvent predicate_guard,
                                      ReductionOpID reduction_op_id,
                                      IndexSpaceExpression *copy_expression,
                                      Operation *op, const unsigned index,
                                      const FieldMask &copy_mask,
                                      PhysicalManager *src_point,
                                      const PhysicalTraceInfo &trace_info,
                                      std::set<RtEvent> &recorded_events,
                                      std::set<RtEvent> &applied_events,
                                      CopyAcrossHelper *across_helper,
                                      const bool manage_dst_events,
                                      const bool copy_restricted,
                                      const bool need_valid_return)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert((across_helper == NULL) || !manage_dst_events);
#endif
      // Compute the preconditions first
      const UniqueID op_id = op->get_unique_op_id();
      // We'll need to compute our destination precondition no matter what
      if (manage_dst_events)
      {
        const ApEvent dst_pre = find_copy_preconditions(
          false/*reading*/, reduction_op_id, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);  
        if (dst_pre.exists())
        {
          if (precondition.exists())
            precondition =
              Runtime::merge_events(&trace_info, precondition, dst_pre);
          else
            precondition = dst_pre;
        }
      }
      const FieldMask *src_mask = (across_helper == NULL) ? &copy_mask :
          new FieldMask(across_helper->convert_dst_to_src(copy_mask));
      // Several cases here:
      // 1. The source is another individual manager - just straight up 
      //    compute the dependences and do the copy or reduction
      // 2. The source is a normal collective manager - issue a copy from
      //    an instance close to the destination instance
      // 3. The source is a reduction collective manager - build a reduction
      //    tree down to a source instance close to the destination instance
      ApEvent result;
      if (src_view->is_individual_view())
      {
        IndividualView *source_view = src_view->as_individual_view();
        // Case 1: Source manager is another instance manager
        const ApEvent src_pre = source_view->find_copy_preconditions(
            true/*reading*/, 0/*redop*/, *src_mask, copy_expression,
            op_id, index, applied_events, trace_info);
        if (src_pre.exists())
        {
          if (precondition.exists())
            precondition =
              Runtime::merge_events(&trace_info, precondition, src_pre);
          else
            precondition = src_pre;
        }
        // Compute the field offsets
        std::vector<CopySrcDstField> dst_fields, src_fields;
        if (across_helper == NULL)
          manager->compute_copy_offsets(copy_mask, dst_fields);
        else
          across_helper->compute_across_offsets(*src_mask, dst_fields);
        PhysicalManager *source_manager = source_view->get_manager();
        source_manager->compute_copy_offsets(*src_mask, src_fields);
        std::vector<Reservation> reservations;
        // If we're doing a reduction operation then set the reduction
        // information on the source-dst fields
        if (reduction_op_id > 0)
        {
#ifdef DEBUG_LEGION
          assert((get_redop() == 0) || (get_redop() == reduction_op_id));
#endif
          // Get the reservations
          find_field_reservations(copy_mask, reservations);
          // Set the redop on the destination fields
          // Note that we can mark these as exclusive copies since
          // we are protecting them with the reservations
          for (unsigned idx = 0; idx < dst_fields.size(); idx++)
            dst_fields[idx].set_redop(reduction_op_id, 
                (get_redop() > 0)/*fold*/, true/*exclusive*/);
        }
        result = copy_expression->issue_copy(op, trace_info, dst_fields,
                                             src_fields, reservations,
#ifdef LEGION_SPY
                                             source_manager->tree_id,
                                             manager->tree_id,
#endif
                                             precondition, predicate_guard);
        if (result.exists())
        {
          const RtEvent collect_event = trace_info.get_collect_event();
          source_view->add_copy_user(true/*reading*/, 0/*redop*/, result,
              collect_event, *src_mask, copy_expression, op_id, index,
              recorded_events, trace_info.recording, runtime->address_space);
          if (manage_dst_events)
            add_copy_user(false/*reading*/, reduction_op_id, result,
                collect_event, copy_mask, copy_expression, op_id, index,
              recorded_events, trace_info.recording, runtime->address_space);
        }
        if (trace_info.recording)
        {
          const UniqueInst src_inst(source_view);
          const UniqueInst dst_inst(this);
          trace_info.record_copy_insts(result, copy_expression, src_inst,
              dst_inst, *src_mask, copy_mask, reduction_op_id, applied_events);
        }
      }
      else
      {
        CollectiveView *collective = src_view->as_collective_view();
        std::vector<CopySrcDstField> dst_fields;
        if (across_helper == NULL)
          manager->compute_copy_offsets(copy_mask, dst_fields);
        else
          across_helper->compute_across_offsets(*src_mask, dst_fields);
        std::vector<Reservation> reservations;
        if (reduction_op_id > 0)
        {
#ifdef DEBUG_LEGION
          assert((get_redop() == 0) || (get_redop() == reduction_op_id));
#endif
          find_field_reservations(copy_mask, reservations);
          // Set the redop on the destination fields
          // Note that we can mark these as exclusive copies since
          // we are protecting them with the reservations
          for (unsigned idx = 0; idx < dst_fields.size(); idx++)
            dst_fields[idx].set_redop(reduction_op_id,
                (get_redop() > 0)/*fold*/, true/*exclusive*/);
        }
        if (collective->is_allreduce_view())
        {
#ifdef DEBUG_LEGION
          assert(reduction_op_id == collective->get_redop());
#endif
          AllreduceView *allreduce = collective->as_allreduce_view();
          // Case 3
          // This is subtle as fuck
          // In the normal case where we're doing a reduction from a
          // collective instance to a normal instance then we can get
          // away with just building the reduction tree.
          //
          // An important note here: we only need to build a reduction tree
          // and not do an all-reduce for the collective reduction instance
          // because we know the equivalence set code above will only ever
          // issue a single copy from a reduction instance into another 
          // instance before that reduction instance is refreshed, so it
          // is safe to break the invariant that all instances in the 
          // collective manager have the same data.
          //
          // However, in the case where we are doing a copy-across, then we
          // might still be asked to do an intra-region reduction later so 
          // it's unsafe to do the partial accumulations into our own
          // instances. Therefore for now we will hammer all the source
          // instances into the destination instance without any
          // intermediate reductions.
          const UniqueInst dst_inst(this);
          if (manage_dst_events)
          {
            // Reduction-tree case
            const AddressSpaceID origin = (src_point != NULL) ?
              src_point->owner_space :
              collective->select_source_space(owner_space);
            // There will always be a single result for this copy
            if (origin != local_space)
            {
              const RtUserEvent recorded = Runtime::create_rt_user_event();
              const RtUserEvent applied = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(allreduce->did);
                pack_fields(rez, dst_fields);
                rez.serialize<size_t>(reservations.size());
                for (unsigned idx = 0; idx < reservations.size(); idx++)
                  rez.serialize(reservations[idx]);
                rez.serialize(precondition);
                rez.serialize(predicate_guard);
                copy_expression->pack_expression(rez, origin);
                op->pack_remote_operation(rez, origin, applied_events);
                rez.serialize(index);
                rez.serialize(*src_mask);
                rez.serialize(copy_mask);
                if (src_point != NULL)
                  rez.serialize(src_point->did);
                else
                  rez.serialize<DistributedID>(0);
                dst_inst.serialize(rez);
                trace_info.pack_trace_info(rez, applied_events);
                rez.serialize(recorded);
                rez.serialize(applied);
                if (trace_info.recording)
                {
                  ApBarrier bar(Realm::Barrier::create_barrier(1/*arrivals*/));
                  const ShardID sid = trace_info.record_managed_barrier(bar, 1);
                  rez.serialize(bar);
                  if (bar.exists())
                    rez.serialize(sid);
                  result = bar;
                }
                else
                {
                  const ApUserEvent to_trigger =
                    Runtime::create_ap_user_event(&trace_info);
                  result = to_trigger;
                  rez.serialize(to_trigger);
                }
                rez.serialize(origin);
              }
              runtime->send_collective_distribute_reduction(origin, rez);
              recorded_events.insert(recorded);
              applied_events.insert(applied);
            }
            else
            {
              const ApUserEvent to_trigger =
                Runtime::create_ap_user_event(&trace_info);
              result = to_trigger;
              allreduce->perform_collective_reduction(dst_fields,
                  reservations, precondition, predicate_guard, copy_expression,
                  op, index, *src_mask, copy_mask, 
                  (src_point != NULL) ? src_point->did : 0, dst_inst,
                  trace_info, recorded_events, applied_events, 
                  to_trigger, origin);
            }
          }
          else
          {
            // Hammer reduction case
            // Issue a performance warning if we're ever going to 
            // be doing this case and the number of instance is large
            if (collective->instances.size() > LEGION_COLLECTIVE_RADIX)
              REPORT_LEGION_WARNING(LEGION_WARNING_COLLECTIVE_HAMMER_REDUCTION,
                  "WARNING: Performing copy-across reduction hammer with %zd "
                  "instances into a single instance from collective manager "
                  "%llx to normal manager %llx. Please report this use case "
                  "to the Legion developers' mailing list.",
                  collective->instances.size(), collective->did, did)
            const AddressSpaceID origin =
              collective->select_source_space(owner_space);
            if (origin != local_space)
            {
              const RtUserEvent recorded = Runtime::create_rt_user_event();
              const RtUserEvent applied = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(allreduce->did);
                pack_fields(rez, dst_fields);
                rez.serialize<size_t>(reservations.size());
                for (unsigned idx = 0; idx < reservations.size(); idx++)
                  rez.serialize(reservations[idx]);
                rez.serialize(precondition);
                rez.serialize(predicate_guard);
                copy_expression->pack_expression(rez, origin);
                op->pack_remote_operation(rez, origin, applied_events);
                rez.serialize(index);
                rez.serialize(*src_mask);
                rez.serialize(copy_mask);
                dst_inst.serialize(rez);
                trace_info.pack_trace_info(rez, applied_events);
                rez.serialize(recorded);
                rez.serialize(applied);
                if (trace_info.recording)
                {
                  ApBarrier bar(Realm::Barrier::create_barrier(1/*arrivals*/));
                  ShardID sid = trace_info.record_managed_barrier(bar, 1);
                  rez.serialize(bar);
                  rez.serialize(sid);
                  result = bar;
                }
                else
                {
                  const ApUserEvent to_trigger =
                    Runtime::create_ap_user_event(&trace_info);
                  rez.serialize(to_trigger);             
                  result = to_trigger; 
                }
                rez.serialize(origin);
              }
              runtime->send_collective_hammer_reduction(origin, rez);
              recorded_events.insert(recorded);
              applied_events.insert(applied);
            }
            else
              result = allreduce->perform_hammer_reduction(
                  dst_fields, reservations, precondition, predicate_guard,
                  copy_expression, op, index, *src_mask, copy_mask, dst_inst,
                  trace_info, recorded_events, applied_events, origin);
          }
        }
        else
        {
          // Case 2
          // We can issue the copy from an instance in the source
          const Memory location = manager->memory_manager->memory;
          const DomainPoint no_point;
          const AddressSpaceID origin = (src_point != NULL) ?
              src_point->owner_space :
              collective->select_source_space(owner_space);
          const UniqueInst dst_inst(this);
          if (origin != local_space)
          {
            const RtUserEvent recorded = Runtime::create_rt_user_event();
            const RtUserEvent applied = Runtime::create_rt_user_event();
            ApUserEvent to_trigger = Runtime::create_ap_user_event(&trace_info);
            Serializer rez;
            {
              RezCheck z(rez);
              rez.serialize(collective->did);
              pack_fields(rez, dst_fields);
              rez.serialize<size_t>(reservations.size());
              for (unsigned idx = 0; idx < reservations.size(); idx++)
                rez.serialize(reservations[idx]);
              rez.serialize(precondition);
              rez.serialize(predicate_guard);
              copy_expression->pack_expression(rez, origin);
              op->pack_remote_operation(rez, origin, applied_events);
              rez.serialize(index);
              rez.serialize(*src_mask);
              rez.serialize(copy_mask);
              rez.serialize(location);
              dst_inst.serialize(rez);
              if (src_point != NULL)
                rez.serialize(src_point->did);
              else
                rez.serialize<DistributedID>(0);
              trace_info.pack_trace_info(rez, applied_events);
              rez.serialize(recorded);
              rez.serialize(applied);
              rez.serialize(to_trigger);             
            }
            runtime->send_collective_distribute_point(origin, rez);
            recorded_events.insert(recorded);
            applied_events.insert(applied);
            result = to_trigger;
          }
          else
            result = collective->perform_collective_point(
                dst_fields, reservations, precondition, predicate_guard,
                copy_expression, op, index, *src_mask, copy_mask, location, 
                dst_inst, (src_point != NULL) ? src_point->did : 0,
                trace_info, recorded_events, applied_events);
        }
        if (result.exists() && manage_dst_events)
        {
          const RtEvent collect_event = trace_info.get_collect_event();
          add_copy_user(false/*reading*/, reduction_op_id, result,
              collect_event, copy_mask, copy_expression, op_id, index,
            recorded_events, trace_info.recording, runtime->address_space);
        }
      } 
      if (across_helper != NULL)
        delete src_mask;
      return result;
    }

    //--------------------------------------------------------------------------
    ApEvent IndividualView::register_collective_user(const RegionUsage &usage,
                                       const FieldMask &user_mask,
                                       IndexSpaceNode *expr,
                                       const UniqueID op_id,
                                       const size_t op_ctx_index,
                                       const unsigned index,
                                       ApEvent term_event,
                                       RtEvent collect_event,
                                       PhysicalManager *target,
                                       CollectiveMapping *analysis_mapping,
                                       size_t local_collective_arrivals,
                                       std::vector<RtEvent> &registered_events,
                                       std::set<RtEvent> &applied_events,
                                       const PhysicalTraceInfo &trace_info,
                                       const bool symbolic)
    //--------------------------------------------------------------------------
    {
      // This case occurs when all the points mapping to the same logical
      // region also map to the same physical instance. Most commonly this
      // will occur with control replication doing attach operations on 
      // file instances, but can occur outside of control replication as 
      // well, especially in intra-node cases
#ifdef DEBUG_LEGION
      assert(local_collective_arrivals > 0);
      assert((analysis_mapping != NULL) || (local_collective_arrivals > 1));
#endif
      // First we need to decide which node is going to be the owner node
      // We'll prefer it to be the logical view owner since that is where
      // the event will be produced, otherwise, we'll just pick whichever
      // is closest to the logical view node
      const AddressSpaceID origin = (analysis_mapping == NULL) ? local_space : 
        analysis_mapping->contains(logical_owner) ? logical_owner : 
        analysis_mapping->find_nearest(logical_owner);
      ApUserEvent result;
      RtUserEvent applied, registered;
      std::vector<ApEvent> term_events;
      PhysicalTraceInfo *result_info = NULL;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.remaining_local_arrivals = local_collective_arrivals;
          rendezvous.local_initialized = true;
          rendezvous.remaining_remote_arrivals = (analysis_mapping == NULL) ? 0
            : analysis_mapping->count_children(origin, local_space);
          rendezvous.ready_event = Runtime::create_ap_user_event(&trace_info);
          rendezvous.trace_info = new PhysicalTraceInfo(trace_info);
          rendezvous.registered = Runtime::create_rt_user_event();
          rendezvous.applied = Runtime::create_rt_user_event();
        }
        else if (!finder->second.local_initialized)
        {
#ifdef DEBUG_LEGION
          assert(!finder->second.ready_event.exists());
          assert(finder->second.trace_info == NULL);
#endif
          // First local arrival
          finder->second.remaining_local_arrivals = local_collective_arrivals;
          finder->second.ready_event =
            Runtime::create_ap_user_event(&trace_info);
          finder->second.trace_info = new PhysicalTraceInfo(trace_info);
          if (!finder->second.remote_ready_events.empty())
          {
            for (std::map<ApUserEvent,PhysicalTraceInfo*>::const_iterator it =
                  finder->second.remote_ready_events.begin(); it !=
                  finder->second.remote_ready_events.end(); it++)
            {
              Runtime::trigger_event(it->second, it->first, 
                                finder->second.ready_event);
              delete it->second;
            }
            finder->second.remote_ready_events.clear();
          }
        }
        result = finder->second.ready_event;
        result_info = finder->second.trace_info;
        registered = finder->second.registered;
        registered_events.push_back(registered);
        applied = finder->second.applied;
        applied_events.insert(applied);
        if (term_event.exists())
          finder->second.term_events.push_back(term_event);
#ifdef DEBUG_LEGION
        assert(finder->second.local_initialized);
        assert(finder->second.remaining_local_arrivals > 0);
#endif
        // If we're still expecting arrivals then nothing to do yet
        if ((--finder->second.remaining_local_arrivals > 0) ||
            (finder->second.remaining_remote_arrivals > 0))
        {
          // We need to save the trace info no matter what
          if (finder->second.mask == NULL)
          {
            if (local_space == origin)
            {
              // Save our state for performing the registration later
              finder->second.usage = usage;
              finder->second.mask = new FieldMask(user_mask);
              finder->second.expr = expr;
              WrapperReferenceMutator mutator(applied_events);
              expr->add_nested_expression_reference(did, &mutator);
              finder->second.op_id = op_id;
              finder->second.collect_event = collect_event;
              finder->second.symbolic = symbolic;
            }
          }
          return result;
        }
        term_events.swap(finder->second.term_events);
#ifdef DEBUG_LEGION
        assert(finder->second.remote_ready_events.empty());
#endif
        // We're done with our entry after this so no need to keep it
        rendezvous_users.erase(finder);
      }
      if (!term_events.empty())
        term_event = Runtime::merge_events(&trace_info, term_events);
      if (local_space != origin)
      {
        const AddressSpaceID parent = 
          collective_mapping->get_parent(origin, local_space);
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(origin);
          result_info->pack_trace_info(rez, applied_events);
          rez.serialize(term_event);
          rez.serialize(result);
          rez.serialize(registered);
          rez.serialize(applied);
        }
        runtime->send_collective_individual_register_user(parent, rez);
      }
      else
      {
        std::vector<RtEvent> local_registered;
        std::set<RtEvent> local_applied; 
        const ApEvent ready = register_user(usage, user_mask, expr, op_id,
            op_ctx_index, index, term_event, collect_event, target, 
            NULL/*analysis*/, 0/*no collective arrivals*/, local_registered,
            local_applied, *result_info, runtime->address_space, symbolic);
        Runtime::trigger_event(result_info, result, ready);
        if (!local_registered.empty())
          Runtime::trigger_event(registered,
              Runtime::merge_events(local_registered));
        else
          Runtime::trigger_event(registered);
        if (!local_applied.empty())
          Runtime::trigger_event(applied,
              Runtime::merge_events(local_applied));
        else
          Runtime::trigger_event(applied);
      }
      delete result_info;
      return result;
    }

    //--------------------------------------------------------------------------
    void IndividualView::process_collective_user_registration(
                                            const size_t op_ctx_index,
                                            const unsigned index,
                                            const AddressSpaceID origin,
                                            const PhysicalTraceInfo &trace_info,
                                            ApEvent remote_term_event,
                                            ApUserEvent remote_ready_event,
                                            RtUserEvent remote_registered,
                                            RtUserEvent remote_applied)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.local_initialized = false;
          rendezvous.remaining_remote_arrivals =
            collective_mapping->count_children(origin, local_space);
          // Don't make the ready event, that needs to be done with a
          // local trace_info
          rendezvous.registered = Runtime::create_rt_user_event();
          rendezvous.applied = Runtime::create_rt_user_event();
        }
        if (remote_term_event.exists())
          finder->second.term_events.push_back(remote_term_event);
        Runtime::trigger_event(remote_registered, finder->second.registered);
        Runtime::trigger_event(remote_applied, finder->second.applied);
        if (!finder->second.ready_event.exists())
          finder->second.remote_ready_events[remote_ready_event] =
            new PhysicalTraceInfo(trace_info);
        else
          Runtime::trigger_event(&trace_info, remote_ready_event, 
                                 finder->second.ready_event);
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_remote_arrivals > 0);
#endif
        // Check to see if we've done all the arrivals
        if ((--finder->second.remaining_remote_arrivals > 0) ||
            !finder->second.local_initialized ||
            (finder->second.remaining_local_arrivals > 0))
          return;
#ifdef DEBUG_LEGION
        assert(finder->second.remote_ready_events.empty());
        assert(finder->second.trace_info != NULL);
#endif
        // Last needed arrival, see if we're the origin or not
        to_perform = std::move(finder->second);
        rendezvous_users.erase(finder);
      }
      ApEvent term_event;
      if (!to_perform.term_events.empty())
        term_event =
          Runtime::merge_events(to_perform.trace_info, to_perform.term_events);
      if (local_space != origin)
      {
#ifdef DEBUG_LEGION
        assert(to_perform.applied.exists());
#endif
        // Send the message to the parent
        const AddressSpaceID parent = 
            collective_mapping->get_parent(origin, local_space);
        std::set<RtEvent> applied_events;
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(origin);
          to_perform.trace_info->pack_trace_info(rez, applied_events);
          rez.serialize(term_event);
          rez.serialize(to_perform.ready_event);
          rez.serialize(to_perform.registered);
          rez.serialize(to_perform.applied);
        }
        runtime->send_collective_individual_register_user(parent, rez);
        if (!applied_events.empty())
          Runtime::trigger_event(to_perform.applied,
              Runtime::merge_events(applied_events));
        else
          Runtime::trigger_event(to_perform.applied);
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(!to_perform.applied.exists());
#endif
        std::vector<RtEvent> registered_events;
        std::set<RtEvent> applied_events;
        const ApEvent ready = register_user(to_perform.usage,
            *to_perform.mask, to_perform.expr, to_perform.op_id, op_ctx_index,
            index, term_event, to_perform.collect_event, manager,
            NULL/*no analysis mapping*/, 0/*no collective arrivals*/,
            registered_events, applied_events, *to_perform.trace_info,
            runtime->address_space, to_perform.symbolic);
        Runtime::trigger_event(to_perform.trace_info, 
                      to_perform.ready_event, ready);
        if (!registered_events.empty())
          Runtime::trigger_event(to_perform.registered,
              Runtime::merge_events(registered_events));
        else
          Runtime::trigger_event(to_perform.registered);
        if (!applied_events.empty())
          Runtime::trigger_event(to_perform.applied,
              Runtime::merge_events(applied_events));
        else
          Runtime::trigger_event(to_perform.applied);
        if (to_perform.expr->remove_nested_expression_reference(did))
          delete to_perform.expr;
        delete to_perform.mask;
      }
      delete to_perform.trace_info;
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_collective_user_registration(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      AddressSpaceID origin;
      derez.deserialize(origin);
      PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime); 
      ApEvent term_event;
      derez.deserialize(term_event);
      ApUserEvent ready_event;
      derez.deserialize(ready_event);
      RtUserEvent registered_event, applied_event;
      derez.deserialize(registered_event);
      derez.deserialize(applied_event);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();

      view->process_collective_user_registration(op_ctx_index, index, origin,
          trace_info, term_event, ready_event, registered_event, applied_event);
    }

    //--------------------------------------------------------------------------
    void IndividualView::pack_fields(Serializer &rez,
                               const std::vector<CopySrcDstField> &fields) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(fields.size());
      for (unsigned idx = 0; idx < fields.size(); idx++)
        rez.serialize(fields[idx]);
      if (runtime->legion_spy_enabled)
      {
        rez.serialize<size_t>(0); // not part of the collective
        rez.serialize(did);
      }
    }

    //--------------------------------------------------------------------------
    void IndividualView::find_atomic_reservations(const FieldMask &mask, 
                                       Operation *op, unsigned index, bool excl)
    //--------------------------------------------------------------------------
    {
      std::vector<Reservation> reservations;
      find_field_reservations(mask, reservations); 
      for (unsigned idx = 0; idx < reservations.size(); idx++)
        op->update_atomic_locks(index, reservations[idx], excl);
    } 

    //--------------------------------------------------------------------------
    void IndividualView::find_field_reservations(const FieldMask &mask,
                                         std::vector<Reservation> &reservations)
    //--------------------------------------------------------------------------
    {
      const RtEvent ready = 
        find_field_reservations(mask, &reservations, runtime->address_space);
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      // Sort them into order if necessary
      if (reservations.size() > 1)
        std::sort(reservations.begin(), reservations.end());
    }

    //--------------------------------------------------------------------------
    RtEvent IndividualView::find_field_reservations(const FieldMask &mask,
                               std::vector<Reservation> *reservations,
                               AddressSpaceID source, RtUserEvent to_trigger)
    //--------------------------------------------------------------------------
    {
      std::vector<Reservation> results;
      if (is_owner())
      {
        results.reserve(mask.pop_count());
        // We're the owner so we can make all the fields
        AutoLock v_lock(view_lock);
        for (int idx = mask.find_first_set(); idx >= 0;
              idx = mask.find_next_set(idx+1))
        {
          std::map<unsigned,Reservation>::const_iterator finder =
            view_reservations.find(idx);
          if (finder == view_reservations.end())
          {
            // Make a new reservation and add it to the set
            Reservation handle = Reservation::create_reservation();
            view_reservations[idx] = handle;
            results.push_back(handle);
          }
          else
            results.push_back(finder->second);
        }
      }
      else
      {
        // See if we can find them all locally
        {
          AutoLock v_lock(view_lock, 1, false/*exclusive*/);
          for (int idx = mask.find_first_set(); idx >= 0;
                idx = mask.find_next_set(idx+1))
          {
            std::map<unsigned,Reservation>::const_iterator finder =
              view_reservations.find(idx);
            if (finder != view_reservations.end())
              results.push_back(finder->second);
            else
              break;
          }
        }
        if (results.size() < mask.pop_count())
        {
          // Couldn't find them all so send the request to the owner
          if (!to_trigger.exists())
            to_trigger = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(mask);
            rez.serialize(reservations);
            rez.serialize(source);
            rez.serialize(to_trigger);
          }
          runtime->send_atomic_reservation_request(owner_space, rez);
          return to_trigger;
        }
      }
      if (source != local_space)
      {
#ifdef DEBUG_LEGION
        assert(to_trigger.exists());
#endif
        // Send the result back to the source
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(mask);
          rez.serialize(reservations);
          rez.serialize<size_t>(results.size());
          for (std::vector<Reservation>::const_iterator it =
                results.begin(); it != results.end(); it++)
            rez.serialize(*it);
          rez.serialize(to_trigger);
        }
        runtime->send_atomic_reservation_response(source, rez);
      }
      else
      {
        reservations->swap(results);
        if (to_trigger.exists())
          Runtime::trigger_event(to_trigger);
      }
      return to_trigger;
    }

    //--------------------------------------------------------------------------
    void IndividualView::update_field_reservations(const FieldMask &mask,
                                   const std::vector<Reservation> &reservations)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_owner());
#endif
      AutoLock v_lock(view_lock);
      unsigned offset = 0;
      for (int idx = mask.find_first_set(); idx >= 0;
            idx = mask.find_next_set(idx+1))
        view_reservations[idx] = reservations[offset++];
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_atomic_reservation_request(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
        runtime->find_or_request_logical_view(did, ready));
      FieldMask mask;
      derez.deserialize(mask);
      std::vector<Reservation> *target;
      derez.deserialize(target);
      AddressSpaceID source;
      derez.deserialize(source);
      RtUserEvent to_trigger;
      derez.deserialize(to_trigger);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->find_field_reservations(mask, target, source, to_trigger);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_atomic_reservation_response(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
        runtime->find_or_request_logical_view(did, ready));
      FieldMask mask;
      derez.deserialize(mask);
      std::vector<Reservation> *target;
      derez.deserialize(target);
      size_t num_reservations;
      derez.deserialize(num_reservations);
      target->resize(num_reservations);
      for (unsigned idx = 0; idx < num_reservations; idx++)
        derez.deserialize((*target)[idx]);
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->update_field_reservations(mask, *target);
      RtUserEvent to_trigger;
      derez.deserialize(to_trigger);
      Runtime::trigger_event(to_trigger);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_view_find_copy_pre_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      bool reading;
      derez.deserialize<bool>(reading);
      ReductionOpID redop;
      derez.deserialize(redop);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      IndexSpaceExpression *copy_expr = 
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      UniqueID op_id;
      derez.deserialize(op_id);
      unsigned index;
      derez.deserialize(index);
      ApUserEvent to_trigger;
      derez.deserialize(to_trigger);
      RtUserEvent applied;
      derez.deserialize(applied);
      std::set<RtEvent> applied_events;
      const PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);

      // This blocks the virtual channel, but keeps queries in-order 
      // with respect to updates from the same node which is necessary
      // for preventing cycles in the realm event graph
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      IndividualView *inst_view = view->as_individual_view();
      const ApEvent pre = inst_view->find_copy_preconditions(reading, redop,
          copy_mask, copy_expr, op_id, index, applied_events, trace_info);
      Runtime::trigger_event(&trace_info, to_trigger, pre);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_view_add_copy_user(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      bool reading;
      derez.deserialize(reading);
      ReductionOpID redop;
      derez.deserialize(redop);
      ApEvent term_event;
      derez.deserialize(term_event);
      RtEvent collect_event;
      derez.deserialize(collect_event);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      IndexSpaceExpression *copy_expr =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      UniqueID op_id;
      derez.deserialize(op_id);
      unsigned index;
      derez.deserialize(index);
      RtUserEvent applied_event;
      derez.deserialize(applied_event);
      bool trace_recording;
      derez.deserialize(trace_recording);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_individual_view());
#endif
      IndividualView *inst_view = view->as_individual_view();

      std::set<RtEvent> applied_events;
      inst_view->add_copy_user(reading,redop,term_event,collect_event,copy_mask,
          copy_expr, op_id, index, applied_events, trace_recording, source);
      if (!applied_events.empty())
      {
        const RtEvent precondition = Runtime::merge_events(applied_events);
        Runtime::trigger_event(applied_event, precondition);
        // Send back a response to the source removing the remote valid ref
        if (inst_view->is_logical_owner())
          inst_view->send_remote_valid_decrement(source, NULL, precondition);
      }
      else
      {
        Runtime::trigger_event(applied_event);
        // Send back a response to the source removing the remote valid ref
        if (inst_view->is_logical_owner())
          inst_view->send_remote_valid_decrement(source);
      }
    }

    //--------------------------------------------------------------------------
    void IndividualView::handle_view_find_last_users_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      RtEvent manager_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, manager_ready);

      std::vector<ApEvent> *target;
      derez.deserialize(target);
      RegionUsage usage;
      derez.deserialize(usage);
      FieldMask mask;
      derez.deserialize(mask);
      IndexSpaceExpression *expr =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      RtUserEvent done;
      derez.deserialize(done);

      std::set<ApEvent> result;
      std::vector<RtEvent> applied;
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      if (manager_ready.exists() && !manager_ready.has_triggered())
        manager_ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_individual_view());
#endif
      IndividualView *inst_view = view->as_individual_view();
      inst_view->find_last_users(manager, result, usage, mask, expr, applied);
      if (!result.empty())
      {
        Serializer rez;
        {
          RezCheck z2(rez);
          rez.serialize(target);
          rez.serialize<size_t>(result.size());
          for (std::set<ApEvent>::const_iterator it =
                result.begin(); it != result.end(); it++)
            rez.serialize(*it);
          rez.serialize(done);
          if (!applied.empty())
            rez.serialize(Runtime::merge_events(applied));
          else
            rez.serialize(RtEvent::NO_RT_EVENT);
        }
        runtime->send_view_find_last_users_response(source, rez);
      }
      else
      {
        if (!applied.empty())
          Runtime::trigger_event(done, Runtime::merge_events(applied));
        else
          Runtime::trigger_event(done);
      }
    }

    //--------------------------------------------------------------------------
    void IndividualView::handle_view_find_last_users_response(
                                                            Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      std::set<ApEvent> *target;
      derez.deserialize(target);
      size_t num_events;
      derez.deserialize(num_events);
      for (unsigned idx = 0; idx < num_events; idx++)
      {
        ApEvent event;
        derez.deserialize(event);
        target->insert(event);
      }
      RtUserEvent done;
      derez.deserialize(done);
      RtEvent pre;
      derez.deserialize(pre);
      Runtime::trigger_event(done, pre);
    }

    /////////////////////////////////////////////////////////////
    // MaterializedView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MaterializedView::MaterializedView(
                               RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID own_addr,
                               AddressSpaceID log_own, PhysicalManager *man,
                               UniqueID own_ctx, bool register_now,
                               CollectiveMapping *mapping)
      : IndividualView(ctx, encode_materialized_did(did), man, own_addr,
                       log_own, own_ctx, register_now, mapping), 
        expr_cache_uses(0), outstanding_additions(0)
#ifdef ENABLE_VIEW_REPLICATION
        , remote_added_users(0), remote_pending_users(NULL)
#endif
    //--------------------------------------------------------------------------
    {
#ifdef ENABLE_VIEW_REPLICATION
      repl_ptr.replicated_copies = NULL;
#endif
      if (is_logical_owner())
      {
        current_users = new ExprView(ctx,manager,this,manager->instance_domain);
        current_users->add_reference();
      }
      else
        current_users = NULL;
#ifdef LEGION_GC
      log_garbage.info("GC Materialized View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space, 
          LEGION_DISTRIBUTED_ID_FILTER(manager->did)); 
#endif
    }

    //--------------------------------------------------------------------------
    MaterializedView::~MaterializedView(void)
    //--------------------------------------------------------------------------
    {
      if ((current_users != NULL) && current_users->remove_reference())
        delete current_users;
#ifdef ENABLE_VIEW_REPLICATION
      if (repl_ptr.replicated_copies != NULL)
      {
#ifdef DEBUG_LEGION
        assert(is_logical_owner());
#endif
        // We should only have replicated copies here
        // If there are replicated requests that is very bad
        delete repl_ptr.replicated_copies;
      }
#ifdef DEBUG_LEGION
      assert(remote_pending_users == NULL);
#endif
#endif
    }

    //--------------------------------------------------------------------------
    const FieldMask& MaterializedView::get_physical_mask(void) const
    //--------------------------------------------------------------------------
    {
      return manager->layout->allocated_fields;
    }

    //--------------------------------------------------------------------------
    bool MaterializedView::has_space(const FieldMask &space_mask) const
    //--------------------------------------------------------------------------
    {
      return !(space_mask - manager->layout->allocated_fields);
    } 

    //--------------------------------------------------------------------------
    void MaterializedView::add_initial_user(ApEvent term_event,
                                            const RegionUsage &usage,
                                            const FieldMask &user_mask,
                                            IndexSpaceExpression *user_expr,
                                            const UniqueID op_id,
                                            const unsigned index)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(current_users != NULL);
#endif
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                term_event, false/*copy user*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy user*/, true/*covers*/);
#endif
      // No need to take the lock since we are just initializing
      // If it's the root this is easy
      if (user_expr == current_users->view_expr)
      {
        current_users->add_current_user(user, term_event, RtEvent::NO_RT_EVENT,
                                        user_mask, false);
        return;
      }
      // See if we have it in the cache
      std::map<IndexSpaceExprID,ExprView*>::const_iterator finder = 
        expr_cache.find(user_expr->expr_id);
      if (finder == expr_cache.end() || 
          !(finder->second->invalid_fields * user_mask))
      {
        // No need for expr_lock since this is initialization
        if (finder == expr_cache.end())
        {
          ExprView *target_view = current_users->find_congruent_view(user_expr);
          // Couldn't find a congruent view so we need to make one
          if (target_view == NULL)
            target_view = new ExprView(context, manager, this, user_expr);
          expr_cache[user_expr->expr_id] = target_view;
          finder = expr_cache.find(user_expr->expr_id);
        }
        if (finder->second != current_users)
        {
          // Now insert it for the invalid fields
          FieldMask insert_mask = user_mask & finder->second->invalid_fields;
          // Mark that we're removing these fields from the invalid fields
          // first since we're later going to destroy the insert mask
          finder->second->invalid_fields -= insert_mask;
          // Then insert the subview into the tree
          current_users->insert_subview(finder->second, insert_mask);
        }
      }
      // Now that the view is valid we can add the user to it
      finder->second->add_current_user(user, term_event, RtEvent::NO_RT_EVENT,
                                       user_mask, false);
      // No need to launch a collection task as the destructor will handle it 
    }

    //--------------------------------------------------------------------------
    ApEvent MaterializedView::register_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *user_expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         CollectiveMapping *analysis_mapping,
                                         size_t local_collective_arrivals,
                                         std::vector<RtEvent> &registered,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const AddressSpaceID source,
                                         const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target == manager);
#endif
      // Handle the collective rendezvous if necessary
      if (local_collective_arrivals > 0)
        return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, analysis_mapping, local_collective_arrivals,
              registered, applied_events, trace_info, symbolic);
      // Quick test for empty index space expressions
      if (!symbolic && user_expr->is_empty())
        return manager->get_use_event(term_event);
      if (!is_logical_owner())
      {
        ApUserEvent ready_event;
        // Check to see if this user came from somewhere that wasn't
        // the logical owner, if so we need to send the update back 
        // to the owner to be handled
        if (source != logical_owner)
        {
          // If we're not the logical owner send a message there 
          // to do the analysis and provide a user event to trigger
          // with the precondition
          ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent registered_event = Runtime::create_rt_user_event();
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(target->did);
            rez.serialize(usage);
            rez.serialize(user_mask);
            rez.serialize(user_expr->handle);
            rez.serialize(op_id);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(local_collective_arrivals);
            rez.serialize(ready_event);
            rez.serialize(registered_event);
            rez.serialize(applied_event);
            trace_info.pack_trace_info(rez, applied_events);
          }
          // Add a remote valid reference that will be removed by 
          // the receiver once the changes have been applied
          WrapperReferenceMutator mutator(applied_events);
          add_base_valid_ref(REMOTE_DID_REF, &mutator);
          runtime->send_view_register_user(logical_owner, rez);
          registered.push_back(registered_event);
          applied_events.insert(applied_event);
        }
#ifdef ENABLE_VIEW_REPLICATION
        // If we have any local fields then we also need to update
        // them here too since the owner isn't going to send us any
        // updates itself, Do this after sending the message to make
        // sure that we see a sound set of local fields
        AutoLock r_lock(replicated_lock);
        // Only need to add it if it's still replicated
        const FieldMask local_mask = user_mask & replicated_fields;
        if (!!local_mask)
        {
          // See if we need to make the current users data structure
          if (current_users == NULL)
          {
            // Prevent races between multiple added users at the same time
            AutoLock v_lock(view_lock);
            // See if we lost the race
            if (current_users == NULL)
            {
              current_users = 
               new ExprView(context, manager, this, manager->instance_domain);
              current_users->add_reference();
            }
          }
          // Add our local user
          add_internal_task_user(usage, user_expr, local_mask, term_event,
                       collect_event, op_id, index, trace_info.recording);
          // Increment the number of remote added users
          remote_added_users++;
        }
        // If we have outstanding requests to be made a replicated
        // copy then we need to buffer this user so it can be applied
        // later once we actually do get the update from the owner
        // This only applies to updates from the local node though since
        // any remote updates will be sent to us again by the owner
        if ((repl_ptr.replicated_requests != NULL) && (source == local_space))
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_requests->empty());
#endif
          FieldMask buffer_mask;
          for (LegionMap<RtUserEvent,FieldMask>::const_iterator
                it = repl_ptr.replicated_requests->begin();
                it != repl_ptr.replicated_requests->end(); it++)
          {
            const FieldMask overlap = user_mask & it->second;
            if (!overlap)
              continue;
#ifdef DEBUG_LEGION
            assert(overlap * buffer_mask);
#endif
            buffer_mask |= overlap;
            // This user isn't fully applied until the request comes
            // back to make this view valid and the user gets applied
            applied_events.insert(it->first);
          }
          if (!!buffer_mask)
          {
            // Protected by exclusive replicated lock
            if (remote_pending_users == NULL)
              remote_pending_users = new std::list<RemotePendingUser*>();
            remote_pending_users->push_back(
                new PendingTaskUser(usage, buffer_mask, user_expr, op_id,
                                    index, term_event, collect_event));
          }
        }
        if (remote_added_users >= user_cache_timeout)
          update_remote_replication_state(applied_events);
#endif // ENABLE_VIEW_REPLICATION
        return ready_event;
      }
      else
      {
#ifdef ENABLE_VIEW_REPLICATION
        // We need to hold a read-only copy of the replicated lock when
        // doing this in order to make sure it's atomic with any 
        // replication requests that arrive
        AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
        // Send updates to any remote copies to get them in flight
        if (repl_ptr.replicated_copies != NULL)
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_copies->empty());
#endif
          const FieldMask repl_mask = replicated_fields & user_mask;
          if (!!repl_mask)
          {
            for (LegionMap<AddressSpaceID,FieldMask>::const_iterator
                  it = repl_ptr.replicated_copies->begin(); 
                  it != repl_ptr.replicated_copies->end(); it++)
            {
              if (it->first == source)
                continue;
              const FieldMask overlap = it->second & repl_mask;
              if (!overlap)
                continue;
              // Send the update to the remote node
              RtUserEvent registered_event = Runtime::create_rt_user_event();
              RtUserEvent applied_event = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize(target->did);
                rez.serialize(usage);
                rez.serialize(overlap);
                rez.serialize(user_expr->handle);
                rez.serialize(op_id);
                rez.serialize(op_ctx_index);
                rez.serialize(index);
                rez.serialize(term_event);
                rez.serialize(collect_event);
                rez.serialize(local_collective_arrivals);
                rez.serialize(ApUserEvent::NO_AP_USER_EVENT);
                rez.seriliaze(registered_event);
                rez.serialize(applied_event);
                trace_info.pack_trace_info(rez, applied_events);
              }
              runtime->send_view_register_user(it->first, rez);
              registered_events.push_back(registered_event);
              applied_events.insert(applied_event);
            }
          }
        }
#endif // ENABLE_VIEW_REPLICATION
        // Now we can do our local analysis
        std::set<ApEvent> wait_on_events;
        ApEvent start_use_event = manager->get_use_event(term_event);
        if (start_use_event.exists())
          wait_on_events.insert(start_use_event);
        // Find the preconditions
        const bool user_dominates = 
          (user_expr->expr_id == current_users->view_expr->expr_id) ||
          (user_expr->get_volume() == current_users->get_view_volume());
        {
          // Traversing the tree so need the expr_view lock
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_user_preconditions(usage, user_expr, 
                            user_dominates, user_mask, term_event, 
                            op_id, index, wait_on_events, trace_info.recording);
        }
        // Add our local user
        add_internal_task_user(usage, user_expr, user_mask, term_event, 
                               collect_event, op_id,index,trace_info.recording);
        // At this point tasks shouldn't be allowed to wait on themselves
#ifdef DEBUG_LEGION
        if (term_event.exists())
          assert(wait_on_events.find(term_event) == wait_on_events.end());
#endif
        // Return the merge of the events
        if (!wait_on_events.empty())
          return Runtime::merge_events(&trace_info, wait_on_events);
        else
          return ApEvent::NO_AP_EVENT;
      }
    }

    //--------------------------------------------------------------------------
    ApEvent MaterializedView::find_copy_preconditions(bool reading,
                                            ReductionOpID redop,
                                            const FieldMask &copy_mask,
                                            IndexSpaceExpression *copy_expr,
                                            UniqueID op_id, unsigned index,
                                            std::set<RtEvent> &applied_events,
                                            const PhysicalTraceInfo &trace_info)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        // Check to see if there are any replicated fields here which we
        // can handle locally so we don't have to send a message to the owner
        ApEvent result_event;
#ifdef ENABLE_VIEW_REPLICATION
        FieldMask new_remote_fields;
#endif
        FieldMask request_mask(copy_mask);
#ifdef ENABLE_VIEW_REPLICATION
        // See if we can handle this now while all the fields are local
        {
          AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
          if (!!replicated_fields)
          {
            request_mask -= replicated_fields;
            if (!request_mask)
            {
              // All of our fields are local here so we can do the
              // analysis now without waiting for anything
              // We do this while holding the read-only lock on
              // replication to prevent invalidations of the
              // replication state while we're doing this analysis
#ifdef DEBUG_LEGION
              assert(current_users != NULL);
#endif
              std::set<ApEvent> preconditions;
              ApEvent start_use_event = manager->get_use_event();
              if (start_use_event.exists())
                preconditions.insert(start_use_event);
              const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
                  LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
              const bool copy_dominates = 
                (copy_expr->expr_id == current_users->view_expr->expr_id) ||
                (copy_expr->get_volume() == current_users->get_view_volume());
              {
                // Need a read-only copy of the expr_view lock to 
                // traverse the tree
                AutoLock e_lock(expr_lock,1,false/*exclusive*/);
                current_users->find_copy_preconditions(usage, copy_expr, 
                                       copy_dominates, copy_mask, op_id, 
                                       index, preconditions,
                                       trace_info.recording);
              }
              if (!preconditions.empty())
                result_event = Runtime::merge_events(&trace_info,preconditions);
              // See if there are any new fields we need to record
              // as having been used for copy precondition testing
              // We'll have to update them later with the lock in
              // exclusive mode, this is technically unsafe, but in
              // the worst case it will just invalidate the cache
              // and we'll have to make it valid again later
              new_remote_fields = copy_mask - remote_copy_pre_fields;
            }
          }
        }
        if (!!request_mask)
#endif // ENABLE_VIEW_REPLICATION
        {
          // All the fields are not local, first send the request to 
          // the owner to do the analysis since we're going to need 
          // to do that anyway, then issue any request for replicated
          // fields to be moved to this node and record it as a 
          // precondition for the mapping
          ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent applied = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize<bool>(reading);
            rez.serialize(redop);
            rez.serialize(copy_mask);
            copy_expr->pack_expression(rez, logical_owner);
            rez.serialize(op_id);
            rez.serialize(index);
            rez.serialize(ready_event);
            rez.serialize(applied);
            trace_info.pack_trace_info(rez, applied_events);
          }
          runtime->send_view_find_copy_preconditions_request(logical_owner,rez);
          applied_events.insert(applied);
          result_event = ready_event;
#ifdef ENABLE_VIEW_REPLICATION
#ifndef DISABLE_VIEW_REPLICATION
          // Need the lock for this next part
          AutoLock r_lock(replicated_lock);
          // Record these fields as being sampled
          remote_copy_pre_fields |= (new_remote_fields & replicated_fields);
          // Recompute this to make sure we didn't lose any races
          request_mask = copy_mask - replicated_fields;
          if (!!request_mask && (repl_ptr.replicated_requests != NULL))
          {
            for (LegionMap<RtUserEvent,FieldMask>::const_iterator it = 
                  repl_ptr.replicated_requests->begin(); it !=
                  repl_ptr.replicated_requests->end(); it++)
            {
              request_mask -= it->second;
              if (!request_mask)
                break;
            }
          }
          if (!!request_mask)
          {
            // Send the request to the owner to make these replicated fields
            const RtUserEvent request_event = Runtime::create_rt_user_event();
            Serializer rez2;
            {
              RezCheck z2(rez2);
              rez2.serialize(did);
              rez2.serialize(request_mask);
              rez2.serialize(request_event);
            }
            runtime->send_view_replication_request(logical_owner, rez2);
            if (repl_ptr.replicated_requests == NULL)
              repl_ptr.replicated_requests =
                new LegionMap<RtUserEvent,FieldMask>();
            (*repl_ptr.replicated_requests)[request_event] = request_mask;
            // Make sure this is done before things are considered "applied"
            // in order to prevent dangling requests
            aggregator.record_reference_mutation_effect(request_event);
          }
#endif
#endif
        }
#ifdef ENABLE_VIEW_REPLICATION
        else if (!!new_remote_fields)
        {
          AutoLock r_lock(replicated_lock);
          // Record any new fields which are still replicated
          remote_copy_pre_fields |= (new_remote_fields & replicated_fields);
          // Then fall through like normal
        }
#endif 
        return result_event;
      }
      else
      {
        // In the case where we're the owner we can just handle
        // this without needing to do anything
        std::set<ApEvent> preconditions;
        const ApEvent start_use_event = manager->get_use_event();
        if (start_use_event.exists())
          preconditions.insert(start_use_event);
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        const bool copy_dominates = 
          (copy_expr->expr_id == current_users->view_expr->expr_id) ||
          (copy_expr->get_volume() == current_users->get_view_volume());
        {
          // Need a read-only copy of the expr_lock to traverse the tree
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_copy_preconditions(usage,copy_expr,copy_dominates,
                  copy_mask, op_id, index, preconditions, trace_info.recording);
        }
        if (preconditions.empty())
          return ApEvent::NO_AP_EVENT;
        return Runtime::merge_events(&trace_info, preconditions);
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::add_copy_user(bool reading, ReductionOpID redop,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         const FieldMask &copy_mask,
                                         IndexSpaceExpression *copy_expr,
                                         UniqueID op_id, unsigned index,
                                         std::set<RtEvent> &applied_events,
                                         const bool trace_recording,
                                         const AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        // Check to see if this update came from some place other than the
        // source in which case we need to send it back to the source
        if (source != logical_owner)
        {
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize<bool>(reading);
            rez.serialize(redop);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(copy_mask);
            copy_expr->pack_expression(rez, logical_owner);
            rez.serialize(op_id);
            rez.serialize(index);
            rez.serialize(applied_event);
            rez.serialize<bool>(trace_recording);
          }
          // Add a remote valid reference that will be removed by 
          // the receiver once the changes have been applied
          WrapperReferenceMutator mutator(applied_events);
          add_base_valid_ref(REMOTE_DID_REF, &mutator);
          runtime->send_view_add_copy_user(logical_owner, rez);
          applied_events.insert(applied_event);
        }
#ifdef ENABLE_VIEW_REPLICATION
        AutoLock r_lock(replicated_lock);
        // Only need to add it if it's still replicated
        const FieldMask local_mask = copy_mask & replicated_fields;
        // If we have local fields to handle do that here
        if (!!local_mask)
        {
          // See if we need to make the current users data structure
          if (current_users == NULL)
          {
            // Prevent races between multiple added users at the same time
            AutoLock v_lock(view_lock);
            // See if we lost the race
            if (current_users == NULL)
            {
              current_users = 
               new ExprView(context, manager, this, manager->instance_domain);
              current_users->add_reference();
            }
          }
          const RegionUsage usage(reading ? LEGION_READ_ONLY: (redop > 0) ? 
              LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
          add_internal_copy_user(usage, copy_expr, local_mask, term_event, 
                                 collect_event, op_id, index, trace_recording);
          // Increment the remote added users count
          remote_added_users++;
        }
        // If we have pending replicated requests that overlap with this
        // user then we need to record this as a pending user to be applied
        // once we receive the update from the owner node
        // This only applies to updates from the local node though since
        // any remote updates will be sent to us again by the owner
        if ((repl_ptr.replicated_requests != NULL) && (source == local_space))
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_requests->empty());
#endif
          FieldMask buffer_mask;
          for (LegionMap<RtUserEvent,FieldMask>::const_iterator
                it = repl_ptr.replicated_requests->begin();
                it != repl_ptr.replicated_requests->end(); it++)
          {
            const FieldMask overlap = copy_mask & it->second;
            if (!overlap)
              continue;
#ifdef DEBUG_LEGION
            assert(overlap * buffer_mask);
#endif
            buffer_mask |= overlap;
            // This user isn't fully applied until the request comes
            // back to make this view valid and the user gets applied
            applied_events.insert(it->first);
          }
          if (!!buffer_mask)
          {
            // Protected by exclusive replicated lock
            if (remote_pending_users == NULL)
              remote_pending_users = new std::list<RemotePendingUser*>();
            remote_pending_users->push_back(
                new PendingCopyUser(reading, buffer_mask, copy_expr, op_id,
                                    index, term_event, collect_event));
          }
        }
        if (remote_added_users >= user_cache_timeout)
          update_remote_replication_state(applied_events);
#endif // ENABLE_VIEW_REPLICATION
      }
      else
      {
#ifdef ENABLE_VIEW_REPLICATION
        // We need to hold this lock in read-only mode to properly
        // synchronize this with any replication requests that arrive
        AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
        // Send updates to any remote copies to get them in flight
        if (repl_ptr.replicated_copies != NULL)
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_copies->empty());
#endif
          const FieldMask repl_mask = replicated_fields & copy_mask;
          if (!!repl_mask)
          {
            for (LegionMap<AddressSpaceID,FieldMask>::const_iterator
                  it = repl_ptr.replicated_copies->begin(); 
                  it != repl_ptr.replicated_copies->end(); it++)
            {
              if (it->first == source)
                continue;
              const FieldMask overlap = it->second & repl_mask;
              if (!overlap)
                continue;
              RtUserEvent applied_event = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize<bool>(reading);
                rez.serialize(redop);
                rez.serialize(term_event);
                rez.serialize(collect_event);
                rez.serialize(copy_mask);
                copy_expr->pack_expression(rez, it->first);
                rez.serialize(op_id);
                rez.serialize(index);
                rez.serialize(applied_event);
                rez.serialize<bool>(trace_recording);
              }
              runtime->send_view_add_copy_user(it->first, rez);
              applied_events.insert(applied_event);
            }
          }
        }
#endif
        // Now we can do our local analysis
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        add_internal_copy_user(usage, copy_expr, copy_mask, term_event, 
                               collect_event, op_id, index, trace_recording);
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::find_last_users(PhysicalManager *instance,
                                      std::set<ApEvent> &events,
                                      const RegionUsage &usage,
                                      const FieldMask &mask,
                                      IndexSpaceExpression *expr,
                                      std::vector<RtEvent> &ready_events) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      // Check to see if we're on the right node to perform this analysis
      if (logical_owner != local_space)
      {
        const RtUserEvent ready = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(manager->did);
          rez.serialize(&events);
          rez.serialize(usage);
          rez.serialize(mask);
          expr->pack_expression(rez, logical_owner);
          rez.serialize(ready);
        }
        runtime->send_view_find_last_users_request(logical_owner, rez);
        ready_events.push_back(ready);
      }
      else
      {
        const bool expr_dominates = 
          (expr->expr_id == current_users->view_expr->expr_id) ||
          (expr->get_volume() == current_users->get_view_volume());
        {
          // Need a read-only copy of the expr_lock to traverse the tree
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_last_users(usage, expr, expr_dominates,
                                         mask, events);
        }
      }
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_request(AddressSpaceID source,
                                                  const FieldMask &request_mask,
                                                  RtUserEvent done_event)
    //--------------------------------------------------------------------------
    {
      // Atomically we need to package up the response and send it back
      AutoLock r_lock(replicated_lock); 
      if (repl_ptr.replicated_copies == NULL)
        repl_ptr.replicated_copies = 
          new LegionMap<AddressSpaceID,FieldMask>();
      LegionMap<AddressSpaceID,FieldMask>::iterator finder = 
        repl_ptr.replicated_copies->find(source);
      if (finder != repl_ptr.replicated_copies->end())
      {
#ifdef DEBUG_LEGION
        assert(finder->second * request_mask);
#endif
        finder->second |= request_mask; 
      }
      else
        (*repl_ptr.replicated_copies)[source] = request_mask;
      // Update the summary as well
      replicated_fields |= request_mask;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(done_event);
        std::map<PhysicalUser*,unsigned> indexes;
        // Make sure no one else is mutating the state of the tree
        // while we are doing the packing
        AutoLock e_lock(expr_lock,1,false/*exclusive*/);
        current_users->pack_replication(rez, indexes, request_mask, source);
      }
      runtime->send_view_replication_response(source, rez);
    }

    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_response(RtUserEvent done,
                                                        Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_logical_owner());
#endif
      AutoLock r_lock(replicated_lock);
      {
        // Take the view lock so we can modify the cache as well
        // as part of our unpacking
        AutoLock v_lock(view_lock);
        if (current_users == NULL)
        {
          current_users = 
            new ExprView(context, manager, this, manager->instance_domain);
          current_users->add_reference();
        }
        // We need to hold the expr lock here since we might have to 
        // make ExprViews and we need this to be atomic with other
        // operations that might also try to mutate the tree 
        AutoLock e_lock(expr_lock);
        std::vector<PhysicalUser*> users;
        // The source is always from the logical owner space
        current_users->unpack_replication(derez, current_users, 
                                          logical_owner, expr_cache, users);
        // Remove references from all our users
        for (unsigned idx = 0; idx < users.size(); idx++)
          if (users[idx]->remove_reference())
            delete users[idx]; 
      }
#ifdef DEBUG_LEGION
      assert(repl_ptr.replicated_requests != NULL);
#endif
      LegionMap<RtUserEvent,FieldMask>::iterator finder = 
        repl_ptr.replicated_requests->find(done);
#ifdef DEBUG_LEGION
      assert(finder != repl_ptr.replicated_requests->end());
#endif
      // Go through and apply any pending remote users we've recorded 
      if (remote_pending_users != NULL)
      {
        for (std::list<RemotePendingUser*>::iterator it = 
              remote_pending_users->begin(); it != 
              remote_pending_users->end(); /*nothing*/)
        {
          if ((*it)->apply(this, finder->second))
          {
            delete (*it);
            it = remote_pending_users->erase(it);
          }
          else
            it++;
        }
        if (remote_pending_users->empty())
        {
          delete remote_pending_users;
          remote_pending_users = NULL;
        }
      }
      // Record that these fields are now replicated
      replicated_fields |= finder->second;
      repl_ptr.replicated_requests->erase(finder);
      if (repl_ptr.replicated_requests->empty())
      {
        delete repl_ptr.replicated_requests;
        repl_ptr.replicated_requests = NULL;
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_removal(AddressSpaceID source,
                                                  const FieldMask &removal_mask)
    //--------------------------------------------------------------------------
    {
      AutoLock r_lock(replicated_lock);
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(repl_ptr.replicated_copies != NULL);
#endif
      LegionMap<AddressSpaceID,FieldMask>::iterator finder = 
        repl_ptr.replicated_copies->find(source);
#ifdef DEBUG_LEGION
      assert(finder != repl_ptr.replicated_copies->end());
      // We should know about all the fields being removed
      assert(!(removal_mask - finder->second));
#endif
      finder->second -= removal_mask;
      if (!finder->second)
      {
        repl_ptr.replicated_copies->erase(finder);
        if (repl_ptr.replicated_copies->empty())
        {
          delete repl_ptr.replicated_copies;
          repl_ptr.replicated_copies = NULL;
          replicated_fields.clear();
          return;
        }
        // Otherwise fall through and rebuild the replicated fields
      }
      // Rebuild the replicated fields so they are precise
      if (repl_ptr.replicated_copies->size() > 1)
      {
        replicated_fields.clear();
        for (LegionMap<AddressSpaceID,FieldMask>::const_iterator it =
              repl_ptr.replicated_copies->begin(); it !=
              repl_ptr.replicated_copies->end(); it++)
          replicated_fields |= finder->second;
      }
      else
        replicated_fields = repl_ptr.replicated_copies->begin()->second;
    }
#endif // ENABLE_VIEW_REPLICATION
 
    //--------------------------------------------------------------------------
    void MaterializedView::add_internal_task_user(const RegionUsage &usage,
                                            IndexSpaceExpression *user_expr,
                                            const FieldMask &user_mask,
                                            ApEvent term_event, 
                                            RtEvent collect_event, 
                                            UniqueID op_id,
                                            const unsigned index,
                                            const bool trace_recording)
    //--------------------------------------------------------------------------
    {
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                             collect_event, false/*copy user*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy user*/, true/*covers*/);
#endif
      // Hold a reference to this in case it finishes before we're done
      // with the analysis and its get pruned/deleted
      user->add_reference();
      ExprView *target_view = NULL;
      bool has_target_view = false;
      // Handle an easy case first, if the user_expr is the same as the 
      // view_expr for the root then this is easy
      bool update_count = true;
      bool update_cache = false;
      if (user_expr != current_users->view_expr)
      {
        // Hard case where we will have subviews
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // See if we can find the entry in the cache and it's valid 
        // for all of our fields
        LegionMap<IndexSpaceExprID,ExprView*>::const_iterator
          finder = expr_cache.find(user_expr->expr_id);
        if (finder != expr_cache.end())
        {
          target_view = finder->second;
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          if (finder->second->invalid_fields * user_mask)
            has_target_view = true;
        }
        else
          update_cache = true;
        // increment the number of outstanding additions
        outstanding_additions.fetch_add(1);
      }
      else // This is just going to add at the top so never needs to wait
      {
        target_view = current_users;
        update_count = false;
        has_target_view = true;
      }
      if (!has_target_view)
      {
        // This could change the shape of the view tree so we need
        // exclusive privilege son the expr lock to serialize it
        // with everything else traversing the tree
        AutoLock e_lock(expr_lock);
        // If we don't have a target view see if there is a 
        // congruent one already in the tree
        if (target_view == NULL)
        {
          target_view = current_users->find_congruent_view(user_expr);
          if (target_view == NULL)
            target_view = new ExprView(context, manager, this, user_expr);
        }
        if (target_view != current_users)
        {
          // Now see if we need to insert it
          FieldMask insert_mask = user_mask & target_view->invalid_fields;
          if (!!insert_mask)
          {
            // Remove these fields from being invalid before we
            // destroy the insert mask
            target_view->invalid_fields -= insert_mask;
            // Do the insertion into the tree
            current_users->insert_subview(target_view, insert_mask);
          }
        }
      }
      // Now we know the target view and it's valid for all fields
      // so we can add it to the expr view
      target_view->add_current_user(user, term_event, collect_event,
                                    user_mask, trace_recording);
      if (user->remove_reference())
        delete user;
      AutoLock v_lock(view_lock);
      if (update_count)
      {
#ifdef DEBUG_LEGION
        assert(outstanding_additions.load() > 0);
#endif
        if ((outstanding_additions.fetch_sub(1) == 1) && clean_waiting.exists())
        {
          // Wake up the clean waiter
          Runtime::trigger_event(clean_waiting);
          clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
        }
      }
      if (!update_cache)
      {
        // Update the timeout and see if we need to clear the cache
        if (!expr_cache.empty())
        {
          expr_cache_uses++;
          // Check for equality guarantees only one thread in here at a time
          if (expr_cache_uses == user_cache_timeout)
          {
            // Wait until there are are no more outstanding additions
            while (outstanding_additions.load() > 0)
            {
#ifdef DEBUG_LEGION
              assert(!clean_waiting.exists());
#endif
              clean_waiting = Runtime::create_rt_user_event();
              const RtEvent wait_on = clean_waiting;
              v_lock.release();
              wait_on.wait();
              v_lock.reacquire();
            }
            clean_cache<true/*need expr lock*/>();
          }
        }
      }
      else
        expr_cache[user_expr->expr_id] = target_view;
    }

    //--------------------------------------------------------------------------
    void MaterializedView::add_internal_copy_user(const RegionUsage &usage,
                                            IndexSpaceExpression *user_expr,
                                            const FieldMask &user_mask,
                                            ApEvent term_event, 
                                            RtEvent collect_event, 
                                            UniqueID op_id,
                                            const unsigned index,
                                            const bool trace_recording)
    //--------------------------------------------------------------------------
    { 
      // First we're going to check to see if we can add this directly to 
      // an existing ExprView with the same expresssion in which case
      // we'll be able to mark this user as being precise
      ExprView *target_view = NULL;
      bool has_target_view = false;
      // Handle an easy case first, if the user_expr is the same as the 
      // view_expr for the root then this is easy
      bool update_count = false;
      bool update_cache = false;
      if (user_expr != current_users->view_expr)
      {
        // Hard case where we will have subviews
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // See if we can find the entry in the cache and it's valid 
        // for all of our fields
        LegionMap<IndexSpaceExprID,ExprView*>::const_iterator
          finder = expr_cache.find(user_expr->expr_id);
        if (finder != expr_cache.end())
        {
          target_view = finder->second;
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          if (finder->second->invalid_fields * user_mask)
            has_target_view = true;
        }
        // increment the number of outstanding additions
        outstanding_additions.fetch_add(1);
        update_count = true;
      }
      else // This is just going to add at the top so never needs to wait
      {
        target_view = current_users;
        has_target_view = true;
      }
      if (!has_target_view)
      {
        // Do a quick test to see if we can find a target view
        AutoLock e_lock(expr_lock);
        // If we haven't found it yet, see if we can find it
        if (target_view == NULL)
        {
          target_view = current_users->find_congruent_view(user_expr);
          if (target_view != NULL)
            update_cache = true;
        }
        // Don't make it though if we don't already have it
        if (target_view != NULL)
        {
          // No need to insert this if it's the root
          if (target_view != current_users)
          {
            FieldMask insert_mask = target_view->invalid_fields & user_mask;
            if (!!insert_mask)
            {
              target_view->invalid_fields -= insert_mask;
              current_users->insert_subview(target_view, insert_mask);
            }
          }
          has_target_view = true;
        }
      }
      if (has_target_view)
      {
        // If we have a target view, then we know we cover it because
        // the expressions match directly
#ifdef ENABLE_VIEW_REPLICATION
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                               collect_event, true/*copy user*/,true/*covers*/);
#else
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                              true/*copy user*/,true/*covers*/);
#endif
        // Hold a reference to this in case it finishes before we're done
        // with the analysis and its get pruned/deleted
        user->add_reference();
        // We already know the view so we can just add the user directly
        // there and then do any updates that we need to
        target_view->add_current_user(user, term_event, collect_event, 
                                      user_mask, trace_recording);
        if (user->remove_reference())
          delete user;
        if (update_count || update_cache)
        {
          AutoLock v_lock(view_lock);
          if (update_cache)
            expr_cache[user_expr->expr_id] = target_view;
          if (update_count)
          {
#ifdef DEBUG_LEGION
            assert(outstanding_additions.load() > 0);
#endif
            if ((outstanding_additions.fetch_sub(1) == 1) && 
                clean_waiting.exists())
            {
              // Wake up the clean waiter
              Runtime::trigger_event(clean_waiting);
              clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
            }
          }
        }
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(update_count); // this should always be true
        assert(!update_cache); // this should always be false
#endif
        // This is a case where we don't know where to add the copy user
        // so we need to traverse down and find one, 
        {
          // We're traversing the view tree but not modifying it so 
          // we need a read-only copy of the expr_lock
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->add_partial_user(usage, op_id, index,
                                          user_mask, term_event, 
                                          collect_event, user_expr, 
                                          user_expr->get_volume(), 
                                          trace_recording);
        }
        AutoLock v_lock(view_lock);
#ifdef DEBUG_LEGION
        assert(outstanding_additions.load() > 0);
#endif
        if ((outstanding_additions.fetch_sub(1) == 1) && clean_waiting.exists())
        {
          // Wake up the clean waiter
          Runtime::trigger_event(clean_waiting);
          clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
        }
      } 
    }

    //--------------------------------------------------------------------------
    template<bool NEED_EXPR_LOCK>
    void MaterializedView::clean_cache(void)
    //--------------------------------------------------------------------------
    {
      // Clear the cache
      expr_cache.clear();
      // Reset the cache use counter
      expr_cache_uses = 0;
      // Anytime we clean the cache, we also traverse the 
      // view tree and see if there are any views we can 
      // remove because they no longer have live users
      FieldMask dummy_mask; 
      FieldMaskSet<ExprView> clean_set;
      if (NEED_EXPR_LOCK)
      {
        // Take the lock in exclusive mode since we might be modifying the tree
        AutoLock e_lock(expr_lock);
        current_users->clean_views(dummy_mask, clean_set);
        // We can safely repopulate the cache with any view expressions which
        // are still valid, remove all references for views in the clean set 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              clean_set.begin(); it != clean_set.end(); it++)
        {
          if (!!(~(it->first->invalid_fields)))
            expr_cache[it->first->view_expr->expr_id] = it->first;
          if (it->first->remove_reference())
            delete it->first;
        }
      }
      else
      {
        // Same as above, but without needing to acquire the lock
        // because the caller promised that they already have it
        current_users->clean_views(dummy_mask, clean_set);
        // We can safely repopulate the cache with any view expressions which
        // are still valid, remove all references for views in the clean set 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              clean_set.begin(); it != clean_set.end(); it++)
        {
          if (!!(~(it->first->invalid_fields)))
            expr_cache[it->first->view_expr->expr_id] = it->first;
          if (it->first->remove_reference())
            delete it->first;
        }
      }
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void MaterializedView::update_remote_replication_state(
                                              std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_logical_owner());
      assert(!!replicated_fields);
      assert(current_users != NULL);
      assert(remote_added_users >= user_cache_timeout);
#endif
      // We can reset the counter now
      remote_added_users = 0;
      // See what fields haven't been sampled recently and therefore
      // we should stop maintaining as remote duplicates
      const FieldMask deactivate_mask = 
        replicated_fields - remote_copy_pre_fields; 
      // We can clear this now for the next epoch
      remote_copy_pre_fields.clear();
      // If we have any outstanding requests though keep those
      if (repl_ptr.replicated_requests != NULL)
      {
        for (LegionMap<RtUserEvent,FieldMask>::const_iterator it = 
              repl_ptr.replicated_requests->begin(); it !=
              repl_ptr.replicated_requests->end(); it++)
        {
#ifdef DEBUG_LEGION
          assert(it->second * deactivate_mask);
#endif
          remote_copy_pre_fields |= it->second;
        }
      }
      // If we don't have any fields to deactivate then we're done
      if (!deactivate_mask)
        return;
      // Send the message to do the deactivation on the owner node
      RtUserEvent done_event = Runtime::create_rt_user_event();
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(deactivate_mask);
        rez.serialize(done_event);
      }
      runtime->send_view_replication_removal(logical_owner, rez);
      applied_events.insert(done_event);
      // Perform it locally
      {
        // Anytime we do a deactivate that can influence the valid
        // set of ExprView objects so we need to clean the cache
        AutoLock v_lock(view_lock);
#ifdef DEBUG_LEGION
        // There should be no outstanding_additions when we're here
        // because we're already protected by the replication lock
        assert(outstanding_additions.load() == 0);
#endif
        // Go through and remove any users for the deactivate mask
        // Need an exclusive copy of the expr_lock to do this
        AutoLock e_lock(expr_lock);
        current_users->deactivate_replication(deactivate_mask);
        // Then clean the cache since we likely invalidated some
        // things. This will also go through and remove any views
        // that no longer have any active users
        clean_cache<false/*need expr lock*/>();
      }
      // Record that these fields are no longer replicated 
      replicated_fields -= deactivate_mask;
    }
#endif // ENABLE_VIEW_REPLICATION

    //--------------------------------------------------------------------------
    void MaterializedView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(manager->did);
        rez.serialize(owner_space);
        rez.serialize(logical_owner);
        rez.serialize(owner_context);
      }
      runtime->send_materialized_view(target, rez);
      update_remote_instances(target);
    } 

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::handle_send_materialized_view(
                  Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez); 
      DistributedID did;
      derez.deserialize(did);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      AddressSpaceID logical_owner;
      derez.deserialize(logical_owner);
      UniqueID context_uid;
      derez.deserialize(context_uid);
      RtEvent man_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, man_ready);
      if (man_ready.exists() && !man_ready.has_triggered())
      {
        // Defer this until the manager is ready
        DeferMaterializedViewArgs args(did, manager, owner_space,
                                       logical_owner, context_uid);
        runtime->issue_runtime_meta_task(args, 
            LG_LATENCY_RESPONSE_PRIORITY, man_ready);
      }
      else
        create_remote_view(runtime, did, manager, owner_space, 
                           logical_owner, context_uid); 
    }

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::handle_defer_materialized_view(
                                             const void *args, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      const DeferMaterializedViewArgs *dargs = 
        (const DeferMaterializedViewArgs*)args; 
      create_remote_view(runtime, dargs->did, dargs->manager, 
          dargs->owner_space, dargs->logical_owner, dargs->context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::create_remote_view(Runtime *runtime,
                            DistributedID did, PhysicalManager *manager,
                            AddressSpaceID owner_space,
                            AddressSpaceID logical_owner, UniqueID context_uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager->is_physical_manager());
#endif
      PhysicalManager *inst_manager = manager->as_physical_manager();
      void *location;
      MaterializedView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) MaterializedView(runtime->forest,
                                              did, owner_space, 
                                              logical_owner, inst_manager,
                                              context_uid,
                                              false/*register now*/);
      else
        view = new MaterializedView(runtime->forest, did, owner_space,
                                    logical_owner, inst_manager, 
                                    context_uid, false/*register now*/);
      // Register only after construction
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // DeferredView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    DeferredView::DeferredView(RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID owner_sp, bool register_now,
                               CollectiveMapping *mapping)
      : LogicalView(ctx, did, owner_sp, register_now, mapping)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    DeferredView::~DeferredView(void)
    //--------------------------------------------------------------------------
    {
    }

    /////////////////////////////////////////////////////////////
    // FillView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FillView::FillView(RegionTreeForest *ctx, DistributedID did,
                       AddressSpaceID owner_proc,
                       FillViewValue *val, bool register_now,
#ifdef LEGION_SPY
                       UniqueID op_uid,
#endif
                       CollectiveMapping *map)
      : DeferredView(ctx, encode_fill_did(did), owner_proc, register_now, map), 
        value(val)
#ifdef LEGION_SPY
        , fill_op_uid(op_uid)
#endif
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(value != NULL);
#endif
      value->add_reference();
#ifdef LEGION_GC
      log_garbage.info("GC Fill View %lld %d", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space);
#endif
    }

    //--------------------------------------------------------------------------
    FillView::FillView(const FillView &rhs)
      : DeferredView(NULL, 0, 0, false), value(NULL)
#ifdef LEGION_SPY
        , fill_op_uid(0)
#endif
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }
    
    //--------------------------------------------------------------------------
    FillView::~FillView(void)
    //--------------------------------------------------------------------------
    {
      if (value->remove_reference())
        delete value;
    }

    //--------------------------------------------------------------------------
    FillView& FillView::operator=(const FillView &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FillView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_increment(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void FillView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_decrement(owner_space, mutator);
    }
    
    //--------------------------------------------------------------------------
    void FillView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Nothing to do
    }

    //--------------------------------------------------------------------------
    void FillView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Nothing to do
    }

    //--------------------------------------------------------------------------
    void FillView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
      assert(collective_mapping == NULL);
#endif
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_space);
        rez.serialize(value->value_size);
        rez.serialize(value->value, value->value_size);
#ifdef LEGION_SPY
        rez.serialize(fill_op_uid);
#endif
      }
      runtime->send_fill_view(target, rez);
      // We've now done the send so record it
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    void FillView::flatten(CopyFillAggregator &aggregator,
                InstanceView *dst_view, const FieldMask &src_mask,
                IndexSpaceExpression *expr, PredEvent pred_guard,
                const PhysicalTraceInfo &trace_info, EquivalenceSet *tracing_eq,
                std::set<RtEvent> &applied, CopyAcrossHelper *helper)
    //--------------------------------------------------------------------------
    {
      aggregator.record_fill(dst_view, this, src_mask, expr, 
                             pred_guard, tracing_eq, applied, helper);
    }

    //--------------------------------------------------------------------------
    /*static*/ void FillView::handle_send_fill_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      size_t value_size;
      derez.deserialize(value_size);
      void *value = malloc(value_size);
      derez.deserialize(value, value_size);
#ifdef LEGION_SPY
      UniqueID op_uid;
      derez.deserialize(op_uid);
#endif
      
      FillView::FillViewValue *fill_value = 
                      new FillView::FillViewValue(value, value_size);
      void *location;
      FillView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) FillView(runtime->forest, did,
                                      owner_space, fill_value,
                                      false/*register now*/
#ifdef LEGION_SPY
                                      , op_uid
#endif
                                      );
      else
        view = new FillView(runtime->forest, did, owner_space,
                            fill_value, false/*register now*/
#ifdef LEGION_SPY
                            , op_uid
#endif
                            );
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // PhiView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PhiView::PhiView(RegionTreeForest *ctx, DistributedID did, 
                     AddressSpaceID owner_space,
                     PredEvent tguard, PredEvent fguard,
                     FieldMaskSet<DeferredView> &&true_vws,
                     FieldMaskSet<DeferredView> &&false_vws,
                     bool register_now) 
      : DeferredView(ctx, encode_phi_did(did), owner_space, register_now),
        true_guard(tguard), false_guard(fguard),
        true_views(true_vws), false_views(false_vws)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(true_guard.exists());
      assert(false_guard.exists());
      assert(true_views.get_valid_mask() == false_views.get_valid_mask());
#endif
      if (register_now)
        add_resource_references();
#ifdef LEGION_GC
      log_garbage.info("GC Phi View %lld %d", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space);
#endif
    }

    //--------------------------------------------------------------------------
    PhiView::~PhiView(void)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it = 
            true_views.begin(); it != true_views.end(); it++)
        if (it->first->remove_nested_resource_ref(did))
          delete it->first;
      for (FieldMaskSet<DeferredView>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
        if (it->first->remove_nested_resource_ref(did))
          delete it->first;
    }

    //--------------------------------------------------------------------------
    void PhiView::add_resource_references(void)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it = 
            true_views.begin(); it != true_views.end(); it++)
        it->first->add_nested_resource_ref(did);
      for (FieldMaskSet<DeferredView>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
        it->first->add_nested_resource_ref(did);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->add_nested_gc_ref(did, mutator);
      for (FieldMaskSet<DeferredView>::const_iterator it = 
            false_views.begin(); it != false_views.end(); it++)
        it->first->add_nested_gc_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->remove_nested_gc_ref(did, mutator);
      for (FieldMaskSet<DeferredView>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
        it->first->remove_nested_gc_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->add_nested_valid_ref(did, mutator);
      for (FieldMaskSet<DeferredView>::const_iterator it = 
            false_views.begin(); it != false_views.end(); it++)
        it->first->add_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (FieldMaskSet<DeferredView>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->remove_nested_valid_ref(did, mutator);
      for (FieldMaskSet<DeferredView>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
        it->first->remove_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
      assert(collective_mapping == NULL);
#endif
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_space);
        rez.serialize(true_guard);
        rez.serialize(false_guard);
        rez.serialize<size_t>(true_views.size());
        for (FieldMaskSet<DeferredView>::const_iterator it = 
              true_views.begin(); it != true_views.end(); it++)
        {
          rez.serialize(it->first->did);
          rez.serialize(it->second);
        }
        rez.serialize<size_t>(false_views.size());
        for (FieldMaskSet<DeferredView>::const_iterator it = 
              false_views.begin(); it != false_views.end(); it++)
        {
          rez.serialize(it->first->did);
          rez.serialize(it->second);
        }
      }
      runtime->send_phi_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    void PhiView::flatten(CopyFillAggregator &aggregator,
                InstanceView *dst_view, const FieldMask &src_mask,
                IndexSpaceExpression *expr, PredEvent pred_guard,
                const PhysicalTraceInfo &trace_info, EquivalenceSet *tracing_eq,
                std::set<RtEvent> &applied, CopyAcrossHelper *helper)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!(src_mask - true_views.get_valid_mask()));
      assert(!(src_mask - false_views.get_valid_mask()));
#endif
      const PredEvent next_true = !pred_guard.exists() ? true_guard :
        Runtime::merge_events(&trace_info, pred_guard, true_guard);
      for (FieldMaskSet<DeferredView>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
      {
        const FieldMask overlap = src_mask & it->second;
        if (!overlap)
          continue;
        it->first->flatten(aggregator, dst_view, overlap, expr, next_true,
                           trace_info, tracing_eq, applied, helper);
      }
      const PredEvent next_false = !pred_guard.exists() ? false_guard :
        Runtime::merge_events(&trace_info, pred_guard, false_guard);
      for (FieldMaskSet<DeferredView>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
      {
        const FieldMask overlap = src_mask & it->second;
        if (!overlap)
          continue;
        it->first->flatten(aggregator, dst_view, overlap, expr, next_false,
                           trace_info, tracing_eq, applied, helper);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void PhiView::handle_send_phi_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      AddressSpaceID owner;
      derez.deserialize(owner);
      PredEvent true_guard, false_guard;
      derez.deserialize(true_guard);
      derez.deserialize(false_guard);
      std::set<RtEvent> ready_events;
      FieldMaskSet<DeferredView> true_views, false_views;
      size_t num_true_views;
      derez.deserialize(num_true_views);
      for (unsigned idx = 0; idx < num_true_views; idx++)
      {
        DistributedID view_did;
        derez.deserialize(view_did);
        RtEvent ready;
        DeferredView *view = static_cast<DeferredView*>(
            runtime->find_or_request_logical_view(view_did, ready));
        FieldMask mask;
        derez.deserialize(mask);
        true_views.insert(view, mask);
        if (ready.exists() && !ready.has_triggered())
          ready_events.insert(ready);
      }
      size_t num_false_views;
      derez.deserialize(num_false_views);
      for (unsigned idx = 0; idx < num_false_views; idx++)
      {
        DistributedID view_did;
        derez.deserialize(view_did);
        RtEvent ready;
        DeferredView *view = static_cast<DeferredView*>(
            runtime->find_or_request_logical_view(view_did, ready));
        FieldMask mask;
        derez.deserialize(mask);
        false_views.insert(view, mask);
        if (ready.exists() && !ready.has_triggered())
          ready_events.insert(ready);
      }
      // Make the phi view but don't register it yet
      void *location;
      PhiView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) PhiView(runtime->forest, did, owner,
                                     true_guard, false_guard,
                                     std::move(true_views),
                                     std::move(false_views),
                                     false/*register_now*/);
      else
        view = new PhiView(runtime->forest, did, owner, true_guard, 
                           false_guard, std::move(true_views),
                           std::move(false_views), false/*register now*/);
      if (!ready_events.empty())
      {
        RtEvent wait_on = Runtime::merge_events(ready_events);
        DeferPhiViewRegistrationArgs args(view);
        runtime->issue_runtime_meta_task(args, LG_LATENCY_DEFERRED_PRIORITY,
                                         wait_on);
      }
      else
      {
        // Add the resource references
        view->add_resource_references();
        view->register_with_runtime();
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void PhiView::handle_deferred_view_registration(const void *args)
    //--------------------------------------------------------------------------
    {
      const DeferPhiViewRegistrationArgs *pargs = 
        (const DeferPhiViewRegistrationArgs*)args;
      pargs->view->add_resource_references();
      pargs->view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // ReductionView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReductionView::ReductionView(RegionTreeForest *ctx, DistributedID did,
                                 AddressSpaceID own_sp,
                                 AddressSpaceID log_own,
                                 PhysicalManager *man, UniqueID own_ctx, 
                                 bool register_now, CollectiveMapping *mapping)
      : IndividualView(ctx, encode_reduction_did(did), man, own_sp, log_own, 
                       own_ctx, register_now, mapping)
    //--------------------------------------------------------------------------
    {
#ifdef LEGION_GC
      log_garbage.info("GC Reduction View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space,
          LEGION_DISTRIBUTED_ID_FILTER(manager->did));
#endif
    }

    //--------------------------------------------------------------------------
    ReductionView::~ReductionView(void)
    //--------------------------------------------------------------------------
    { 
      if (!initial_user_events.empty())
      {
        for (std::set<ApEvent>::const_iterator it = initial_user_events.begin();
              it != initial_user_events.end(); it++)
          filter_local_users(*it);
      }
#if !defined(LEGION_DISABLE_EVENT_PRUNING) && defined(DEBUG_LEGION)
      assert(writing_users.empty());
      assert(reduction_users.empty());
      assert(reading_users.empty());
      assert(outstanding_gc_events.empty());
#endif
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_initial_user(ApEvent term_event, 
                                         const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         const UniqueID op_id,
                                         const unsigned index)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(IS_READ_ONLY(usage) || IS_REDUCE(usage));
#endif
      // We don't use field versions for doing interference tests on
      // reductions so there is no need to record it
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                term_event, false/*copy*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy*/, true/*covers*/);
#endif
      user->add_reference();
      add_physical_user(user, IS_READ_ONLY(usage), term_event, user_mask);
      initial_user_events.insert(term_event);
      // Don't need to actual launch a collection task, destructor
      // will handle this case
      outstanding_gc_events.insert(term_event);
    }

    //--------------------------------------------------------------------------
    ApEvent ReductionView::register_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *user_expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         CollectiveMapping *analysis_mapping,
                                         size_t local_collective_arrivals,
                                         std::vector<RtEvent> &registered,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const AddressSpaceID source,
                                         const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(usage.redop == manager->redop);
      assert(target == manager);
#endif
      // Handle the collective rendezvous if necessary
      if (local_collective_arrivals > 0)
        return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, analysis_mapping, local_collective_arrivals,
              registered, applied_events, trace_info, symbolic);
      // Quick test for empty index space expressions
      if (!symbolic && user_expr->is_empty())
        return manager->get_use_event(term_event);
      if (!is_logical_owner())
      {
        // If we're not the logical owner send a message there 
        // to do the analysis and provide a user event to trigger
        // with the precondition
        ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
        RtUserEvent registered_event = Runtime::create_rt_user_event();
        RtUserEvent applied_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(target->did);
          rez.serialize(usage);
          rez.serialize(user_mask);
          rez.serialize(user_expr->handle);
          rez.serialize(op_id);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(term_event);
          rez.serialize(collect_event);
          rez.serialize(local_collective_arrivals);
          rez.serialize(ready_event);
          rez.serialize(registered_event);
          rez.serialize(applied_event);
          trace_info.pack_trace_info(rez, applied_events);
        }
        // Add a remote valid reference that will be removed by 
        // the receiver once the changes have been applied
        WrapperReferenceMutator mutator(applied_events);
        add_base_valid_ref(REMOTE_DID_REF, &mutator);
        runtime->send_view_register_user(logical_owner, rez);
        registered.push_back(registered_event);
        applied_events.insert(applied_event);
        return ready_event;
      }
      else
      {
        std::set<ApEvent> wait_on_events;
        ApEvent start_use_event = manager->get_use_event(term_event);
        if (start_use_event.exists())
          wait_on_events.insert(start_use_event);
        // At the moment we treat exclusive reductions the same as
        // atomic reductions, this might change in the future
        const RegionUsage reduce_usage(usage.privilege,
            (usage.prop == LEGION_EXCLUSIVE) ? LEGION_ATOMIC : usage.prop,
            usage.redop);
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(reduce_usage, user_mask,
                                      user_expr, wait_on_events);
        }
        // Add our local user
        const bool issue_collect = add_user(reduce_usage, user_expr,
                                      user_mask, term_event, collect_event,
                                      op_id, index, false/*copy*/,
                                      applied_events, trace_info.recording);
        // Launch the garbage collection task, if it doesn't exist
        // then the user wasn't registered anyway, see add_local_user
        if (issue_collect)
        {
          WrapperReferenceMutator mutator(applied_events);
          defer_collect_user(get_manager(), term_event, collect_event,&mutator);
        }
        if (!wait_on_events.empty())
          return Runtime::merge_events(&trace_info, wait_on_events);
        else
          return ApEvent::NO_AP_EVENT;
      }
    }

    //--------------------------------------------------------------------------
    ApEvent ReductionView::find_copy_preconditions(bool reading,
                                            ReductionOpID redop,
                                            const FieldMask &copy_mask,
                                            IndexSpaceExpression *copy_expr,
                                            UniqueID op_id, unsigned index,
                                            std::set<RtEvent> &applied_events,
                                            const PhysicalTraceInfo &trace_info)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
        RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize<bool>(reading);
          rez.serialize(redop);
          rez.serialize(copy_mask);
          copy_expr->pack_expression(rez, logical_owner);
          rez.serialize(op_id);
          rez.serialize(index);
          rez.serialize(ready_event);
          rez.serialize(applied);
          trace_info.pack_trace_info(rez, applied_events);
        }
        runtime->send_view_find_copy_preconditions_request(logical_owner, rez);
        applied_events.insert(applied);
        return ready_event;
      }
      else
      {
        std::set<ApEvent> preconditions;
        ApEvent start_use_event = manager->get_use_event();
        if (start_use_event.exists())
          preconditions.insert(start_use_event);
        if (reading)
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reading_preconditions(copy_mask, copy_expr, preconditions);
        }
        else if (redop > 0)
        {
#ifdef DEBUG_LEGION
          assert(redop == manager->redop);
#endif
          // With bulk reduction copies we're always doing atomic reductions
          const RegionUsage usage(LEGION_REDUCE, LEGION_ATOMIC, redop);
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(usage,copy_mask,copy_expr,preconditions);
        }
        else
        {
          AutoLock v_lock(view_lock);
          find_writing_preconditions(copy_mask, copy_expr, preconditions);
        }
        // Return any preconditions we found to the aggregator
        if (preconditions.empty())
          return ApEvent::NO_AP_EVENT;
        return Runtime::merge_events(&trace_info, preconditions);
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_copy_user(bool reading, ReductionOpID redop,
                                      ApEvent term_event, RtEvent collect_event,
                                      const FieldMask &copy_mask,
                                      IndexSpaceExpression *copy_expr,
                                      UniqueID op_id, unsigned index,
                                      std::set<RtEvent> &applied_events,
                                      const bool trace_recording,
                                      const AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // At most one of these should be true 
      assert(!(reading && (redop > 0)));
#endif
      if (!is_logical_owner())
      {
        RtUserEvent applied_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize<bool>(reading);
          rez.serialize(redop);
          rez.serialize(term_event);
          rez.serialize(collect_event);
          rez.serialize(copy_mask);
          copy_expr->pack_expression(rez, logical_owner);
          rez.serialize(op_id);
          rez.serialize(index);
          rez.serialize(applied_event);
          rez.serialize<bool>(trace_recording);
        }
        // Add a remote valid reference that will be removed by 
        // the receiver once the changes have been applied
        WrapperReferenceMutator mutator(applied_events);
        add_base_valid_ref(REMOTE_DID_REF, &mutator);
        runtime->send_view_add_copy_user(logical_owner, rez);
        applied_events.insert(applied_event);
      }
      else
      {
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        const bool issue_collect = add_user(usage, copy_expr, copy_mask,
            term_event, collect_event, op_id, index, true/*copy*/,
            applied_events, trace_recording);
        // Launch the garbage collection task, if it doesn't exist
        // then the user wasn't registered anyway, see add_local_user
        if (issue_collect)
        {
          WrapperReferenceMutator mutator(applied_events);
          defer_collect_user(get_manager(), term_event, collect_event,&mutator);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_last_users(PhysicalManager *instance,
                                        std::set<ApEvent> &events,
                                        const RegionUsage &usage,
                                        const FieldMask &mask,
                                        IndexSpaceExpression *expr,
                                        std::vector<RtEvent> &ready_events)const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      // Check to see if we're on the right node to perform this analysis
      if (logical_owner != local_space)
      {
        const RtUserEvent ready = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(instance->did);
          rez.serialize(&events);
          rez.serialize(usage);
          rez.serialize(mask);
          expr->pack_expression(rez, logical_owner);
          rez.serialize(ready);
        }
        runtime->send_view_find_last_users_request(logical_owner, rez);
        ready_events.push_back(ready);
      }
      else
      {
        if (IS_READ_ONLY(usage))
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reading_preconditions(mask, expr, events);
        }
        else if (usage.redop > 0)
        {
#ifdef DEBUG_LEGION
          assert(usage.redop == manager->redop);
#endif
          // With bulk reduction copies we're always doing atomic reductions
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(usage, mask, expr, events);
        }
        else
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_initializing_last_users(mask, expr, events);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_reducing_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               std::set<ApEvent> &wait_on) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences(writing_users, user_expr, user_mask, wait_on);
      find_dependences(reading_users, user_expr, user_mask, wait_on);
      // check for coherence dependences on previous reduction users
      for (EventFieldUsers::const_iterator uit = reduction_users.begin();
            uit != reduction_users.end(); uit++)
      {
        const FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
#ifdef DEBUG_LEGION
          assert(it->first->usage.redop == usage.redop);
#endif
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          // If they are both simultaneous then we can skip
          if (IS_SIMULT(usage) && IS_SIMULT(it->first->usage))
            continue;
          // Atomic and exclusive are the same for the purposes of reductions
          // at the moment since we'll end up using the reservations to 
          // protect the use of the instance anyway
          if ((IS_EXCLUSIVE(usage) || IS_ATOMIC(usage)) && 
              (IS_EXCLUSIVE(it->first->usage) || IS_ATOMIC(it->first->usage)))
            continue;
          // Otherwise we need to check for dependences
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          wait_on.insert(uit->first);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_dependences(const EventFieldUsers &users,
                                         IndexSpaceExpression *user_expr,
                                         const FieldMask &user_mask,
                                         std::set<ApEvent> &wait_on) const
    //--------------------------------------------------------------------------
    {
      for (EventFieldUsers::const_iterator uit =
            users.begin(); uit != users.end(); uit++)
      {
        const FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          wait_on.insert(uit->first);
          break;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_writing_preconditions(
                                                const FieldMask &user_mask,
                                                IndexSpaceExpression *user_expr,
                                                std::set<ApEvent> &wait_on)
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences_and_filter(writing_users, user_expr, user_mask, wait_on); 
      find_dependences_and_filter(reduction_users, user_expr,user_mask,wait_on);
      find_dependences_and_filter(reading_users, user_expr, user_mask, wait_on);
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_dependences_and_filter(EventFieldUsers &users,
                                                IndexSpaceExpression *user_expr,
                                                const FieldMask &user_mask,
                                                std::set<ApEvent> &wait_on)
    //--------------------------------------------------------------------------
    {
      for (EventFieldUsers::iterator uit = users.begin();
            uit != users.end(); /*nothing*/)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
        {
          uit++;
          continue;
        }
        std::vector<PhysicalUser*> to_delete;
        for (EventUsers::iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          wait_on.insert(uit->first);
          // See if we can prune out this user because it is dominated
          if (expr_overlap->get_volume() == it->first->expr->get_volume())
          {
            it.filter(overlap);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
        if (!to_delete.empty())
        {
          for (std::vector<PhysicalUser*>::const_iterator it = 
                to_delete.begin(); it != to_delete.end(); it++)
          {
            uit->second.erase(*it);
            if ((*it)->remove_reference())
              delete (*it);
          }
          if (uit->second.empty())
          {
            EventFieldUsers::iterator to_erase = uit++;
            users.erase(to_erase);
          }
          else
          {
            uit->second.tighten_valid_mask();
            uit++;
          }
        }
        else
          uit++;
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_reading_preconditions(const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         std::set<ApEvent> &preconditions) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences(writing_users, user_expr, user_mask, preconditions);
      find_dependences(reduction_users, user_expr, user_mask, preconditions);
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_initializing_last_users(
                                         const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         std::set<ApEvent> &preconditions) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      // we know that reduces dominate earlier fills so we don't need to check
      // those, but we do need to check both reducers and readers since it is
      // possible there were no readers of reduction instance
      for (EventFieldUsers::const_iterator uit = reduction_users.begin();
            uit != reduction_users.end(); uit++)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          preconditions.insert(uit->first);
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
      }
      for (EventFieldUsers::const_iterator uit = reading_users.begin();
            uit != reading_users.end(); uit++)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          preconditions.insert(uit->first);
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
      }
    }

    //--------------------------------------------------------------------------
    bool ReductionView::add_user(const RegionUsage &usage,
                                 IndexSpaceExpression *user_expr,
                                 const FieldMask &user_mask, 
                                 ApEvent term_event, RtEvent collect_event,
                                 UniqueID op_id, unsigned index, bool copy_user,
                                 std::set<RtEvent> &applied_events,
                                 const bool trace_recording)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
#endif
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *new_user = new PhysicalUser(usage, user_expr, op_id, index, 
                                     collect_event, copy_user, true/*covers*/);
#else
      PhysicalUser *new_user = new PhysicalUser(usage, user_expr, op_id, index, 
                                                copy_user, true/*covers*/);
#endif
      new_user->add_reference();
      // No matter what, we retake the lock in exclusive mode so we
      // can handle any clean-up and add our user
      AutoLock v_lock(view_lock);
      add_physical_user(new_user, IS_READ_ONLY(usage), term_event, user_mask);

      if (outstanding_gc_events.find(term_event) == outstanding_gc_events.end())
      {
        outstanding_gc_events.insert(term_event);
        return true;
      }
      else
        return false;
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_physical_user(PhysicalUser *user, bool reading,
                                          ApEvent term_event, 
                                          const FieldMask &user_mask)
    //--------------------------------------------------------------------------
    {
      // Better already be holding the lock
      EventUsers &event_users = reading ? reading_users[term_event] : 
                 IS_REDUCE(user->usage) ? reduction_users[term_event] : 
                                          writing_users[term_event];
#ifdef DEBUG_LEGION
      assert(event_users.find(user) == event_users.end());
#endif
      event_users.insert(user, user_mask);
    }

    //--------------------------------------------------------------------------
    void ReductionView::filter_local_users(ApEvent term_event)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(context->runtime, 
                        REDUCTION_VIEW_FILTER_LOCAL_USERS_CALL);
      // Better be holding the lock before calling this
      std::set<ApEvent>::iterator event_finder = 
        outstanding_gc_events.find(term_event);
      if (event_finder != outstanding_gc_events.end())
      {
        EventFieldUsers::iterator finder = writing_users.find(term_event);
        if (finder != writing_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          writing_users.erase(finder);
        }
        finder = reduction_users.find(term_event);
        if (finder != reduction_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          reduction_users.erase(finder);
        }
        finder = reading_users.find(term_event);
        if (finder != reading_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          reading_users.erase(finder);
        }
        outstanding_gc_events.erase(event_finder);
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mutator != NULL);
#endif
      // Only the logical owner adds the full GC reference as this is where
      // the actual garbage collection algorithm will take place and we know
      // that we have all the valid gc event users
      if (is_logical_owner())
        add_base_gc_ref(PENDING_GC_REF, mutator);
      else
        add_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    bool ReductionView::remove_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_logical_owner())
        return remove_base_gc_ref(PENDING_GC_REF, mutator);
      else
        return remove_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    void ReductionView::collect_users(const std::set<ApEvent> &term_events)
    //--------------------------------------------------------------------------
    {
      // Do not do this if we are in LegionSpy so we can see 
      // all of the dependences
#ifndef LEGION_DISABLE_EVENT_PRUNING
      AutoLock v_lock(view_lock);
      for (std::set<ApEvent>::const_iterator it = term_events.begin();
            it != term_events.end(); it++)
      {
        filter_local_users(*it); 
      }
#endif
    }

    //--------------------------------------------------------------------------
    void ReductionView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      // Don't take the lock, it's alright to have duplicate sends
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(manager->did);
        rez.serialize(owner_space);
        rez.serialize(logical_owner);
        rez.serialize(owner_context);
      }
      runtime->send_reduction_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    ReductionOpID ReductionView::get_redop(void) const
    //--------------------------------------------------------------------------
    {
      return manager->redop;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::handle_send_reduction_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez); 
      DistributedID did;
      derez.deserialize(did);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      AddressSpaceID logical_owner;
      derez.deserialize(logical_owner);
      UniqueID context_uid;
      derez.deserialize(context_uid);

      RtEvent man_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, man_ready);
      if (man_ready.exists() && !man_ready.has_triggered())
      {
        // Defer this until the manager is ready
        DeferReductionViewArgs args(did, manager, owner_space,
                                    logical_owner, context_uid);
        runtime->issue_runtime_meta_task(args,
            LG_LATENCY_RESPONSE_PRIORITY, man_ready);
      }
      else
        create_remote_view(runtime, did, manager, owner_space, 
                           logical_owner, context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::handle_defer_reduction_view(
                                             const void *args, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      const DeferReductionViewArgs *dargs = 
        (const DeferReductionViewArgs*)args; 
      create_remote_view(runtime, dargs->did, dargs->manager, 
          dargs->owner_space, dargs->logical_owner, dargs->context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::create_remote_view(Runtime *runtime,
                            DistributedID did, PhysicalManager *manager,
                            AddressSpaceID owner_space, 
                            AddressSpaceID logical_owner, UniqueID context_uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager->is_reduction_manager());
#endif
      void *location;
      ReductionView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) ReductionView(runtime->forest, did, owner_space, 
                                           logical_owner, manager,
                                           context_uid, false/*register now*/);
      else
        view = new ReductionView(runtime->forest, did, owner_space,
                                 logical_owner, manager, 
                                 context_uid, false/*register now*/);
      // Only register after construction
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // CollectiveView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    CollectiveView::CollectiveView(RegionTreeForest *ctx, DistributedID id,
                                   AddressSpaceID owner_proc, 
                                   UniqueID owner_context, 
                                   const std::vector<IndividualView*> &views,
                                   const std::vector<DistributedID> &insts,
                                   bool register_now,CollectiveMapping *mapping)
      : InstanceView(ctx, id, owner_proc, owner_context, register_now, mapping),
        instances(insts), local_views(views), deletion_notified(false) 
    //--------------------------------------------------------------------------
    {
      for (std::vector<IndividualView*>::const_iterator it =
            local_views.begin(); it != local_views.end(); it++)
      {
#ifdef DEBUG_LEGION
        // For collective instances we always want the logical analysis 
        // node for the view to be on the same node as the owner for actual
        // physical instance to aid in our ability to do the analysis
        // See the get_analysis_space function for why we check this
        assert((*it)->logical_owner == (*it)->get_manager()->owner_space);
#endif
        (*it)->add_nested_resource_ref(did);
        // Record ourselves with each of our local views so they can 
        // notify us when they are deleted
        PhysicalManager *manager = (*it)->get_manager();
        manager->register_deletion_subscriber(this);
      }
      // If we're the owner then the context also holds a reference to us to 
      // keep the entire collective view alive as long as it can still match 
      if (is_owner())
        add_base_resource_ref(CONTEXT_REF);
    }

    //--------------------------------------------------------------------------
    CollectiveView::~CollectiveView(void)
    //--------------------------------------------------------------------------
    {
      for (std::vector<IndividualView*>::const_iterator it =
            local_views.begin(); it != local_views.end(); it++)
        if ((*it)->remove_nested_resource_ref(did))
          delete (*it);
      for (std::map<PhysicalManager*,IndividualView*>::const_iterator it =
            remote_instances.begin(); it != remote_instances.end(); it++)
        if (it->second->remove_nested_resource_ref(did))
          delete it->second;
    }

    //--------------------------------------------------------------------------
    AddressSpaceID CollectiveView::get_analysis_space(
                                                PhysicalManager *instance) const
    //--------------------------------------------------------------------------
    {
      return instance->owner_space;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_instance_deletion(PhysicalManager *manager)
    //--------------------------------------------------------------------------
    {
      notify_instance_deletion(manager->tree_id);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::add_subscriber_reference(PhysicalManager *manager)
    //--------------------------------------------------------------------------
    {
      add_nested_resource_ref(manager->did);
    }

    //--------------------------------------------------------------------------
    bool CollectiveView::remove_subscriber_reference(PhysicalManager *manager)
    //--------------------------------------------------------------------------
    {
      return remove_nested_resource_ref(manager->did);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_instance_deletion(RegionTreeID tid)
    //--------------------------------------------------------------------------
    {
      // Check to see if we're the first deletion arrival
      if (deletion_notified.exchange(true))
        return;
      if (is_owner())
      {
        // Notify the context that this can be deleted
        // See if the context is local or not
        const AddressSpaceID ctx_space = 
          runtime->get_runtime_owner(owner_context);
        if (ctx_space != local_space)
        {
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(tid);
            rez.serialize(owner_context);
          }
          runtime->send_collective_view_deletion(ctx_space, rez);
        }
        else
        {
          InnerContext *context = 
            runtime->find_context(owner_context, true/*can fail*/);
          if (context != NULL)
          {
            context->notify_collective_deletion(tid, did);
            if (context->remove_reference())
              delete context;
          }
        }
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(collective_mapping != NULL);
        assert(collective_mapping->contains(local_space));
#endif
        // Send the notification down to the parent
        Serializer rez;
        rez.serialize(did);
        rez.serialize(tid);
        runtime->send_collective_view_notification(
            collective_mapping->get_parent(owner_space, local_space), rez);
      }
      // Unregister ourselves with all our local instances
      for (std::vector<IndividualView*>::const_iterator it =
            local_views.begin(); it != local_views.end(); it++)
      {
        PhysicalManager *manager = (*it)->get_manager();
        manager->unregister_deletion_subscriber(this);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_collective_view_deletion(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DistributedID did;
      derez.deserialize(did);
      RegionTreeID tid;
      derez.deserialize(tid);
      // Might have already received a deletion from somewhere else so 
      // do a weak find check
      DistributedCollectable *dc = 
        runtime->weak_find_distributed_collectable(did);
      if (dc == NULL)
        return;
      CollectiveView *view = static_cast<CollectiveView*>(dc);
      view->notify_instance_deletion(tid);
      if (view->remove_base_resource_ref(RUNTIME_REF))
        delete view;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Propagate gc references to all the children
      if ((collective_mapping != NULL) && 
          collective_mapping->contains(local_space))
      {
        std::vector<AddressSpaceID> children;
        collective_mapping->get_children(owner_space, local_space, children);
        for (unsigned idx = 0; idx < children.size(); idx++)
          send_remote_gc_increment(children[idx], mutator);
      }
      // Add valid references to our local views
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        local_views[idx]->add_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Remove gc references from all the children
      if ((collective_mapping != NULL) &&
          collective_mapping->contains(local_space))
      {
        std::vector<AddressSpaceID> children;
        collective_mapping->get_children(owner_space, local_space, children);
        for (unsigned idx = 0; idx < children.size(); idx++)
          send_remote_gc_decrement(children[idx], mutator);
      }
      // Remove valid references from our local views
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        local_views[idx]->remove_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_owner())
      {
        // Send out gc references to all the children
        if (collective_mapping != NULL)
        {
          std::vector<AddressSpaceID> children;
          collective_mapping->get_children(owner_space, local_space, children);
          for (unsigned idx = 0; idx < children.size(); idx++)
            send_remote_gc_increment(children[idx], mutator);
        }
      }
      else
      {
        // Propagate valid references down towards the owner
        if ((collective_mapping != NULL) &&
            collective_mapping->contains(local_space))
          send_remote_valid_increment(
            collective_mapping->get_parent(owner_space, local_space), mutator);
        else
          send_remote_valid_increment(owner_space, mutator);
      }
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_owner())
      {
        // Remove gc references on all the children
        if (collective_mapping != NULL)
        {
          std::vector<AddressSpaceID> children;
          collective_mapping->get_children(owner_space, local_space, children);
          for (unsigned idx = 0; idx < children.size(); idx++)
            send_remote_gc_decrement(children[idx], mutator);
        }
      }
      else
      {
        // Remove valid references down towards the owner
        if ((collective_mapping != NULL) &&
            collective_mapping->contains(local_space))
          send_remote_valid_decrement(
            collective_mapping->get_parent(owner_space, local_space), mutator);
        else
          send_remote_valid_decrement(owner_space, mutator);
      }
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::fill_from(FillView *fill_view,
                                      ApEvent precondition,
                                      PredEvent predicate_guard,
                                      IndexSpaceExpression *fill_expression,
                                      Operation *op, const unsigned index,
                                      const FieldMask &fill_mask,
                                      const PhysicalTraceInfo &trace_info,
                                      std::set<RtEvent> &recorded_events,
                                      std::set<RtEvent> &applied_events,
                                      CopyAcrossHelper *across_helper,
                                      const bool manage_dst_events,
                                      const bool fill_restricted,
                                      const bool need_valid_return)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // Should never have a copy-across with a collective manager as the target
      assert(manage_dst_events);
      assert(across_helper == NULL);
      assert(collective_mapping != NULL);
#endif
      // This one is easy, just tree broadcast out to all the nodes and 
      // perform the fill operation on each one of them
      ApEvent result;
      if (need_valid_return)
        result = Runtime::create_ap_user_event(&trace_info);
      if (!collective_mapping->contains(local_space))
      {
        // This node doesn't have any instances, so start at one that
        // is contained within the collective mapping
        AddressSpaceID origin = collective_mapping->find_nearest(local_space);
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(fill_view->did);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          fill_expression->pack_expression(rez, origin);
          rez.serialize<bool>(fill_restricted);
          if (fill_restricted)
            op->pack_remote_operation(rez, origin, applied_events);
          rez.serialize(index);
          rez.serialize(op->get_ctx_index());
          rez.serialize(fill_mask);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (trace_info.recording)
          {
            ApBarrier bar;
            ShardID sid = 0;
            if (need_valid_return)
            {
              bar = ApBarrier(Realm::Barrier::create_barrier(1/*arrivals*/));
              sid = trace_info.record_managed_barrier(bar, 1/*arrivals*/);
              result = bar;
            }
            rez.serialize(bar);
            if (bar.exists())
              rez.serialize(sid);
          }
          else
          {
            ApUserEvent to_trigger;
            if (need_valid_return)
            {
              to_trigger = Runtime::create_ap_user_event(&trace_info);
              result = to_trigger;
            }
            rez.serialize(to_trigger);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_fill(origin, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      else
      {
        ApUserEvent to_trigger;
        if (need_valid_return)
        {
          to_trigger = Runtime::create_ap_user_event(&trace_info);
          result = to_trigger;
        }
        perform_collective_fill(fill_view, precondition,
            predicate_guard, fill_expression, op, index, op->get_ctx_index(),
            fill_mask, trace_info, recorded_events, applied_events,
            to_trigger, local_space, fill_restricted);
      }
      return result;
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::copy_from(InstanceView *src_view,
                                      ApEvent precondition,
                                      PredEvent predicate_guard,
                                      ReductionOpID reduction_op_id,
                                      IndexSpaceExpression *copy_expression,
                                      Operation *op, const unsigned index,
                                      const FieldMask &copy_mask,
                                      PhysicalManager *src_point,
                                      const PhysicalTraceInfo &trace_info,
                                      std::set<RtEvent> &recorded_events,
                                      std::set<RtEvent> &applied_events,
                                      CopyAcrossHelper *across_helper,
                                      const bool manage_dst_events,
                                      const bool copy_restricted,
                                      const bool need_valid_return)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // Should never have a copy-across with a collective manager as the target
      assert(manage_dst_events);
      assert(across_helper == NULL);
      assert(collective_mapping != NULL);
      assert(reduction_op_id == src_view->get_redop());
#endif
      // Several cases here:
      // 1. The source is a normal individual manager - in this case we'll issue
      //    the copy/reduction from the source to an instance on the closest
      //    node and then build the broadcast tree from there 
      // 2. The source is another normal collective manager - we'll do a 
      //    broadcast out to all the nodes and have each of them pick a 
      //    source instance to copy from and then do the copy
      // 3. The source is a reduction collective instance with the same 
      //      collective mapping as the destination - broadcast control
      //    out to all the nodes and then perform the all-reduce between the
      //    instances of the source, then do the reduction the same as the 
      //    case for copies with a normal collective manager
      // 4. The source is a reduction manager that is either an individual
      //      instance or a collective instance with a different mapping
      //      than the destination - Build a reduction tree down to a
      //    single instance if necessary and then broadcast out the
      //    reduction data to all the other instances
      ApUserEvent all_done;
      if (need_valid_return)
        all_done = Runtime::create_ap_user_event(&trace_info);
      if (!src_view->is_collective_view())
      {
        // Case 1: the source is an individual manager
        // Copy to one of our instances and then broadcast it
        IndividualView *source_view = src_view->as_individual_view();
        const UniqueID op_id = op->get_unique_op_id();
        // Get the precondition as well
        const ApEvent src_pre = source_view->find_copy_preconditions(
            true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
            op_id, index, applied_events, trace_info);
        if (src_pre.exists())
        {
          if (precondition.exists())
            precondition =
              Runtime::merge_events(&trace_info, precondition, src_pre);
          else
            precondition = src_pre;
        }
        PhysicalManager *source_manager = source_view->get_manager();
        std::vector<CopySrcDstField> src_fields;
        source_manager->compute_copy_offsets(copy_mask, src_fields);
        // We have to follow the tree for other kinds of operations here
        const AddressSpaceID origin = select_origin_space(); 
        ApUserEvent copy_done = Runtime::create_ap_user_event(&trace_info);
        // Record the copy done event on the source view
        source_view->add_copy_user(true/*reading*/, 0/*redop*/, copy_done,
            trace_info.get_collect_event(), copy_mask, copy_expression,
            op_id, index, recorded_events, trace_info.recording,
            runtime->address_space);
        ApBarrier all_bar;
        ShardID owner_shard = 0;
        if (trace_info.recording && 
            (all_done.exists() || (source_view->get_redop() > 0)))
        {
          const size_t arrivals = collective_mapping->size();
          all_bar = ApBarrier(Realm::Barrier::create_barrier(arrivals));
          owner_shard = trace_info.record_managed_barrier(all_bar, arrivals);
          // Tracing copy-optimization will eliminate this when
          // the trace gets optimized
          if (all_done.exists())
            Runtime::trigger_event(&trace_info, all_done, all_bar);
          if (source_view->get_redop() > 0)
          {
            Runtime::trigger_event(&trace_info, copy_done, all_bar);
#ifdef DEBUG_LEGION
            copy_done = ApUserEvent::NO_AP_USER_EVENT;
#endif
          }
        }
        const UniqueInst src_inst(source_view);
        if (origin != local_space)
        {
          const RtUserEvent recorded = Runtime::create_rt_user_event();
          const RtUserEvent applied = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(this->did);
            if (reduction_op_id > 0)
              rez.serialize(source_view->did);
            source_view->pack_fields(rez, src_fields);
            src_inst.serialize(rez);
            rez.serialize(precondition);
            rez.serialize(predicate_guard);
            copy_expression->pack_expression(rez, origin);
            rez.serialize<bool>(copy_restricted);
            if (copy_restricted)
              op->pack_remote_operation(rez, origin, applied_events);
            rez.serialize(index);
            rez.serialize(op->get_ctx_index());
            rez.serialize(copy_mask);
            trace_info.pack_trace_info(rez, applied_events);
            rez.serialize(recorded);
            rez.serialize(applied);
            if (trace_info.recording)
            {
              // If this is a reducecast case, then the barrier is for
              // all of the different reductions
              if (source_view->get_redop() == 0)
              {
                ApBarrier copy_bar(Realm::Barrier::create_barrier(1/*count*/));
                ShardID sid = trace_info.record_managed_barrier(copy_bar, 1);
                Runtime::trigger_event(&trace_info, copy_done, copy_bar);
                rez.serialize(copy_bar);
                rez.serialize(sid);
              }
              rez.serialize(all_bar);
              if (all_bar.exists())
                rez.serialize(owner_shard);
            }
            else
            {
              rez.serialize(copy_done);
              if (source_view->get_redop() == 0)
                rez.serialize(all_done);
            }
            rez.serialize(origin);
          }
          if (reduction_op_id > 0)
            runtime->send_collective_distribute_reducecast(origin, rez);
          else
            runtime->send_collective_distribute_broadcast(origin, rez);
          recorded_events.insert(recorded);
          applied_events.insert(applied);
        }
        else
        {
          if (reduction_op_id > 0)
            perform_collective_reducecast(source_view->as_reduction_view(), 
                src_fields, precondition, predicate_guard, copy_expression,
                op, index, op->get_ctx_index(), copy_mask, src_inst, 
                trace_info, recorded_events, applied_events, copy_done, 
                all_bar, owner_shard, origin, copy_restricted);
          else
            perform_collective_broadcast(src_fields, precondition,
                predicate_guard, copy_expression, op, index, 
                op->get_ctx_index(), copy_mask, src_inst, trace_info,
                recorded_events, applied_events, copy_done, all_done, all_bar,
                owner_shard, origin, copy_restricted); 
        }
      }
      else
      {
        CollectiveView *collective = src_view->as_collective_view();
        const AddressSpaceID origin = select_origin_space();
        // If the source is a reduction collective instance then we need
        // to see if we can go down the point-wise route based on performing
        // an all-reduce, or whether we have to do a tree reduction followed
        // by a tree broadcast. To do the all-reduce path we need all the
        // collective mappings for both collective instances to be the same
        uint64_t allreduce_tag = 0;
        if (collective->is_allreduce_view())
        {
          // Case 3: this is conceptually an all-reduce
          // We'll handle two separate cases here depending on whether
          // the two collective instances have matching collective mappings
          if ((collective_mapping != collective->collective_mapping) &&
              (*collective_mapping != *(collective->collective_mapping)))
          {
            // The two collective mappings do not align, which should
            // be fairly uncommon, but we'll handle it anyway
            // In this case we'll do a reduction down to a single
            // instance in the source collective manager and then 
            // broadcast back out to all the destination instances
            // For correctness, the reduce cast must start whereever
            // a comparable broadcast or fill would have started
            // on the destination collective instance
            perform_collective_hourglass(collective->as_allreduce_view(),
                precondition, predicate_guard, copy_expression, op, index, 
                copy_mask, (src_point != NULL) ? src_point->did : 0, 
                trace_info, recorded_events, applied_events, 
                all_done, origin, copy_restricted);
            return all_done;
          }
          // Otherwise we can fall through and do the allreduce as part
          // of the pointwise copy, get a tag through for unique identification
          if (origin == local_space)
          {
            AllreduceView *allreduce = collective->as_allreduce_view();
            allreduce_tag = allreduce->generate_unique_allreduce_tag();
          }
        }
        ApBarrier all_bar;
        ShardID owner_shard;
        if (all_done.exists() && trace_info.recording)
        {
          const size_t arrivals = collective_mapping->size();
          all_bar = ApBarrier(Realm::Barrier::create_barrier(arrivals));
          owner_shard = trace_info.record_managed_barrier(all_bar, arrivals);
          // Tracing copy-optimization will eliminate this when
          // the trace gets optimized
          Runtime::trigger_event(&trace_info, all_done, all_bar);
        }
        // Case 2 and 3 (all-reduce): Broadcast out the point-wise command
        if (origin != local_space)
        {
          const RtUserEvent recorded = Runtime::create_rt_user_event();
          const RtUserEvent applied = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(this->did);
            rez.serialize(collective->did);
            rez.serialize(precondition);
            rez.serialize(predicate_guard);
            copy_expression->pack_expression(rez, origin);
            rez.serialize<bool>(copy_restricted);
            if (copy_restricted)
              op->pack_remote_operation(rez, origin, applied_events);
            rez.serialize(index);
            rez.serialize(op->get_ctx_index());
            rez.serialize(copy_mask);
            if (src_point != NULL)
              rez.serialize(src_point->did);
            else
              rez.serialize<DistributedID>(0);
            rez.serialize(op->get_unique_op_id());
            trace_info.pack_trace_info(rez, applied_events);
            rez.serialize(recorded);
            rez.serialize(applied);
            if (trace_info.recording)
            {
              rez.serialize(all_bar);
              if (all_bar.exists())
                rez.serialize(owner_shard);
            }
            else
              rez.serialize(all_done);
            rez.serialize(origin);
            rez.serialize(allreduce_tag);
          }
          runtime->send_collective_distribute_pointwise(origin, rez);
          recorded_events.insert(recorded);
          applied_events.insert(applied);
        }
        else
          perform_collective_pointwise(collective, precondition,
              predicate_guard, copy_expression, op, index, op->get_ctx_index(),
              copy_mask, (src_point != NULL) ? src_point->did : 0, 
              op->get_unique_op_id(), trace_info, recorded_events, 
              applied_events, all_done, all_bar, owner_shard, origin, 
              allreduce_tag, copy_restricted);
      }
      return all_done;
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::register_user(const RegionUsage &usage,
                                          const FieldMask &user_mask,
                                          IndexSpaceNode *user_expr,
                                          const UniqueID op_id,
                                          const size_t op_ctx_index,
                                          const unsigned index,
                                          ApEvent term_event,
                                          RtEvent collect_event,
                                          PhysicalManager *target,
                                          CollectiveMapping *analysis_mapping,
                                          size_t local_collective_arrivals,
                                          std::vector<RtEvent> &registered,
                                          std::set<RtEvent> &applied_events,
                                          const PhysicalTraceInfo &trace_info,
                                          const AddressSpaceID source,
                                          const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
      if (local_collective_arrivals > 0)
      {
        // Check to see if we're on the right node for this
        if (!target->is_owner())
        {
          ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent registered_event = Runtime::create_rt_user_event();
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(target->did);
            rez.serialize(usage);
            rez.serialize(user_mask);
            rez.serialize(user_expr->handle);
            rez.serialize(op_id);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(local_collective_arrivals);
            rez.serialize(ready_event);
            rez.serialize(registered_event);
            rez.serialize(applied_event);
            trace_info.pack_trace_info(rez, applied_events);
          }
          runtime->send_view_register_user(target->owner_space, rez);
          registered.push_back(registered_event);
          applied_events.insert(applied_event);
          return ready_event;
        }
        else
          return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, local_collective_arrivals, registered,
              applied_events, trace_info, symbolic);
      }
#ifdef DEBUG_LEGION
      assert(target->is_owner());
      assert(analysis_mapping == NULL);
#endif
      // Iterate through our local views and find the view for the target
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        if (local_views[idx]->get_manager() == target)
          return local_views[idx]->register_user(usage, user_mask, 
              user_expr, op_id, op_ctx_index, index, term_event, collect_event, 
              target, analysis_mapping, local_collective_arrivals,
              registered, applied_events, trace_info, source, symbolic);
      // Should never get here
      assert(false);
      return ApEvent::NO_AP_EVENT;
    }

    //--------------------------------------------------------------------------
    bool CollectiveView::contains(PhysicalManager *manager) const
    //--------------------------------------------------------------------------
    {
      const AddressSpaceID manager_space = get_analysis_space(manager);
      if (manager_space != local_space)
      {
        if ((collective_mapping == NULL) || 
            !collective_mapping->contains(manager_space))
          return false;
        // Check all the current 
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          std::map<PhysicalManager*,IndividualView*>::const_iterator finder =
            remote_instances.find(manager);
          if (finder != remote_instances.end())
            return true;
          // If we already have all the managers from that node then
          // we don't need to check again
          if (remote_instance_responses.contains(manager_space))
            return false;
        }
        // Send the request and wait for the result
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(manager_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        return (remote_instances.find(manager) != remote_instances.end());
      }
      else
      {
        for (unsigned idx = 0; idx < local_views.size(); idx++)
          if (local_views[idx]->get_manager() == manager)
            return true;
        return false;
      }
    }

    //--------------------------------------------------------------------------
    bool CollectiveView::meets_regions(
             const std::vector<LogicalRegion> &regions, bool tight_bounds) const
    //--------------------------------------------------------------------------
    {
      if (!local_views.empty())
        return local_views.front()->get_manager()->meets_regions(regions,
                                                                 tight_bounds);
#ifdef DEBUG_LEGION
      assert((collective_mapping == NULL) ||
              !collective_mapping->contains(local_space));
#endif
      PhysicalManager *manager = NULL;
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        if (!remote_instances.empty())
          manager = remote_instances.begin()->first;
      }
      if (manager == NULL)
      {
        const AddressSpaceID target_space = (collective_mapping == NULL) ?
          owner_space : collective_mapping->find_nearest(local_space);
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(target_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
#ifdef DEBUG_LEGION
        assert(!remote_instances.empty());
#endif
        manager = remote_instances.begin()->first;
      }
      return manager->meets_regions(regions, tight_bounds);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::find_instances_in_memory(Memory memory,
                                       std::vector<PhysicalManager*> &instances)
    //--------------------------------------------------------------------------
    {
      const AddressSpaceID memory_space = memory.address_space();
      if (memory_space != local_space)
      {
        // No point checking if we know that it won't have it
        if ((collective_mapping == NULL) ||
            !collective_mapping->contains(memory_space))
          return;
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          // See if we need the check
          if (remote_instance_responses.contains(memory_space))
          {
            for (std::map<PhysicalManager*,IndividualView*>::const_iterator it =
                  remote_instances.begin(); it != remote_instances.end(); it++)
              if (it->first->memory_manager->memory == memory)
                instances.push_back(it->first);
            return;
          }
        }
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(memory_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        for (std::map<PhysicalManager*,IndividualView*>::const_iterator it =
              remote_instances.begin(); it != remote_instances.end(); it++)
          if (it->first->memory_manager->memory == memory)
            instances.push_back(it->first);
      }
      else
      {
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          PhysicalManager *manager = local_views[idx]->get_manager();
          if (manager->memory_manager->memory == memory)
            instances.push_back(manager);
        }
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_remote_instances_request(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, ready));
      RtUserEvent done;
      derez.deserialize(done);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(!view->local_views.empty());
#endif
      Serializer rez;
      {
        RezCheck z2(rez);
        rez.serialize(did);
        rez.serialize<size_t>(view->local_views.size());
        for (unsigned idx = 0; idx < view->local_views.size(); idx++)
          rez.serialize(view->local_views[idx]->did);
        rez.serialize(done);
      }
      runtime->send_collective_remote_instances_response(source, rez);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_remote_instances_response(AddressSpaceID src,
                                      const std::vector<IndividualView*> &views)
    //--------------------------------------------------------------------------
    {
      AutoLock v_lock(view_lock);
      // Deduplicate cases where we already received this response
      if (remote_instance_responses.contains(src))
        return;
      for (std::vector<IndividualView*>::const_iterator it =
            views.begin(); it != views.end(); it++)
      {
        PhysicalManager *manager = (*it)->get_manager();
        if (remote_instances.insert(std::make_pair(manager, *it)).second)
          (*it)->add_nested_resource_ref(did);
      }
      remote_instance_responses.add(src);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::record_remote_instances(
                                      const std::vector<IndividualView*> &views)
    //--------------------------------------------------------------------------
    {
      AutoLock v_lock(view_lock);
      for (std::vector<IndividualView*>::const_iterator it =
            views.begin(); it != views.end(); it++)
      {
        PhysicalManager *manager = (*it)->get_manager();
        if (remote_instances.insert(std::make_pair(manager, *it)).second)
          (*it)->add_nested_resource_ref(did);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_remote_instances_response(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, ready));
      std::vector<RtEvent> ready_events;
      if (ready.exists())
        ready_events.push_back(ready);
      size_t num_instances;
      derez.deserialize(num_instances);
      std::vector<IndividualView*> instances(num_instances);
      for (unsigned idx = 0; idx < num_instances; idx++)
      {
        derez.deserialize(did);
        instances[idx] = static_cast<IndividualView*>(
            runtime->find_or_request_logical_view(did, ready));
        if (ready.exists())
          ready_events.push_back(ready);
      }
      RtUserEvent done;
      derez.deserialize(done);

      if (ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }
      view->process_remote_instances_response(source, instances);
      Runtime::trigger_event(done);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::find_instances_nearest_memory(Memory memory,
                       std::vector<PhysicalManager*> &instances, bool bandwidth)
    //--------------------------------------------------------------------------
    {
      constexpr size_t size_max = std::numeric_limits<size_t>::max();
      size_t best = bandwidth ? 0 : size_max;
      if (collective_mapping != NULL)
      {
        std::atomic<size_t> atomic_best(best);
        const AddressSpaceID origin = select_origin_space();
        std::vector<DistributedID> best_instances;
        RtEvent ready = find_instances_nearest_memory(memory, local_space,
            &best_instances, &atomic_best, origin, best, bandwidth);
        if (ready.exists() && !ready.has_triggered())
          ready.wait();
        std::vector<RtEvent> ready_events;
        for (std::vector<DistributedID>::const_iterator it =
              best_instances.begin(); it != best_instances.end(); it++)
        {
          instances.push_back(
              runtime->find_or_request_instance_manager(*it, ready));
          if (ready.exists())
            ready_events.push_back(ready);
        }
        if (!ready_events.empty())
        {
          ready = Runtime::merge_events(ready_events);
          if (ready.exists() && !ready.has_triggered())
            ready.wait();
        }
      }
      else
      {
        if (!is_owner())
        {
          const RtUserEvent ready_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(ready_event);
          }
          runtime->send_collective_remote_instances_request(owner_space, rez);
          if (!ready_event.has_triggered())
            ready_event.wait();
          std::map<Memory,size_t> searches;
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          for (std::map<PhysicalManager*,IndividualView*>::const_iterator it =
                remote_instances.begin(); it != remote_instances.end(); it++)
          {
            const Memory local = it->first->memory_manager->memory;
            std::map<Memory,size_t>::const_iterator finder =
              searches.find(local);
            if (finder == searches.end())
            {
              Realm::Machine::AffinityDetails affinity;
              if (runtime->machine.has_affinity(memory, local, &affinity))
              {
#ifdef DEBUG_LEGION
                assert(0 < affinity.bandwidth);
#ifndef __clang__ // clang's idea of size_max is off by one
                assert(affinity.bandwidth < size_max);
#endif
#endif
                if (bandwidth)
                {
                  searches[local] = affinity.bandwidth;
                  if (affinity.bandwidth >= best)
                  {
                    if (affinity.bandwidth > best)
                    {
                      instances.clear();
                      best = affinity.bandwidth;
                    }
                    instances.push_back(it->first);
                  }
                }
                else
                {
#ifdef DEBUG_LEGION
                  assert(0 < affinity.latency);
#ifndef __clang__ // clang's idea of size_max is off by one
                  assert(affinity.latency < size_max);
#endif
#endif
                  searches[local] = affinity.latency;
                  if (affinity.latency <= best)
                  {
                    if (affinity.latency < best)
                    {
                      instances.clear();
                      best = affinity.latency;
                    }
                    instances.push_back(it->first);
                  }
                }
              }
              else
                searches[local] = bandwidth ? 0 : size_max;
            }
            else if (finder->second == best)
              instances.push_back(it->first);
          }
        }
        else
          find_nearest_local_instances(memory, best, instances, bandwidth);
      }
    }

    //--------------------------------------------------------------------------
    RtEvent CollectiveView::find_instances_nearest_memory(Memory memory,
                  AddressSpaceID source, std::vector<DistributedID> *instances,
                  std::atomic<size_t> *target, AddressSpaceID origin,
                  size_t best, bool bandwidth) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
#endif
      const AddressSpaceID space = memory.address_space();
      if (space != local_space)
      {
        if (collective_mapping->contains(space))
        {
#ifdef DEBUG_LEGION
          assert(source == local_space);
#endif
          // Assume that all memmories in the same space are always inherently
          // closer to the target memory than any others, so we can send the
          // request straight to that node and do the lookup
          const RtUserEvent done = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(memory);
            rez.serialize(source);
            rez.serialize(instances);
            rez.serialize(target);
            rez.serialize(origin);
            rez.serialize(best);
            rez.serialize<bool>(bandwidth);
            rez.serialize(done);
          }
          runtime->send_collective_nearest_instances_request(space, rez);
          return done;
        }
        else
        {
          if (collective_mapping->contains(local_space))
          {
            // Do our local check and update the best
            std::vector<PhysicalManager*> local_results;
            find_nearest_local_instances(memory, best, local_results,bandwidth);
            std::vector<RtEvent> done_events;
            std::vector<AddressSpaceID> children;
            collective_mapping->get_children(origin, local_space, children);
            for (std::vector<AddressSpaceID>::const_iterator it = 
                  children.begin(); it != children.end(); it++)
            {
              const RtUserEvent done = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize(memory);
                rez.serialize(source);
                rez.serialize(instances);
                rez.serialize(target);
                rez.serialize(origin);
                rez.serialize(best);
                rez.serialize<bool>(bandwidth);
                rez.serialize(done);
              }
              runtime->send_collective_nearest_instances_request(*it, rez);
              done_events.push_back(done);
            }
            if (!local_results.empty())
            {
              const RtUserEvent done = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(instances);
                rez.serialize(target);
                rez.serialize(best);
                rez.serialize<size_t>(local_results.size());
                for (std::vector<PhysicalManager*>::const_iterator it =
                      local_results.begin(); it != local_results.end(); it++)
                  rez.serialize((*it)->did);
                rez.serialize<bool>(bandwidth);
                rez.serialize(done);
              }
              runtime->send_collective_nearest_instances_response(source, rez);
              done_events.push_back(done);
            }
            if (!done_events.empty())
              return Runtime::merge_events(done_events);
          }
          else
          {
#ifdef DEBUG_LEGION
            assert(source == local_space);
#endif
            // Send to the origin to start
            const RtUserEvent done = Runtime::create_rt_user_event();
            Serializer rez;
            {
              RezCheck z(rez);
              rez.serialize(did);
              rez.serialize(memory);
              rez.serialize(source);
              rez.serialize(instances);
              rez.serialize(target);
              rez.serialize(origin);
              rez.serialize(best);
              rez.serialize<bool>(bandwidth);
              rez.serialize(done);
            }
            runtime->send_collective_nearest_instances_request(origin, rez);
            return done;
          }
        }
      }
      else
      {
        // Assume that all memories in the same space are always inherently
        // closer to the target memory than any others
        // See if we find the memory itself
        std::vector<PhysicalManager*> results;
        find_nearest_local_instances(memory, best, results, bandwidth);
        if (source != local_space)
        {
          if (!results.empty())
          {
            const RtUserEvent done = Runtime::create_rt_user_event();
            Serializer rez;
            {
              RezCheck z(rez);
              rez.serialize(instances);
              rez.serialize(target);
              rez.serialize(best);
              rez.serialize<size_t>(results.size());
              for (std::vector<PhysicalManager*>::const_iterator it =
                    results.begin(); it != results.end(); it++)
                rez.serialize((*it)->did);
              rez.serialize<bool>(bandwidth);
              rez.serialize(done);
            }
            runtime->send_collective_nearest_instances_response(source, rez);
            return done;
          }
        }
        else
        {
          // This is the local case, so there's no atomicity required
          for (std::vector<PhysicalManager*>::const_iterator it =
                results.begin(); it != results.end(); it++)
            instances->push_back((*it)->did);
          target->store(best);
        }
      }
      return RtEvent::NO_RT_EVENT;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::find_nearest_local_instances(Memory memory,
     size_t &best, std::vector<PhysicalManager*> &results, bool bandwidth) const
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        PhysicalManager *manager = local_views[idx]->get_manager();
        if (manager->memory_manager->memory == memory)
          results.push_back(manager);
      }
      constexpr size_t size_max = std::numeric_limits<size_t>::max();
      if (results.empty())
      {
        // Nothing in the memory itself, so see which of our memories
        // are closer to anything else
        std::map<Memory,size_t> searches;
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          PhysicalManager *manager = local_views[idx]->get_manager();
          const Memory local = manager->memory_manager->memory;
          std::map<Memory,size_t>::const_iterator finder =
            searches.find(local);
          if (finder == searches.end())
          {
            Realm::Machine::AffinityDetails affinity;
            if (runtime->machine.has_affinity(memory, local, &affinity))
            {
#ifdef DEBUG_LEGION
              assert(0 < affinity.bandwidth);
#ifndef __clang__ // clang's idea of size_max is off by one
              assert(affinity.bandwidth < size_max);
#endif
#endif
              if (bandwidth)
              {
                searches[local] = affinity.bandwidth;
                if (affinity.bandwidth >= best)
                {
                  if (affinity.bandwidth > best)
                  {
                    results.clear();
                    best = affinity.bandwidth;
                  }
                  results.push_back(manager);
                }
              }
              else
              {
#ifdef DEBUG_LEGION
                assert(0 < affinity.latency);
#ifndef __clang__ // clang's idea of size_max is off by one
                assert(affinity.latency < size_max);
#endif
#endif
                searches[local] = affinity.latency;
                if (affinity.latency <= best)
                {
                  if (affinity.latency < best)
                  {
                    results.clear();
                    best = affinity.latency;
                  }
                  results.push_back(manager);
                }
              }
            }
            else
              searches[local] = bandwidth ? 0 : size_max;
          }
          else if (finder->second == best)
            results.push_back(manager);
        }
      }
      else
        best = bandwidth ? size_max-1 : 1;
    } 

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_nearest_instances_request(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      Memory memory;
      derez.deserialize(memory);
      AddressSpaceID source;
      derez.deserialize(source);
      std::vector<DistributedID> *instances;
      derez.deserialize(instances);
      std::atomic<size_t> *target;
      derez.deserialize(target);
      AddressSpaceID origin;
      derez.deserialize(origin);
      size_t best;
      derez.deserialize(best);
      bool bandwidth;
      derez.deserialize(bandwidth);
      RtUserEvent done;
      derez.deserialize(done);

      CollectiveView *manager = static_cast<CollectiveView*>(
          runtime->weak_find_distributed_collectable(did));
      if (manager != NULL)     
      {
        Runtime::trigger_event(done, manager->find_instances_nearest_memory(
              memory, source, instances, target, origin, best, bandwidth));
        if (manager->remove_base_resource_ref(RUNTIME_REF))
          delete manager;
      }
      else
        Runtime::trigger_event(done);
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_nearest_instances_response(
                                                            Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      std::vector<DistributedID> *instances;
      derez.deserialize(instances);
      std::atomic<size_t> *target;
      derez.deserialize(target);
      size_t best;
      derez.deserialize(best);
      size_t num_instances;
      derez.deserialize(num_instances);
      std::vector<DistributedID> results(num_instances);
      for (unsigned idx = 0; idx < num_instances; idx++)
        derez.deserialize(results[idx]);
      bool bandwidth;
      derez.deserialize(bandwidth);
      // spin until we can get safely set the guard to add our entries
      const size_t guard = 
        bandwidth ? std::numeric_limits<size_t>::max() : 0;
      size_t current = target->load();
      while ((current == guard) ||
             (bandwidth && (current <= best)) ||
             (!bandwidth && (best <= current)))
      {
        if (!target->compare_exchange_weak(current, guard))
          continue;
        // If someone else still holds the guard then keep trying
        if (current == guard)
          continue;
        if (bandwidth)
        {
          if (current < best)
            instances->clear();
          for (unsigned idx = 0; idx < results.size(); idx++)
            instances->push_back(results[idx]);
        }
        else
        {
          if (best < current)
            instances->clear();
          for (unsigned idx = 0; idx < results.size(); idx++)
            instances->push_back(results[idx]);
        }
        target->store(best);
        break;
      }
      RtUserEvent done;
      derez.deserialize(done);
    }

    //--------------------------------------------------------------------------
    AddressSpaceID CollectiveView::select_source_space(
                                               AddressSpaceID destination) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
#endif
      // 1. If the collective manager has instances on the same node
      //    as the destination then we'll use one of them
      if (collective_mapping->contains(destination))
        return destination;
      // 2. If the collective manager has instances on the local node
      //    then we'll use one of them
      if (collective_mapping->contains(local_space))
        return local_space;
      // 3. Pick the node closest to the destination in the collective
      //    manager and use that to issue copies
      return collective_mapping->find_nearest(destination);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::pack_fields(Serializer &rez,
                               const std::vector<CopySrcDstField> &fields) const
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(fields.size());
      for (unsigned idx = 0; idx < fields.size(); idx++)
        rez.serialize(fields[idx]);
      if (runtime->legion_spy_enabled)
      {
        // Pack the instance points for these instances so we can check to 
        // see if we already fetched them on the remote node
        std::set<DistributedID> to_send;
        for (std::vector<CopySrcDstField>::const_iterator it =
              fields.begin(); it != fields.end(); it++)
        {
          bool found = false;
          for (unsigned idx = 0; idx < local_views.size(); idx++)
          {
            PhysicalManager *manager = local_views[idx]->get_manager();
            if (manager->instance != it->inst)
              continue;
            to_send.insert(local_views[idx]->did);
            found = true;
            break;
          }
          if (!found)
          {
            AutoLock v_lock(view_lock,1,false/*exclusive*/);
            for (std::map<PhysicalManager*,IndividualView*>::const_iterator 
                  rit = remote_instances.begin();
                  rit != remote_instances.end(); rit++)
            {
              if (rit->first->instance != it->inst)
                continue;
              to_send.insert(rit->second->did);
              found = true;
              break;
            }
#ifdef DEBUG_LEGION
            assert(found);
#endif
          }
        }
#ifdef DEBUG_LEGION
        assert(!to_send.empty());
#endif
        rez.serialize<size_t>(to_send.size());
        for (std::set<DistributedID>::const_iterator it =
              to_send.begin(); it != to_send.end(); it++)
          rez.serialize(*it);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::unpack_fields(
                     std::vector<CopySrcDstField> &fields,
                     Deserializer &derez, std::set<RtEvent> &ready_events,
                     CollectiveView *view, RtEvent view_ready, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!fields.empty());
#endif
      const Processor local_proc = Processor::get_executing_processor();
      for (unsigned idx = 0; idx < fields.size(); idx++)
      {
        CopySrcDstField &field = fields[idx];
        derez.deserialize(field);
        // Check to see if we fetched the metadata for this instance
        RtEvent ready(field.inst.fetch_metadata(local_proc));
        if (ready.exists() && !ready.has_triggered())
          ready_events.insert(ready);
      }
      if (runtime->legion_spy_enabled)
      {
        // Legion Spy is a bit dumb currently and needs to have logged every
        // instance on every node where it might be used currently, so check
        // to make sure we've logged it by loading the individual view and
        // therefore the manager for each instances that we need
        size_t num_views;
        derez.deserialize(num_views);
        if (num_views > 0)
        {
          std::vector<RtEvent> wait_events;
          std::vector<IndividualView*> views(num_views);
          for (unsigned idx = 0; idx < num_views; idx++)
          {
            DistributedID did;
            derez.deserialize(did);
            RtEvent ready;
            views[idx] = static_cast<IndividualView*>(
                runtime->find_or_request_logical_view(did, ready));
            if (ready.exists())
              wait_events.push_back(ready);
          }
          if (!wait_events.empty())
          {
            if (view_ready.exists())
              wait_events.push_back(view_ready);
            const RtEvent wait_on = Runtime::merge_events(wait_events);
            if (wait_on.exists() && ! wait_on.has_triggered())
              wait_on.wait();
          }
          else if (view_ready.exists() && !view_ready.has_triggered())
            view_ready.wait();
          view->record_remote_instances(views);
        }
        else
        {
          // These fields are from an individual manager so just
          // load a copy of it here
          DistributedID did;
          derez.deserialize(did);
          RtEvent ready;
          runtime->find_or_request_logical_view(did, ready);
          if (ready.exists())
            ready_events.insert(ready);
        }
      }
    }

    //--------------------------------------------------------------------------
    unsigned CollectiveView::find_local_index(PhysicalManager *target) const
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        if (local_views[idx]->get_manager() == target)
          return idx;
      // We should always find it
      assert(false);
      return 0;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::register_collective_analysis(PhysicalManager *target,
                                              CollectiveAnalysis *analysis,
                                              size_t local_collective_arrivals,
                                              std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_collective_arrivals > 0);
#endif
      // First check to see if we are on the right node for this target
      const AddressSpaceID analysis_space = get_analysis_space(target);
      if (analysis_space != local_space)
      {
        const RtEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(target->did);
          analysis->pack_collective_analysis(rez,analysis_space,applied_events);
          rez.serialize(local_collective_arrivals);
          rez.serialize(applied);
        }
        runtime->send_collective_remote_registration(analysis_space, rez);
        applied_events.insert(applied);
        return;
      }
      const unsigned local_index = find_local_index(target);
      const RendezvousKey key(analysis->get_context_index(), 
                              analysis->get_requirement_index());
      AutoLock v_lock(view_lock);
      std::map<RendezvousKey,UserRendezvous>::iterator finder =
        rendezvous_users.find(key);
      if (finder == rendezvous_users.end())
      {
        finder = rendezvous_users.insert(
            std::make_pair(key,UserRendezvous())).first; 
        UserRendezvous &rendezvous = finder->second;
        // Count how many expected arrivals we have
        rendezvous.local_initialized = false;
        rendezvous.remaining_remote_arrivals =
          collective_mapping->count_children(owner_space, local_space);
        rendezvous.local_registered = Runtime::create_rt_user_event();
        rendezvous.global_registered = Runtime::create_rt_user_event();
        rendezvous.local_applied = Runtime::create_rt_user_event();
        rendezvous.global_applied = Runtime::create_rt_user_event();
      }
      // Perform the registration
      if (finder->second.analyses.empty())
      {
        finder->second.analyses.resize(local_views.size(), NULL);
        finder->second.remaining_analyses = local_collective_arrivals;
      }
#ifdef DEBUG_LEGION
      assert(local_index < finder->second.analyses.size());
      assert(finder->second.remaining_analyses > 0);
#endif
      // Only need to save it if we're the first ones for this local view
      if (finder->second.analyses[local_index] == NULL)
      {
        finder->second.analyses[local_index] = analysis;
        analysis->add_analysis_reference();
      }
      if ((--finder->second.remaining_analyses == 0) &&
          finder->second.analyses_ready.exists())
        Runtime::trigger_event(finder->second.analyses_ready);
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_remote_analysis_registration(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent view_ready, manager_ready;
      CollectiveView *collective_view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, view_ready));
      derez.deserialize(did);
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(did, manager_ready);
      std::set<RtEvent> applied_events;
      RemoteCollectiveAnalysis *analysis = 
        RemoteCollectiveAnalysis::unpack(derez, runtime, applied_events);
      analysis->add_reference();
      size_t local_collective_arrivals;
      derez.deserialize(local_collective_arrivals);
      RtUserEvent applied;
      derez.deserialize(applied);

      if (view_ready.exists() && !view_ready.has_triggered())
        applied_events.insert(view_ready);
      if (manager_ready.exists() && !manager_ready.has_triggered())
        applied_events.insert(manager_ready);
      if (!applied_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(applied_events);
        applied_events.clear();
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }
      collective_view->register_collective_analysis(manager, analysis,
                            local_collective_arrivals, applied_events);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      if (analysis->remove_reference())
        delete analysis;
    }

    //--------------------------------------------------------------------------
    RtEvent CollectiveView::find_collective_analyses(size_t context_index,
              unsigned index, const std::vector<CollectiveAnalysis*> *&analyses)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
#endif
      const RendezvousKey key(context_index, index);
      AutoLock v_lock(view_lock);
      std::map<RendezvousKey,UserRendezvous>::iterator finder =
        rendezvous_users.find(key);
      if (finder == rendezvous_users.end())
      {
        finder = rendezvous_users.insert(
            std::make_pair(key,UserRendezvous())).first; 
        UserRendezvous &rendezvous = finder->second;
        rendezvous.local_initialized = false;
        rendezvous.remaining_remote_arrivals =
          collective_mapping->count_children(owner_space, local_space);
        rendezvous.local_registered = Runtime::create_rt_user_event();
        rendezvous.global_registered = Runtime::create_rt_user_event();
        rendezvous.local_applied = Runtime::create_rt_user_event();
        rendezvous.global_applied = Runtime::create_rt_user_event();
      }
      analyses = &finder->second.analyses;
      if ((finder->second.analyses.empty() || 
            (finder->second.remaining_analyses > 0)) &&
          !finder->second.analyses_ready.exists())
        finder->second.analyses_ready = Runtime::create_rt_user_event();
      return finder->second.analyses_ready;
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::register_collective_user(const RegionUsage &usage,
                                       const FieldMask &user_mask,
                                       IndexSpaceNode *expr,
                                       const UniqueID op_id,
                                       const size_t op_ctx_index,
                                       const unsigned index,
                                       ApEvent term_event,
                                       RtEvent collect_event,
                                       PhysicalManager *target,
                                       size_t local_collective_arrivals,
                                       std::vector<RtEvent> &registered_events,
                                       std::set<RtEvent> &applied_events,
                                       const PhysicalTraceInfo &trace_info,
                                       const bool symbolic)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(((collective_mapping != NULL) && 
            collective_mapping->contains(local_space)) || is_owner());
#endif
      const unsigned target_index = find_local_index(target);
      // We performing a collective analysis, this function performs a 
      // parallel rendezvous to ensure several important invariants.
      // 1. SUBTLE!!! Make sure that all the participants have arrived
      //    at this function before performing any view analysis. This
      //    is required to ensure that any copies that need to be issued
      //    have had a chance to record their view users first before we
      //    attempt to look for preconditions for this user.
      // 2. Similarly make sure that the applied events reflects the case
      //    where all the users have been recorded across the views on 
      //    each node to ensure that any downstream copies or users will
      //    observe all the most recent users.
      // 3. Deduplicate across all the participants on the same node since
      //    there is always just a single view on each node. This function
      //    call will always return the local user precondition for the
      //    local instances. Make sure to merge together all the partcipant
      //    postconditions for the local instances to reflect in the view
      //    that the local instances are ready when they are all ready.
      // 4. Do NOT block in this function call or you can risk deadlock because
      //    we might be doing several of these calls for a region requirement
      //    on different instances and the orders might vary on each node.
      
      // The unique tag for the rendezvous is our context ID which will be
      // the same across all points and the index of our region requirement
      PhysicalTraceInfo *result_info;
      RtUserEvent local_registered, global_registered;
      RtUserEvent local_applied, global_applied;
      std::vector<RtEvent> remote_registered, remote_applied;
      std::vector<ApUserEvent> local_ready_events;
      std::vector<std::vector<ApEvent> > local_term_events;
      std::vector<CollectiveAnalysis*> analyses;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          // Count how many expected arrivals we have
          // If we're doing collective per space 
          rendezvous.remaining_local_arrivals = local_collective_arrivals;
          rendezvous.local_initialized = true;
          rendezvous.remaining_remote_arrivals =
            (collective_mapping == NULL) ? 0 :
            collective_mapping->count_children(owner_space, local_space);
          rendezvous.local_term_events.resize(local_views.size());
          rendezvous.ready_events.resize(local_views.size());
          for (unsigned idx = 0; idx < local_views.size(); idx++)
            rendezvous.ready_events[idx] =
              Runtime::create_ap_user_event(&trace_info);
          rendezvous.trace_info = new PhysicalTraceInfo(trace_info);
          rendezvous.local_registered = Runtime::create_rt_user_event();
          rendezvous.global_registered = Runtime::create_rt_user_event();
          rendezvous.local_applied = Runtime::create_rt_user_event();
          rendezvous.global_applied = Runtime::create_rt_user_event();
        }
        else if (!finder->second.local_initialized)
        {
          // First local arrival, but rendezvous was made by a remote
          // arrival so we need to make the ready event
#ifdef DEBUG_LEGION
          assert(finder->second.ready_events.empty());
          assert(finder->second.local_term_events.empty());
          assert(finder->second.trace_info == NULL);
#endif
          finder->second.local_term_events.resize(local_views.size());
          finder->second.ready_events.resize(local_views.size());
          for (unsigned idx = 0; idx < local_views.size(); idx++)
            finder->second.ready_events[idx] =
              Runtime::create_ap_user_event(&trace_info);
          finder->second.trace_info = new PhysicalTraceInfo(trace_info);
          finder->second.remaining_local_arrivals = local_collective_arrivals;
          finder->second.local_initialized = true;
        } 
        if (term_event.exists())
          finder->second.local_term_events[target_index].push_back(term_event);
        // Record the applied events
        registered_events.push_back(finder->second.global_registered);
        applied_events.insert(finder->second.global_applied);
        // The result will be the ready event
        ApEvent result = finder->second.ready_events[target_index];
        result_info = finder->second.trace_info;
#ifdef DEBUG_LEGION
        assert(finder->second.local_initialized);
        assert(finder->second.remaining_local_arrivals > 0);
#endif
        // See if we've seen all the arrivals
        if (--finder->second.remaining_local_arrivals == 0)
        {
          // If we're going to need to defer this then save
          // all of our local state needed to perform registration
          // for when it is safe to do so
          if (!is_owner() || 
              (finder->second.remaining_remote_arrivals > 0))
          {
            // Save the state that we need for finalization later
            finder->second.usage = usage;
            finder->second.mask = new FieldMask(user_mask);
            finder->second.expr = expr;
            WrapperReferenceMutator mutator(applied_events);
            expr->add_nested_expression_reference(did, &mutator);
            finder->second.op_id = op_id;
            finder->second.collect_event = collect_event;
            finder->second.symbolic = symbolic;
          }
          if (finder->second.remaining_remote_arrivals == 0)
          {
            if (!is_owner())
            {
              // Not the owner so send the message to the parent
              RtEvent registered = finder->second.local_registered;
              if (!finder->second.remote_registered.empty())
              {
                finder->second.remote_registered.push_back(registered);
                registered =
                  Runtime::merge_events(finder->second.remote_registered);
              }
              RtEvent applied = finder->second.local_applied;
              if (!finder->second.remote_applied.empty())
              {
                finder->second.remote_applied.push_back(applied);
                applied =
                  Runtime::merge_events(finder->second.remote_applied);
              }
              const AddressSpaceID parent = 
                collective_mapping->get_parent(owner_space, local_space);
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize(op_ctx_index);
                rez.serialize(index);
                rez.serialize(registered);
                rez.serialize(applied);
              }
              runtime->send_collective_register_user_request(parent, rez);
              return result;
            }
            else
            {
#ifdef DEBUG_LEGION
              assert(finder->second.remaining_analyses == 0);
#endif
              // We're going to fall through so grab the state
              // that we need to do the finalization now
              remote_registered.swap(finder->second.remote_registered);
              remote_applied.swap(finder->second.remote_applied);
              local_registered = finder->second.local_registered;
              global_registered = finder->second.global_registered;
              local_applied = finder->second.local_applied;
              global_applied = finder->second.global_applied;
              local_ready_events.swap(finder->second.ready_events);
              local_term_events.swap(finder->second.local_term_events);
              analyses.swap(finder->second.analyses);
              // We can erase this from the data structure now
              rendezvous_users.erase(finder);
            }
          }
          else // Still waiting for remote arrivals
            return result;
        }
        else // Not the last local arrival so we can just return the result
          return result;
      }
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      finalize_collective_user(usage, user_mask, expr, op_id, op_ctx_index, 
          index, collect_event, local_registered, global_registered,
          local_applied, global_applied, local_ready_events, 
          local_term_events, result_info, analyses, symbolic);
      RtEvent all_registered = local_registered;
      if (!remote_registered.empty())
      {
        remote_registered.push_back(all_registered);
        all_registered = Runtime::merge_events(remote_registered);
      }
      Runtime::trigger_event(global_registered, all_registered); 
      RtEvent all_applied = local_applied;
      if (!remote_applied.empty())
      {
        remote_applied.push_back(all_applied);
        all_applied = Runtime::merge_events(remote_applied);
      }
      Runtime::trigger_event(global_applied, all_applied);
      return local_ready_events[target_index];
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_register_user_request(
                                const size_t op_ctx_index, const unsigned index,
                                RtEvent registered, RtEvent applied)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator
          finder = rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.local_initialized = false;
          rendezvous.remaining_remote_arrivals =
            collective_mapping->count_children(owner_space, local_space);
          rendezvous.local_registered = Runtime::create_rt_user_event();
          rendezvous.global_registered = Runtime::create_rt_user_event();
          rendezvous.local_applied = Runtime::create_rt_user_event();
          rendezvous.global_applied = Runtime::create_rt_user_event();
        }
        finder->second.remote_registered.push_back(registered);
        finder->second.remote_applied.push_back(applied);
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_remote_arrivals > 0);
#endif
        // If we're not the last arrival then we're done
        if ((--finder->second.remaining_remote_arrivals > 0) ||
            !finder->second.local_initialized ||
            (finder->second.remaining_local_arrivals > 0))
          return;
        if (!is_owner())
        {
          // Continue sending the message up the tree to the parent
          registered = finder->second.local_registered;
          if (!finder->second.remote_registered.empty())
          {
            finder->second.remote_registered.push_back(registered);
            registered =
              Runtime::merge_events(finder->second.remote_registered);
          }
          applied = finder->second.local_applied;
          if (!finder->second.remote_applied.empty())
          {
            finder->second.remote_applied.push_back(applied);
            applied = Runtime::merge_events(finder->second.remote_applied);
          }
          const AddressSpaceID parent = 
            collective_mapping->get_parent(owner_space, local_space);
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(registered);
            rez.serialize(applied);
          }
          runtime->send_collective_register_user_request(parent, rez);
          return;
        }
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_analyses == 0);
#endif
        // We're the owner so we can start doing the user registration
        // Grab everything we need to call finalize_collective_user
        to_perform = std::move(finder->second);
        // Then we can erase the entry
        rendezvous_users.erase(finder);
      }
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      finalize_collective_user(to_perform.usage, *(to_perform.mask),
          to_perform.expr, to_perform.op_id, op_ctx_index, index, 
          to_perform.collect_event, to_perform.local_registered,
          to_perform.global_registered, to_perform.local_applied,
          to_perform.global_applied, to_perform.ready_events, 
          to_perform.local_term_events, to_perform.trace_info,
          to_perform.analyses, to_perform.symbolic);
      RtEvent all_registered = to_perform.local_registered;
      if (!to_perform.remote_registered.empty())
      {
        to_perform.remote_registered.push_back(all_registered);
        all_registered = Runtime::merge_events(to_perform.remote_registered);
      }
      Runtime::trigger_event(to_perform.global_registered, all_registered);
      RtEvent all_applied = to_perform.local_applied;
      if (!to_perform.remote_applied.empty())
      {
        to_perform.remote_applied.push_back(all_applied);
        all_applied = Runtime::merge_events(to_perform.remote_applied);
      }
      Runtime::trigger_event(to_perform.global_applied, all_applied);
      if (to_perform.expr->remove_nested_expression_reference(did))
        delete to_perform.expr;
      delete to_perform.mask;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_register_user_request(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      RtEvent registered, applied;
      derez.deserialize(registered);
      derez.deserialize(applied);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->process_register_user_request(op_ctx_index, index,
                                          registered, applied);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_register_user_response(
            const size_t op_ctx_index, const unsigned index, 
            const RtEvent registered, const RtEvent applied)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_owner());
      assert(!local_views.empty());
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
#ifdef DEBUG_LEGION
        assert(finder != rendezvous_users.end());
        assert(finder->second.remaining_analyses == 0);
#endif
        to_perform = std::move(finder->second);
        // Can now remove this from the data structure
        rendezvous_users.erase(finder);
      }
      // Now we can perform the user registration
      finalize_collective_user(to_perform.usage, *(to_perform.mask),
          to_perform.expr, to_perform.op_id, op_ctx_index, index,
          to_perform.collect_event, to_perform.local_registered, 
          to_perform.global_registered, to_perform.local_applied,
          to_perform.global_applied, to_perform.ready_events,
          to_perform.local_term_events, to_perform.trace_info,
          to_perform.analyses, to_perform.symbolic);
      Runtime::trigger_event(to_perform.global_registered, registered);
      Runtime::trigger_event(to_perform.global_applied, applied);
      if (to_perform.expr->remove_nested_expression_reference(did))
        delete to_perform.expr;
      delete to_perform.mask;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_register_user_response(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      RtEvent registered, applied;
      derez.deserialize(registered);
      derez.deserialize(applied);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->process_register_user_response(op_ctx_index, index, 
                                           registered, applied);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::finalize_collective_user(
                                const RegionUsage &usage,
                                const FieldMask &user_mask,
                                IndexSpaceNode *expr,
                                const UniqueID op_id,
                                const size_t op_ctx_index,
                                const unsigned index,
                                RtEvent collect_event,
                                RtUserEvent local_registered,
                                RtEvent global_registered,
                                RtUserEvent local_applied,
                                RtEvent global_applied,
                                std::vector<ApUserEvent> &ready_events,
                                std::vector<std::vector<ApEvent> > &term_events,
                                const PhysicalTraceInfo *trace_info,
                                std::vector<CollectiveAnalysis*> &analyses,
                                const bool symbolic) const
    //--------------------------------------------------------------------------
    {
      // First send out any messages to the children so they can start
      // their own registrations
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(owner_space, local_space, children);
      if (!children.empty())
      {
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(global_registered);
          rez.serialize(global_applied);
        }
        for (std::vector<AddressSpaceID>::const_iterator it =
              children.begin(); it != children.end(); it++)
          runtime->send_collective_register_user_response(*it, rez);
      }
#ifdef DEBUG_LEGION
      assert(local_views.size() == term_events.size());
      assert(local_views.size() == ready_events.size());
#endif
      // Perform the registration on the local views
      std::vector<RtEvent> registered_events;
      std::set<RtEvent> applied_events;
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        const ApEvent term_event = 
          Runtime::merge_events(trace_info, term_events[idx]);
        const ApEvent ready = local_views[idx]->register_user(usage, user_mask,
            expr, op_id, op_ctx_index, index, term_event, collect_event,
            local_views[idx]->get_manager(), NULL/*analysis mapping*/,
            0/*no collective arrivals*/, registered_events, applied_events,
            *trace_info, runtime->address_space, symbolic);
        Runtime::trigger_event(trace_info, ready_events[idx], ready);
      }
      if (!registered_events.empty())
        Runtime::trigger_event(local_registered,
            Runtime::merge_events(registered_events));
      else
        Runtime::trigger_event(local_registered);
      if (!applied_events.empty())
        Runtime::trigger_event(local_applied,
            Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(local_applied);
      // Remove any references on the analyses
      for (std::vector<CollectiveAnalysis*>::const_iterator it =
            analyses.begin(); it != analyses.end(); it++)
        if ((*it)->remove_analysis_reference())
          delete (*it);
      delete trace_info;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::perform_collective_fill(FillView *fill_view, 
                                         ApEvent precondition,
                                         PredEvent predicate_guard,
                                         IndexSpaceExpression *fill_expression,
                                         Operation *op, const unsigned index,
                                         const size_t op_context_index,
                                         const FieldMask &fill_mask,
                                         const PhysicalTraceInfo &trace_info,
                                         std::set<RtEvent> &recorded_events,
                                         std::set<RtEvent> &applied_events,
                                         ApUserEvent ready_event,
                                         AddressSpaceID origin,
                                         const bool fill_restricted)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
      assert((op != NULL) || !fill_restricted);
#endif
      RtEvent analyses_ready;
      const std::vector<CollectiveAnalysis*> *local_analyses = NULL;
      if (!fill_restricted)
      {
        // If this is not a fill-out to a restricted collective instance 
        // then we should be able to find our local analyses to use for 
        // performing operations
        analyses_ready =
          find_collective_analyses(op_context_index, index, local_analyses);
#ifdef DEBUG_LEGION
        assert(local_analyses != NULL);
#endif
        // If we're recording then we need to wait now to get a valid
        // trace info for capturing the trace for events we send to 
        // remote nodes, otherwise we just need to wait before doing
        // the fill calls
        if ((trace_info.recording || (op == NULL)) && 
            analyses_ready.exists() && !analyses_ready.has_triggered())
          analyses_ready.wait();
#ifdef DEBUG_LEGION
        assert(local_analyses != NULL);
#endif
        if (op == NULL)
          op = local_analyses->front()->get_operation();
      }
#ifdef DEBUG_LEGION
      assert(op != NULL);
#endif
      const PhysicalTraceInfo &local_info = 
        ((local_analyses == NULL) || !trace_info.recording) ? trace_info : 
        local_analyses->front()->get_trace_info();
#ifdef DEBUG_LEGION
      assert(local_info.recording == trace_info.recording);
#endif
      // Send it on to any children in the broadcast tree first
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      std::vector<ApEvent> ready_events;
      ApBarrier trace_barrier;
      ShardID trace_shard = 0;
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(fill_view->did);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          fill_expression->pack_expression(rez, *it);
          rez.serialize<bool>(fill_restricted);
          if (fill_restricted)
            op->pack_remote_operation(rez, *it, applied_events);
          rez.serialize(index);
          rez.serialize(op_context_index);
          rez.serialize(fill_mask);
          local_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (local_info.recording)
          {
            if (ready_event.exists() && !trace_barrier.exists())
            {
              trace_barrier =
                ApBarrier(Realm::Barrier::create_barrier(children.size()));
              trace_shard = local_info.record_managed_barrier(trace_barrier,
                                                            children.size());
              ready_events.push_back(trace_barrier);
            }
            rez.serialize(trace_barrier);
            if (trace_barrier.exists())
              rez.serialize(trace_shard);
          }
          else
          {
            ApUserEvent child_ready;
            if (ready_event.exists())
            {
              child_ready = Runtime::create_ap_user_event(&local_info);
              ready_events.push_back(child_ready);
            }
            rez.serialize(child_ready);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_fill(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      // Now we can perform the fills for our instances
      const UniqueID op_id = op->get_unique_op_id();
      // Do the last wait before we need our analyses for recording 
      // and profiling requests from the mappers
      if (analyses_ready.exists() && !analyses_ready.has_triggered())
        analyses_ready.wait();
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
          trace_info : local_analyses->at(idx)->get_trace_info();
        IndividualView *local_view = local_views[idx];
        ApEvent dst_precondition = local_view->find_copy_preconditions(
            false/*reading*/, 0/*redop*/, fill_mask, fill_expression,
            op_id, index, applied_events, inst_info);
        if (dst_precondition.exists())
        {
          if (precondition.exists())
            dst_precondition =
              Runtime::merge_events(&inst_info, precondition, dst_precondition);
        }
        else
          dst_precondition = precondition;
        PhysicalManager *local_manager = local_view->get_manager();
        std::vector<CopySrcDstField> dst_fields;
        local_manager->compute_copy_offsets(fill_mask, dst_fields);
        const ApEvent result = fill_expression->issue_fill(op,
                                 inst_info, dst_fields,
                                 fill_view->value->value,
                                 fill_view->value->value_size,
#ifdef LEGION_SPY
                                 fill_view->fill_op_uid,
                                 local_view->manager->field_space_node->handle,
                                 local_view->manager->tree_id,
#endif
                                 dst_precondition, predicate_guard);
        if (result.exists())
        {
          if (ready_event.exists())
            ready_events.push_back(result);
          const RtEvent collect_event = inst_info.get_collect_event();
          local_view->add_copy_user(false/*reading*/, 0/*redop*/, result,
              collect_event, fill_mask, fill_expression, op_id, index,
              recorded_events, inst_info.recording, runtime->address_space);
        }
        if (inst_info.recording)
        {
          const UniqueInst dst_inst(local_view);
          inst_info.record_fill_inst(result, fill_expression, dst_inst, 
                         fill_mask, applied_events, (get_redop() > 0));
        }
      }
      // Use the trace info for doing the trigger if necessary
      if (!ready_events.empty())
      {
#ifdef DEBUG_LEGION
        assert(ready_event.exists());
#endif
        Runtime::trigger_event(&trace_info, ready_event,
            Runtime::merge_events(&local_info, ready_events));
      }
      else if (ready_event.exists())
        Runtime::trigger_event(&trace_info, ready_event);
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_fill(Runtime *runtime,
                                     AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did, fill_did;
      derez.deserialize(view_did);
      RtEvent view_ready, fill_ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      derez.deserialize(fill_did);
      FillView *fill_view = static_cast<FillView*>(
          runtime->find_or_request_logical_view(fill_did, fill_ready));
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *fill_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      bool fill_restricted;
      derez.deserialize<bool>(fill_restricted);
      Operation *op = NULL;
      std::set<RtEvent> ready_events;
      if (fill_restricted)
        op = RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      FieldMask fill_mask;
      derez.deserialize(fill_mask);
      std::set<RtEvent> recorded_events, applied_events;
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready;
      if (trace_info.recording)
      {
        ApBarrier bar;
        derez.deserialize(bar);
        if (bar.exists())
        {
          ShardID sid;
          derez.deserialize(sid);
          // Copy-elmination will take care of this for us
          // when the trace is optimized
          ready = Runtime::create_ap_user_event(&trace_info);
          Runtime::phase_barrier_arrive(bar, 1/*count*/, ready);
          trace_info.record_barrier_arrival(bar, ready, 1/*count*/, 
                                            applied_events, sid);
        }
      }
      else
        derez.deserialize(ready);
      AddressSpaceID origin;
      derez.deserialize(origin);
      

      // Make sure all the distributed collectables are ready
      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (fill_ready.exists() && !fill_ready.has_triggered())
        ready_events.insert(fill_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      view->perform_collective_fill(fill_view, precondition,
          predicate_guard, fill_expression, op, index, op_ctx_index,
          fill_mask, trace_info, recorded_events, applied_events, ready,
          origin, fill_restricted);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      if (op != NULL)
        delete op;
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::perform_collective_point(
                                const std::vector<CopySrcDstField> &dst_fields,
                                const std::vector<Reservation> &reservations,
                                ApEvent precondition,
                                PredEvent predicate_guard,
                                IndexSpaceExpression *copy_expression,
                                Operation *op, const unsigned index,
                                const FieldMask &copy_mask,
                                const FieldMask &dst_mask,
                                const Memory location,
                                const UniqueInst &dst_inst,
                                const DistributedID src_inst_did,
                                const PhysicalTraceInfo &trace_info,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif 
      // Figure out which instance we're going to use for the copy
      unsigned instance_index = 0;
      if (src_inst_did > 0)
      {
#ifdef DEBUG_LEGION
        instance_index = UINT_MAX;
#endif
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          PhysicalManager *manager = local_views[idx]->get_manager();
          if (manager->did != src_inst_did)
            continue;
          instance_index = idx;
          break;
        }
#ifdef DEBUG_LEGION
        assert(instance_index != UINT_MAX);
#endif
      }
      else if (instances.size() > 1)
      {
        int best_bandwidth = -1;
        const Machine &machine = runtime->machine;
        Machine::AffinityDetails details;
        if (machine.has_affinity(location,
              local_views[0]->get_manager()->memory_manager->memory, &details))
          best_bandwidth = details.bandwidth;
        for (unsigned idx = 1; idx < local_views.size(); idx++)
        {
          const Memory memory = 
            local_views[idx]->get_manager()->memory_manager->memory;
          if (machine.has_affinity(location, memory, &details))
          {
            if ((best_bandwidth < 0) || 
                (int(details.bandwidth) > best_bandwidth))
            {
              best_bandwidth = details.bandwidth;
              instance_index = idx;
            }
          }
        }
      }
      // Compute the src_fields
      IndividualView *local_view = local_views[instance_index];
      // Compute the source precondition to get that in flight
      const UniqueID op_id = op->get_unique_op_id();
      const ApEvent src_pre = local_view->find_copy_preconditions(
          true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      if (src_pre.exists())
      {
        if (precondition.exists())
          precondition =
            Runtime::merge_events(&trace_info, precondition, src_pre);
        else
          precondition = src_pre;
      }
      PhysicalManager *local_manager = local_view->get_manager();
      std::vector<CopySrcDstField> src_fields;
      local_manager->compute_copy_offsets(copy_mask, src_fields);
      // Issue the copy
      const ApEvent copy_post = copy_expression->issue_copy(op,
            trace_info, dst_fields, src_fields, reservations,
#ifdef LEGION_SPY
            local_manager->tree_id, dst_inst.tid,
#endif
            precondition, predicate_guard);
      // Record the user
      if (copy_post.exists())
      {
        const RtEvent collect_event = trace_info.get_collect_event();
        local_view->add_copy_user(true/*reading*/, 0/*redop*/, copy_post,
            collect_event, copy_mask, copy_expression, op_id, index,
            recorded_events, trace_info.recording, runtime->address_space);
      }
      if (trace_info.recording)
      {
        const UniqueInst src_inst(local_view);
        trace_info.record_copy_insts(copy_post, copy_expression,
          src_inst, dst_inst, copy_mask, dst_mask, get_redop(), applied_events);
      }
      return copy_post;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_point(Runtime *runtime,
                                     AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did;
      derez.deserialize(view_did);
      RtEvent view_ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      size_t num_fields;
      derez.deserialize(num_fields);
      std::vector<CopySrcDstField> dst_fields(num_fields);
      std::set<RtEvent> recorded_events, ready_events, applied_events;
      unpack_fields(dst_fields, derez, ready_events, view, view_ready, runtime);
      size_t num_reservations;
      derez.deserialize(num_reservations);
      std::vector<Reservation> reservations(num_reservations);
      for (unsigned idx = 0; idx < num_reservations; idx++)
        derez.deserialize(reservations[idx]);
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      Operation *op =
        RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      FieldMask copy_mask, dst_mask;
      derez.deserialize(copy_mask);
      derez.deserialize(dst_mask);
      Memory location;
      derez.deserialize(location);
      UniqueInst dst_inst;
      dst_inst.deserialize(derez);
      DistributedID src_inst_did;
      derez.deserialize(src_inst_did);
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready;
      derez.deserialize(ready);

      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      const ApEvent result = view->perform_collective_point(
          dst_fields, reservations, precondition, predicate_guard,
          copy_expression, op, index, copy_mask, dst_mask, location, 
          dst_inst, src_inst_did, trace_info, recorded_events, applied_events);

      Runtime::trigger_event(&trace_info, ready, result);
      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      delete op;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::perform_collective_broadcast(
                                const std::vector<CopySrcDstField> &src_fields,
                                ApEvent precondition,
                                PredEvent predicate_guard,
                                IndexSpaceExpression *copy_expression,
                                Operation *op, const unsigned index,
                                const size_t op_ctx_index,
                                const FieldMask &copy_mask,
                                const UniqueInst &src_inst,
                                const PhysicalTraceInfo &trace_info,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                ApUserEvent copy_done, ApUserEvent all_done,
                                ApBarrier all_bar, ShardID owner_shard,
                                AddressSpaceID origin, 
                                const bool copy_restricted)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(copy_done.exists());
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
      assert((op != NULL) || !copy_restricted);
#endif
      RtEvent analyses_ready;
      const std::vector<CollectiveAnalysis*> *local_analyses = NULL;
      if (!copy_restricted)
      {
        // If this is not a copy-out to a restricted collective instance 
        // then we should be able to find our local analyses to use for 
        // performing operations
        analyses_ready =
          find_collective_analyses(op_ctx_index, index, local_analyses);
#ifdef DEBUG_LEGION
        assert(local_analyses != NULL);
#endif
        // If we're recording then we need to wait now to get a valid
        // trace info for capturing the trace for events we send to 
        // remote nodes, otherwise we just need to wait before doing
        // the fill calls
        if ((trace_info.recording || (op == NULL)) && 
            analyses_ready.exists() && !analyses_ready.has_triggered())
          analyses_ready.wait();
        if (op == NULL)
          op = local_analyses->front()->get_operation();
      }
#ifdef DEBUG_LEGION
      assert(op != NULL);
#endif
      const PhysicalTraceInfo &local_info = 
        ((local_analyses == NULL) || !trace_info.recording) ? trace_info : 
        local_analyses->front()->get_trace_info();
      const UniqueID op_id = op->get_unique_op_id();
      // Do the copy to our local instance first
      IndividualView *local_view = local_views.front();
      ApEvent local_pre = local_view->find_copy_preconditions(
          false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, local_info);
      // Get the precondition for the local copy
      if (precondition.exists())
      {
        if (local_pre.exists())
          local_pre =
            Runtime::merge_events(&local_info, precondition, local_pre);
        else
          local_pre = precondition;
      }
      // Get the dst_fields and reservations for performing the local reductions
      std::vector<CopySrcDstField> local_fields;
      PhysicalManager *local_manager = local_view->get_manager();
      local_manager->compute_copy_offsets(copy_mask, local_fields);
      const std::vector<Reservation> no_reservations;
      const ApEvent copy_post = copy_expression->issue_copy(
          op, local_info, local_fields, src_fields, no_reservations,
#ifdef LEGION_SPY
          src_inst.tid, local_manager->tree_id,
#endif
          local_pre, predicate_guard);
      if (local_info.recording)
      {
        const UniqueInst dst_inst(local_view);
        local_info.record_copy_insts(copy_post, copy_expression,
          src_inst, dst_inst, copy_mask, copy_mask, 0/*redop*/, applied_events);
      }
      Runtime::trigger_event(&trace_info, copy_done, copy_post);
      // Always record the writer to ensure later reads catch it
      local_view->add_copy_user(false/*reading*/, 0/*redop*/, copy_post,
          local_info.get_collect_event(), copy_mask, copy_expression,
          op_id, index, recorded_events, local_info.recording,
          runtime->address_space);
      // Broadcast out the copy events to any children
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      // See if we're done
      if (children.empty() && (instances.size() == 1))
      {
        if (all_done.exists())
          Runtime::trigger_event(&trace_info, all_done, copy_post);
        return;
      }
      local_pre = local_view->find_copy_preconditions(
          true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, local_info);
      ApBarrier broadcast_bar;
      ShardID broadcast_shard = 0;
      std::vector<ApEvent> read_events, done_events;
      const UniqueInst local_inst(local_view);
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          pack_fields(rez, local_fields);
          local_inst.serialize(rez);
          rez.serialize(local_pre);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, *it);
          rez.serialize<bool>(copy_restricted);
          if (copy_restricted)
            op->pack_remote_operation(rez, *it, applied_events);
          rez.serialize(index);
          rez.serialize(op_ctx_index);
          rez.serialize(copy_mask);
          local_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (local_info.recording)
          {
            if (!broadcast_bar.exists())
            {
              broadcast_bar =
                ApBarrier(Realm::Barrier::create_barrier(children.size()));
              broadcast_shard = local_info.record_managed_barrier(broadcast_bar,
                                                               children.size());
              read_events.push_back(broadcast_bar);
            }
            rez.serialize(broadcast_bar);
            rez.serialize(broadcast_shard);
            rez.serialize(all_bar);
            if (all_bar.exists())
              rez.serialize(owner_shard);
          }
          else
          {
            const ApUserEvent broadcast = 
              Runtime::create_ap_user_event(&local_info);
            rez.serialize(broadcast);
            read_events.push_back(broadcast);
            ApUserEvent done;
            if (all_done.exists())
            {
              done = Runtime::create_ap_user_event(&local_info);
              done_events.push_back(done);
            }
            rez.serialize(done);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_broadcast(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      // Now broadcast out to the rest of our local instances
      // TODO: for now we just blast this out but we could at
      // some point build a local broadcast tree here for the
      // instances within this node
      // Do the last wait before we need our analyses for recording 
      // and profiling requests from the mappers
      if (analyses_ready.exists() && !analyses_ready.has_triggered())
        analyses_ready.wait();
      for (unsigned idx = 1; idx < local_views.size(); idx++)
      {
        IndividualView *dst_view = local_views[idx];
        PhysicalManager *dst_manager = dst_view->get_manager(); 
        std::vector<CopySrcDstField> dst_fields;
        dst_manager->compute_copy_offsets(copy_mask, dst_fields);
        const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
          trace_info : local_analyses->at(idx)->get_trace_info();
        ApEvent dst_pre = dst_view->find_copy_preconditions(
          false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, inst_info);
        if (local_pre.exists())
        {
          if (dst_pre.exists())
            dst_pre = Runtime::merge_events(&inst_info, dst_pre, local_pre);
          else
            dst_pre = local_pre;
        }
        const ApEvent local_copy = copy_expression->issue_copy(
            op, inst_info, dst_fields, local_fields, no_reservations,
#ifdef LEGION_SPY
            local_manager->tree_id, dst_manager->tree_id,
#endif
            dst_pre, predicate_guard);
        if (local_copy.exists())
        {
          read_events.push_back(local_copy);
          const RtEvent collect_event = inst_info.get_collect_event();
          dst_view->add_copy_user(false/*reading*/, 0/*redop*/, local_copy,
              collect_event, copy_mask, copy_expression, op_id, index,
              recorded_events, local_info.recording, runtime->address_space);
        }
        if (inst_info.recording)
        {
          const UniqueInst dst_inst(dst_view);
          inst_info.record_copy_insts(local_copy, copy_expression, local_inst,
              dst_inst, copy_mask, copy_mask, 0/*redop*/, applied_events);
        }
      }
      if (!read_events.empty())
      {
        ApEvent read_done = Runtime::merge_events(&local_info, read_events);
        if (read_done.exists())
        {
          local_view->add_copy_user(true/*reading*/, 0/*redop*/, read_done,
              local_info.get_collect_event(), copy_mask, copy_expression,
              op_id, index, recorded_events, local_info.recording,
              runtime->address_space);
          if (all_bar.exists() || all_done.exists())
            done_events.push_back(all_done);
        }
      }
      if (all_bar.exists())
      {
        ApEvent arrival;
        if (!done_events.empty())
          arrival = Runtime::merge_events(&local_info, done_events);
        Runtime::phase_barrier_arrive(all_bar, 1/*count*/, arrival);
        local_info.record_barrier_arrival(all_bar, arrival, 1/*count*/,
                                          applied_events, owner_shard);
      }
      else if (all_done.exists())
      {
        if (!done_events.empty())
          Runtime::trigger_event(&trace_info, all_done,
              Runtime::merge_events(&local_info, done_events));
        else
          Runtime::trigger_event(&local_info, all_done);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_broadcast(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did;
      derez.deserialize(view_did);
      RtEvent view_ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      size_t num_fields;
      derez.deserialize(num_fields);
      std::vector<CopySrcDstField> src_fields(num_fields);
      std::set<RtEvent> recorded_events, ready_events, applied_events;
      unpack_fields(src_fields, derez, ready_events, view, view_ready, runtime);
      UniqueInst src_inst;
      src_inst.deserialize(derez);
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      bool copy_restricted;
      derez.deserialize(copy_restricted);
      Operation *op = NULL;
      if (copy_restricted)
        op = RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready, all_done;
      ApBarrier all_bar;
      ShardID owner_shard = 0;
      if (trace_info.recording)
      {
        ApBarrier broadcast_bar;
        derez.deserialize(broadcast_bar);
        ShardID broadcast_shard;
        derez.deserialize(broadcast_shard);
        // Copy-elmination will take care of this for us
        // when the trace is optimized
        ready = Runtime::create_ap_user_event(&trace_info);
        Runtime::phase_barrier_arrive(broadcast_bar, 1/*count*/, ready);
        trace_info.record_barrier_arrival(broadcast_bar, ready, 1/*count*/, 
                                          applied_events, broadcast_shard);
        derez.deserialize(all_bar);
        if (all_bar.exists())
          derez.deserialize(owner_shard);
      }
      else
      {
        derez.deserialize(ready);
        derez.deserialize(all_done);
      }
      AddressSpaceID origin;
      derez.deserialize(origin); 

      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      view->perform_collective_broadcast(src_fields, precondition,
          predicate_guard, copy_expression, op, index, op_ctx_index,
          copy_mask, src_inst, trace_info, recorded_events, applied_events,
          ready, all_done, all_bar, owner_shard, origin, copy_restricted);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      if (op != NULL)
        delete op;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::perform_collective_reducecast(ReductionView *source,
                                const std::vector<CopySrcDstField> &src_fields,
                                ApEvent precondition,
                                PredEvent predicate_guard,
                                IndexSpaceExpression *copy_expression,
                                Operation *op, const unsigned index,
                                const size_t op_ctx_index,
                                const FieldMask &copy_mask,
                                const UniqueInst &src_inst,
                                const PhysicalTraceInfo &trace_info,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                ApUserEvent reduce_done, ApBarrier all_bar,
                                ShardID owner_shard, AddressSpaceID origin, 
                                const bool copy_restricted)
    //--------------------------------------------------------------------------
    {
      ReductionOpID src_redop = source->get_redop();
#ifdef DEBUG_LEGION
      assert(src_redop > 0);
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
      assert((op != NULL) || !copy_restricted);
      // Only one of these should be valid
      assert(reduce_done.exists() != all_bar.exists());
#endif
      // If we have any children, broadcast this out to the first in parallel
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      std::vector<ApEvent> reduce_events;
      if (!children.empty() && !trace_info.recording)
      {
        // Help out with broadcasting the precondition event
        // In the tracing case the precondition is a barrier 
        // so there's no need for us to do this
        const ApUserEvent local_precondition =
          Runtime::create_ap_user_event(&trace_info);
        Runtime::trigger_event(&trace_info, local_precondition, precondition);
        precondition = local_precondition;
      }
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(source->did);
          source->pack_fields(rez, src_fields);
          src_inst.serialize(rez);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, *it);
          rez.serialize<bool>(copy_restricted);
          if (copy_restricted)
            op->pack_remote_operation(rez, *it, applied_events);
          rez.serialize(index);
          rez.serialize(op_ctx_index);
          rez.serialize(copy_mask);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (trace_info.recording)
          {
            rez.serialize(all_bar);
            rez.serialize(owner_shard);
          }
          else
          {
            const ApUserEvent reduced =
              Runtime::create_ap_user_event(&trace_info);
            rez.serialize(reduced);
            reduce_events.push_back(reduced);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_reducecast(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      RtEvent analyses_ready;
      const std::vector<CollectiveAnalysis*> *local_analyses = NULL;
      if (!copy_restricted)
      {
        // If this is not a copy-out to a restricted collective instance 
        // then we should be able to find our local analyses to use for 
        // performing operations
        analyses_ready = 
          find_collective_analyses(op_ctx_index, index, local_analyses);
#ifdef DEBUG_LEGION
        assert(local_analyses != NULL);
#endif
        // If we're recording then we need to wait now to get a valid
        // trace info for capturing the trace for events we send to 
        // remote nodes, otherwise we just need to wait before doing
        // the fill calls
        if ((trace_info.recording || (op == NULL)) && 
            analyses_ready.exists() && !analyses_ready.has_triggered())
          analyses_ready.wait();
        if (op == NULL)
          op = local_analyses->front()->get_operation();
      }
#ifdef DEBUG_LEGION
      assert(op != NULL);
#endif
      const PhysicalTraceInfo &local_info = 
        ((local_analyses == NULL) || !trace_info.recording) ? trace_info : 
        local_analyses->front()->get_trace_info();
      const UniqueID op_id = op->get_unique_op_id();
      std::vector<ApEvent> local_done_events; 
      std::vector<CopySrcDstField> local_fields;
      std::vector<Reservation> local_reservations;
      // Issue the reductions to our local instances
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
          trace_info : local_analyses->at(idx)->get_trace_info();
        IndividualView *dst_view = local_views[idx];
        // Compute the reducing precondition for our local instances
        ApEvent reduce_pre = dst_view->find_copy_preconditions(
            false/*reading*/, src_redop, copy_mask, copy_expression,
            op_id, index, applied_events, inst_info);
        if (precondition.exists())
        {
          if (reduce_pre.exists())
            reduce_pre =
              Runtime::merge_events(&inst_info, precondition, reduce_pre);
          else
            reduce_pre = precondition;
        }
        PhysicalManager *dst_manager = dst_view->get_manager();
        dst_manager->compute_copy_offsets(copy_mask, local_fields);
        for (std::vector<CopySrcDstField>::iterator it =
              local_fields.begin(); it != local_fields.end(); it++)
          it->set_redop(src_redop, (get_redop() > 0), true/*exclusive*/);
        dst_view->find_field_reservations(copy_mask, local_reservations);
        const ApEvent reduce_done = copy_expression->issue_copy(
            op, inst_info, local_fields, src_fields, local_reservations,
#ifdef LEGION_SPY
            src_inst.tid, dst_manager->tree_id,
#endif
            reduce_pre, predicate_guard);
        if (reduce_done.exists())
        {
          local_done_events.push_back(reduce_done);
          const RtEvent collect_event = inst_info.get_collect_event();
          dst_view->add_copy_user(false/*reading*/, src_redop, reduce_done,
              collect_event, copy_mask, copy_expression, op_id, index,
              recorded_events, inst_info.recording, runtime->address_space);
        }
        if (inst_info.recording)
        {
          const UniqueInst dst_inst(dst_view);
          inst_info.record_copy_insts(reduce_done, copy_expression, src_inst,
              dst_inst, copy_mask, copy_mask, src_redop, applied_events);
        }
        local_fields.clear();
        local_reservations.clear();
      }
      if (all_bar.exists())
      {
        ApEvent local_done;
        if (!local_done_events.empty())
          local_done = Runtime::merge_events(&local_info, local_done_events);
        Runtime::phase_barrier_arrive(all_bar, 1/*count*/, local_done);
        local_info.record_barrier_arrival(all_bar, local_done, 1/*count*/,
                                          applied_events, owner_shard);
      }
      else
      {
        if (!local_done_events.empty())
          reduce_events.insert(reduce_events.end(),
              local_done_events.begin(), local_done_events.end());
        if (!reduce_events.empty())
          Runtime::trigger_event(&local_info, reduce_done, 
              Runtime::merge_events(&local_info, reduce_events));
        else
          Runtime::trigger_event(&local_info, reduce_done);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_reducecast(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did, src_did;
      derez.deserialize(view_did);
      RtEvent view_ready, src_ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      derez.deserialize(src_did);
      ReductionView *src_view = static_cast<ReductionView*>(
          runtime->find_or_request_logical_view(src_did, src_ready));
      size_t num_fields;
      derez.deserialize(num_fields);
      std::vector<CopySrcDstField> src_fields(num_fields);
      std::set<RtEvent> recorded_events, ready_events, applied_events;
      unpack_fields(src_fields, derez, ready_events, view, view_ready, runtime);
      UniqueInst src_inst;
      src_inst.deserialize(derez);
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      bool copy_restricted;
      derez.deserialize(copy_restricted);
      Operation *op = NULL;
      if (copy_restricted)
        op = RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready;
      ApBarrier all_bar;
      ShardID owner_shard = 0;
      if (trace_info.recording)
      {
        derez.deserialize(all_bar);
        if (all_bar.exists())
          derez.deserialize(owner_shard);
      }
      else
        derez.deserialize(ready);
      AddressSpaceID origin;
      derez.deserialize(origin); 

      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (src_ready.exists() && !src_ready.has_triggered())
        ready_events.insert(src_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      view->perform_collective_reducecast(src_view,
          src_fields, precondition, predicate_guard, copy_expression, op, 
          index, op_ctx_index, copy_mask, src_inst, trace_info, recorded_events,
          applied_events, ready, all_bar, owner_shard, origin, copy_restricted);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      if (op != NULL)
        delete op;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::perform_collective_hourglass(
                                          AllreduceView *source,
                                          ApEvent precondition,
                                          PredEvent predicate_guard,
                                          IndexSpaceExpression *copy_expression,
                                          Operation *op, const unsigned index,
                                          const FieldMask &copy_mask,
                                          const DistributedID src_inst_did,
                                          const PhysicalTraceInfo &trace_info,
                                          std::set<RtEvent> &recorded_events,
                                          std::set<RtEvent> &applied_events,
                                          ApUserEvent all_done,
                                          AddressSpaceID target,
                                          const bool copy_restricted)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(op != NULL);
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif
      if (target != local_space)
      {
        // Send this to where the target address space is
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(this->did);
          rez.serialize(source->did);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, target);
          op->pack_remote_operation(rez, target, applied_events);
          rez.serialize(index);
          rez.serialize(copy_mask);
          rez.serialize(src_inst_did);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          rez.serialize(all_done);
          rez.serialize(copy_restricted);
        }
        runtime->send_collective_distribute_hourglass(target, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
        return;
      }
#ifdef DEBUG_LEGION
      assert(!instances.empty());
#endif
      const UniqueID op_id = op->get_unique_op_id();
      IndividualView *local_view = local_views.front();
      // Perform the collective reduction first on the source
      ApEvent reduce_pre = local_view->find_copy_preconditions(
          false/*reding*/, source->redop, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      if (precondition.exists())
      {
        if (reduce_pre.exists())
          reduce_pre =
            Runtime::merge_events(&trace_info, precondition, reduce_pre);
        else
          reduce_pre = precondition;
      }
      PhysicalManager *local_manager = local_view->get_manager();
      // We'll just use the first instance for the target
      std::vector<CopySrcDstField> local_fields;
      local_manager->compute_copy_offsets(copy_mask, local_fields);
      std::vector<Reservation> reservations;
      local_view->find_field_reservations(copy_mask, reservations);
      for (unsigned idx = 0; idx < local_fields.size(); idx++)
        local_fields[idx].set_redop(source->redop, false/*fold*/,
                                    true/*exclusive*/);
      // Build the reduction tree down to our first instance
      const AddressSpaceID origin = (src_inst_did > 0) ? 
        runtime->determine_owner(src_inst_did) :
        source->select_source_space(local_space);
      ApEvent reduced;
      const UniqueInst local_inst(local_view);
      // Note that there is something subtle going on here!
      // If the copy aggregator needs to issue multiple reduction copies
      // to this collective instance, they might all need to do an
      // hourglass or a reducecast case, to keep things correct with
      // the broadcast, we rely on the ordering of collective messages
      // going out from the same source node to all the other nodes to
      // ensure that copies are done in the right order
      if (origin != local_space)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(source->did);
          pack_fields(rez, local_fields);
          rez.serialize<size_t>(reservations.size());
          for (unsigned idx = 0; idx < reservations.size(); idx++)
            rez.serialize(reservations[idx]);
          rez.serialize(reduce_pre);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, origin);
          op->pack_remote_operation(rez, origin, applied_events); 
          rez.serialize(index);
          rez.serialize(copy_mask);
          rez.serialize(copy_mask);
          rez.serialize(src_inst_did);
          local_inst.serialize(rez);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (trace_info.recording)
          {
            ApBarrier bar(Realm::Barrier::create_barrier(1/*arrivals*/));
            const ShardID sid = trace_info.record_managed_barrier(bar, 1);
            rez.serialize(bar);
            rez.serialize(sid);
            reduced = bar;
          }
          else
          {
            const ApUserEvent to_trigger = 
              Runtime::create_ap_user_event(&trace_info);
            rez.serialize(to_trigger);
            reduced = to_trigger;
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_reduction(origin, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      else
      {
        const ApUserEvent to_trigger = 
          Runtime::create_ap_user_event(&trace_info);
        source->perform_collective_reduction(local_fields,
            reservations, reduce_pre, predicate_guard, copy_expression,
            op, index, copy_mask, copy_mask, src_inst_did, local_inst, 
            trace_info, recorded_events, applied_events, to_trigger, origin);
        reduced = to_trigger;
      }
      // Record the write 
      if (reduced.exists())
        local_view->add_copy_user(false/*reading*/, source->redop, reduced,
            trace_info.get_collect_event(), copy_mask, copy_expression,
            op_id, index, recorded_events, trace_info.recording,
            runtime->address_space);
      // Do the broadcast out, start with any children
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(local_space, local_space, children);
      ApBarrier all_bar;
      ShardID owner_shard = 0;
      std::vector<ApEvent> all_done_events;
      if (!children.empty() || (local_views.size() > 1))
      {
        ApEvent broadcast_pre = local_view->find_copy_preconditions(
            true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
            op_id, index, applied_events, trace_info);
        if (precondition.exists())
        {
          if (broadcast_pre.exists())
            broadcast_pre =
              Runtime::merge_events(&trace_info, precondition, broadcast_pre);
          else
            broadcast_pre = precondition;
        }
        ApBarrier broadcast_bar;
        ShardID broadcast_shard = 0;
        std::vector<ApEvent> broadcast_events;
        if (all_done.exists() && trace_info.recording)
        {
          const size_t arrivals = collective_mapping->size();
          all_bar = ApBarrier(Realm::Barrier::create_barrier(arrivals));
          owner_shard = trace_info.record_managed_barrier(all_bar, arrivals);
        }
        for (std::vector<AddressSpaceID>::const_iterator it =
              children.begin(); it != children.end(); it++)
        {
          const RtUserEvent recorded = Runtime::create_rt_user_event();
          const RtUserEvent applied = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(this->did);
            rez.serialize(local_view->did);
            pack_fields(rez, local_fields);
            local_inst.serialize(rez);
            rez.serialize(broadcast_pre);
            rez.serialize(predicate_guard);
            copy_expression->pack_expression(rez, *it);
            rez.serialize<bool>(copy_restricted);
            if (copy_restricted)
              op->pack_remote_operation(rez, origin, applied_events); 
            rez.serialize(index);
            rez.serialize(op->get_ctx_index());
            rez.serialize(copy_mask);
            trace_info.pack_trace_info(rez, applied_events);
            rez.serialize(recorded);
            rez.serialize(applied);
            if (trace_info.recording)
            {
              if (!broadcast_bar.exists())
              {
                broadcast_bar =
                  ApBarrier(Realm::Barrier::create_barrier(children.size()));
                broadcast_shard = trace_info.record_managed_barrier(
                                      broadcast_bar, children.size());
                broadcast_events.push_back(broadcast_bar);
              }
              rez.serialize(broadcast_bar);
              rez.serialize(broadcast_shard);
              rez.serialize(all_bar);
              if (all_bar.exists())
                rez.serialize(owner_shard);
            }
            else
            {
              const ApUserEvent done =
                Runtime::create_ap_user_event(&trace_info);
              rez.serialize(done);
              broadcast_events.push_back(done);
              ApUserEvent all;
              if (all_done.exists())
              {
                all = Runtime::create_ap_user_event(&trace_info);
                all_done_events.push_back(all);
              }
              rez.serialize(all);
            }
            rez.serialize(origin);
          }
          runtime->send_collective_distribute_broadcast(origin, rez);
          recorded_events.insert(recorded);
          applied_events.insert(applied);
        }
        // Then do our local broadcast
        // TODO: if the number of local instances is large then we could
        // turn this into a tree broadcast, but for now we're just going
        // to copy everything out of the first instance
        for (unsigned idx = 1; idx < local_views.size(); idx++)
        {
          ApEvent copy_pre = local_views[idx]->find_copy_preconditions(
              false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
              op_id, index, applied_events, trace_info);
          if (broadcast_pre.exists())
          {
            if (copy_pre.exists())
              copy_pre = Runtime::merge_events(&trace_info, 
                                  copy_pre, broadcast_pre);
            else
              copy_pre = broadcast_pre;
          }
          PhysicalManager *dst_manager = local_views[idx]->get_manager();
          std::vector<CopySrcDstField> dst_fields;
          dst_manager->compute_copy_offsets(copy_mask, local_fields);
          const std::vector<Reservation> no_reservations;
          const ApEvent copy_post = copy_expression->issue_copy(
              op, trace_info, dst_fields, local_fields, no_reservations,
#ifdef LEGION_SPY
              local_manager->tree_id, dst_manager->tree_id,
#endif
              copy_pre, predicate_guard);
          if (copy_post.exists())
          {
            broadcast_events.push_back(copy_post);
            local_views[idx]->add_copy_user(false/*reading*/, 0/*redop*/,
                copy_post, trace_info.get_collect_event(), copy_mask, 
                copy_expression, op_id, index, recorded_events,
                trace_info.recording, runtime->address_space);
          }
          if (trace_info.recording)
          {
            const UniqueInst dst_inst(local_views[idx]);
            trace_info.record_copy_insts(copy_post, copy_expression, local_inst,
                   local_inst, copy_mask, copy_mask, 0/*redop*/, applied_events);
          }
        }
        if (!broadcast_events.empty())
        {
          const ApEvent broadcast_done =
            Runtime::merge_events(&trace_info, broadcast_events);
          if (broadcast_done.exists())
          {
            local_view->add_copy_user(true/*reading*/, 0/*redop*/, 
                broadcast_done, trace_info.get_collect_event(), copy_mask, 
                copy_expression, op_id, index, recorded_events,
                trace_info.recording, runtime->address_space);
            if (all_done.exists())
              all_done_events.push_back(broadcast_done);
          }
        }
      }
      if (all_done.exists())
      {
        if (all_bar.exists())
        {
          ApEvent arrival;
          if (!all_done_events.empty())
            arrival = Runtime::merge_events(&trace_info, all_done_events);
          Runtime::phase_barrier_arrive(all_bar, 1/*count*/, arrival);
          trace_info.record_barrier_arrival(all_bar, arrival, 1/*count*/,
                                            applied_events, owner_shard);
          Runtime::trigger_event(&trace_info, all_done, all_bar);
        }
        else
        {
          if (!all_done_events.empty())
            Runtime::trigger_event(&trace_info, all_done,
                Runtime::merge_events(&trace_info, all_done_events));
          else
            Runtime::trigger_event(&trace_info, all_done);
        }
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_hourglass(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent dst_view_ready, src_view_ready;
      CollectiveView *target = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, dst_view_ready));
      derez.deserialize(did);
      AllreduceView *src_view = static_cast<AllreduceView*>(
          runtime->find_or_request_logical_view(did, src_view_ready));
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      std::set<RtEvent> ready_events;
      Operation *op =
        RemoteOp::unpack_remote_operation(derez, runtime, ready_events); 
      unsigned index;
      derez.deserialize(index);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      DistributedID src_inst_did;
      derez.deserialize(src_inst_did);
      std::set<RtEvent> recorded_events, applied_events;
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent all_done;
      derez.deserialize(all_done);
      bool copy_restricted;
      derez.deserialize<bool>(copy_restricted);

      if (src_view_ready.exists() && !src_view_ready.has_triggered())
        ready_events.insert(src_view_ready);
      if (dst_view_ready.exists() && !dst_view_ready.has_triggered())
        ready_events.insert(dst_view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      target->perform_collective_hourglass(src_view,
          precondition, predicate_guard, copy_expression, op, index,
          copy_mask, src_inst_did, trace_info, recorded_events, applied_events,
          all_done, runtime->address_space, copy_restricted);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      delete op;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::perform_collective_pointwise(
                                          CollectiveView *source,
                                          ApEvent precondition,
                                          PredEvent predicate_guard,
                                          IndexSpaceExpression *copy_expression,
                                          Operation *op, const unsigned index,
                                          const size_t op_ctx_index,
                                          const FieldMask &copy_mask,
                                          const DistributedID src_inst_did,
                                          const UniqueID src_inst_did_op,
                                          const PhysicalTraceInfo &trace_info,
                                          std::set<RtEvent> &recorded_events,
                                          std::set<RtEvent> &applied_events,
                                          ApUserEvent all_done,
                                          ApBarrier all_bar,
                                          ShardID owner_shard,
                                          AddressSpaceID origin,
                                          const uint64_t allreduce_tag,
                                          const bool copy_restricted)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(collective_mapping->contains(local_space));
      assert((op != NULL) || !copy_restricted);
#endif
      RtEvent analyses_ready;
      const std::vector<CollectiveAnalysis*> *local_analyses = NULL;
      if (!copy_restricted)
      {
        // If this is not a copy-out to a restricted collective instance 
        // then we should be able to find our local analyses to use for 
        // performing operations
        analyses_ready =
          find_collective_analyses(op_ctx_index, index, local_analyses);
#ifdef DEBUG_LEGION
        assert(local_analyses != NULL);
#endif
        // If we're recording then we need to wait now to get a valid
        // trace info for capturing the trace for events we send to 
        // remote nodes, otherwise we just need to wait before doing
        // the fill calls
        if ((trace_info.recording || (op == NULL)) && 
            analyses_ready.exists() && !analyses_ready.has_triggered())
          analyses_ready.wait();
        if (op == NULL)
          op = local_analyses->front()->get_operation();
      }
#ifdef DEBUG_LEGION
      assert(op != NULL);
#endif
      const PhysicalTraceInfo &local_info = 
        ((local_analyses == NULL) || !trace_info.recording) ? trace_info : 
        local_analyses->front()->get_trace_info();
      // First distribute this off to all the child nodes
      std::vector<ApEvent> done_events;
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(this->did);
          rez.serialize(source->did);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, *it);
          rez.serialize<bool>(copy_restricted);
          if (copy_restricted)
            op->pack_remote_operation(rez, *it, applied_events);
          rez.serialize(index);
          rez.serialize(op_ctx_index);
          rez.serialize(copy_mask);
          rez.serialize(src_inst_did);
          rez.serialize(src_inst_did_op);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (local_info.recording)
          {
            rez.serialize(all_bar);
            if (all_bar.exists())
              rez.serialize(owner_shard);
          }
          else
          {
            ApUserEvent done; 
            if (all_done.exists())
            {
              done = Runtime::create_ap_user_event(&local_info);
              done_events.push_back(done);
            }
            rez.serialize(done);
          }
          rez.serialize(origin);
          rez.serialize(allreduce_tag);
        }
        runtime->send_collective_distribute_pointwise(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      const UniqueID op_id = op->get_unique_op_id();
      // If the source is a reduction manager, this is where we need
      // to perform the all-reduce before issuing the pointwise copies
      if (source->is_allreduce_view())
      {
#ifdef DEBUG_LEGION
        // Better have the same collective mappings if we're doing all-reduce
        assert((collective_mapping == source->collective_mapping) ||
            ((*collective_mapping) == (*(source->collective_mapping))));
        assert(source->is_reduction_kind());
#endif
        AllreduceView *allreduce = source->as_allreduce_view();
        // Wait for the analyses to be available if they aren't already
        if (analyses_ready.exists() && !analyses_ready.has_triggered())
          analyses_ready.wait();
        allreduce->perform_collective_allreduce(precondition,
            predicate_guard, copy_expression, op, index, copy_mask, local_info,
            local_analyses, recorded_events, applied_events, allreduce_tag);
      }
      
      // Wait for the analyses to be available if they aren't already
      if (analyses_ready.exists() && !analyses_ready.has_triggered())
        analyses_ready.wait();
      // Now we can do our local copies
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
          trace_info : local_analyses->at(idx)->get_trace_info();
        IndividualView *local_view = local_views[idx];
        // Find the precondition for all our local copies
        ApEvent dst_pre = local_view->find_copy_preconditions(
            false/*reading*/, source->get_redop(), copy_mask, 
            copy_expression, op_id, index, applied_events, inst_info);
        if (precondition.exists())
        {
          if (dst_pre.exists())
            dst_pre = Runtime::merge_events(&local_info, precondition, dst_pre);
          else
            dst_pre = precondition;
        }
        PhysicalManager *local_manager = local_view->get_manager();
        // Get our dst_fields
        std::vector<CopySrcDstField> dst_fields;
        local_manager->compute_copy_offsets(copy_mask, dst_fields); 
        std::vector<Reservation> reservations;
        if (source->get_redop() > 0)
        {
          local_view->find_field_reservations(copy_mask, reservations);
          for (unsigned idx = 0; idx < dst_fields.size(); idx++)
            dst_fields[idx].set_redop(source->get_redop(), false/*fold*/,
                                      true/*exclusive*/);
        }
        const Memory location = local_manager->memory_manager->memory;
        // Now we need to pick the source point for this copy if it hasn't
        // already been picked by the mapper
        DistributedID local_src_inst_did = 0;
        if (!copy_restricted)
        {
#ifdef DEBUG_LEGION
          assert(local_analyses != NULL);
#endif
          CollectiveAnalysis *analysis = local_analyses->at(idx);
          // See if this is the same analysis that already had a change to
          // pick the source instance because it was the one issuing this
          // copy in the first place. If not then we give the mapper a 
          // chance to pick the source now
          Operation *analysis_op = analysis->get_operation();
          if (analysis_op->get_unique_op_id() != src_inst_did_op)
          {
            // invoke the mapper to pick the source point in this case
            std::vector<InstanceView*> src_views(1, source);
            std::vector<unsigned> ranking;
            std::map<unsigned,PhysicalManager*> points;
            analysis_op->select_sources(analysis->get_requirement_index(),
                                local_manager, src_views, ranking, points);
            std::map<unsigned,PhysicalManager*>::const_iterator finder = 
              points.find(0);
            if (finder != points.end())
              local_src_inst_did = finder->second->did;
          }
          else // mapper already had a chance to pick the source point
            local_src_inst_did = src_inst_did;
        }
        // TODO: how to let the mapper pick in copy-out cases
        // If the mapper didn't pick a source point then we can
        const AddressSpaceID src = (local_src_inst_did > 0) ?
          runtime->determine_owner(local_src_inst_did) :
          source->select_source_space(local_space);
        const UniqueInst dst_inst(local_view);
        ApEvent local_done;
        if (src != local_space)
        {
          const RtUserEvent recorded = Runtime::create_rt_user_event();
          const RtUserEvent applied = Runtime::create_rt_user_event();
          ApUserEvent done = Runtime::create_ap_user_event(&inst_info);
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(source->did);
            pack_fields(rez, dst_fields);
            rez.serialize<size_t>(reservations.size());
            for (unsigned idx2 = 0; idx2 < reservations.size(); idx2++)
              rez.serialize(reservations[idx2]);
            rez.serialize(precondition);
            rez.serialize(predicate_guard);
            copy_expression->pack_expression(rez, src);
            op->pack_remote_operation(rez, src, applied_events);
            rez.serialize(index);
            rez.serialize(copy_mask);
            rez.serialize(copy_mask); // again for dst mask
            rez.serialize(location);
            dst_inst.serialize(rez);
            rez.serialize(local_src_inst_did);
            inst_info.pack_trace_info(rez, applied_events);
            rez.serialize(recorded);
            rez.serialize(applied);
            rez.serialize(done);
          }
          runtime->send_collective_distribute_point(src, rez);
          recorded_events.insert(recorded);
          applied_events.insert(applied);
          local_done = done;
        }
        else
          local_done = source->perform_collective_point(
              dst_fields, reservations, precondition, predicate_guard,
              copy_expression, op, index, copy_mask, copy_mask, location,
              dst_inst, local_src_inst_did, inst_info, recorded_events, 
              applied_events);
        if (local_done.exists())
        {
          done_events.push_back(local_done);
          const RtEvent collect_event = inst_info.get_collect_event();
          local_view->add_copy_user(false/*reading*/, source->get_redop(),
              local_done, collect_event, copy_mask, copy_expression,
              op_id, index, recorded_events, inst_info.recording,
              runtime->address_space);
        }
      }
      if (all_bar.exists())
      {
        ApEvent arrival;
        if (!done_events.empty())
          arrival = Runtime::merge_events(&local_info, done_events);
        Runtime::phase_barrier_arrive(all_bar, 1/*count*/, arrival);
        local_info.record_barrier_arrival(all_bar, arrival, 1/*count*/,
                                          applied_events, owner_shard);
      }
      else if (all_done.exists())
      {
        if (!done_events.empty())
          Runtime::trigger_event(&local_info, all_done,
              Runtime::merge_events(&local_info, done_events));
        else
          Runtime::trigger_event(&local_info, all_done);
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_distribute_pointwise(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez); 
      DistributedID did;
      derez.deserialize(did);
      RtEvent dst_view_ready, src_view_ready;
      CollectiveView *dst_view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, dst_view_ready));
      derez.deserialize(did);
      CollectiveView *src_view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, src_view_ready));
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      bool copy_restricted;
      derez.deserialize(copy_restricted);
      Operation *op = NULL;
      std::set<RtEvent> ready_events;
      if (copy_restricted)
        op = RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      DistributedID src_inst_did;
      derez.deserialize(src_inst_did);
      UniqueID src_inst_did_op;
      derez.deserialize(src_inst_did_op);
      std::set<RtEvent> recorded_events, applied_events;
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApBarrier all_bar;
      ShardID owner_shard = 0;
      ApUserEvent all_done;
      if (trace_info.recording)
      {
        derez.deserialize(all_bar);
        if (all_bar.exists())
          derez.deserialize(owner_shard);
      }
      else
        derez.deserialize(all_done);
      AddressSpaceID origin;
      derez.deserialize(origin);
      uint64_t allreduce_tag;
      derez.deserialize(allreduce_tag); 

      if (src_view_ready.exists() && !src_view_ready.has_triggered())
        ready_events.insert(src_view_ready);
      if (dst_view_ready.exists() && !dst_view_ready.has_triggered())
        ready_events.insert(dst_view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }
      // Check if this is the first invocation for allreduce on a
      // node where we can get a tag
      if ((allreduce_tag == 0) && src_view->is_allreduce_view())
      {
        AllreduceView *allreduce = src_view->as_allreduce_view();
        allreduce_tag = allreduce->generate_unique_allreduce_tag();
      }

      dst_view->perform_collective_pointwise(src_view,
          precondition, predicate_guard, copy_expression, op, index,
          op_ctx_index, copy_mask, src_inst_did, src_inst_did_op,
          trace_info, recorded_events, applied_events, all_done, all_bar,
          owner_shard, origin, allreduce_tag, copy_restricted);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      if (op != NULL)
        delete op;
    }

    /////////////////////////////////////////////////////////////
    // ReplicatedView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplicatedView::ReplicatedView(RegionTreeForest *ctx, DistributedID id,
                                   AddressSpaceID owner_proc, 
                                   UniqueID owner_context, 
                                   const std::vector<IndividualView*> &views,
                                   const std::vector<DistributedID> &insts,
                                   bool register_now,CollectiveMapping *mapping)
      : CollectiveView(ctx, encode_replicated_did(id), owner_proc,
                       owner_context, views, insts, register_now, mapping)
    //--------------------------------------------------------------------------
    {
#ifdef LEGION_GC
      log_garbage.info("GC Replicated View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space); 
#endif
    }

    //--------------------------------------------------------------------------
    ReplicatedView::~ReplicatedView(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    void ReplicatedView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_context);
        rez.serialize<size_t>(instances.size());
        rez.serialize(&instances.front(), 
            instances.size() * sizeof(DistributedID));
        if (collective_mapping != NULL)
          collective_mapping->pack(rez);
        else
          rez.serialize<size_t>(0);
      }
      runtime->send_replicated_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReplicatedView::handle_send_replicated_view(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      UniqueID owner_context;
      derez.deserialize(owner_context);
      size_t num_insts;
      derez.deserialize(num_insts);
      std::vector<DistributedID> instances(num_insts);
      derez.deserialize(&instances.front(), num_insts * sizeof(DistributedID));
      size_t num_spaces;
      derez.deserialize(num_spaces);
      CollectiveMapping *mapping = NULL;
      if (num_spaces > 0)
      {
        mapping = new CollectiveMapping(derez, num_spaces);
        mapping->add_reference();
      }
      void *location;
      ReplicatedView *view = NULL;
      std::vector<IndividualView*> no_views;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) ReplicatedView(runtime->forest, did, source,
                                            owner_context, no_views, instances,
                                            false/*register now*/, mapping);
      else
        view = new ReplicatedView(runtime->forest, did, source,
                                  owner_context, no_views, instances,
                                  false/*register now*/, mapping);
      // Register only after construction
      view->register_with_runtime();
      if ((mapping != NULL) && mapping->remove_reference())
        delete mapping;
    }

    /////////////////////////////////////////////////////////////
    // AllreduceView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    AllreduceView::AllreduceView(RegionTreeForest *ctx, DistributedID id,
                                 AddressSpaceID owner_proc, 
                                 UniqueID owner_context, 
                                 const std::vector<IndividualView*> &views,
                                 const std::vector<DistributedID> &insts,
                                 bool register_now, CollectiveMapping *mapping,
                                 ReductionOpID redop_id)
      : CollectiveView(ctx, encode_allreduce_did(id), owner_proc, owner_context,
                       views, insts, register_now, mapping), redop(redop_id),
        reduction_op(runtime->get_reduction_op(redop)),
        fill_view(runtime->find_or_create_reduction_fill_view(redop)),
        unique_allreduce_tag(mapping->contains(local_space) ? 
            mapping->find_index(local_space) : 0), multi_instance(false),
        evaluated_multi_instance(false)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        assert(local_views[idx]->get_redop() == redop);
#endif
      fill_view->add_nested_resource_ref(did);
      // We reserve the 0 all-reduce tag to mean no-tag
      if (unique_allreduce_tag.load() == 0)
        unique_allreduce_tag.fetch_add(collective_mapping->size());
#ifdef LEGION_GC
      log_garbage.info("GC Allreduce View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space); 
#endif
    }

    //--------------------------------------------------------------------------
    AllreduceView::~AllreduceView(void)
    //--------------------------------------------------------------------------
    {
      if (fill_view->remove_nested_resource_ref(did))
        delete fill_view;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_context);
        rez.serialize<size_t>(instances.size());
        rez.serialize(&instances.front(), 
            instances.size() * sizeof(DistributedID));
        if (collective_mapping != NULL)
          collective_mapping->pack(rez);
        else
          rez.serialize<size_t>(0);
        rez.serialize(redop);
      }
      runtime->send_allreduce_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    /*static*/ void AllreduceView::handle_send_allreduce_view(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      UniqueID owner_context;
      derez.deserialize(owner_context);
      size_t num_insts;
      derez.deserialize(num_insts);
      std::vector<DistributedID> instances(num_insts);
      derez.deserialize(&instances.front(), num_insts * sizeof(DistributedID));
      size_t num_spaces;
      derez.deserialize(num_spaces);
      CollectiveMapping *mapping = NULL;
      if (num_spaces > 0)
      {
        mapping = new CollectiveMapping(derez, num_spaces);
        mapping->add_reference();
      }
      ReductionOpID redop;
      derez.deserialize(redop);
      void *location;
      AllreduceView *view = NULL;
      std::vector<IndividualView*> no_views;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) AllreduceView(runtime->forest, did, source,
                                           owner_context, no_views, instances,
                                           false/*register now*/, 
                                           mapping, redop);
      else
        view = new AllreduceView(runtime->forest, did, source,
                                 owner_context, no_views, instances,
                                 false/*register now*/, mapping, redop);
      // Register only after construction
      view->register_with_runtime();
      if ((mapping != NULL) && mapping->remove_reference())
        delete mapping;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::perform_collective_reduction(
                                const std::vector<CopySrcDstField> &dst_fields,
                                const std::vector<Reservation> &reservations,
                                ApEvent precondition,
                                PredEvent predicate_guard,
                                IndexSpaceExpression *copy_expression,
                                Operation *op, const unsigned index,
                                const FieldMask &copy_mask,
                                const FieldMask &dst_mask,
                                const DistributedID src_inst_did,
                                const UniqueInst &dst_inst,
                                const PhysicalTraceInfo &trace_info,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                ApUserEvent result, AddressSpaceID origin)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(redop > 0);
      assert(op != NULL);
      assert(result.exists());
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif
      unsigned target_index = 0;
      if (src_inst_did > 0)
      {
#ifdef DEBUG_LEGION
        target_index = UINT_MAX;
#endif
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          if (local_views[idx]->get_manager()->did != src_inst_did)
            continue;
          target_index = idx;
          break;
        }
#ifdef DEBUG_LEGION
        assert(target_index != UINT_MAX);
#endif
      }
      IndividualView *local_view = local_views[target_index];
      PhysicalManager *local_manager = local_view->get_manager();
      // Get the dst_fields and reservations for performing the local reductions
      std::vector<CopySrcDstField> local_fields;
      local_manager->compute_copy_offsets(copy_mask, local_fields);

      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      // Get the precondition for performing reductions to one of our instances
      ApEvent reduce_pre; 
      std::vector<Reservation> local_reservations;
      const UniqueID op_id = op->get_unique_op_id();
      if (!children.empty() || (instances.size() > 1))
      {
        // Compute the precondition for performing any reductions
        reduce_pre = local_view->find_copy_preconditions(false/*reading*/,
            redop, copy_mask, copy_expression, op_id, 
            index, applied_events, trace_info);
        // If we're going to be doing reductions we need the reservations
        local_view->find_field_reservations(copy_mask, local_reservations);
        for (unsigned idx = 0; idx < local_fields.size(); idx++)
          local_fields[idx].set_redop(redop, true/*fold*/, true/*exclusive*/);
      }
      std::vector<ApEvent> reduce_events;
      // If we have any children, send them messages to reduce to our instance
      ApBarrier trace_barrier;
      ShardID trace_shard = 0;
      const UniqueInst local_inst(local_view);
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          pack_fields(rez, local_fields);
          rez.serialize<size_t>(local_reservations.size());
          for (unsigned idx = 0; idx < local_reservations.size(); idx++)
            rez.serialize(local_reservations[idx]);
          rez.serialize(reduce_pre);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, *it);
          op->pack_remote_operation(rez, *it, applied_events);
          rez.serialize(index);
          rez.serialize(copy_mask);
          rez.serialize(dst_mask);
          rez.serialize<DistributedID>(0); // no source point in this case
          local_inst.serialize(rez);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (trace_info.recording)
          {
            if (!trace_barrier.exists())
            {
              trace_barrier = 
                ApBarrier(Realm::Barrier::create_barrier(children.size()));
              trace_shard = trace_info.record_managed_barrier(trace_barrier,
                                                              children.size());
              reduce_events.push_back(trace_barrier);
            }
            rez.serialize(trace_barrier);
            if (trace_barrier.exists())
              rez.serialize(trace_shard);
          }
          else
          {
            const ApUserEvent reduced =
              Runtime::create_ap_user_event(&trace_info);
            rez.serialize(reduced);
            reduce_events.push_back(reduced);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_distribute_reduction(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      // Perform our local reductions
      if (local_views.size() > 1)
      {
        // TODO: We could build a tree reduction here inside the
        // local node as well, but that seems unnecessary for most
        // cases so we're just going to reduce everything to the 
        // target for now
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          if (idx == target_index)
            continue;
          std::vector<CopySrcDstField> src_fields;
          IndividualView *src_view = local_views[idx];
          PhysicalManager *src_manager = src_view->get_manager();
          src_manager->compute_copy_offsets(copy_mask, src_fields);
          ApEvent read_pre = src_view->find_copy_preconditions(
              true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
              op_id, index, applied_events, trace_info);
          ApEvent local_pre = reduce_pre.exists() ? (read_pre.exists() ?
            Runtime::merge_events(&trace_info, reduce_pre, read_pre) :
            reduce_pre) : read_pre;
          const ApEvent local_reduce = copy_expression->issue_copy(
              op, trace_info, local_fields, src_fields, local_reservations,
#ifdef LEGION_SPY
              local_manager->tree_id, src_manager->tree_id,
#endif
              local_pre, predicate_guard); 
          if (local_reduce.exists())
          {
            reduce_events.push_back(local_reduce);
            const RtEvent collect_event = trace_info.get_collect_event();
            src_view->add_copy_user(true/*reading*/, 0/*redop*/, local_reduce,
                collect_event, copy_mask, copy_expression, op_id, index, 
                recorded_events, trace_info.recording, runtime->address_space);
          }
          if (trace_info.recording)
          {
            const UniqueInst src_inst(src_view);
            trace_info.record_copy_insts(local_reduce, copy_expression,
             src_inst, local_inst, copy_mask, copy_mask, redop, applied_events);
          }
        }
      }
      if (!reduce_events.empty())
      {
        const ApEvent reduce_post =
          Runtime::merge_events(&trace_info, reduce_events);
        if (reduce_post.exists())
        {
          const RtEvent collect_event = trace_info.get_collect_event();
          local_view->add_copy_user(false/*reading*/, redop, reduce_post,
              collect_event, copy_mask, copy_expression, op_id, index,
              recorded_events, trace_info.recording, runtime->address_space);
        }
      }
      // Peform the reduction back to the destination
      const ApEvent read_pre = local_view->find_copy_preconditions(
          true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      // Set the redops back to 0
      for (unsigned idx = 0; idx < local_fields.size(); idx++)
        local_fields[idx].set_redop(0, false/*fold*/);
      if (precondition.exists())
      {
        if (read_pre.exists())
          precondition = 
            Runtime::merge_events(&trace_info, precondition, read_pre);
      }
      else
        precondition = read_pre;
      // Perform the reduction to the destination
      const ApEvent reduce_post = copy_expression->issue_copy(
          op, trace_info, dst_fields, local_fields, reservations,
#ifdef LEGION_SPY
          local_manager->tree_id, dst_inst.tid,
#endif
          precondition, predicate_guard);
      // Trigger the output
      Runtime::trigger_event(&trace_info, result, reduce_post);
      // Save the result, note that this reading of this final reduction
      // always dominates any incoming reductions so we don't need to 
      // record them separately
      if (reduce_post.exists())
      {
        const RtEvent collect_event = trace_info.get_collect_event();
        local_view->add_copy_user(true/*reading*/, 0/*redop*/, reduce_post,
            collect_event, copy_mask, copy_expression, op_id, index,
            recorded_events, trace_info.recording, runtime->address_space);
      }
      if (trace_info.recording)
        trace_info.record_copy_insts(reduce_post, copy_expression,
            local_inst, dst_inst, copy_mask, dst_mask, redop, applied_events);
    }

    //--------------------------------------------------------------------------
    uint64_t AllreduceView::generate_unique_allreduce_tag(void)
    //--------------------------------------------------------------------------
    {
      // We should always be calling this one of the original collective
      // nodes for the allreduce view at the moment
#ifdef DEBUG_LEGION
      assert(collective_mapping->contains(local_space));
#endif
      return unique_allreduce_tag.fetch_add(collective_mapping->size());
    }

    //--------------------------------------------------------------------------
    /*static*/ void AllreduceView::handle_distribute_reduction(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did;
      derez.deserialize(view_did);
      RtEvent view_ready;
      AllreduceView *view = static_cast<AllreduceView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      size_t num_fields;
      derez.deserialize(num_fields);
      std::vector<CopySrcDstField> dst_fields(num_fields);
      std::set<RtEvent> recorded_events, ready_events, applied_events;
      unpack_fields(dst_fields, derez, ready_events, view, view_ready ,runtime);
      size_t num_reservations;
      derez.deserialize(num_reservations);
      std::vector<Reservation> reservations(num_reservations);
      for (unsigned idx = 0; idx < num_reservations; idx++)
        derez.deserialize(reservations[idx]);
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      Operation *op =
        RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      FieldMask copy_mask, dst_mask;
      derez.deserialize(copy_mask);
      derez.deserialize(dst_mask);
      DistributedID src_inst_did;
      derez.deserialize(src_inst_did);
      UniqueInst dst_inst;
      dst_inst.deserialize(derez);
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready;
      if (trace_info.recording)
      {
        ApBarrier bar;
        derez.deserialize(bar);
        ShardID sid;
        derez.deserialize(sid);
        // Copy-elmination will take care of this for us
        // when the trace is optimized
        ready = Runtime::create_ap_user_event(&trace_info);
        Runtime::phase_barrier_arrive(bar, 1/*count*/, ready);
        trace_info.record_barrier_arrival(bar, ready, 1/*count*/, 
                                          applied_events, sid);
      }
      else
        derez.deserialize(ready);
      AddressSpaceID origin;
      derez.deserialize(origin);

      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      view->perform_collective_reduction(dst_fields, reservations,
          precondition, predicate_guard, copy_expression, op, index, copy_mask,
          dst_mask, src_inst_did, dst_inst, trace_info, recorded_events, 
          applied_events, ready, origin);

      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      delete op;
    }

    //--------------------------------------------------------------------------
    ApEvent AllreduceView::perform_hammer_reduction(
                                const std::vector<CopySrcDstField> &dst_fields,
                                const std::vector<Reservation> &reservations,
                                ApEvent precondition,
                                PredEvent predicate_guard,
                                IndexSpaceExpression *copy_expression,
                                Operation *op, const unsigned index,
                                const FieldMask &copy_mask,
                                const FieldMask &dst_mask,
                                const UniqueInst &dst_inst,
                                const PhysicalTraceInfo &trace_info,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                AddressSpaceID origin)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(redop > 0);
      assert(op != NULL);
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif
      // Distribute out to the other nodes first
      std::vector<ApEvent> done_events;
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(origin, local_space, children);
      ApBarrier trace_barrier;
      ShardID trace_shard = 0;
      for (std::vector<AddressSpaceID>::const_iterator it =
            children.begin(); it != children.end(); it++)
      {
        const RtUserEvent recorded = Runtime::create_rt_user_event();
        const RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(this->did);
          pack_fields(rez, dst_fields);
          rez.serialize<size_t>(reservations.size());
          for (unsigned idx = 0; idx < reservations.size(); idx++)
            rez.serialize(reservations[idx]);
          rez.serialize(precondition);
          rez.serialize(predicate_guard);
          copy_expression->pack_expression(rez, *it);
          op->pack_remote_operation(rez, *it, applied_events); 
          rez.serialize(index);
          rez.serialize(copy_mask);
          rez.serialize(dst_mask);
          dst_inst.serialize(rez);
          trace_info.pack_trace_info(rez, applied_events);
          rez.serialize(recorded);
          rez.serialize(applied);
          if (trace_info.recording)
          {
            if (!trace_barrier.exists())
            {
              trace_barrier =
                ApBarrier(Realm::Barrier::create_barrier(children.size()));
              trace_shard = trace_info.record_managed_barrier(trace_barrier,
                                                              children.size());
              done_events.push_back(trace_barrier);
            }
            rez.serialize(trace_barrier);
            rez.serialize(trace_shard);
          }
          else
          {
            const ApUserEvent done = Runtime::create_ap_user_event(&trace_info);
            rez.serialize(done);
            done_events.push_back(done);
          }
          rez.serialize(origin);
        }
        runtime->send_collective_hammer_reduction(*it, rez);
        recorded_events.insert(recorded);
        applied_events.insert(applied);
      }
      const UniqueID op_id = op->get_unique_op_id();
      // Issue the copies
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        IndividualView *local_view = local_views[idx];
        ApEvent src_pre = local_view->find_copy_preconditions(
            true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
            op_id, index, applied_events, trace_info);
        if (src_pre.exists())
        {
          if (precondition.exists())
            src_pre =
              Runtime::merge_events(&trace_info, precondition, src_pre);
        }
        else
          src_pre = precondition;
        PhysicalManager *local_manager = local_view->get_manager();
        std::vector<CopySrcDstField> src_fields;
        local_manager->compute_copy_offsets(copy_mask, src_fields);
        const ApEvent copy_post = copy_expression->issue_copy(
            op, trace_info, dst_fields, src_fields, reservations,
#ifdef LEGION_SPY
            local_manager->tree_id, dst_inst.tid,
#endif
            src_pre, predicate_guard);
        if (copy_post.exists())
        {
          done_events.push_back(copy_post);
          const RtEvent collect_event = trace_info.get_collect_event();
          local_view->add_copy_user(true/*reading*/, 0/*redop*/, copy_post,
              collect_event, copy_mask, copy_expression, op_id, index,
              recorded_events, trace_info.recording, runtime->address_space);
        }
        if (trace_info.recording)
        {
          const UniqueInst src_inst(local_view);
          trace_info.record_copy_insts(copy_post, copy_expression, src_inst,
                      dst_inst, copy_mask, dst_mask, redop, applied_events);
        }
      }
      // Merge the done events together
      if (done_events.empty())
        return ApEvent::NO_AP_EVENT;
      return Runtime::merge_events(&trace_info, done_events);
    }

    //--------------------------------------------------------------------------
    /*static*/ void AllreduceView::handle_hammer_reduction(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID view_did;
      derez.deserialize(view_did);
      RtEvent view_ready;
      AllreduceView *view = static_cast<AllreduceView*>(
          runtime->find_or_request_logical_view(view_did, view_ready));
      size_t num_fields;
      derez.deserialize(num_fields);
      std::vector<CopySrcDstField> dst_fields(num_fields);
      std::set<RtEvent> recorded_events, ready_events, applied_events;
      unpack_fields(dst_fields, derez, ready_events, view, view_ready, runtime);
      size_t num_reservations;
      derez.deserialize(num_reservations);
      std::vector<Reservation> reservations(num_reservations);
      for (unsigned idx = 0; idx < num_reservations; idx++)
        derez.deserialize(reservations[idx]);
      ApEvent precondition;
      derez.deserialize(precondition);
      PredEvent predicate_guard;
      derez.deserialize(predicate_guard);
      IndexSpaceExpression *copy_expression =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      Operation *op =
        RemoteOp::unpack_remote_operation(derez, runtime, ready_events);
      unsigned index;
      derez.deserialize(index);
      FieldMask copy_mask, dst_mask;
      derez.deserialize(copy_mask);
      derez.deserialize(dst_mask);
      UniqueInst dst_inst;
      dst_inst.deserialize(derez);
      PhysicalTraceInfo trace_info =
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);
      RtUserEvent recorded, applied;
      derez.deserialize(recorded);
      derez.deserialize(applied);
      ApUserEvent ready;
      if (trace_info.recording)
      {
        ApBarrier bar;
        derez.deserialize(bar);
        ShardID sid;
        derez.deserialize(sid);
        ready = Runtime::create_ap_user_event(&trace_info);
        Runtime::phase_barrier_arrive(bar, 1/*count*/, ready);
        trace_info.record_barrier_arrival(bar, ready, 1/*count*/,
                                          applied_events, sid);
      }
      else
        derez.deserialize(ready);
      AddressSpaceID origin;
      derez.deserialize(origin);

      if (view_ready.exists() && !view_ready.has_triggered())
        ready_events.insert(view_ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      const ApEvent result = view->perform_hammer_reduction(
          dst_fields, reservations, precondition, predicate_guard,
          copy_expression, op, index, copy_mask, dst_mask, dst_inst,
          trace_info, recorded_events, applied_events, origin);

      Runtime::trigger_event(&trace_info, ready, result);
      if (!recorded_events.empty())
        Runtime::trigger_event(recorded,Runtime::merge_events(recorded_events));
      else
        Runtime::trigger_event(recorded);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
      delete op;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::perform_collective_allreduce(ApEvent precondition,
                                          PredEvent predicate_guard,
                                          IndexSpaceExpression *copy_expression,
                                          Operation *op, const unsigned index,
                                          const FieldMask &copy_mask,
                                          const PhysicalTraceInfo &trace_info,
                         const std::vector<CollectiveAnalysis*> *local_analyses,
                                          std::set<RtEvent> &recorded_events,
                                          std::set<RtEvent> &applied_events,
                                          const uint64_t allreduce_tag)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(redop > 0);
      assert(op != NULL);
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif
      // We're guaranteed to get one call to this function for each space
      // in the collective mapping from perform_collective_pointwise, so
      // we've already distributed control
      // Our job in this function is to build a butterfly all-reduce network
      // for exchanging the reduction data so each reduction instance in this
      // collective instance contains all the same data
      // There is a major complicating factor here because we can't do a 
      // natural in-place all-reduce across our instances since the finish
      // event for Realm copies only says when the whole copy is done and not
      // when the copy has finished reading out from the source instance.
      // Furthermore, we can't control when the reductions into the destination
      // instances start happening as they precondition just governs the start
      // of the whole copy. Therefore, we need to fake an in-place all-reduce.
      // We fake things in one of two ways:
      // Case 1: If we know that each node has at least two instances, then 
      //         we can use one instance as the source for outgoing reduction
      //         copies and the other as the destination for incoming
      //         reduction copies and ping pong between them.
      // Case 2: If we don't have at least two instances on each node then
      //         we will pair up nodes and have them do the same trick as in
      //         case 1 but using the two instances on adjacent nodes as the
      //         sources and destinations.
      // We handle unusual numbers of nodes that are not a power of the 
      // collective radix in the normal way by picking a number of participants
      // that is the largest power of the radix still less than or equal to
      // the number of nodes and using an extra stage to fold-in the 
      // non-participants values before doing the butterfly.

      // See if we've got to do the multi-node all-reduce
      if (collective_mapping->size() > 1)
      {
        if (is_multi_instance())
          // Case 1: each node has multiple instances
          perform_multi_allreduce(allreduce_tag, op, index, precondition, 
              predicate_guard, copy_expression, copy_mask, trace_info, 
              local_analyses, recorded_events, applied_events);
        else
          // Case 2: there are some nodes that only have one instance
          // Pair up nodes to have them cooperate to have two buffers
          // that we can ping-pong between to do the all-reduce "inplace"
          perform_single_allreduce(allreduce_tag, op, index, precondition,
              predicate_guard, copy_expression, copy_mask, trace_info, 
              local_analyses, recorded_events, applied_events);
      }
      else
      {
        // Everything is local so this is easy
        std::vector<std::vector<CopySrcDstField> > 
          local_fields(local_views.size());
        std::vector<std::vector<Reservation> > reservations(local_views.size());
        std::vector<ApEvent> instance_events(local_views.size());
        initialize_allreduce_with_reductions(precondition, predicate_guard,
            op, index, copy_expression, copy_mask, trace_info, local_analyses,
            applied_events, instance_events, local_fields, reservations);
        complete_initialize_allreduce_with_reductions(op, index,
            copy_expression, copy_mask, trace_info, recorded_events,
            applied_events, instance_events, local_fields);
        finalize_allreduce_with_broadcasts(predicate_guard, op, index,
            copy_expression, copy_mask, trace_info, local_analyses,
            recorded_events, applied_events, instance_events, local_fields);
        complete_finalize_allreduce_with_broadcasts(op, index, copy_expression,
            copy_mask, trace_info, recorded_events, instance_events);
      }
    }

    //--------------------------------------------------------------------------
    bool AllreduceView::is_multi_instance(void)
    //--------------------------------------------------------------------------
    {
      if (evaluated_multi_instance.load())
        return multi_instance.load();
      bool result = false;
      // Must have at least twice as many collective instances as nodes
      // in order for this to qualify as multi instance
      if (instances.size() >= (2*collective_mapping->size()))
      {
        // Check that there is at least two instances on every node
        std::vector<unsigned> counts(collective_mapping->size(), 0);
        for (std::vector<DistributedID>::const_iterator it =
              instances.begin(); it != instances.end(); it++)
        {
          const AddressSpaceID owner = runtime->determine_owner(*it);
#ifdef DEBUG_LEGION
          assert(collective_mapping->contains(owner));
#endif
          const unsigned index = collective_mapping->find_index(owner);
          counts[index]++;
        }
        result = true;
        for (unsigned idx = 0; idx < counts.size(); idx++)
        {
          if (counts[idx] > 1)
            continue;
          result = false;
          break;
        }
      }
      multi_instance.store(result);
      evaluated_multi_instance.store(true);
      return result;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::perform_single_allreduce(
                         const uint64_t allreduce_tag,
                         Operation *op, unsigned index,
                         ApEvent precondition, PredEvent predicate_guard,
                         IndexSpaceExpression *copy_expression,
                         const FieldMask &copy_mask,
                         const PhysicalTraceInfo &trace_info,
                         const std::vector<CollectiveAnalysis*> *local_analyses,
                         std::set<RtEvent> &recorded_events,
                         std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!multi_instance);
#endif
      // Case 2: there are some nodes that only have one instance
      // Pair up nodes to have them cooperate to have two buffers
      // that we can ping-pong between to do the all-reduce "inplace"
      const int participants = collective_mapping->size() / 2; // truncate
      const int local_index = collective_mapping->find_index(local_space);
      const int local_rank = local_index / 2;
      const int local_offset = local_index % 2;
      int collective_radix = runtime->legion_collective_radix;
      int collective_log_radix, collective_stages;
      int participating_ranks, collective_last_radix;
      const bool participating = configure_collective_settings(
          participants, local_rank, collective_radix, collective_log_radix,
          collective_stages, participating_ranks, collective_last_radix);
      std::vector<std::vector<CopySrcDstField> > 
        local_fields(local_views.size());
      std::vector<std::vector<Reservation> > reservations(local_views.size());
      std::vector<ApEvent> instance_events(local_views.size());
      if (participating)
      {
        // Check to see if we need to handle stage -1 from non-participants
        // As well as from offset=1 down to offset=0
        if (local_offset == 0)
        {
          const ApEvent reduce_pre = initialize_allreduce_with_reductions(
              precondition, predicate_guard, op, index, copy_expression,
              copy_mask, trace_info, local_analyses, applied_events,
              instance_events, local_fields, reservations);
          // We definitely will be expecting our partner
          std::vector<int> expected_ranks(1, local_rank);
          // We could be expecting up to two non-participants
          // User their index instead of rank to avoid key collision
          const int nonpart_index = local_index + 2*participating_ranks;
          for (int offset = 0; offset < 2; offset++)
          {
            const int rank = nonpart_index + offset;
            if (rank >= int(collective_mapping->size()))
              break;
            expected_ranks.push_back(rank);
          }
          std::vector<ApEvent> reduce_events;
          const UniqueInst dst_inst(local_views.front());
          receive_allreduce_stage(dst_inst, allreduce_tag, -1/*stage*/, op,
            reduce_pre, predicate_guard, copy_expression,
            copy_mask, trace_info, applied_events, local_fields[0], 
            reservations[0], &expected_ranks.front(),
            expected_ranks.size(), reduce_events);
          complete_initialize_allreduce_with_reductions(op, index, 
              copy_expression, copy_mask, trace_info, recorded_events,
              applied_events, instance_events, local_fields, &reduce_events);
        }
        else
        {
          // local_offset == 1
          initialize_allreduce_without_reductions(precondition, 
              predicate_guard, op, index, copy_expression, copy_mask, 
              trace_info, local_analyses, recorded_events, applied_events,
              instance_events, local_fields, reservations);
          // Just need to send the reduction down to our partner
          const AddressSpaceID target = (*collective_mapping)[local_index-1];
          std::vector<ApEvent> read_events;
          send_allreduce_stage(allreduce_tag, -1/*stage*/, local_rank,
              instance_events[0], predicate_guard, copy_expression,
              trace_info, local_fields[0], 0/*src index*/,
              &target, 1/*target count*/, read_events);
          if (!read_events.empty())
          {
#ifdef DEBUG_LEGION
            assert(read_events.size() == 1);
#endif
            instance_events[0] = read_events[0];
          }
        }
        // Do the stages
        for (int stage = 0; stage < collective_stages; stage++)
        {
          // Figure out the participating ranks
          std::vector<int> stage_ranks;
          if (stage < (collective_stages-1))
          {
            // Normal radix
            stage_ranks.reserve(collective_radix);
            for (int r = 1; r < collective_radix; r++)
            {
              int target = local_rank ^
                (r << (stage * collective_log_radix));
              stage_ranks.push_back(target);
            }
          }
          else
          {
            // Last stage so special radix
            stage_ranks.reserve(collective_last_radix);
            for (int r = 1; r < collective_last_radix; r++)
            {
              int target = local_rank ^
                (r << (stage * collective_log_radix));
              stage_ranks.push_back(target);
            }
          }
#ifdef DEBUG_LEGION
          assert(!stage_ranks.empty());
#endif
          // Always include ourselves in the ranks as well
          stage_ranks.push_back(local_rank);
          // Check to see if we're sending or receiving this stage
          if ((stage % 2) == local_offset)
          {
            // We're doing a sending stage
            std::vector<AddressSpaceID> targets(stage_ranks.size());
            for (unsigned idx = 0; idx < stage_ranks.size(); idx++)
            {
              // If we're even, send to the odd
              // If we're odd, send to the even
              const unsigned index =
                2 * stage_ranks[idx] + ((local_offset == 0) ? 1 : 0);
#ifdef DEBUG_LEGION
              assert(index < collective_mapping->size());
#endif
              targets[idx] = (*collective_mapping)[index];
            }
            std::vector<ApEvent> read_events;
            send_allreduce_stage(allreduce_tag, stage, local_rank,
                instance_events[0], predicate_guard, copy_expression,
                trace_info, local_fields[0], 0/*src index*/,
                &targets.front(), targets.size(), read_events);
            if (!read_events.empty())
              instance_events[0] =
                Runtime::merge_events(&trace_info, read_events);
          }
          else
          {
            // We're doing a receiving stage
            // First issue a fill to initialize the instance
            // Realm should ignore the redop data on these fields
            instance_events[0] = copy_expression->issue_fill(
                op, trace_info, local_fields[0], reduction_op->identity,
                reduction_op->sizeof_rhs,
#ifdef LEGION_SPY
                fill_view->fill_op_uid, 
                local_views[0]->manager->field_space_node->handle,
                local_views[0]->manager->tree_id,
#endif
                instance_events[0], predicate_guard);
            if (trace_info.recording)
            {
              const UniqueInst dst_inst(local_views[0]);
              trace_info.record_fill_inst(instance_events[0],
                  copy_expression, dst_inst, copy_mask,
                  applied_events, (redop > 0));
            }
            // Then check to see if we've received any reductions
            std::vector<ApEvent> reduce_events;
            set_redop(local_fields[0]);
            const UniqueInst dst_inst(local_views.front());
            receive_allreduce_stage(dst_inst, allreduce_tag, stage, op,
                instance_events[0], predicate_guard, copy_expression,
                copy_mask, trace_info, applied_events, local_fields[0],
                reservations[0], &stage_ranks.front(),
                stage_ranks.size(), reduce_events);
            clear_redop(local_fields[0]);
            if (!reduce_events.empty())
              instance_events[0] =
                Runtime::merge_events(&trace_info, reduce_events);
          }
        }
        // If we have to do stage -1 then we can do that now
        // Check to see if we have the valid data or not
        if ((collective_stages % 2) == local_offset)
        {
          const ApEvent broadcast_pre = finalize_allreduce_with_broadcasts(
              predicate_guard, op, index, copy_expression,
              copy_mask, trace_info, local_analyses, recorded_events, 
              applied_events, instance_events, local_fields);
          // We have the valid data, send it to up to two 
          // non-participants as well as our partner
          // If we're odd then make us even and vice-versa
          int partner_index = local_index + ((local_offset == 0) ? 1 : -1);
          const AddressSpaceID partner = (*collective_mapping)[partner_index];
          std::vector<AddressSpaceID> targets(1, partner);
          // Check for the two non-participants
          const unsigned offset = 2*participating_ranks;
          const unsigned one = offset + local_index;
          if (one < collective_mapping->size())
            targets.push_back((*collective_mapping)[one]);
          const unsigned two = offset + partner_index;
          if (two < collective_mapping->size())
            targets.push_back((*collective_mapping)[two]);
          std::vector<ApEvent> read_events;
          send_allreduce_stage(allreduce_tag, -2/*stage*/, local_rank,
              broadcast_pre, predicate_guard, copy_expression,
              trace_info, local_fields[0], 0/*src index*/,
              &targets.front(), targets.size(), read_events);
          complete_finalize_allreduce_with_broadcasts(op, index,
              copy_expression, copy_mask, trace_info, recorded_events, 
              instance_events, &read_events);
        }
        else
        {
          // Not reducing here, just standard copy
          // See if we received the copy from our partner
          std::vector<ApEvent> reduce_events;
          // No reservations since this is a straight copy
          const std::vector<Reservation> no_reservations;
          const UniqueInst dst_inst(local_views.front());
          receive_allreduce_stage(dst_inst, allreduce_tag, -2/*stage*/, op,
              instance_events[0], predicate_guard, copy_expression,
              copy_mask, trace_info, applied_events, local_fields[0],
              no_reservations, &local_rank, 1/*total ranks*/, reduce_events);
          if (!reduce_events.empty())
          {
#ifdef DEBUG_LEGION
            assert(reduce_events.size() == 1);
#endif
            instance_events[0] = reduce_events[0];
          }
          finalize_allreduce_without_broadcasts(predicate_guard, op, index,
              copy_expression, copy_mask, trace_info, local_analyses,
              recorded_events, applied_events, instance_events, local_fields);
        }
      }
      else
      {
        // Not a participant in the stages, so just need to do
        // the stage -1 send and receive
        initialize_allreduce_without_reductions(precondition, 
            predicate_guard, op, index, copy_expression, copy_mask, 
            trace_info, local_analyses, recorded_events, applied_events,
            instance_events, local_fields, reservations);
        // Truncate down
        const int target_rank = (local_index - 2*participating_ranks) / 2;
#ifdef DEBUG_LEGION
        assert(target_rank >= 0);
#endif
        // Then convert back to the appropriate index
        const int target_index = 2 * target_rank;
#ifdef DEBUG_LEGION
        assert(target_index < int(collective_mapping->size()));
#endif
        const AddressSpaceID target = (*collective_mapping)[target_index];
        std::vector<ApEvent> read_events;
        // Intentionally use the local_index here to avoid key collisions
        send_allreduce_stage(allreduce_tag, -1/*stage*/, local_index,
            instance_events[0], predicate_guard, copy_expression,
            trace_info, local_fields[0], 0/*src index*/,
            &target, 1/*total targets*/, read_events);
        if (!read_events.empty())
        {
#ifdef DEBUG_LEGION
          assert(read_events.size() == 1);
#endif
          instance_events[0] = read_events[0];
        }
        // Check to see if we received the copy back yet
        // Keep the redop data zeroed out since we're doing normal copies
        // No reservations since this is a straight copy
        const std::vector<Reservation> no_reservations;
        std::vector<ApEvent> reduce_events;
        const UniqueInst dst_inst(local_views.front());
        receive_allreduce_stage(dst_inst, allreduce_tag, -2/*stage*/, op,
            instance_events[0], predicate_guard, copy_expression,
            copy_mask, trace_info, applied_events, local_fields[0],
            no_reservations, &target_rank, 1/*total ranks*/, reduce_events);
        if (!reduce_events.empty())
        {
#ifdef DEBUG_LEGION
          assert(reduce_events.size() == 1);
#endif
          instance_events[0] = reduce_events[0];
        }
        finalize_allreduce_without_broadcasts(predicate_guard, op, index,
            copy_expression, copy_mask, trace_info, local_analyses,
            recorded_events, applied_events, instance_events, local_fields);
      }
    }

    //--------------------------------------------------------------------------
    void AllreduceView::perform_multi_allreduce(
                         const uint64_t allreduce_tag,
                         Operation *op, unsigned index,
                         ApEvent precondition,
                         PredEvent predicate_guard,
                         IndexSpaceExpression *copy_expression,
                         const FieldMask &copy_mask,
                         const PhysicalTraceInfo &trace_info,
                         const std::vector<CollectiveAnalysis*> *local_analyses,
                         std::set<RtEvent> &recorded_events,
                         std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // Case 1: each node has multiple instances
      assert(redop > 0);
      assert(multi_instance);
      assert(instances.size() > 1);
#endif
      const int participants = collective_mapping->size();
      const int local_rank = collective_mapping->find_index(local_space);
      int collective_radix = runtime->legion_collective_radix;
      int collective_log_radix, collective_stages;
      int participating_ranks, collective_last_radix;
      const bool participating = configure_collective_settings(
          participants, local_rank, collective_radix, collective_log_radix,
          collective_stages, participating_ranks, collective_last_radix);
      std::vector<std::vector<CopySrcDstField> > 
        local_fields(local_views.size());
      std::vector<std::vector<Reservation> > reservations(local_views.size());
      std::vector<ApEvent> instance_events(local_views.size());
      if (participating)
      {
        // Check to see if we need to wait for a remainder copy
        // for any non-participating ranks
        int remainder_rank = local_rank + participating_ranks;
        if (collective_mapping->size() <= size_t(remainder_rank))
          remainder_rank = -1;
        if (remainder_rank >= 0)
        {
          const ApEvent reduce_pre = initialize_allreduce_with_reductions(
              precondition, predicate_guard, op, index, copy_expression,
              copy_mask, trace_info, local_analyses, applied_events,
              instance_events, local_fields, reservations);
          const UniqueInst dst_inst(local_views.front());
          std::vector<ApEvent> reduce_events;
          receive_allreduce_stage(dst_inst, allreduce_tag, -1/*stage*/, op,
              reduce_pre, predicate_guard, copy_expression, copy_mask,
              trace_info, applied_events, local_fields[0], reservations[0], 
              &remainder_rank, 1/*total ranks*/, reduce_events);
          complete_initialize_allreduce_with_reductions(op, index,
              copy_expression, copy_mask, trace_info, recorded_events,
              applied_events, instance_events, local_fields, &reduce_events);
        }
        else 
          initialize_allreduce_without_reductions(precondition,
              predicate_guard, op, index, copy_expression, copy_mask,
              trace_info, local_analyses, recorded_events, applied_events,
              instance_events, local_fields, reservations);
        unsigned src_inst_index = 0;
        unsigned dst_inst_index = 1;
        // Issue the stages
        for (int stage = 0; stage < collective_stages; stage++)
        { 
          // Figure out where to send out messages first
          std::vector<int> stage_ranks;
          if (stage < (collective_stages-1))
          {
            // Normal radix
            stage_ranks.reserve(collective_radix-1);
            for (int r = 1; r < collective_radix; r++)
            {
              int target = local_rank ^
                (r << (stage * collective_log_radix));
              stage_ranks.push_back(target);
            }
          }
          else
          {
            // Last stage so special radix
            stage_ranks.reserve(collective_last_radix-1);
            for (int r = 1; r < collective_last_radix; r++)
            {
              int target = local_rank ^
                (r << (stage * collective_log_radix));
              stage_ranks.push_back(target);
            }
          }
#ifdef DEBUG_LEGION
          assert(!stage_ranks.empty());
#endif
          // Send out the messages to the dst ranks to perform copies
          std::vector<AddressSpaceID> targets(stage_ranks.size());
          for (unsigned idx = 0; idx < stage_ranks.size(); idx++)
            targets[idx] = (*collective_mapping)[stage_ranks[idx]];
          std::vector<ApEvent> src_events;
          const PhysicalTraceInfo &src_info = (local_analyses == NULL) ?
            trace_info : local_analyses->at(src_inst_index)->get_trace_info();
          send_allreduce_stage(allreduce_tag, stage, local_rank,
              instance_events[src_inst_index], predicate_guard,
              copy_expression, src_info, local_fields[src_inst_index],
              src_inst_index, &targets.front(), targets.size(), src_events);
          // Issuse the fill for the destination instance
          // Realm should ignore the redop data on these fields
          const PhysicalTraceInfo &dst_info = (local_analyses == NULL) ?
            trace_info : local_analyses->at(dst_inst_index)->get_trace_info();
          instance_events[dst_inst_index] =
            copy_expression->issue_fill(op, dst_info,
                local_fields[dst_inst_index],
                reduction_op->identity, reduction_op->sizeof_rhs,
#ifdef LEGION_SPY
                fill_view->fill_op_uid,
                local_views[dst_inst_index]->manager->field_space_node->handle,
                local_views[dst_inst_index]->manager->tree_id,
#endif
                instance_events[dst_inst_index], predicate_guard);
          if (dst_info.recording)
          {
            const UniqueInst dst_inst(local_views[dst_inst_index]);
            dst_info.record_fill_inst(instance_events[dst_inst_index],
                copy_expression, dst_inst, copy_mask, 
                applied_events, true/*reduction*/);
          }
          set_redop(local_fields[dst_inst_index]);
          // Issue the reduction from the source to the destination
          ApEvent local_precondition = Runtime::merge_events(&dst_info,
              instance_events[src_inst_index], instance_events[dst_inst_index]);
          const ApEvent local_post = copy_expression->issue_copy(op, dst_info,
              local_fields[dst_inst_index], local_fields[src_inst_index],
              reservations[dst_inst_index],
#ifdef LEGION_SPY
              local_views[src_inst_index]->manager->tree_id,
              local_views[dst_inst_index]->manager->tree_id,
#endif
              local_precondition, predicate_guard);
          std::vector<ApEvent> dst_events;
          if (local_post.exists())
          {
            src_events.push_back(local_post);
            dst_events.push_back(local_post);
          }
          if (dst_info.recording)
          {
            const UniqueInst src_inst(local_views[src_inst_index]);
            const UniqueInst dst_inst(local_views[dst_inst_index]);
            dst_info.record_copy_insts(local_post, copy_expression,
               src_inst, dst_inst, copy_mask, copy_mask, redop, applied_events);
          }
          // Update the source instance precondition
          // to reflect all the reduction copies read from it
          if (!src_events.empty())
            instance_events[src_inst_index] =
              Runtime::merge_events(&src_info, src_events);
          // Now check to see if we're received any messages
          // for this stage, and if not make place holders for them
          const UniqueInst dst_inst(local_views[dst_inst_index]);
          receive_allreduce_stage(dst_inst, allreduce_tag, stage, op,
              instance_events[dst_inst_index], predicate_guard,
              copy_expression, copy_mask, dst_info, applied_events, 
              local_fields[dst_inst_index], reservations[dst_inst_index],
              &stage_ranks.front(), stage_ranks.size(), dst_events);
          clear_redop(local_fields[dst_inst_index]);
          if (!dst_events.empty())
            instance_events[dst_inst_index] =
              Runtime::merge_events(&dst_info, dst_events);
          // Update the src and dst instances for the next stage
          if (++src_inst_index == instances.size())
            src_inst_index = 0;
          if (++dst_inst_index == instances.size())
            dst_inst_index = 0;
        }
        // Send out the result to any non-participating ranks
        if (remainder_rank >= 0)
        {
          const ApEvent broadcast_pre = finalize_allreduce_with_broadcasts(
              predicate_guard, op, index, copy_expression, copy_mask,
              trace_info, local_analyses, recorded_events, applied_events,
              instance_events, local_fields, src_inst_index);
          std::vector<ApEvent> broadcast_events;
          const AddressSpaceID target = (*collective_mapping)[remainder_rank];
          send_allreduce_stage(allreduce_tag, -1/*stage*/, local_rank,
              broadcast_pre, predicate_guard, copy_expression, trace_info,
              local_fields[src_inst_index], src_inst_index,
              &target, 1/*total targets*/, broadcast_events);
          complete_finalize_allreduce_with_broadcasts(op, index, 
              copy_expression, copy_mask, trace_info, recorded_events,
              instance_events, &broadcast_events, src_inst_index);
        }
        else
          finalize_allreduce_without_broadcasts(predicate_guard, op, index,
              copy_expression, copy_mask, trace_info, local_analyses,
              recorded_events, applied_events, instance_events,
              local_fields, src_inst_index);
      }
      else
      {
        // Not a participant in the stages so just need to 
        // do the stage -1 send and receive
#ifdef DEBUG_LEGION
        assert(local_rank >= participating_ranks);
#endif
        initialize_allreduce_without_reductions(precondition, 
            predicate_guard, op, index, copy_expression, copy_mask,
            trace_info, local_analyses, recorded_events, applied_events,
            instance_events, local_fields, reservations);
        const int mirror_rank = local_rank - participating_ranks;
        const AddressSpaceID target = (*collective_mapping)[mirror_rank];
        std::vector<ApEvent> read_events;
        send_allreduce_stage(allreduce_tag, -1/*stage*/, local_rank,
            instance_events[0], predicate_guard, copy_expression,
            trace_info, local_fields[0], 0/*src index*/,
            &target, 1/*total targets*/, read_events);
        if (!read_events.empty())
        {
#ifdef DEBUG_LEGION
          assert(read_events.size() == 1);
#endif
          instance_events[0] = read_events[0];
        }
        // We can put this back in the first buffer without any
        // anti-dependences because we know the computation of the
        // result coming back had to already depend on the copy we
        // sent out to the target
        // Keep the local fields redop cleared since we're going to 
        // doing direct copies here into these instance and not reductions
        std::vector<ApEvent> reduce_events;
        const std::vector<Reservation> no_reservations;
        const UniqueInst dst_inst(local_views.front());
        receive_allreduce_stage(dst_inst, allreduce_tag, -1/*stage*/, op,
            instance_events[0], predicate_guard, copy_expression,
            copy_mask, trace_info, applied_events, local_fields[0],
            no_reservations, &mirror_rank, 1/*total ranks*/, reduce_events);
        if (!reduce_events.empty())
        {
#ifdef DEBUG_LEGION
          assert(reduce_events.size() == 1);
#endif
          instance_events[0] = reduce_events[0];
        }
        finalize_allreduce_without_broadcasts(predicate_guard, op, index,
            copy_expression, copy_mask, trace_info, local_analyses,
            recorded_events, applied_events, instance_events, local_fields);
      }
    }

    //--------------------------------------------------------------------------
    ApEvent AllreduceView::initialize_allreduce_with_reductions(
                                ApEvent precondition, PredEvent predicate_guard,
                                Operation *op, unsigned index,
                                IndexSpaceExpression *copy_expression,
                                const FieldMask &copy_mask,
                                const PhysicalTraceInfo &trace_info,
                    const std::vector<CollectiveAnalysis*> *local_analyses,
                                std::set<RtEvent> &applied_events,
                                std::vector<ApEvent> &instance_events,
                    std::vector<std::vector<CopySrcDstField> > &local_fields,
                    std::vector<std::vector<Reservation> > &reservations)
    //--------------------------------------------------------------------------
    {
      const UniqueID op_id = op->get_unique_op_id(); 
      IndividualView *local_view = local_views.front();
      // Compute the reduction precondition for the first instance
      ApEvent reduce_pre = local_view->find_copy_preconditions(
          false/*reading*/, redop, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      if (precondition.exists())
      {
        if (reduce_pre.exists())
          reduce_pre =
            Runtime::merge_events(&trace_info, reduce_pre, precondition);
        else
          reduce_pre = precondition;
      }
      local_view->find_field_reservations(copy_mask, reservations.front());
      PhysicalManager *local_manager = local_view->get_manager();
      local_manager->compute_copy_offsets(copy_mask, local_fields.front());
      // Perform any local reductions and record their events
      set_redop(local_fields[0]);
      if (local_views.size() > 1)
      {
        const UniqueInst dst_inst(local_view);
        for (unsigned idx = 1; idx < local_views.size(); idx++)
        {
          const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
            trace_info : local_analyses->at(idx)->get_trace_info();
          // Find the reservations for the other instances for later
          IndividualView *src_view = local_views[idx];
          // Technically we're reading here, but we're going to be "writing" the
          // allreduce result so we pretend like we're writing
          ApEvent copy_pre = src_view->find_copy_preconditions(
              false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
              op_id, index, applied_events, inst_info);
          if (reduce_pre.exists())
          {
            if (copy_pre.exists())
              copy_pre = Runtime::merge_events(&inst_info, copy_pre,reduce_pre);
            else
              copy_pre = reduce_pre;
          }
          src_view->find_field_reservations(copy_mask, reservations[idx]);
          PhysicalManager *src_manager = local_views[idx]->get_manager();
          src_manager->compute_copy_offsets(copy_mask, local_fields[idx]);
          const ApEvent reduced = copy_expression->issue_copy(op, inst_info,
              local_fields.front(), local_fields[idx], reservations.front(),
#ifdef LEGION_SPY
              src_view->manager->tree_id, local_view->manager->tree_id,
#endif
              copy_pre, predicate_guard);
          // No need to record the output with the view
          // We'll do that at the end of the full all-reduce
          if (reduced.exists())
            instance_events[idx] = reduced;
          if (inst_info.recording)
          {
            const UniqueInst src_inst(src_view);
            inst_info.record_copy_insts(reduced, copy_expression,
               src_inst, dst_inst, copy_mask, copy_mask, redop, applied_events);
          }
        }
      }
      return reduce_pre;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::complete_initialize_allreduce_with_reductions(
                                   Operation *op, unsigned index,
                                   IndexSpaceExpression *copy_expression,
                                   const FieldMask &copy_mask,
                                   const PhysicalTraceInfo &trace_info,
                                   std::set<RtEvent> &recorded_events,
                                   std::set<RtEvent> &applied_events,
                                   std::vector<ApEvent> &instance_events,
                       std::vector<std::vector<CopySrcDstField> > &local_fields,
                                   std::vector<ApEvent> *reduced)
    //--------------------------------------------------------------------------
    {
      ApEvent reduce_post;
      if (reduced != NULL)
      {
        for (unsigned idx = 1; idx < instance_events.size(); idx++)
          if (instance_events[idx].exists())
            reduced->push_back(instance_events[idx]);
        reduce_post = Runtime::merge_events(&trace_info, *reduced);
      }
      else
        reduce_post = Runtime::merge_events(&trace_info, instance_events);
      const UniqueID op_id = op->get_unique_op_id();
      if (reduce_post.exists())
        local_views[0]->add_copy_user(false/*reading*/, redop, reduce_post,
            trace_info.get_collect_event(), copy_mask, copy_expression,
            op_id, index, recorded_events, trace_info.recording,
            runtime->address_space);
      instance_events[0] = local_views[0]->find_copy_preconditions(
          false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      clear_redop(local_fields[0]);
    }

    //--------------------------------------------------------------------------
    void AllreduceView::initialize_allreduce_without_reductions(
                                ApEvent precondition, PredEvent predicate_guard,
                                Operation *op, unsigned index,
                                IndexSpaceExpression *copy_expression,
                                const FieldMask &copy_mask,
                                const PhysicalTraceInfo &trace_info,
                    const std::vector<CollectiveAnalysis*> *local_analyses,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                std::vector<ApEvent> &instance_events,
                    std::vector<std::vector<CopySrcDstField> > &local_fields,
                    std::vector<std::vector<Reservation> > &reservations)
    //--------------------------------------------------------------------------
    {
      if (local_views.size() == 1)
      {
        const UniqueID op_id = op->get_unique_op_id(); 
        IndividualView *local_view = local_views.front();
        instance_events[0] = local_view->find_copy_preconditions(
          false/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
        local_view->find_field_reservations(copy_mask, reservations.front());
        PhysicalManager *local_manager = local_view->get_manager();
        local_manager->compute_copy_offsets(copy_mask, local_fields.front());
      }
      else
      {
        initialize_allreduce_with_reductions(precondition, predicate_guard, 
            op, index, copy_expression, copy_mask, trace_info, local_analyses,
            applied_events, instance_events, local_fields, reservations);
        complete_initialize_allreduce_with_reductions(op, index,
            copy_expression, copy_mask, trace_info, recorded_events, 
            applied_events, instance_events, local_fields);
      }
    }

    //--------------------------------------------------------------------------
    ApEvent AllreduceView::finalize_allreduce_with_broadcasts(
                                PredEvent predicate_guard,
                                Operation *op, unsigned index,
                                IndexSpaceExpression *copy_expression,
                                const FieldMask &copy_mask,
                                const PhysicalTraceInfo &trace_info,
                    const std::vector<CollectiveAnalysis*> *local_analyses,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                std::vector<ApEvent> &instance_events,
              const std::vector<std::vector<CopySrcDstField> > &local_fields,
                                const unsigned final_index)
    //--------------------------------------------------------------------------
    {
      const UniqueID op_id = op->get_unique_op_id(); 
      IndividualView *local_view = local_views[final_index];
      if (instance_events[final_index].exists())
      {
        local_view->add_copy_user(false/*reading*/, 0/*redop*/,
            instance_events[final_index], trace_info.get_collect_event(),
            copy_mask, copy_expression, op_id, index, recorded_events,
            trace_info.recording, runtime->address_space);
        instance_events[final_index] = ApEvent::NO_AP_EVENT;
      }
      const ApEvent broadcast_pre = local_view->find_copy_preconditions(
          true/*reading*/, 0/*redop*/, copy_mask, copy_expression,
          op_id, index, applied_events, trace_info);
      if (local_views.size() > 1)
      {
        const UniqueInst src_inst(local_view);
        const std::vector<Reservation> no_reservations;
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          if (idx == final_index)
            continue;
          const PhysicalTraceInfo &inst_info = (local_analyses == NULL) ?
            trace_info : local_analyses->at(idx)->get_trace_info();
          // Find the reservations for the other instances for later
          IndividualView *dst_view = local_views[idx];
          // Technically we're reading here, but we're going to be "writing" the
          // allreduce result so we pretend like we're writing
          ApEvent copy_pre = instance_events[idx]; 
          if (broadcast_pre.exists())
          {
            if (broadcast_pre.exists())
              copy_pre = 
                Runtime::merge_events(&inst_info, copy_pre, broadcast_pre);
            else
              copy_pre = broadcast_pre;
          }
          const ApEvent copy_post = copy_expression->issue_copy(op, inst_info,
              local_fields.front(), local_fields[idx], no_reservations,
#ifdef LEGION_SPY
              local_view->manager->tree_id, dst_view->manager->tree_id,
#endif
              copy_pre, predicate_guard);
          // No need to record the output with the view
          // We'll do that at the end of the full all-reduce
          if (copy_post.exists())
          {
            dst_view->add_copy_user(false/*reading*/, 0/*redop*/, copy_post,
                inst_info.get_collect_event(), copy_mask, copy_expression,
                op_id, index, recorded_events, inst_info.recording,
                runtime->address_space);
            instance_events[idx] = copy_post;
          }
          if (inst_info.recording)
          {
            const UniqueInst dst_inst(dst_view);
            inst_info.record_copy_insts(copy_post, copy_expression,
               src_inst, dst_inst, copy_mask, copy_mask, redop, applied_events);
          }
        }
      }
      return broadcast_pre;
    }

    //--------------------------------------------------------------------------
    void AllreduceView::complete_finalize_allreduce_with_broadcasts(
                                    Operation *op, unsigned index,
                                    IndexSpaceExpression *copy_expression,
                                    const FieldMask &copy_mask,
                                    const PhysicalTraceInfo &trace_info,
                                    std::set<RtEvent> &recorded_events,
                                    const std::vector<ApEvent> &instance_events,
                                    std::vector<ApEvent> *broadcast,
                                    const unsigned final_index)
    //--------------------------------------------------------------------------
    {
      ApEvent broadcast_post;
      if (broadcast != NULL)
      {
        for (unsigned idx = 0; idx < instance_events.size(); idx++)
          if ((idx != final_index) && instance_events[idx].exists())
            broadcast->push_back(instance_events[idx]);
        broadcast_post = Runtime::merge_events(&trace_info, *broadcast);
      }
      else
        broadcast_post = Runtime::merge_events(&trace_info, instance_events);
      const UniqueID op_id = op->get_unique_op_id();
      if (broadcast_post.exists())
        local_views[final_index]->add_copy_user(false/*reading*/, 0/*redop*/, 
            broadcast_post, trace_info.get_collect_event(), copy_mask,
            copy_expression, op_id, index, recorded_events,
            trace_info.recording, runtime->address_space);
    }

    //--------------------------------------------------------------------------
    void AllreduceView::finalize_allreduce_without_broadcasts(
                                PredEvent predicate_guard,
                                Operation *op, unsigned index,
                                IndexSpaceExpression *copy_expression,
                                const FieldMask &copy_mask,
                                const PhysicalTraceInfo &trace_info,
                    const std::vector<CollectiveAnalysis*> *local_analyses,
                                std::set<RtEvent> &recorded_events,
                                std::set<RtEvent> &applied_events,
                                std::vector<ApEvent> &instance_events,
              const std::vector<std::vector<CopySrcDstField> > &local_fields,
                                const unsigned final_index)
    //--------------------------------------------------------------------------
    {
      if (local_views.size() == 1)
      {
        if (instance_events[final_index].exists())
        {
          const UniqueID op_id = op->get_unique_op_id(); 
          IndividualView *local_view = local_views[final_index];
          local_view->add_copy_user(false/*reading*/, 0/*redop*/,
              instance_events[final_index], trace_info.get_collect_event(),
              copy_mask, copy_expression, op_id, index, recorded_events,
              trace_info.recording, runtime->address_space);
        }
      }
      else
      {
        finalize_allreduce_with_broadcasts(predicate_guard, op, index,
            copy_expression, copy_mask, trace_info, local_analyses, 
            recorded_events, applied_events, instance_events, 
            local_fields, final_index);
        complete_finalize_allreduce_with_broadcasts(op, index,
            copy_expression, copy_mask, trace_info, recorded_events,
            instance_events, NULL/*broadcast events*/, final_index);
      }
    }

    //--------------------------------------------------------------------------
    void AllreduceView::send_allreduce_stage(const uint64_t allreduce_tag,
                                 const int stage, const int local_rank,
                                 ApEvent precondition,PredEvent predicate_guard,
                                 IndexSpaceExpression *copy_expression, 
                                 const PhysicalTraceInfo &trace_info,
                                 const std::vector<CopySrcDstField> &src_fields,
                                 const unsigned src_index,
                                 const AddressSpaceID *targets, size_t total,
                                 std::vector<ApEvent> &src_events)
    //--------------------------------------------------------------------------
    {
      ApBarrier src_bar;
      ShardID src_bar_shard = 0;
      const UniqueInst src_inst(local_views[src_index]);
      for (unsigned t = 0; t < total; t++)
      {
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(allreduce_tag);
          rez.serialize(local_rank);
          rez.serialize(stage);
          pack_fields(rez, src_fields);
          src_inst.serialize(rez);
          rez.serialize(precondition);
          rez.serialize<bool>(trace_info.recording);
          if (trace_info.recording)
          {
            if (!src_bar.exists())
            {
              src_bar = 
                ApBarrier(Realm::Barrier::create_barrier(total));
              src_bar_shard =
                trace_info.record_managed_barrier(src_bar, total);
              src_events.push_back(src_bar);
            }
            rez.serialize(src_bar);
            rez.serialize(src_bar_shard);
          }
          else
          {
            const ApUserEvent src_done =
              Runtime::create_ap_user_event(&trace_info);
            rez.serialize(src_done);
            src_events.push_back(src_done);
          }
        }
        runtime->send_collective_distribute_allreduce(targets[t], rez);
      }
    }

    //--------------------------------------------------------------------------
    void AllreduceView::receive_allreduce_stage(const UniqueInst dst_inst,
                            const uint64_t allreduce_tag, 
                            const int stage, Operation *op,
                            ApEvent dst_precondition, PredEvent predicate_guard,
                            IndexSpaceExpression *copy_expression,
                            const FieldMask &copy_mask,
                            const PhysicalTraceInfo &trace_info,
                            std::set<RtEvent> &applied_events,
                            const std::vector<CopySrcDstField> &dst_fields,
                            const std::vector<Reservation> &reservations,
                            const int *expected_ranks, size_t total_ranks,
                            std::vector<ApEvent> &dst_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert((stage != -2) || (total_ranks == 1));
#endif
      std::vector<AllReduceCopy> to_perform;
      {
        unsigned remaining = 0;
        AutoLock v_lock(view_lock);
        for (unsigned r = 0; r < total_ranks; r++)
        {
          const CopyKey key(allreduce_tag, expected_ranks[r], stage);
          std::map<CopyKey,AllReduceCopy>::iterator finder =
            all_reduce_copies.find(key);
          if (finder != all_reduce_copies.end())
          {
            to_perform.emplace_back(std::move(finder->second));
            all_reduce_copies.erase(finder);
          }
          else
            remaining++;
        }
        if (remaining > 0)
        {
          // If we still have outstanding copies, save a data
          // structure for them for when they arrive
          const std::pair<uint64_t,int> key(allreduce_tag, stage);
#ifdef DEBUG_LEGION
          assert(remaining_stages.find(key) == remaining_stages.end());
#endif
          AllReduceStage &pending = remaining_stages[key];
          pending.dst_inst = dst_inst;
          pending.op = op;
          pending.copy_expression = copy_expression;
          copy_expression->add_nested_expression_reference(
              this->did, applied_events);
          pending.copy_mask = copy_mask;
          pending.dst_fields = dst_fields;
          pending.reservations = reservations;
          pending.trace_info = new PhysicalTraceInfo(trace_info);
          pending.dst_precondition = dst_precondition;
          pending.predicate_guard = predicate_guard;
          pending.remaining_postconditions.reserve(remaining);
          for (unsigned idx = 0; idx < remaining; idx++)
          {
            const ApUserEvent post = Runtime::create_ap_user_event(&trace_info);
            pending.remaining_postconditions.push_back(post);
            dst_events.push_back(post);
          }
          if (trace_info.recording)
          {
            pending.applied_event = Runtime::create_rt_user_event();
            applied_events.insert(pending.applied_event);
          }
        }
      }
      // Now we can perform any copies that we received
      for (std::vector<AllReduceCopy>::const_iterator it =
            to_perform.begin(); it != to_perform.end(); it++)
      {
        const ApEvent pre = Runtime::merge_events(&trace_info,
          it->src_precondition, dst_precondition);
        const ApEvent post = copy_expression->issue_copy(
            op, trace_info, dst_fields, it->src_fields, reservations,
#ifdef LEGION_SPY
            it->src_inst.tid, dst_inst.tid,
#endif
            pre, predicate_guard);
        if (trace_info.recording)
          trace_info.record_copy_insts(post, copy_expression, it->src_inst,
              dst_inst, copy_mask, copy_mask, redop, applied_events);
        if (it->barrier_postcondition.exists())
        {
          Runtime::phase_barrier_arrive(
              it->barrier_postcondition, 1/*count*/, post);
          if (trace_info.recording)
            trace_info.record_barrier_arrival(it->barrier_postcondition,
                post, 1/*count*/, applied_events, it->barrier_shard);
        }
        else
        {
#ifdef DEBUG_LEGION
          assert(it->src_postcondition.exists());
#endif
          Runtime::trigger_event(&trace_info, it->src_postcondition, post);
        }
        if (post.exists())
          dst_events.push_back(post);
      }
    }

    //--------------------------------------------------------------------------
    void AllreduceView::process_distribute_allreduce(
              const uint64_t allreduce_tag, const int src_rank, const int stage,
              std::vector<CopySrcDstField> &src_fields,
              const ApEvent src_precondition, ApUserEvent src_postcondition,
              ApBarrier src_barrier, ShardID barrier_shard, 
              const UniqueInst &src_inst)
    //--------------------------------------------------------------------------
    {
      LegionMap<std::pair<uint64_t,int>,AllReduceStage>::iterator finder;
      {
        AutoLock v_lock(view_lock);
        const std::pair<uint64_t,int> stage_key(allreduce_tag, stage);
        finder = remaining_stages.find(stage_key);
        if (finder == remaining_stages.end())
        {
          const CopyKey key(allreduce_tag, src_rank, stage);
#ifdef DEBUG_LEGION
          assert(all_reduce_copies.find(key) == all_reduce_copies.end());
#endif
          AllReduceCopy &copy = all_reduce_copies[key];
          copy.src_fields.swap(src_fields);
          copy.src_precondition = src_precondition;
          copy.src_postcondition = src_postcondition;
          copy.barrier_postcondition = src_barrier;
          copy.barrier_shard = barrier_shard;
          copy.src_inst = src_inst;
          return;
        }
#ifdef DEBUG_LEGION
        assert(!finder->second.remaining_postconditions.empty());
#endif
        // We can release the lock because we know map iterators are 
        // not invalidated by insertion/deletion and any other copies
        // for this same stage are also just going to be reading except
        // for when we need to grab our event at the end to trigger
        // which we can re-take the lock to do
      }
      const ApEvent precondition = Runtime::merge_events(
          finder->second.trace_info, src_precondition,
          finder->second.dst_precondition);
      const ApEvent copy_post = finder->second.copy_expression->issue_copy(
          finder->second.op, *(finder->second.trace_info),
          finder->second.dst_fields, src_fields, finder->second.reservations,
#ifdef LEGION_SPY
          src_inst.tid, finder->second.dst_inst.tid,
#endif
          precondition, finder->second.predicate_guard);
      std::set<RtEvent> applied_events;
      if (finder->second.trace_info->recording)
        finder->second.trace_info->record_copy_insts(copy_post, 
            finder->second.copy_expression, src_inst, finder->second.dst_inst,
            finder->second.copy_mask, finder->second.copy_mask,
            redop, applied_events);
      if (src_barrier.exists())
      {
        Runtime::phase_barrier_arrive(src_barrier, 1/*count*/, copy_post);
        finder->second.trace_info->record_barrier_arrival(src_barrier,
            copy_post, 1/*count*/, applied_events, barrier_shard);
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(src_postcondition.exists());
#endif
        Runtime::trigger_event(finder->second.trace_info, 
                               src_postcondition, copy_post);
      }
      RtUserEvent applied;
      ApUserEvent to_trigger;
      PhysicalTraceInfo *trace_info = NULL;
      IndexSpaceExpression *copy_expression = NULL;
      {
        // Retake the lock and see if we're the last arrival
        AutoLock v_lock(view_lock);
        // Save any applied events that we have
        if (!applied_events.empty())
        {
          finder->second.applied_events.insert(
              applied_events.begin(), applied_events.end());
#ifdef DEBUG_LEGION
          applied_events.clear();
#endif
        }
#ifdef DEBUG_LEGION
        assert(!finder->second.remaining_postconditions.empty());
#endif
        to_trigger = finder->second.remaining_postconditions.back();
        finder->second.remaining_postconditions.pop_back();
        if (finder->second.remaining_postconditions.empty())
        {
          // Last pass through, grab data and remove from the stages
          trace_info = finder->second.trace_info;
          copy_expression = finder->second.copy_expression;
          applied = finder->second.applied_event;
          applied_events.swap(finder->second.applied_events);
          remaining_stages.erase(finder);
        }
        else // Need a copy of this
          trace_info = new PhysicalTraceInfo(*(finder->second.trace_info));
      }
      Runtime::trigger_event(trace_info, to_trigger, copy_post); 
      if (applied.exists())
      {
        if (!applied_events.empty())
          Runtime::trigger_event(applied,Runtime::merge_events(applied_events));
        else
          Runtime::trigger_event(applied);
#ifdef DEBUG_LEGION
        applied_events.clear();
#endif
      }
#ifdef DEBUG_LEGION
      assert(applied_events.empty());
#endif
      delete trace_info;
      if ((copy_expression != NULL) &&
          copy_expression->remove_nested_expression_reference(this->did))
        delete copy_expression;
    }

    //--------------------------------------------------------------------------
    /*static*/ void AllreduceView::handle_distribute_allreduce(
                   Runtime *runtime, AddressSpaceID source, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      AllreduceView *view= static_cast<AllreduceView*>(
          runtime->find_or_request_logical_view(did, ready));
      uint64_t allreduce_tag;
      derez.deserialize(allreduce_tag);
      int src_rank;
      derez.deserialize(src_rank);
      int stage;
      derez.deserialize(stage);
      size_t num_src_fields;
      derez.deserialize(num_src_fields);
      std::vector<CopySrcDstField> src_fields(num_src_fields);
      std::set<RtEvent> ready_events;
      unpack_fields(src_fields, derez, ready_events, view, ready, runtime);
      UniqueInst src_inst;
      src_inst.deserialize(derez);
      ApEvent src_precondition;
      derez.deserialize(src_precondition);
      bool recording;
      derez.deserialize<bool>(recording);
      ApBarrier src_barrier;
      ShardID barrier_shard = 0;
      ApUserEvent src_postcondition;
      if (recording)
      {
        derez.deserialize(src_barrier);
        derez.deserialize(barrier_shard);
      }
      else
        derez.deserialize(src_postcondition);

      if (ready.exists() && !ready.has_triggered())
        ready_events.insert(ready);
      if (!ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }

      view->process_distribute_allreduce(allreduce_tag, src_rank, stage,
                            src_fields, src_precondition, src_postcondition,
                            src_barrier, barrier_shard, src_inst);
    }

  }; // namespace Internal 
}; // namespace Legion

