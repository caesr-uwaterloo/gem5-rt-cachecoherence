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

#include "base/random.hh"
#include "cpu/testers/rubytest/Check.hh"
#include "debug/RubyTest.hh"
#include "mem/ruby/common/SubBlock.hh"
#include "mem/ruby/system/System.hh"
#include "cpu/testers/rubytest/Trace.hh"

typedef RubyTester::SenderState SenderState;
std::map<Check::coreWarpPairType, Check::pendingEntry> Check::pendingAddrWarpMap;

Check::Check(const Address &address, const Address &pc, int _num_writers,
             int _num_readers, RubyTester *_tester)
    : m_num_writers(_num_writers), m_num_readers(_num_readers),
      m_tester_ptr(_tester) {
  m_status = TesterStatus_Idle;

  pickValue();
  pickInitiatingNode();
  changeAddress(address);
  m_pc = pc;
  m_access_mode = RubyAccessMode(random_mt.random(0, RubyAccessMode_NUM - 1));
  m_store_count = 0;

  // Similar to a single MSHR table entry
  pendingPackets = new PacketPtr[m_num_writers]();
}

/* First check for pending requests and then do the needful */

void Check::initiate(int cpuID, bool isRead, unsigned int warpID, unsigned long ts, bool coalesced,bool& issued) {
  DPRINTF(RubyTest, "initiating cpuid : %s\n", cpuID);
  debugPrint();

  // currently no protocols support prefetches
  if (false && (random_mt.random(0, 0xf) == 0)) {
    initiatePrefetch(); // Prefetch from random processor
  }

  if (m_tester_ptr->getCheckFlush() && (random_mt.random(0, 0xff) == 0)) {
    initiateFlush(); // issue a Flush request from random processor
  }

  if (m_status == TesterStatus_Idle) {
    issued = initiateAction(cpuID, isRead, warpID, ts, coalesced);
  } else if (m_status == TesterStatus_Ready) {
    initiateCheck();
  } else {
    // Pending - do nothing
    DPRINTF(RubyTest,
            "initiating action/check - failed: action/check is pending\n");
  }
}

void Check::initiatePrefetch() {
  DPRINTF(RubyTest, "initiating prefetch\n");

  int index = random_mt.random(0, m_num_readers - 1);
  MasterPort *port = m_tester_ptr->getReadableCpuPort(index);

  Request::Flags flags;
  flags.set(Request::PREFETCH);

  Packet::Command cmd;

  // 1 in 8 chance this will be an exclusive prefetch
  if (random_mt.random(0, 0x7) != 0) {
    cmd = MemCmd::ReadReq;

    // if necessary, make the request an instruction fetch
    if (m_tester_ptr->isInstReadableCpuPort(index)) {
      flags.set(Request::INST_FETCH);
    }
  } else {
    cmd = MemCmd::WriteReq;
    flags.set(Request::PF_EXCLUSIVE);
  }

  // Prefetches are assumed to be 0 sized
  Request *req =
      new Request(m_address.getAddress(), 0, flags, m_tester_ptr->masterId(),
                  curTick(), m_pc.getAddress());
  req->setThreadContext(index, 0);

  PacketPtr pkt = new Packet(req, cmd);
  // despite the oddity of the 0 size (questionable if this should
  // even be allowed), a prefetch is still a read and as such needs
  // a place to store the result
  uint8_t *data = new uint8_t[1];
  pkt->dataDynamic(data);

  // push the subblock onto the sender state.  The sequencer will
  // update the subblock on the return
  pkt->senderState = new SenderState(m_address, req->getSize());

  if (port->sendTimingReq(pkt)) {
    DPRINTF(RubyTest, "successfully initiated prefetch.\n");
  } else {
    // If the packet did not issue, must delete
    delete pkt->senderState;
    delete pkt->req;
    delete pkt;

    DPRINTF(RubyTest, "prefetch initiation failed because Port was busy.\n");
  }
}

