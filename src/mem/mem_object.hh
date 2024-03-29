/*
 * Copyright (c) 2012 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Ron Dreslinski
 *          Andreas Hansson
 */

/**
 * @file
 * MemObject declaration.
 */

#ifndef __MEM_MEM_OBJECT_HH__
#define __MEM_MEM_OBJECT_HH__

#include "mem/port.hh"
#include "params/MemObject.hh"
#include "sim/clocked_object.hh"

/**
 * The MemObject class extends the ClockedObject with accessor functions
 * to get its master and slave ports.
 */
class MemObject : public ClockedObject {
public:
  typedef MemObjectParams Params;
  const Params *params() const { return dynamic_cast<const Params *>(_params); }

  MemObject(const Params *params);

  /**
   * Get a master port with a given name and index. This is used at
   * binding time and returns a reference to a protocol-agnostic
   * base master port.
   *
   * @param if_name Port name
   * @param idx Index in the case of a VectorPort
   *
   * @return A reference to the given port
   */
  virtual BaseMasterPort &getMasterPort(const std::string &if_name,
                                        PortID idx = InvalidPortID);

  /**
   * Get a slave port with a given name and index. This is used at
   * binding time and returns a reference to a protocol-agnostic
   * base master port.
   *
   * @param if_name Port name
   * @param idx Index in the case of a VectorPort
   *
   * @return A reference to the given port
   */
  virtual BaseSlavePort &getSlavePort(const std::string &if_name,
                                      PortID idx = InvalidPortID);
};

#endif //__MEM_MEM_OBJECT_HH__
