# Copyright (c) 2005-2007 The Regents of The University of Michigan
# Copyright (c) 2009 Advanced Micro Devices, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

from MemObject import MemObject
from m5.params import *
from m5.proxy import *

class RubyTester(MemObject):
    type = 'RubyTester'
    cxx_header = "cpu/testers/rubytest/RubyTester.hh"
    num_cpus = Param.Int("number of cpus / RubyPorts")
    cpuDataPort = VectorMasterPort("the cpu data cache ports")
    cpuInstPort = VectorMasterPort("the cpu inst cache ports")
    checks_to_complete = Param.Int(100, "checks to complete")
    deadlock_threshold = Param.Int(50000, "how often to check for deadlock")
    wakeup_frequency = Param.Int(10, "number of cycles between wakeups")
    check_flush = Param.Bool(False, "check cache flushing")
    system = Param.System(Parent.any, "System we belong to")
    trace_path = Param.String("the path to trace file")
    #arbitration_scheme = Param.String("TDM_1");
    #coalesced_cacheline = Param.Int(2, "maximal size of coalesced request, in cacheline")
    #region_bits = Param.Int(9, "the offset bit width within a region, assume block size = 64B and 16 blocks in one region")