void Check::initiateFlush() {

  DPRINTF(RubyTest, "initiating Flush\n");

  int index = random_mt.random(0, m_num_writers - 1);
  MasterPort *port = m_tester_ptr->getWritableCpuPort(index);

  Request::Flags flags;

  Request *req =
      new Request(m_address.getAddress(), CHECK_SIZE, flags,
                  m_tester_ptr->masterId(), curTick(), m_pc.getAddress());

  Packet::Command cmd;

  cmd = MemCmd::FlushReq;

  PacketPtr pkt = new Packet(req, cmd);

  // push the subblock onto the sender state.  The sequencer will
  // update the subblock on the return
  pkt->senderState = new SenderState(m_address, req->getSize());

  if (port->sendTimingReq(pkt)) {
    DPRINTF(RubyTest, "initiating Flush - successful\n");
  }
}

void Check::initiateActionRR() {
  DPRINTF(RubyTest, "initiating Action RR style\n");
  assert(m_status == TesterStatus_Idle);

  for (unsigned int i = 0; i < m_num_writers; i++) {
    int index = i;
    MasterPort *port = m_tester_ptr->getWritableCpuPort(index);

    Request::Flags flags;

    // Create the particular address for the next byte to be written
    Address writeAddr(m_address.getAddress() + m_store_count);

    // Stores are assumed to be 1 byte-sized
    Request *req =
        new Request(writeAddr.getAddress(), 1, flags, m_tester_ptr->masterId(),
                    curTick(), m_pc.getAddress());

    req->setThreadContext(index, 0);
    Packet::Command cmd;

    // 1 out of 8 chance, issue an atomic rather than a write
    // if ((random() & 0x7) == 0) {
    //     cmd = MemCmd::SwapReq;
    // } else {
    cmd = MemCmd::WriteReq;
    // }

    PacketPtr pkt = new Packet(req, cmd);
    uint8_t *writeData = new uint8_t[1];
    *writeData = m_value + m_store_count;
    pkt->dataDynamic(writeData);

    DPRINTF(RubyTest, "data 0x%x check 0x%x\n", *(pkt->getConstPtr<uint8_t>()),
            *writeData);

    // push the subblock onto the sender state.  The sequencer will
    // update the subblock on the return
    pkt->senderState = new SenderState(writeAddr, req->getSize());

    if (port->sendTimingReq(pkt)) {
      DPRINTF(RubyTest, "initiating action - successful\n");
      DPRINTF(RubyTest, "status before action update: %s\n",
              (TesterStatus_to_string(m_status)).c_str());
      m_status = TesterStatus_Action_Pending;
    } else {
      // If the packet did not issue, must delete
      // Note: No need to delete the data, the packet destructor
      // will delete it
      delete pkt->senderState;
      delete pkt->req;
      delete pkt;

      DPRINTF(RubyTest, "failed to initiate action - sequencer not ready\n");
    }

    DPRINTF(RubyTest, "status after action update: %s\n",
            (TesterStatus_to_string(m_status)).c_str());
  }
}


