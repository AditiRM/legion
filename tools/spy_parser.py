#!/usr/bin/env python

# Copyright 2013 Stanford University
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#from spy_state import *
from spy_analysis import *
import sys, re

# All of these calls are based on the print statements in legion_logging.h

prefix    = "\[(?P<node>[0-9]+) - (?P<thread>[0-9a-f]+)\] \{\w+\}\{legion_spy\}: "
# Logger calls for the shape of the machine
utility_pat             = re.compile(prefix+"Utility (?P<pid>[0-9]+)")
processor_pat           = re.compile(prefix+"Processor (?P<pid>[0-9]+) (?P<util>[0-9]+) (?P<kind>[0-9]+)")
memory_pat              = re.compile(prefix+"Memory (?P<mid>[0-9]+) (?P<capacity>[0-9]+)")
proc_mem_pat            = re.compile(prefix+"Processor Memory (?P<pid>[0-9]+) (?P<mid>[0-9]+) (?P<band>[0-9]+) (?P<lat>[0-9]+)")
mem_mem_pat             = re.compile(prefix+"Memory Memory (?P<mone>[0-9]+) (?P<mtwo>[0-9]+) (?P<band>[0-9]+) (?P<lat>[0-9]+)")

# Calls for the shape of region trees
top_index_pat           = re.compile(prefix+"Index Space (?P<uid>[0-9]+)")
index_part_pat          = re.compile(prefix+"Index Partition (?P<pid>[0-9]+) (?P<uid>[0-9]+) (?P<disjoint>[0-1]) (?P<color>[0-9]+)")
index_subspace_pat      = re.compile(prefix+"Index Subspace (?P<pid>[0-9]+) (?P<uid>[0-9]+) (?P<color>[0-9]+)")
field_space_pat         = re.compile(prefix+"Field Space (?P<uid>[0-9]+)")
field_create_pat        = re.compile(prefix+"Field Creation (?P<uid>[0-9]+) (?P<fid>[0-9]+)")
region_pat              = re.compile(prefix+"Region (?P<iid>[0-9]+) (?P<fid>[0-9]+) (?P<tid>[0-9]+)")

# Logger calls for operations
top_task_pat            = re.compile(prefix+"Top Task (?P<tid>[0-9]+) (?P<uid>[0-9]+) (?P<name>\w+)")
single_task_pat         = re.compile(prefix+"Individual Task (?P<ctx>[0-9]+) (?P<tid>[0-9]+) (?P<uid>[0-9]+) (?P<name>\w+)")
index_task_pat          = re.compile(prefix+"Index Task (?P<ctx>[0-9]+) (?P<tid>[0-9]+) (?P<uid>[0-9]+) (?P<name>\w+)")
mapping_pat             = re.compile(prefix+"Mapping Operation (?P<ctx>[0-9]+) (?P<uid>[0-9]+)")
close_pat               = re.compile(prefix+"Close Operation (?P<ctx>[0-9]+) (?P<uid>[0-9]+)")
copy_op_pat             = re.compile(prefix+"Copy Operation (?P<ctx>[0-9]+) (?P<uid>[0-9]+)")
deletion_pat            = re.compile(prefix+"Deletion Operation (?P<ctx>[0-9]+) (?P<uid>[0-9]+)")
index_slice_pat         = re.compile(prefix+"Index Slice (?P<index>[0-9]+) (?P<slice>[0-9]+)")
slice_slice_pat         = re.compile(prefix+"Slice Slice (?P<slice1>[0-9]+) (?P<slice2>[0-9]+)")
slice_point_pat         = re.compile(prefix+"Slice Point (?P<slice>[0-9]+) (?P<point>[0-9]+) (?P<dim>[0-9]+) (?P<val1>[0-9]+) (?P<val2>[0-9]+) (?P<val3>[0-9]+)")
point_point_pat         = re.compile(prefix+"Point Point (?P<point1>[0-9]+) (?P<point2>[0-9]+)")

