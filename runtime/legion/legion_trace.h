/* Copyright 2019 Stanford University, NVIDIA Corporation
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


#ifndef __LEGION_TRACE__
#define __LEGION_TRACE__

#include "legion.h"
#include "legion/legion_ops.h"

namespace Legion {
  namespace Internal {

    /**
     * \class LegionTrace
     * This is the abstract base class for a trace object
     * and is used to support both static and dynamic traces
     */
    class LegionTrace : public Collectable {
    public:
      struct DependenceRecord {
      public:
        DependenceRecord(int idx)
          : operation_idx(idx), prev_idx(-1), next_idx(-1),
            validates(false), dtype(TRUE_DEPENDENCE) { }
        DependenceRecord(int op_idx, int pidx, int nidx,
                         bool val, DependenceType d,
                         const FieldMask &m)
          : operation_idx(op_idx), prev_idx(pidx), 
            next_idx(nidx), validates(val), dtype(d),
            dependent_mask(m) { }
      public:
        inline bool merge(const DependenceRecord &record)
        {
          if ((operation_idx != record.operation_idx) ||
              (prev_idx != record.prev_idx) ||
              (next_idx != record.next_idx) ||
              (validates != record.validates) ||
              (dtype != record.dtype))
            return false;
          dependent_mask |= record.dependent_mask;
          return true;
        }
      public:
        int operation_idx;
        int prev_idx; // previous region requirement index
        int next_idx; // next region requirement index
        bool validates;
        DependenceType dtype;
        FieldMask dependent_mask;
      };
      struct AliasChildren {
      public:
        AliasChildren(unsigned req_idx, unsigned dep, const FieldMask &m)
          : req_index(req_idx), depth(dep), mask(m) { }
      public:
        unsigned req_index;
        unsigned depth;
        FieldMask mask;
      };
    public:
      enum TracingState {
        LOGICAL_ONLY,
        PHYSICAL_RECORD,
        PHYSICAL_REPLAY,
      };
    public:
      LegionTrace(InnerContext *ctx, bool logical_only);
      virtual ~LegionTrace(void);
    public:
      virtual bool is_static_trace(void) const = 0;
      virtual bool is_dynamic_trace(void) const = 0;
      virtual StaticTrace* as_static_trace(void) = 0;
      virtual DynamicTrace* as_dynamic_trace(void) = 0;
      virtual TraceID get_trace_id(void) const = 0;
    public:
      virtual bool is_fixed(void) const = 0;
      virtual bool handles_region_tree(RegionTreeID tid) const = 0;
      virtual void record_static_dependences(Operation *op,
                     const std::vector<StaticDependence> *dependences) = 0;
      virtual void register_operation(Operation *op, GenerationID gen) = 0; 
      virtual void record_dependence(Operation *target, GenerationID target_gen,
                                Operation *source, GenerationID source_gen) = 0;
      virtual void record_region_dependence(
                                    Operation *target, GenerationID target_gen,
                                    Operation *source, GenerationID source_gen,
                                    unsigned target_idx, unsigned source_idx,
                                    DependenceType dtype, bool validates,
                                    const FieldMask &dependent_mask) = 0;
      virtual void record_aliased_children(unsigned req_index, unsigned depth,
                                           const FieldMask &aliased_mask) = 0;
    public:
      bool has_physical_trace(void) { return physical_trace != NULL; }
      PhysicalTrace* get_physical_trace(void) { return physical_trace; }
      void register_physical_only(Operation *op, GenerationID gen);
    public:
      void replay_aliased_children(std::vector<RegionTreePath> &paths) const;
      void end_trace_execution(FenceOp *fence_op);
    public:
      void initialize_tracing_state(void) { state = LOGICAL_ONLY; }
      void set_state_record(void) { state = PHYSICAL_RECORD; }
      void set_state_replay(void) { state = PHYSICAL_REPLAY; }
      bool is_recording(void) const { return state == PHYSICAL_RECORD; }
      bool is_replaying(void) const { return state == PHYSICAL_REPLAY; }
    public:
      void clear_blocking_call(void) { blocking_call_observed = false; }
      void record_blocking_call(void) { blocking_call_observed = true; }
      bool has_blocking_call(void) const { return blocking_call_observed; }
      void invalidate_trace_cache(Operation *invalidator);
#ifdef LEGION_SPY
    public:
      virtual void perform_logging(
                          UniqueID prev_fence_uid, UniqueID curr_fence_uid) = 0;
    public:
      UniqueID get_current_uid_by_index(unsigned op_idx) const;
#endif
    public:
      InnerContext *const ctx;
    protected:
      std::vector<std::pair<Operation*,GenerationID> > operations; 
      // We also need a data structure to record when there are
      // aliased but non-interfering region requirements. This should
      // be pretty sparse so we'll make it a map
      std::map<unsigned,LegionVector<AliasChildren>::aligned> aliased_children;
      volatile TracingState state;
      // Pointer to a physical trace
      PhysicalTrace *physical_trace;
      unsigned last_memoized;
      bool blocking_call_observed;
      std::set<std::pair<Operation*,GenerationID> > frontiers;
#ifdef LEGION_SPY
    protected:
      std::map<std::pair<Operation*,GenerationID>,UniqueID> current_uids;
      std::map<std::pair<Operation*,GenerationID>,unsigned> num_regions;
#endif
    };

    /**
     * \class StaticTrace
     * A static trace is a trace object that is used for 
     * handling cases where the application knows the dependneces
     * for a trace of operations
     */
    class StaticTrace : public LegionTrace,
                        public LegionHeapify<StaticTrace> {
    public:
      static const AllocationType alloc_type = STATIC_TRACE_ALLOC;
    public:
      StaticTrace(InnerContext *ctx, const std::set<RegionTreeID> *trees);
      StaticTrace(const StaticTrace &rhs);
      virtual ~StaticTrace(void);
    public:
      StaticTrace& operator=(const StaticTrace &rhs);
    public:
      virtual bool is_static_trace(void) const { return true; }
      virtual bool is_dynamic_trace(void) const { return false; }
      virtual StaticTrace* as_static_trace(void) { return this; }
      virtual DynamicTrace* as_dynamic_trace(void) { return NULL; }
      virtual TraceID get_trace_id(void) const { return 0; }
    public:
      virtual bool is_fixed(void) const;
      virtual bool handles_region_tree(RegionTreeID tid) const;
      virtual void record_static_dependences(Operation *op,
                              const std::vector<StaticDependence> *dependences);
      virtual void register_operation(Operation *op, GenerationID gen); 
      virtual void record_dependence(Operation *target,GenerationID target_gen,
                                     Operation *source,GenerationID source_gen);
      virtual void record_region_dependence(
                                    Operation *target, GenerationID target_gen,
                                    Operation *source, GenerationID source_gen,
                                    unsigned target_idx, unsigned source_idx,
                                    DependenceType dtype, bool validates,
                                    const FieldMask &dependent_mask);
      virtual void record_aliased_children(unsigned req_index, unsigned depth,
                                           const FieldMask &aliased_mask);
#ifdef LEGION_SPY
    public:
      virtual void perform_logging(
                          UniqueID prev_fence_uid, UniqueID curr_fence_uid);
#endif
    protected:
      const LegionVector<DependenceRecord>::aligned&
                  translate_dependence_records(Operation *op, unsigned index);
    protected:
      std::deque<std::vector<StaticDependence> > static_dependences;
      std::deque<LegionVector<DependenceRecord>::aligned> translated_deps;
      std::set<RegionTreeID> application_trees;
    };

    /**
     * \class DynamicTrace
     * This class is used for memoizing the dynamic
     * dependence analysis for series of operations
     * in a given task's context.
     */
    class DynamicTrace : public LegionTrace,
                         public LegionHeapify<DynamicTrace> {
    public:
      static const AllocationType alloc_type = DYNAMIC_TRACE_ALLOC;
    public:
      struct OperationInfo {
      public:
        OperationInfo(Operation *op)
          : kind(op->get_operation_kind()), count(op->get_region_count()) { }
      public:
        Operation::OpKind kind;
        unsigned count;
      }; 
    public:
      DynamicTrace(TraceID tid, InnerContext *ctx, bool logical_only);
      DynamicTrace(const DynamicTrace &rhs);
      virtual ~DynamicTrace(void);
    public:
      DynamicTrace& operator=(const DynamicTrace &rhs);
    public:
      virtual bool is_static_trace(void) const { return false; }
      virtual bool is_dynamic_trace(void) const { return true; }
      virtual StaticTrace* as_static_trace(void) { return NULL; }
      virtual DynamicTrace* as_dynamic_trace(void) { return this; }
      virtual TraceID get_trace_id(void) const { return tid; }
    public:
      // Called by task execution thread
      virtual bool is_fixed(void) const { return fixed; }
      void fix_trace(void);
    public:
      // Called by analysis thread
      void end_trace_capture(void);
    public:
      virtual void record_static_dependences(Operation *op,
                          const std::vector<StaticDependence> *dependences);
      virtual bool handles_region_tree(RegionTreeID tid) const;
      // Called by analysis thread
      virtual void register_operation(Operation *op, GenerationID gen);
      virtual void record_dependence(Operation *target,GenerationID target_gen,
                                     Operation *source,GenerationID source_gen);
      virtual void record_region_dependence(
                                    Operation *target, GenerationID target_gen,
                                    Operation *source, GenerationID source_gen,
                                    unsigned target_idx, unsigned source_idx,
                                    DependenceType dtype, bool validates,
                                    const FieldMask &dependent_mask);
      virtual void record_aliased_children(unsigned req_index, unsigned depth,
                                           const FieldMask &aliased_mask);
#ifdef LEGION_SPY
    public:
      virtual void perform_logging(
                          UniqueID prev_fence_uid, UniqueID curr_fence_uid);
#endif
    protected:
      // Insert a normal dependence for the current operation
      void insert_dependence(const DependenceRecord &record);
      // Insert an internal dependence for given key
      void insert_dependence(const std::pair<InternalOp*,GenerationID> &key,
                             const DependenceRecord &record);
    protected:
      // Only need this backwards lookup for recording dependences
      std::map<std::pair<Operation*,GenerationID>,unsigned> op_map;
      // Internal operations have a nasty interaction with traces because
      // we can generate different sets of internal operations each time we
      // run the trace depending on the state of the logical region tree.
      // Therefore, we keep track of internal ops done when capturing the trace
      // record transitive dependences on the other operations in the
      // trace that we would have interfered with had the internal operations
      // not been necessary.
      std::map<std::pair<InternalOp*,GenerationID>,
               LegionVector<DependenceRecord>::aligned> internal_dependences;
    protected: 
      // This is the generalized form of the dependences
      // For each operation, we remember a list of operations that
      // it dependens on and whether it is a validates the region
      std::deque<LegionVector<DependenceRecord>::aligned> dependences;
      // Metadata for checking the validity of a trace when it is replayed
      std::vector<OperationInfo> op_info;
    protected:
      const TraceID tid;
      bool fixed;
      bool tracing;
    };

    class TraceOp : public FenceOp {
    public:
      TraceOp(Runtime *rt);
      TraceOp(const TraceOp &rhs);
      virtual ~TraceOp(void);
    public:
      TraceOp& operator=(const TraceOp &rhs);
    public:
      virtual void execute_dependence_analysis(void);
    protected:
      LegionTrace *local_trace;
    };

    /**
     * \class TraceCaptureOp
     * This class represents trace operations which we inject
     * into the operation stream to mark when a trace capture
     * is finished so the DynamicTrace object can compute the
     * dependences data structure.
     */
    class TraceCaptureOp : public TraceOp {
    public:
      static const AllocationType alloc_type = TRACE_CAPTURE_OP_ALLOC;
    public:
      TraceCaptureOp(Runtime *rt);
      TraceCaptureOp(const TraceCaptureOp &rhs);
      virtual ~TraceCaptureOp(void);
    public:
      TraceCaptureOp& operator=(const TraceCaptureOp &rhs);
    public:
      void initialize_capture(InnerContext *ctx, bool has_blocking_call);
    public:
      virtual void activate(void);
      virtual void deactivate(void);
      virtual const char* get_logging_name(void) const;
      virtual OpKind get_operation_kind(void) const;
      virtual void trigger_dependence_analysis(void);
      virtual void trigger_mapping(void);
    protected:
      DynamicTrace *dynamic_trace;
      PhysicalTemplate *current_template;
      bool has_blocking_call;
    };

    /**
     * \class TraceCompleteOp
     * This class represents trace operations which we inject
     * into the operation stream to mark when the execution
     * of a trace has been completed.  This fence operation
     * then registers dependences on all operations in the trace
     * and becomes the new current fence.
     */
    class TraceCompleteOp : public TraceOp {
    public:
      static const AllocationType alloc_type = TRACE_COMPLETE_OP_ALLOC;
    public:
      TraceCompleteOp(Runtime *rt);
      TraceCompleteOp(const TraceCompleteOp &rhs);
      virtual ~TraceCompleteOp(void);
    public:
      TraceCompleteOp& operator=(const TraceCompleteOp &rhs);
    public:
      void initialize_complete(InnerContext *ctx, bool has_blocking_call);
    public:
      virtual void activate(void);
      virtual void deactivate(void);
      virtual const char* get_logging_name(void) const;
      virtual OpKind get_operation_kind(void) const;
      virtual void trigger_dependence_analysis(void);
      virtual void trigger_mapping(void);
    protected:
      PhysicalTemplate *current_template;
      ApEvent template_completion;
      bool replayed;
      bool has_blocking_call;
    };

    /**
     * \class TraceReplayOp
     * This class represents trace operations which we inject
     * into the operation stream to replay a physical trace
     * if there is one that satisfies its preconditions.
     */
    class TraceReplayOp : public TraceOp {
    public:
      static const AllocationType alloc_type = TRACE_REPLAY_OP_ALLOC;
    public:
      TraceReplayOp(Runtime *rt);
      TraceReplayOp(const TraceReplayOp &rhs);
      virtual ~TraceReplayOp(void);
    public:
      TraceReplayOp& operator=(const TraceReplayOp &rhs);
    public:
      void initialize_replay(InnerContext *ctx, LegionTrace *trace);
    public:
      virtual void activate(void);
      virtual void deactivate(void);
      virtual const char* get_logging_name(void) const;
      virtual OpKind get_operation_kind(void) const;
      virtual void trigger_dependence_analysis(void);
    };

    /**
     * \class TraceBeginOp
     * This class represents mapping fences which we inject
     * into the operation stream to begin a trace.  This fence
     * is by a TraceReplayOp if the trace allows physical tracing.
     */
    class TraceBeginOp : public TraceOp {
    public:
      static const AllocationType alloc_type = TRACE_BEGIN_OP_ALLOC;
    public:
      TraceBeginOp(Runtime *rt);
      TraceBeginOp(const TraceBeginOp &rhs);
      virtual ~TraceBeginOp(void);
    public:
      TraceBeginOp& operator=(const TraceBeginOp &rhs);
    public:
      void initialize_begin(InnerContext *ctx, LegionTrace *trace);
    public:
      virtual void activate(void);
      virtual void deactivate(void);
      virtual const char* get_logging_name(void) const;
      virtual OpKind get_operation_kind(void) const;
    };

    class TraceSummaryOp : public Operation {
    public:
      static const AllocationType alloc_type = TRACE_SUMMARY_OP_ALLOC;
    public:
      TraceSummaryOp(Runtime *rt);
      TraceSummaryOp(const TraceSummaryOp &rhs);
      virtual ~TraceSummaryOp(void);
    public:
      TraceSummaryOp& operator=(const TraceSummaryOp &rhs);
    public:
      void initialize_summary(InnerContext *ctx,
                              UniqueID creator_id,
                              const std::vector<RegionRequirement> &reqs,
                              const std::vector<InstanceSet> &insts,
                              const std::vector<unsigned> &indices);
      void perform_logging(void);
    public:
      virtual void activate(void);
      virtual void deactivate(void);
      virtual const char* get_logging_name(void) const;
      virtual OpKind get_operation_kind(void) const;
    public:
      virtual void trigger_dependence_analysis(void);
      virtual void trigger_ready(void);
      virtual void trigger_mapping(void);
      virtual void trigger_commit(void);
    public:
      virtual unsigned find_parent_index(unsigned idx);
    protected:
      UniqueID creator_id;
      std::vector<RegionRequirement> requirements;
      std::vector<InstanceSet> instances;
      std::vector<unsigned> parent_indices;
      std::vector<RegionTreePath> privilege_paths;
      std::vector<VersionInfo> version_infos;
      std::set<RtEvent> map_applied_conditions;
      std::set<ApEvent> mapped_preconditions;
    };

    /**
     * \class PhysicalTrace
     * This class is used for memoizing the dynamic physical dependence
     * analysis for series of operations in a given task's context.
     */
    class PhysicalTrace {
    public:
      PhysicalTrace(Runtime *runtime, LegionTrace *logical_trace);
      PhysicalTrace(const PhysicalTrace &rhs);
      ~PhysicalTrace(void);
    public:
      PhysicalTrace& operator=(const PhysicalTrace &rhs);
    public:
      void clear_cached_template(void) { current_template = NULL; }
      void check_template_preconditions(TraceReplayOp *op);
    public:
      PhysicalTemplate* get_current_template(void) { return current_template; }
      bool has_any_templates(void) const { return templates.size() > 0; }
    public:
      void record_previous_template_completion(ApEvent template_completion)
        { previous_template_completion = template_completion; }
      ApEvent get_previous_template_completion(void) const
        { return previous_template_completion; }
    public:
      PhysicalTemplate* start_new_template(ApEvent fence_event);
      RtEvent fix_trace(PhysicalTemplate *tpl,
                        Operation *op,
                        bool has_blocking_call);
    public:
      void initialize_template(ApEvent fence_completion, bool recurrent);
    public:
      Runtime * const runtime;
      const LegionTrace *logical_trace;
    private:
      mutable LocalLock trace_lock;
      PhysicalTemplate* current_template;
      std::vector<PhysicalTemplate*> templates;
      unsigned nonreplayable_count;
    public:
      std::vector<Processor> replay_targets;
      ApEvent previous_template_completion;
    };

    typedef Memoizable::TraceLocalID TraceLocalID;

    /**
     * \class TraceViewSet
     */
    class TraceViewSet {
    public:
      TraceViewSet(RegionTreeForest *forest);
      virtual ~TraceViewSet(void);
    public:
      void insert(InstanceView *view,
                  EquivalenceSet *eq,
                  const FieldMask &mask);
      void invalidate(InstanceView *view,
                      EquivalenceSet *eq,
                      const FieldMask &mask);
    public:
      bool dominates(InstanceView *view,
                     EquivalenceSet *eq,
                     FieldMask &non_dominated) const;
      bool subsumed_by(const TraceViewSet &set) const;
      bool has_refinements(void) const;
      bool empty(void) const;
    public:
      void dump(void) const;
    protected:
      typedef LegionMap<InstanceView*,
                        FieldMaskSet<EquivalenceSet>  >::aligned Conditions;
    protected:
      RegionTreeForest * const forest;
    protected:
      Conditions conditions;
    };

    /**
     * \class TraceConditionSet
     */
    class TraceConditionSet : public TraceViewSet {
    public:
      TraceConditionSet(RegionTreeForest *forest);
      virtual ~TraceConditionSet(void);
    public:
      void make_ready(void);
    public:
      bool require(Operation *op);
      void ensure(Operation *op);
    private:
      bool cached;
      // The following containers are populated only when the 'cached' is true.
      LegionVector<FieldMaskSet<InstanceView> >::aligned views;
      LegionVector<VersionInfo>::aligned                 version_infos;
    };

    /**
     * \class PhysicalTemplate
     * This class represents a recipe to reconstruct a physical task graph.
     * A template consists of a sequence of instructions, each of which is
     * interpreted by the template engine. The template also maintains
     * the interpreter state (operations and events). These are initialized
     * before the template gets executed.
     */
    struct PhysicalTemplate {
    public:
      struct ReplaySliceArgs : public LgTaskArgs<ReplaySliceArgs> {
      public:
        static const LgTaskID TASK_ID = LG_REPLAY_SLICE_ID;
      public:
        ReplaySliceArgs(PhysicalTemplate *t, unsigned si)
          : LgTaskArgs<ReplaySliceArgs>(0), tpl(t), slice_index(si) { }
      public:
        PhysicalTemplate *tpl;
        unsigned slice_index;
      };
      struct DeleteTemplateArgs : public LgTaskArgs<DeleteTemplateArgs> {
      public:
        static const LgTaskID TASK_ID = LG_DELETE_TEMPLATE_ID;
      public:
        DeleteTemplateArgs(PhysicalTemplate *t)
          : LgTaskArgs<DeleteTemplateArgs>(0), tpl(t) { }
      public:
        PhysicalTemplate *tpl;
      };
    private:
      struct ViewUser {
        ViewUser(const RegionUsage &r, unsigned u, IndexSpaceExpression *e)
          : usage(r), user(u), expr(e)
        {}
        const RegionUsage usage;
        unsigned user;
        IndexSpaceExpression *expr;
      };
    private:
      struct CachedMapping
      {
        VariantID               chosen_variant;
        TaskPriority            task_priority;
        bool                    postmap_task;
        std::vector<Processor>  target_procs;
        std::deque<InstanceSet> physical_instances;
      };
      typedef LegionMap<TraceLocalID,CachedMapping>::aligned CachedMappings;
    private:
      typedef LegionMap<InstanceView*,
                        FieldMaskSet<IndexSpaceExpression> >::aligned ViewExprs;
      typedef LegionMap<InstanceView*,
                        FieldMaskSet<ViewUser> >::aligned             ViewUsers;
      typedef std::map<RegionTreeID,std::set<InstanceView*> >        ViewGroups;
    public:
      PhysicalTemplate(PhysicalTrace *trace, ApEvent fence_event);
      PhysicalTemplate(const PhysicalTemplate &rhs);
    private:
      friend class PhysicalTrace;
      ~PhysicalTemplate(void);
    public:
      void initialize(Runtime *runtime, ApEvent fence_completion,
                      bool recurrent);
      ApEvent get_completion(void) const;
      ApEvent get_completion_for_deletion(void) const;
    public:
      void finalize(Operation *op, bool has_blocking_call);
      void generate_conditions(void);
      bool check_replayable(void) const;
      bool check_subsumption(void) const;
    public:
      void optimize(void);
    private:
      void elide_fences(std::vector<unsigned> &gen);
      void propagate_merges(std::vector<unsigned> &gen);
      void transitive_reduction(void);
      void propagate_copies(std::vector<unsigned> &gen);
      void prepare_parallel_replay(const std::vector<unsigned> &gen);
      void push_complete_replays(void);
    public:
      bool check_preconditions(TraceReplayOp *op);
      void register_operation(Operation *op);
      void execute_all(void);
      void execute_slice(unsigned slice_idx);
    public:
      void generate_summary_operations(void);
      void issue_summary_operations(InnerContext* context,
                                    Operation *invalidator);
    public:
      void dump_template(void);
      void dump_instructions(const std::vector<Instruction*> &instructions);
    public:
#ifdef LEGION_SPY
      void set_fence_uid(UniqueID fence_uid) { prev_fence_uid = fence_uid; }
      UniqueID get_fence_uid(void) const { return prev_fence_uid; }
#endif
    public:
      inline bool is_recording(void) const { return recording; }
      inline bool is_replaying(void) const { return !recording; }
      inline bool is_replayable(void) const { return replayable; }
    public:
      void record_mapper_output(SingleTask *task,
                                const Mapper::MapTaskOutput &output,
                             const std::deque<InstanceSet> &physical_instances);
      void get_mapper_output(SingleTask *task,
                             VariantID &chosen_variant,
                             TaskPriority &task_priority,
                             bool &postmap_task,
                             std::vector<Processor> &target_proc,
                             std::deque<InstanceSet> &physical_instances) const;
    public:
      void record_get_term_event(Memoizable* memo);
      void record_create_ap_user_event(ApUserEvent lhs, Operation *owner);
      void record_trigger_event(ApUserEvent lhs, ApEvent rhs);
    public:
      void record_merge_events(ApEvent &lhs, ApEvent rhs, Operation *owner);
      void record_merge_events(ApEvent &lhs, ApEvent e1, ApEvent e2,
                               Operation *owner);
      void record_merge_events(ApEvent &lhs, ApEvent e1, ApEvent e2,
                               ApEvent e3, Operation *owner);
      void record_merge_events(ApEvent &lhs, const std::set<ApEvent>& rhs,
                               Operation *owner);
    public:
      void record_issue_copy(Memoizable *memo,
                             unsigned src_idx,
                             unsigned dst_idx,
                             ApEvent &lhs,
                             IndexSpaceExpression *expr,
                             const std::vector<CopySrcDstField>& src_fields,
                             const std::vector<CopySrcDstField>& dst_fields,
#ifdef LEGION_SPY
                             FieldSpace handle,
                             RegionTreeID src_tree_id,
                             RegionTreeID dst_tree_id,
#endif
                             ApEvent precondition,
                             ReductionOpID redop,
                             bool reduction_fold,
                             const FieldMaskSet<InstanceView> &tracing_srcs,
                             const FieldMaskSet<InstanceView> &tracing_dsts);
      void record_issue_indirect(Memoizable *memo, ApEvent &lhs,
                             IndexSpaceExpression *expr,
                             const std::vector<CopySrcDstField>& src_fields,
                             const std::vector<CopySrcDstField>& dst_fields,
                             const std::vector<void*> &indirections,
                             ApEvent precondition);
      void record_issue_fill(Memoizable *memo, unsigned idx,
                             ApEvent &lhs,
                             IndexSpaceExpression *expr,
                             const std::vector<CopySrcDstField> &fields,
                             const void *fill_value, size_t fill_size,
#ifdef LEGION_SPY
                             UniqueID fill_uid,
                             FieldSpace handle,
                             RegionTreeID tree_id,
#endif
                             ApEvent precondition,
                             const FieldMaskSet<FillView> &tracing_srcs,
                             const FieldMaskSet<InstanceView> &tracing_dsts);
    public:
      void record_summary_info(const RegionRequirement &region,
                               const InstanceSet &instance_set,
                               unsigned parent_idx);
    public:
      void record_op_view(Memoizable *memo,
                          unsigned idx,
                          InstanceView *view,
                          const RegionUsage &usage,
                          const FieldMask &user_mask,
                          bool update_validity);
      void record_fill_view(FillView *view, const FieldMask &user_mask);
    private:
      void record_views(Memoizable *memo,
                        unsigned idx,
                        unsigned entry,
                        IndexSpaceExpression *expr,
                        const RegionUsage &usage,
                        const FieldMaskSet<InstanceView> &views);
      void update_valid_views(Memoizable *memo,
                              InstanceView *view,
                              EquivalenceSet *eq,
                              const RegionUsage &usage,
                              const FieldMask &user_mask,
                              bool invalidates);
      void add_view_user(InstanceView *view,
                         ViewUser *user,
                         const FieldMask &user_mask);
      void record_copy_views(unsigned copy_id,
                             IndexSpaceExpression *expr,
                             const FieldMaskSet<InstanceView> &views);
      void record_fill_views(const FieldMaskSet<FillView> &views);
    public:
      void record_set_op_sync_event(ApEvent &lhs, Operation *op);
      void record_complete_replay(Operation *op, ApEvent rhs);
    public:
      void record_outstanding_gc_event(CollectableView *view, 
                                       ApEvent term_event);
    public:
      RtEvent defer_template_deletion(void);
    public:
      static void handle_replay_slice(const void *args);
      static void handle_delete_template(const void *args);
    private:
      TraceLocalID find_trace_local_id(Memoizable *memo);
      unsigned find_memo_entry(Memoizable *memo);
      TraceLocalID record_memo_entry(Memoizable *memo, unsigned entry);
    private:
      unsigned convert_event(const ApEvent &event);
      unsigned find_event(const ApEvent &event) const;
      void insert_instruction(Instruction *inst);
    private:
      static bool is_compatible(const RegionUsage &u1,
                                const RegionUsage &u2);
    private:
      void find_all_last_users(ViewExprs &view_exprs,
                               std::set<unsigned> &users);
      void find_last_users(InstanceView *view,
                           IndexSpaceExpression *expr,
                           const FieldMask &mask,
                           std::set<unsigned> &users);
    private:
      PhysicalTrace *trace;
      volatile bool recording;
      bool replayable;
      mutable LocalLock template_lock;
      const unsigned fence_completion_id;
      const unsigned replay_parallelism;
    private:
      RtUserEvent replay_ready;
      RtEvent replay_done;
      std::map<ApEvent,unsigned> event_map;
      std::vector<Instruction*> instructions;
      std::vector<std::vector<Instruction*> > slices;
      std::vector<std::vector<TraceLocalID> > slice_tasks;
      std::map<TraceLocalID,unsigned> memo_entries;
      std::vector<std::pair<RegionRequirement, InstanceSet> > summary_info;
      std::vector<unsigned> parent_indices;
      struct SummaryOpInfo {
        std::vector<RegionRequirement> requirements;
        std::vector<InstanceSet> instances;
        std::vector<unsigned> parent_indices;
      };
      std::vector<SummaryOpInfo> dedup_summary_ops;
      std::map<unsigned,unsigned> frontiers;
#ifdef LEGION_SPY
      UniqueID prev_fence_uid;
#endif
    public:
      std::map<TraceLocalID,Memoizable*> operations;
    public:
      ApEvent                     fence_completion;
      std::vector<ApEvent>        events;
      std::vector<ApUserEvent>    user_events;
      std::map<unsigned,unsigned> crossing_events;
    private:
      CachedMappings cached_mappings;
    private:
      std::map<TraceLocalID,ViewExprs> op_views;
      std::map<unsigned,ViewExprs>     copy_views;
    private:
      TraceConditionSet   pre, post;
      ViewGroups          view_groups;
      ViewUsers           view_users;
      std::set<ViewUser*> all_users;
    private:
      TraceViewSet pre_reductions;
      TraceViewSet post_reductions;
      TraceViewSet consumed_reductions;
    private:
      FieldMaskSet<FillView> pre_fill_views;
      FieldMaskSet<FillView> post_fill_views;
    private:
      std::map<CollectableView*,std::set<ApEvent> > outstanding_gc_events;
    };

    enum InstructionKind
    {
      GET_TERM_EVENT = 0,
      CREATE_AP_USER_EVENT,
      TRIGGER_EVENT,
      MERGE_EVENT,
      ISSUE_COPY,
      ISSUE_FILL,
      SET_OP_SYNC_EVENT,
      ASSIGN_FENCE_COMPLETION,
      COMPLETE_REPLAY,
    };

    /**
     * \class Instruction
     * This class is an abstract parent class for all template instructions.
     */
    struct Instruction {
      Instruction(PhysicalTemplate& tpl, const TraceLocalID &owner);
      virtual ~Instruction(void) {};
      virtual void execute(void) = 0;
      virtual std::string to_string(void) = 0;

      virtual InstructionKind get_kind(void) = 0;
      virtual GetTermEvent* as_get_term_event(void) = 0;
      virtual CreateApUserEvent* as_create_ap_user_event(void) = 0;
      virtual TriggerEvent* as_trigger_event(void) = 0;
      virtual MergeEvent* as_merge_event(void) = 0;
      virtual AssignFenceCompletion* as_assignment_fence_completion(void) = 0;
      virtual IssueCopy* as_issue_copy(void) = 0;
      virtual IssueFill* as_issue_fill(void) = 0;
      virtual SetOpSyncEvent* as_set_op_sync_event(void) = 0;
      virtual CompleteReplay* as_complete_replay(void) = 0;

      virtual Instruction* clone(PhysicalTemplate& tpl,
                               const std::map<unsigned, unsigned> &rewrite) = 0;

    protected:
      std::map<TraceLocalID, Memoizable*> &operations;
      std::vector<ApEvent> &events;
      std::vector<ApUserEvent> &user_events;
    public:
      const TraceLocalID owner;
    };

    /**
     * \class GetTermEvent
     * This instruction has the following semantics:
     *   events[lhs] = operations[rhs].get_memo_completion()
     */
    struct GetTermEvent : public Instruction {
      GetTermEvent(PhysicalTemplate& tpl, unsigned lhs,
                   const TraceLocalID& rhs);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return GET_TERM_EVENT; }
      virtual GetTermEvent* as_get_term_event(void)
        { return this; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
    };

    /**
     * \class CreateApUserEvent
     * This instruction has the following semantics:
     *   events[lhs] = Runtime::create_ap_user_event()
     */
    struct CreateApUserEvent : public Instruction {
      CreateApUserEvent(PhysicalTemplate& tpl, unsigned lhs,
                        const TraceLocalID &owner);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return CREATE_AP_USER_EVENT; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return this; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
    };

    /**
     * \class TriggerEvent
     * This instruction has the following semantics:
     *   Runtime::trigger_event(events[lhs], events[rhs])
     */
    struct TriggerEvent : public Instruction {
      TriggerEvent(PhysicalTemplate& tpl, unsigned lhs, unsigned rhs,
                   const TraceLocalID &owner);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return TRIGGER_EVENT; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return this; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
      unsigned rhs;
    };

    /**
     * \class MergeEvent
     * This instruction has the following semantics:
     *   events[lhs] = Runtime::merge_events(events[rhs])
     */
    struct MergeEvent : public Instruction {
      MergeEvent(PhysicalTemplate& tpl, unsigned lhs,
                 const std::set<unsigned>& rhs,
                 const TraceLocalID &owner);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return MERGE_EVENT; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return this; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
      std::set<unsigned> rhs;
    };

    /**
     * \class AssignFenceCompletion
     * This instruction has the following semantics:
     *   events[lhs] = fence_completion
     */
    struct AssignFenceCompletion : public Instruction {
      AssignFenceCompletion(PhysicalTemplate& tpl, unsigned lhs,
                            const TraceLocalID &owner);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return ASSIGN_FENCE_COMPLETION; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return this; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      ApEvent &fence_completion;
      unsigned lhs;
    };

    /**
     * \class IssueFill
     * This instruction has the following semantics:
     *
     *   events[lhs] = expr->fill(fields, fill_value, fill_size,
     *                            events[precondition_idx]);
     */
    struct IssueFill : public Instruction {
      IssueFill(PhysicalTemplate& tpl,
                unsigned lhs, IndexSpaceExpression *expr,
                const TraceLocalID &op_key,
                const std::vector<CopySrcDstField> &fields,
                const void *fill_value, size_t fill_size,
#ifdef LEGION_SPY
                UniqueID fill_uid,
                FieldSpace handle,
                RegionTreeID tree_id,
#endif
                unsigned precondition_idx);
      virtual ~IssueFill(void);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return ISSUE_FILL; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return this; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
      IndexSpaceExpression *expr;
      std::vector<CopySrcDstField> fields;
      void *fill_value;
      size_t fill_size;
#ifdef LEGION_SPY
      UniqueID fill_uid;
      FieldSpace handle;
      RegionTreeID tree_id;
#endif
      unsigned precondition_idx;
    };

    /**
     * \class IssueCopy
     * This instruction has the following semantics:
     *   events[lhs] = expr->issue_copy(src_fields, dst_fields,
     *                                  events[precondition_idx],
     *                                  predicate_guard,
     *                                  redop, reduction_fold);
     */
    struct IssueCopy : public Instruction {
      IssueCopy(PhysicalTemplate &tpl,
                unsigned lhs, IndexSpaceExpression *expr,
                const TraceLocalID &op_key,
                const std::vector<CopySrcDstField>& src_fields,
                const std::vector<CopySrcDstField>& dst_fields,
#ifdef LEGION_SPY
                FieldSpace handle,
                RegionTreeID src_tree_id,
                RegionTreeID dst_tree_id,
#endif
                unsigned precondition_idx,
                ReductionOpID redop, bool reduction_fold);
      virtual ~IssueCopy(void);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return ISSUE_COPY; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return this; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
      IndexSpaceExpression *expr;
      std::vector<CopySrcDstField> src_fields;
      std::vector<CopySrcDstField> dst_fields;
#ifdef LEGION_SPY
      FieldSpace handle;
      RegionTreeID src_tree_id;
      RegionTreeID dst_tree_id;
#endif
      unsigned precondition_idx;
      ReductionOpID redop;
      bool reduction_fold;
    };

    /**
     * \class SetOpSyncEvent
     * This instruction has the following semantics:
     *   events[lhs] = operations[rhs].compute_sync_precondition()
     */
    struct SetOpSyncEvent : public Instruction {
      SetOpSyncEvent(PhysicalTemplate& tpl, unsigned lhs,
                     const TraceLocalID& rhs);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return SET_OP_SYNC_EVENT; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return this; }
      virtual CompleteReplay* as_complete_replay(void)
        { return NULL; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned lhs;
    };

    /**
     * \class CompleteReplay
     * This instruction has the following semantics:
     *   operations[lhs]->complete_replay(events[rhs])
     */
    struct CompleteReplay : public Instruction {
      CompleteReplay(PhysicalTemplate& tpl, const TraceLocalID& lhs,
                     unsigned rhs);
      virtual void execute(void);
      virtual std::string to_string(void);

      virtual InstructionKind get_kind(void)
        { return COMPLETE_REPLAY; }
      virtual GetTermEvent* as_get_term_event(void)
        { return NULL; }
      virtual CreateApUserEvent* as_create_ap_user_event(void)
        { return NULL; }
      virtual TriggerEvent* as_trigger_event(void)
        { return NULL; }
      virtual MergeEvent* as_merge_event(void)
        { return NULL; }
      virtual AssignFenceCompletion* as_assignment_fence_completion(void)
        { return NULL; }
      virtual IssueCopy* as_issue_copy(void)
        { return NULL; }
      virtual IssueFill* as_issue_fill(void)
        { return NULL; }
      virtual SetOpSyncEvent* as_set_op_sync_event(void)
        { return NULL; }
      virtual CompleteReplay* as_complete_replay(void)
        { return this; }

      virtual Instruction* clone(PhysicalTemplate& tpl,
                                 const std::map<unsigned, unsigned> &rewrite);

    private:
      friend struct PhysicalTemplate;
      unsigned rhs;
    };

  }; // namespace Internal
}; // namespace Legion

#endif // __LEGION_TRACE__