bool Check::initiateAction(int cpuID, bool isRead, unsigned int warpID, unsigned long ts, bool coalesced) {

  DPRINTF(RubyTest, "initiating Action from cpuID: %d (%s)\n", cpuID,
       cpuID == 0 ? "GPU" :
      (cpuID == 1 ? "FPGA" : "CPU"));

  assert(m_status == TesterStatus_Idle);

  int index = -1;
  if (cpuID != -1) {
    index = cpuID;
  } else {
    index = random_mt.random(0, m_num_writers - 1);
  }

  MasterPort *port = m_tester_ptr->getWritableCpuPort(index);

  Request::Flags flags;

  // Create the particular address for the next byte to be written
  Address writeAddr(m_address.getAddress() + m_store_count);

  // Stores are assumed to be 1 byte-sized
  Request *req =
      new Request(writeAddr.getAddress(), 1, flags, m_tester_ptr->masterId(),
                  curTick(), m_pc.getAddress());

  //req->setCoalesced(coalesced);
  req->setThreadContext(index, 0);
  Packet::Command cmd;

  if (isRead) {
    cmd = MemCmd::ReadReq;
  } else {
    cmd = MemCmd::WriteReq;
  }

  PacketPtr pkt = new Packet(req, cmd);
  uint8_t *writeData = new uint8_t[1];
  *writeData = m_value + m_store_count;
  pkt->dataDynamic(writeData);
  pkt->isSharedData = true;

  // push the subblock onto the sender state.  The sequencer will
  // update the subblock on the return
  pkt->senderState = new SenderState(writeAddr, req->getSize());

	bool canIssue = false;

	DPRINTF(RubyTest, "CHECKING PENDING ADDR WARP MAP FOR WARP: %d\n", warpID);

	coreWarpPairType cw = std::make_pair(cpuID, warpID);
	if (pendingAddrWarpMap.find(cw) == pendingAddrWarpMap.end()) {
		DPRINTF(RubyTest, "NO PENDING ADDRESS FROM CPU: %d, CAN PROCEED\n", cpuID);
		canIssue = true;
		pendingEntry pe;
		pe.ts = ts;
		pe.pendingAddr.push_back(pkt->getAddr());
		pe.completeAddr.push_back(false);
		pendingAddrWarpMap.insert(std::pair<coreWarpPairType, pendingEntry>(cw, pe));
		DPRINTF(RubyTest, "ENTERED ADDRESS IN PENDING ADDR WARP MAP, size: %d\n", pendingAddrWarpMap.size());
	}
	else {
		pendingEntry pe = pendingAddrWarpMap[cw];
		if (pe.pendingAddr.size() == 0) {
			assert (pe.completeAddr.size() == 0);
			DPRINTF(RubyTest, "NO PENDING ADDRESS FROM CPU: %d, CAN PROCEED\n", cpuID);
			canIssue = true;
			pendingAddrWarpMap[cw].ts = ts;
			pendingAddrWarpMap[cw].pendingAddr.push_back(pkt->getAddr());
			pendingAddrWarpMap[cw].completeAddr.push_back(false);
		}
		/*
		 * Enable for GPU accesses
		else {	
		// Check if it is from the same warp
			DPRINTF(RubyTest, "PENDING ADDRESS FROM CPU: %d, WARP: %d, CHECKING UNCOALESCED ACCESSES\n", cpuID, warpID);
			if (pe.ts == ts) {
				// Uncoalesced accesses, add it and set canIssue to true
				assert(pe.pendingAddr.size() < WARP_SIZE);
				pendingAddrWarpMap[cw].pendingAddr.push_back(pkt->getAddr());
				pendingAddrWarpMap[cw].completeAddr.push_back(false);
				canIssue = true;
				DPRINTF(RubyTest, "SIMULTANEOUS ADDRESS NOT COALESCED, CAN PROCEED\n");
			}
		}
		*/
	}

	if (!canIssue) {
		//m_status = TesterStatus_Action_Pending;
		DPRINTF(RubyTest, "CANNOT ISSUE ADDR: %lx \n", pkt->getAddr());
		delete pkt->senderState;
		delete pkt->req;
		delete pkt;
		return false;	
	}
	else {
		port->sendTimingReq(pkt);
		return true;
		/*
		if (port->sendTimingReq(pkt)) {	
			DPRINTF(RubyTest, "CAN ISSUE ADDR: %lx \n", pkt->getAddr());
			return true;
		}
		else {
			DPRINTF(RubyTest, "COULD NOT ISSUE ADDR: %lx \n", pkt->getAddr());
			m_status = TesterStatus_Action_Pending;
			return false;
		}
		*/
	}
	return false;
}