# Logger calls for logical mapping dependence analysis
requirement_pat         = re.compile(prefix+"Logical Requirement (?P<uid>[0-9]+) (?P<index>[0-9]+) (?P<is_reg>[0-1]) (?P<ispace>[0-9]+) (?P<fspace>[0-9]+) (?P<tid>[0-9]+) (?P<priv>[0-9]+) (?P<coher>[0-9]+) (?P<redop>[0-9]+)")
req_field_pat           = re.compile(prefix+"Logical Requirement Field (?P<uid>[0-9]+) (?P<index>[0-9]+) (?P<fid>[0-9]+)")
mapping_dep_pat         = re.compile(prefix+"Mapping Dependence (?P<ctx>[0-9]+) (?P<prev_id>[0-9]+) (?P<pidx>[0-9]+) (?P<next_id>[0-9]+) (?P<nidx>[0-9]+) (?P<dtype>[0-9]+)")

# Logger calls for physical dependence analysis
task_inst_req_pat       = re.compile(prefix+"Task Instance Requirement (?P<uid>[0-9]+) (?P<idx>[0-9]+) (?P<index>[0-9]+)")

# Logger calls for events
event_event_pat         = re.compile(prefix+"Event Event (?P<idone>[0-9]+) (?P<genone>[0-9]+) (?P<idtwo>[0-9]+) (?P<gentwo>[0-9]+)")
implicit_event_pat      = re.compile(prefix+"Implicit Event (?P<idone>[0-9]+) (?P<genone>[0-9]+) (?P<idtwo>[0-9]+) (?P<gentwo>[0-9]+)")
op_event_pat            = re.compile(prefix+"Op Events (?P<uid>[0-9]+) (?P<startid>[0-9]+) (?P<startgen>[0-9]+) (?P<termid>[0-9]+) (?P<termgen>[0-9]+)")
copy_event_pat          = re.compile(prefix+"Copy Events (?P<srcman>[0-9]+) (?P<dstman>[0-9]+) (?P<index>[0-9]+) (?P<field>[0-9]+) (?P<tree>[0-9]+) (?P<startid>[0-9]+) (?P<startgen>[0-9]+) (?P<termid>[0-9]+) (?P<termgen>[0-9]+) (?P<redop>[0-9]+) (?P<mask>[0-9\,]+)")

# Logger calls for physical instance usage 
physical_inst_pat       = re.compile(prefix+"Physical Instance (?P<iid>[0-9]+) (?P<mid>[0-9]+) (?P<index>[0-9]+) (?P<field>[0-9]+) (?P<tid>[0-9]+)")
physical_reduc_pat      = re.compile(prefix+"Reduction Instance (?P<iid>[0-9]+) (?P<mid>[0-9]+) (?P<index>[0-9]+) (?P<field>[0-9]+) (?P<tid>[0-9]+) (?P<fold>[0-1]) (?P<indirect>[0-9]+)")
op_user_pat             = re.compile(prefix+"Op Instance User (?P<uid>[0-9]+) (?P<idx>[0-9]+) (?P<iid>[0-9]+)")

