/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
 * Copyright (c) 2009 Advanced Micro Devices, Inc.
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
 */

#ifndef __CPU_RUBYTEST_CHECK_HH__
#define __CPU_RUBYTEST_CHECK_HH__

#include <iostream>
#include <map>
#include "cpu/testers/rubytest/RubyTester.hh"
#include "mem/protocol/RubyAccessMode.hh"
#include "mem/protocol/TesterStatus.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/Global.hh"

class SubBlock;

const int CHECK_SIZE_BITS = 2;
const int CHECK_SIZE = (1 << CHECK_SIZE_BITS);

class Check {
public:
  Check(const Address &address, const Address &pc, int _num_writers,
        int _num_readers, RubyTester *_tester);

  void initiate(int cpuID, bool read, unsigned int warpID, unsigned long ts, bool coalesced, bool& issued); // Does Action or Check or nether, bool
                                       // read = false for write operationsn
  void performCallback(NodeID proc, SubBlock *data, Cycles curTime);
  const Address &getAddress() { return m_address; }
  void changeAddress(const Address &address);

  void print(std::ostream &out) const;

	struct pendingEntry {
		unsigned long ts;
		std::vector<Addr> pendingAddr;
		std::vector<bool> completeAddr;

		pendingEntry(): pendingAddr(0), completeAddr(0){
			ts = 0;
		}
	};
	
	typedef std::pair<unsigned int,unsigned int> coreWarpPairType;
	static std::map<coreWarpPairType, pendingEntry> pendingAddrWarpMap;

private:
  void initiateFlush();
  void initiatePrefetch();
  bool initiateAction(int cpuID, bool isRead, unsigned int warpID, unsigned long ts, bool coalesced);
  void initiateActionRR(); // For Real-time purposes. Generates requests from
                           // all the cores at the same timestamp and waits for
                           // k slots before issuing next
  void initiateCheck();

  void pickValue();
  void pickInitiatingNode();

  void debugPrint();

  TesterStatus m_status;
  uint8_t m_value;
  int m_store_count;
  NodeID m_initiatingNode;
  Address m_address;
  Address m_pc;
  RubyAccessMode m_access_mode;
  int m_num_writers;
  int m_num_readers;
  RubyTester *m_tester_ptr;

  PacketPtr *pendingPackets; // Pending packets per core, indexed by core id
};

inline std::ostream &operator<<(std::ostream &out, const Check &obj) {
  obj.print(out);
  out << std::flush;
  return out;
}

#endif // __CPU_RUBYTEST_CHECK_HH__