void Check::initiateCheck() {
  DPRINTF(RubyTest, "Initiating Check\n");
  assert(m_status == TesterStatus_Ready);

  int index = random_mt.random(0, m_num_readers - 1);
  MasterPort *port = m_tester_ptr->getReadableCpuPort(index);

  Request::Flags flags;

  // If necessary, make the request an instruction fetch
  if (m_tester_ptr->isInstReadableCpuPort(index)) {
    flags.set(Request::INST_FETCH);
  }

  // Checks are sized depending on the number of bytes written
  Request *req =
      new Request(m_address.getAddress(), CHECK_SIZE, flags,
                  m_tester_ptr->masterId(), curTick(), m_pc.getAddress());

  req->setThreadContext(index, 0);
  PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
  uint8_t *dataArray = new uint8_t[CHECK_SIZE];
  pkt->dataDynamic(dataArray);

  // push the subblock onto the sender state.  The sequencer will
  // update the subblock on the return
  pkt->senderState = new SenderState(m_address, req->getSize());

  if (port->sendTimingReq(pkt)) {
    DPRINTF(RubyTest, "initiating check - successful\n");
    DPRINTF(RubyTest, "status before check update: %s\n",
            TesterStatus_to_string(m_status).c_str());
    m_status = TesterStatus_Check_Pending;
  } else {
    // If the packet did not issue, must delete
    // Note: No need to delete the data, the packet destructor
    // will delete it
    delete pkt->senderState;
    delete pkt->req;
    delete pkt;

    DPRINTF(RubyTest, "failed to initiate check - cpu port not ready\n");
  }

  DPRINTF(RubyTest, "status after check update: %s\n",
          TesterStatus_to_string(m_status).c_str());
}

void Check::performCallback(NodeID proc, SubBlock *data, Cycles curTime) {

  Address address = data->getAddress();
  std::ignore = address;
  // This isn't exactly right since we now have multi-byte checks
  //  assert(getAddress() == address);

  assert(getAddress().getLineAddress() == address.getLineAddress());
  assert(data != NULL);

	DPRINTF(RubyTest, "Callback for nodeID: %d\n", proc);
  DPRINTF(RubyTest, "RubyTester Callback\n");
  debugPrint();

  if (TRACE) {
		
		// ANI: What about multiple simultaneous warps issuing accesses to same addresses
		// Check if only one access is generated or multiple accesses to same address?

		// Go through the pendingAddrWarpMap, and check each address
		coreWarpPairType cw = std::make_pair((unsigned int)-1, (unsigned int)-1);
		DPRINTF(RubyTest, "PENDING ADDR WARP MAP SIZE: %d\n", pendingAddrWarpMap.size());
		for (std::map<coreWarpPairType, pendingEntry>::iterator it = pendingAddrWarpMap.begin(); it != pendingAddrWarpMap.end(); it++) {
			if (it->first.first != proc) continue;
			pendingEntry pe = it->second;
			for (unsigned int i = 0; i<pe.pendingAddr.size(); i++) {
				DPRINTF(RubyTest, "Comparing: %lx %lx\n", pe.pendingAddr[i] & ~0x3F, address.getAddress() & ~0x3F);
				if ((pe.pendingAddr[i] & ~0x3F) == (address.getAddress() & ~0x3F)) {
					it->second.completeAddr[i] = true;
					cw = it->first;
					DPRINTF(RubyTest, "Removing address from pendingAddr for core: %d warp ID: %d\n", cw.first, cw.second);
					// Remove this break to remove any pending accesses from *same* core to *address*
					break;
				}
			}
		}

		assert(cw.first != (unsigned int)-1 || cw.second != (unsigned int)-1);


		for (std::map<coreWarpPairType, pendingEntry>::iterator it = pendingAddrWarpMap.begin(); it != pendingAddrWarpMap.end(); it++) {
			if (it->first.first == proc) {
				bool allComplete = true;
				for (unsigned int i = 0; i<it->second.completeAddr.size(); i++) {
					if (it->second.completeAddr[i] == false) {
						allComplete = false;
						break;
					}
				}
				if (allComplete) {
					it->second.completeAddr.clear();
					it->second.pendingAddr.clear();
					m_status = TesterStatus_Idle;
					DPRINTF(RubyTest, "All requests completed, deleting for cpu id: %d, %d\n", it->first.first, it->first.second);
				}
			}
		}
		
		// ANI: here I would change testerStatus to Idle only if all requests for warp has finished
    //m_status = TesterStatus_Idle;
    m_tester_ptr->incrementCheckCompletions();
    pickValue();
  } else {
    if (m_status == TesterStatus_Action_Pending) {
      DPRINTF(RubyTest, "Action callback write value: %d, currently %d\n",
              (m_value + m_store_count), data->getByte(0));
      // Perform store one byte at a time
      data->setByte(0, (m_value + m_store_count));
      m_store_count++;

      if (m_store_count == CHECK_SIZE) {
        m_status = TesterStatus_Ready;
      } else {
        m_status = TesterStatus_Idle;
      }

      DPRINTF(RubyTest, "Action callback return data now %d\n",
              data->getByte(0));
    } else if (m_status == TesterStatus_Check_Pending) {
      DPRINTF(RubyTest, "Check callback\n");
      // Perform load/check
      for (int byte_number = 0; byte_number < CHECK_SIZE; byte_number++) {
        DPRINTF(RubyTest,
                "\n m_value: %d, byte_number: %d, data->getByte: %d \n",
                m_value, byte_number, data->getByte(byte_number));
        if (uint8_t(m_value + byte_number) != data->getByte(byte_number)) {
          // cache_approximate - the value read will be a stale value and it is
          // acceptable. So no need to abort
          // panic("Action/check failure: proc: %d address: %s data: %s "
          //        "byte_number: %d m_value+byte_number: %d byte: %d %s"
          //        "Time: %d\n",
          //        proc, address, data, byte_number,
          //        (int)m_value + byte_number,
          //        (int)data->getByte(byte_number), *this, curTime);

          DPRINTF(RubyTest,
                  "\nStale value : Action/check failure: proc: %d address: %s "
                  "data: %s "
                  "byte_number: %d m_value+byte_number: %d byte: %d %s"
                  "Time: %d\n",
                  proc, address, data, byte_number, (int)m_value + byte_number,
                  (int)data->getByte(byte_number), *this, curTime);
        }
      }
      DPRINTF(RubyTest, "Action/check success\n");
      debugPrint();

      // successful check complete, increment complete
      m_tester_ptr->incrementCheckCompletions();

      m_status = TesterStatus_Idle;
      pickValue();

    } else {
      panic("Unexpected TesterStatus: %s proc: %d data: %s m_status: %s "
            "time: %d\n",
            *this, proc, data, m_status, curTime);
    }
  }
  DPRINTF(RubyTest, "proc: %d, Address: 0x%x\n", proc,
          getAddress().getLineAddress());
  DPRINTF(RubyTest, "Callback done\n");
  debugPrint();
}