def parse_log_file(file_name, state):
    log = open(file_name, 'r')
    matches = 0
    # Since some lines might match, but are out of order due to things getting
    # printed to the log file in weird orders, try reparsing lines
    replay_lines = list()
    for line in log:
        matches = matches + 1
        # Machine shapes
        m = utility_pat.match(line)
        if m <> None:
            state.add_utility(int(m.group('pid')))
            continue
        m = processor_pat.match(line)
        if m <> None:
            if not state.add_processor(int(m.group('pid')), int(m.group('util')), int(m.group('kind'))):
                replay_lines.append(line)
            continue
        m = memory_pat.match(line)
        if m <> None:
            state.add_memory(int(m.group('mid')), int(m.group('capacity')))
            continue
        m = proc_mem_pat.match(line)
        if m <> None:
            if not state.set_proc_mem(int(m.group('pid')), int(m.group('mid')), int(m.group('band')), int(m.group('lat'))):
                replay_lines.append(line)
            continue
        m = mem_mem_pat.match(line)
        if m <> None:
            if not state.set_mem_mem(int(m.group('mone')), int(m.group('mtwo')), int(m.group('band')), int(m.group('lat'))):
                replay_lines.append(line)
            continue
        # Region tree shapes
        m = top_index_pat.match(line)
        if m <> None:
            state.add_index_space(int(m.group('uid')))
            continue
        m = index_part_pat.match(line)
        if m <> None:
            if not state.add_index_partition(int(m.group('pid')), int(m.group('uid')), True if (int(m.group('disjoint'))) == 1 else False, int(m.group('color'))):
                replay_lines.append(line)
            continue
        m = index_subspace_pat.match(line)
        if m <> None:
            if not state.add_index_subspace(int(m.group('pid')), int(m.group('uid')), int(m.group('color'))):
                replay_lines.append(line)
            continue
        m = field_space_pat.match(line)
        if m <> None:
            state.add_field_space(int(m.group('uid')))
            continue
        m = field_create_pat.match(line)
        if m <> None:
            if not state.add_field(int(m.group('uid')), int(m.group('fid'))):
                replay_lines.append(line)
            continue
        m = region_pat.match(line)
        if m <> None:
            if not state.add_region(int(m.group('iid')), int(m.group('fid')), int(m.group('tid'))):
                replay_lines.append(line)
            continue 
        # Operations
        m = top_task_pat.match(line)
        if m <> None:
            state.add_top_task(int(m.group('tid')), int(m.group('uid')), m.group('name'))
            continue
        m = single_task_pat.match(line)
        if m <> None:
            if not state.add_single_task(int(m.group('ctx')), int(m.group('tid')), int(m.group('uid')), m.group('name')):
                replay_lines.append(line)
            continue
        m = index_task_pat.match(line)
        if m <> None:
            if not state.add_index_task(int(m.group('ctx')), int(m.group('tid')), int(m.group('uid')), m.group('name')):
                replay_lines.append(line)
            continue
        m = mapping_pat.match(line)
        if m <> None:
            if not state.add_mapping(int(m.group('ctx')), int(m.group('uid'))):
                replay_lines.append(line)
            continue
        m = close_pat.match(line)
        if m <> None:
            if not state.add_close(int(m.group('ctx')), int(m.group('uid'))):
                replay_lines.append(line)
            continue
        m = copy_op_pat.match(line)
        if m <> None:
            if not state.add_copy_op(int(m.group('ctx')), int(m.group('uid'))):
                replay_lines.append(line)
            continue
        m = deletion_pat.match(line)
        if m <> None:
            if not state.add_deletion(int(m.group('ctx')), int(m.group('uid'))):
                replay_lines.append(line)
            continue
        m = index_slice_pat.match(line)
        if m <> None:
            if not state.add_index_slice(int(m.group('index')),int(m.group('slice'))):
              replay_lines.append(line)
            continue
        m = slice_slice_pat.match(line)
        if m <> None:
            if not state.add_slice_slice(int(m.group('slice1')),int(m.group('slice2'))):
                replay_lines.append(line)
            continue
        m = slice_point_pat.match(line)
        if m <> None:
            if not state.add_slice_point(int(m.group('slice')),int(m.group('point')), int(m.group('dim')), int(m.group('val1')), int(m.group('val2')), int(m.group('val3'))):
                replay_lines.append(line)
            continue
        m = point_point_pat.match(line)
        if m <> None:
            if not state.add_point_point(int(m.group('point1')),int(m.group('point2'))):
                replay_lines.append(line)
            continue
        # Mapping dependence analysis
        m = requirement_pat.match(line)
        if m <> None:
            if not state.add_requirement(int(m.group('uid')), int(m.group('index')), True if (int(m.group('is_reg')))==1 else False, int(m.group('ispace')), int(m.group('fspace')), int(m.group('tid')), int(m.group('priv')), int(m.group('coher')), int(m.group('redop'))):
                replay_lines.append(line)
            continue
        m = req_field_pat.match(line)
        if m <> None:
            if not state.add_req_field(int(m.group('uid')), int(m.group('index')), int(m.group('fid'))):
                replay_lines.append(line)
            continue
        m = mapping_dep_pat.match(line)
        if m <> None:
            if not state.add_mapping_dependence(int(m.group('ctx')), int(m.group('prev_id')), int(m.group('pidx')), int(m.group('next_id')), int(m.group('nidx')), int(m.group('dtype'))):
                replay_lines.append(line)
            continue
        # Physical dependence analysis
        m = task_inst_req_pat.match(line)
        if m <> None:
            if not state.add_instance_requirement(int(m.group('uid')), int(m.group('idx')), int(m.group('index'))):
                replay_lines.append(line)
            continue
        # Physical Analysis
        m = event_event_pat.match(line)
        if m <> None:
            state.add_event_dependence(int(m.group('idone')), int(m.group('genone')), int(m.group('idtwo')), int(m.group('gentwo')))
            continue
        m = implicit_event_pat.match(line)
        if m <> None:
            state.add_implicit_dependence(int(m.group('idone')), int(m.group('genone')), int(m.group('idtwo')), int(m.group('gentwo')))
            continue
        m = op_event_pat.match(line)
        if m <> None:
            if not state.add_op_events(int(m.group('uid')), int(m.group('startid')), int(m.group('startgen')), int(m.group('termid')), int(m.group('termgen'))):
                replay_lines.append(line)
            continue
        m = copy_event_pat.match(line)
        if m <> None:
            if not state.add_copy_events(int(m.group('srcman')), int(m.group('dstman')), int(m.group('index')), int(m.group('field')), int(m.group('tree')), int(m.group('startid')), int(m.group('startgen')), int(m.group('termid')), int(m.group('termgen')), int(m.group('redop')), m.group('mask')):
                replay_lines.append(line)
            continue
        # Physical instance usage
        m = physical_inst_pat.match(line)
        if m <> None:
            if not state.add_physical_instance(int(m.group('iid')), int(m.group('mid')), int(m.group('index')), int(m.group('field')), int(m.group('tid'))):
                replay_lines.append(line)
            continue
        m = physical_reduc_pat.match(line)
        if m <> None:
            if not state.add_reduction_instance(int(m.group('iid')), int(m.group('mid')), int(m.group('index')), int(m.group('field')), int(m.group('tid')), True if (int(m.group('fold')) == 1) else False, int(m.group('indirect'))):
                replay_lines.append(line)
            continue
        m = op_user_pat.match(line)
        if m <> None:
            if not state.add_op_user(int(m.group('uid')), int(m.group('idx')), int(m.group('iid'))):
                replay_lines.append(line)
            continue
        # If we made it here then we failed to match
        matches = matches - 1
        print "Skipping unmatched line: "+line
    log.close()
    # Now see if we have lines that need to be replayed
    while len(replay_lines) > 0:
        to_delete = set()
        for line in replay_lines:
            m = processor_pat.match(line)
            if m <> None:
                if state.add_processor(int(m.group('pid')), int(m.group('util')), int(m.group('kind'))):
                    to_delete.add(line)
                continue
            m = proc_mem_pat.match(line)
            if m <> None:
                if state.set_proc_mem(int(m.group('pid')), int(m.group('mid')), int(m.group('band')), int(m.group('lat'))):
                    to_delete.add(line)
                continue
            m = mem_mem_pat.match(line)
            if m <> None:
                if state.set_mem_mem(int(m.group('mone')), int(m.group('mtwo')), int(m.group('band')), int(m.group('lat'))):
                    to_delete.add(line)
                continue
            m = index_part_pat.match(line)
            if m <> None:
                if state.add_index_partition(int(m.group('pid')), int(m.group('uid')), True if (int(m.group('disjoint'))) == 1 else False, int(m.group('color'))):
                    to_delete.add(line)
                continue
            m = index_subspace_pat.match(line)
            if m <> None:
                if state.add_index_subspace(int(m.group('pid')), int(m.group('uid')), int(m.group('color'))):
                    to_delete.add(line)
                continue
            m = single_task_pat.match(line)
            if m <> None:
                if state.add_single_task(int(m.group('ctx')), int(m.group('tid')), int(m.group('uid')), m.group('name')):
                    to_delete.add(line)
                continue
            m = index_task_pat.match(line)
            if m <> None:
                if state.add_index_task(int(m.group('ctx')), int(m.group('tid')), int(m.group('uid')), m.group('name')):
                    to_delete.add(line)
                continue
            m = mapping_pat.match(line)
            if m <> None:
                if state.add_mapping(int(m.group('ctx')), int(m.group('uid'))):
                    to_delete.add(line)
                continue
            m = close_pat.match(line)
            if m <> None:
                if state.add_close(int(m.group('ctx')), int(m.group('uid'))):
                    to_delete.add(line)
                continue
            m = copy_op_pat.match(line)
            if m <> None:
                if state.add_copy_op(int(m.group('ctx')), int(m.group('uid'))):
                    to_delete.add(line)
                continue
            m = slice_point_pat.match(line)
            if m <> None:
                if state.add_slice_point(int(m.group('slice')),int(m.group('point')), int(m.group('dim')), int(m.group('val1')), int(m.group('val2')), int(m.group('val3'))):
                    to_delete.add(line)
                continue
            m = point_point_pat.match(line)
            if m <> None:
                if state.add_point_point(int(m.group('point1')),int(m.group('point2'))):
                    to_delete.add(line)
                continue
            m = requirement_pat.match(line)
            if m <> None:
                if state.add_requirement(int(m.group('uid')), int(m.group('index')), True if (int(m.group('is_reg')))==1 else False, int(m.group('ispace')), int(m.group('fspace')), int(m.group('tid')), int(m.group('priv')), int(m.group('coher')), int(m.group('redop'))):
                    to_delete.add(line)
                continue
            m = req_field_pat.match(line)
            if m <> None:
                if state.add_req_field(int(m.group('uid')), int(m.group('index')), int(m.group('fid'))):
                    to_delete.add(line)
                continue
            m = mapping_dep_pat.match(line)
            if m <> None:
                if state.add_mapping_dependence(int(m.group('ctx')), int(m.group('prev_id')), int(m.group('pidx')), int(m.group('next_id')), int(m.group('nidx')), int(m.group('dtype'))):
                    to_delete.add(line)
                continue
            m = task_inst_req_pat.match(line)
            if m <> None:
                if state.add_instance_requirement(int(m.group('uid')), int(m.group('idx')), int(m.group('index'))):
                    to_delete.add(line)
                continue
            m = op_event_pat.match(line)
            if m <> None:
                if state.add_op_events(int(m.group('uid')), int(m.group('startid')), int(m.group('startgen')), int(m.group('termid')), int(m.group('termgen'))):
                    to_delete.add(line)
                continue
            m = copy_event_pat.match(line)
            if m <> None:
                if state.add_copy_events(int(m.group('srcman')), int(m.group('dstman')), int(m.group('index')), int(m.group('field')), int(m.group('tree')), int(m.group('startid')), int(m.group('startgen')), int(m.group('termid')), int(m.group('termgen')), int(m.group('redop')), m.group('mask')):
                    to_delete.add(line)
                continue
            m = physical_inst_pat.match(line)
            if m <> None:
                if state.add_physical_instance(int(m.group('iid')), int(m.group('mid')), int(m.group('index')), int(m.group('field')), int(m.group('tid'))):
                    to_delete.add(line)
                continue
            m = physical_reduc_pat.match(line)
            if m <> None:
                if state.add_reduction_instance(int(m.group('iid')), int(m.group('mid')), int(m.group('index')), int(m.group('field')), int(m.group('tid')), True if (int(m.group('fold')) == 1) else False, int(m.group('indirect'))):
                    to_delete.add(line)
                continue
            m = op_user_pat.match(line)
            if m <> None:
                if state.add_op_user(int(m.group('uid')), int(m.group('idx')), int(m.group('iid'))):
                    to_delete.add(line)
                continue
        # Now check to make sure we actually did something
        # If not then we're not making forward progress which is bad
        if len(to_delete) == 0:
            print "ERROR: NO PROGRESS PARSING! BUG IN LEGION SPY LOGGING ASSUMPTIONS!"
            assert False
        # Now delete any lines to delete and go again until we're done
        for line in to_delete:
            replay_lines.remove(line)
    return matches

# EOF