void Check::changeAddress(const Address &address) {
  assert(m_status == TesterStatus_Idle || m_status == TesterStatus_Ready);
  m_status = TesterStatus_Idle;
  m_address = address;
  m_store_count = 0;
}

void Check::pickValue() {
  assert(m_status == TesterStatus_Idle);
  m_status = TesterStatus_Idle;
  m_value = random_mt.random(0, 0xff); // One byte
  m_store_count = 0;
}

void Check::pickInitiatingNode() {
  assert(m_status == TesterStatus_Idle || m_status == TesterStatus_Ready);
  m_status = TesterStatus_Idle;
  m_initiatingNode = (random_mt.random(0, m_num_writers - 1));
  // DPRINTF(RubyTest, "picked initiating node %d\n", m_initiatingNode);
  m_store_count = 0;
}

void Check::print(std::ostream &out) const {
  out << "[" << m_address << ", value: " << (int)m_value
      << ", status: " << m_status << ", initiating node: " << m_initiatingNode
      << ", store_count: " << m_store_count << "]" << std::flush;
}

void Check::debugPrint() {
  DPRINTF(
      RubyTest,
      "[%#x, value: %d, status: %s, initiating node: %d, store_count: %d]\n",
      m_address.getAddress(), (int)m_value,
      TesterStatus_to_string(m_status).c_str(), m_initiatingNode,
      m_store_count);
}
