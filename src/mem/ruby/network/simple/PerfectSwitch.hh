/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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

/*
 * Perfect switch, of course it is perfect and no latency or what so
 * ever. Every cycle it is woke up and perform all the necessary
 * routings that must be done. Note, this switch also has number of
 * input ports/output ports and has a routing table as well.
 */

#ifndef __MEM_RUBY_NETWORK_SIMPLE_PERFECTSWITCH_HH__
#define __MEM_RUBY_NETWORK_SIMPLE_PERFECTSWITCH_HH__

#include <iostream>
#include <string>
#include <vector>
#include <string>
#include <deque>

#include "mem/ruby/common/Consumer.hh"
#include "mem/ruby/common/Global.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/system/System.hh"
#include "cpu/testers/rubytest/Trace.hh"
#include "debug/RubyNetwork.hh"

class MessageBuffer;
class NetDest;
class SimpleNetwork;
class Switch;

struct LinkOrder {
  int m_link;
  int m_value;
};

struct ReqStructType {
	int coreID;
	bool isRead;
	Address a;
	int pos;
};

bool operator<(const LinkOrder &l1, const LinkOrder &l2);
bool decreasingSort(const ReqStructType& a, const ReqStructType& b);

class PerfectSwitch : public Consumer {
public:
  PerfectSwitch(SwitchID sid, Switch *, uint32_t);
  ~PerfectSwitch();

	enum phaseEnumType {COLLECTION, REORDER, BROADCAST};
	enum bcastEnumType {RESP, WB, REQ};
	enum reqEnumType {MEM_REQ, MEM_RESP, WB_RESP};

  std::string name() { return csprintf("PerfectSwitch-%i", m_switch_id); }

  void init(SimpleNetwork *);
  void addInPort(const std::vector<MessageBuffer *> &in);
  void addOutPort(const std::vector<MessageBuffer *> &out,
                  const NetDest &routing_table_entry);

  int getInLinks() const { return m_in.size(); }
  int getOutLinks() const { return m_out.size(); }

  void wakeup();
  void storeEventInfo(int info);
  void storeEventInfo(int link_id, int vnet);


	// arbitration schemes
	void cilkuMCSArb();
	void depArb();

  void regStats(std::string name);
  void clearStats();
  void collateStats();
  void print(std::ostream &out) const;
  SwitchID returnSwitchID() { return m_switch_id; }

  int getID() { return 10 + m_switch_id; }

  bool isStartDemandSlotNew() {
    // depends on the slot width
    if (g_system_ptr->curCycle() % SLOT_WIDTH == 0)
      return true;
    return false;
  }

	bool isStartDemandSlotNew(Cycles curCycle) {
		if (lastRecordedCycle > curCycle) return false;
		if ((curCycle - lastRecordedCycle)%SLOT_WIDTH == 0) return true;
		return false;
	}

	void setLastRecordCycle(Cycles c) {
		lastRecordedCycle = c;
	}

  int getSlotOwnerNew() {
    unsigned int slot_period = (getTotalDedicatedSlots()) * SLOT_WIDTH;
    unsigned int curCycleModSlotPeriod = g_system_ptr->curCycle() % slot_period;
    int owner = curCycleModSlotPeriod / SLOT_WIDTH;
    DPRINTF(RubyNetwork, "owner %s dedicatedslots %s dedHRT %s dedSRT %s \n",
            owner, getTotalDedicatedSlots(), getTotalDedicatedHRTSlots(),
            getNumberOfSRTCores());

    DPRINTF(RubyNetwork, "#wakeup %s %s \n",
            owner < getTotalDedicatedHRTSlots(),
            ((owner - getTotalDedicatedHRTSlots()) < getNumberOfSRTCores()));

    if (owner < getTotalDedicatedHRTSlots()) {
      DPRINTF(RubyNetwork, "#wakeup HRT \n");
      // if owner < |Allocated HRT slots|, then it is one of the HRT slots
      return AB_SLOT_ALLOCATION[owner];
    } else if ((owner - getTotalDedicatedHRTSlots()) < getNumberOfSRTCores()) {
      DPRINTF(RubyNetwork, "#wakeup SRT \n");
      // if owner - |Allocated HRT slots| < |SRT|,
      // then it is one of the dedicated SRT slots
      return CD_CORES[owner - getTotalDedicatedHRTSlots()];
    }
    return -1;
  }

  // checks if the current owner is the last slot of SRT.
  bool isCurrentSRTReserveSlot() {

    if (getNumberOfSRTCores() == 0) {
      return false;
    }
    unsigned int slot_period = (getTotalDedicatedSlots()) * SLOT_WIDTH;
    unsigned int curCycleModSlotPeriod = g_system_ptr->curCycle() % slot_period;
    int owner = curCycleModSlotPeriod / SLOT_WIDTH;
    if (owner == getTotalDedicatedSlots() - 1) {
      return true;
		}
    else {
      return false;
		}
  }

  bool isTherePendingDemandReq(int incoming);
  bool isTherePendingDemandReqSpecial(int incoming);
  bool isTherePendingWBReq(int incoming);
  bool isTherePendingWBToNRT(int incoming);
  bool isTherePendingWBToHRT(int incoming);
  bool isTherePendingResponse(int incoming);
  int isTherePendingResponse(int incoming, int destination);
  int isTherePendingResponseToMemory(int incoming);
  bool isTherePendingResponse(int incoming, std::vector<std::string> types);
  int isThereCacheToCachePendingResponseTo(int incoming, int &sender_core);
  int isThereCacheToCachePendingResponseFrom(int sender_core, int &dst_core);
  Address getPendingDemandReqAddr(int incoming);
  Address getPendingWBAddr(int incoming);
  Address getPendingNCWBAddr(int incoming);
  bool removeAllPendingResponseFromMEMToNRT(Address addr);
  void removeAllPendingWBToNRT(Address addr, int coreID);
  void removeAllPendingCWBToAddress(Address addr, int coreID);

	void enqueueOps(int coreID, int pos, Address a, reqEnumType r, bool isRead); 
	bool isReadReqType(int coreID);
	bool isSharedReqType(int coreID);
	void scheduleSlack(int slackSlots);
	void reorderDelayRead(std::deque<ReqStructType>& reqList, std::deque<ReqStructType>& reorderedList);
	void reorderDelayWrite(std::deque<ReqStructType>& reqList, std::deque<ReqStructType>& reorderedList);

	std::deque<ReqStructType>& getRespList() { return _respList;}
	std::deque<ReqStructType>& getWBList() { return _wbList;}
	std::deque<ReqStructType>& getReqList() {return _reqList;}

	int getRespListSize() { return _respList.size(); }
	int getReqListSize() { return _reqList.size(); }
	int getWBListSize() { return _wbList.size(); }

private:
  Stats::Scalar m_unused_slack;
  Stats::Scalar m_total_slack;
	Stats::Scalar m_total_collection;
	Stats::Scalar m_reordered_opp;
	Stats::Scalar m_slack_opp;
	Stats::Scalar m_slack_and_reordered_opp;
	phaseEnumType currPhase;
	bcastEnumType bcastType;


	Cycles lastRecordedCycle;

  // Private copy constructor and assignment operator
  PerfectSwitch(const PerfectSwitch &obj);
  PerfectSwitch &operator=(const PerfectSwitch &obj);

  void operateVnet(int vnet);

  void operateVnet(int incoming, int vnet, int pos);

  SwitchID m_switch_id;

	// Slack utilization
	std::vector<int> slackUtil;

  // vector of queues from the components
  std::vector<std::vector<MessageBuffer *> > m_in;
  std::vector<std::vector<MessageBuffer *> > m_out;

  std::vector<NetDest> m_routing_table;
  std::vector<LinkOrder> m_link_order;

  uint32_t m_virtual_networks;
  int m_round_robin_start;
  int m_wakeups_wo_switch;

  SimpleNetwork *m_network_ptr;
  std::vector<int> m_pending_message_count;
  std::vector<std::vector<int> > m_pending_message_count_mtx;

  int m_srt_idx;
  int m_slot_owner;
  bool m_is_slack_slot;
  unsigned long long m_last_time_srt_slot_used;

  bool msgSent = false;

  void increSRTIdxRoundRobin() {
    m_srt_idx++;
    if (m_srt_idx >= getNumberOfSRTCores()) {
      m_srt_idx = 0;
    }
  }

  std::map<int, unsigned long long> m_pending_req_map;

  std::vector<bool> m_token;

  bool getToken(int core) {
    //m_token[core] = !m_token[core];
    DPRINTF(RubyNetwork, "#getToken m_token[%s] %s \n", core, m_token[core]);
    return m_token[core];
  }

  void updateToken(int core) { m_token[core] = !m_token[core]; }

  bool getTokenSpecial() {
    // This can only be used in special case for HRT slots
    unsigned int slot_period =
        (getTotalDedicatedSlots() + getNumberOfHRTCores()) * SLOT_WIDTH;
    unsigned int curCycleModSlotPeriod = g_system_ptr->curCycle() % slot_period;
    int owner = curCycleModSlotPeriod / SLOT_WIDTH;

    DPRINTF(RubyNetwork, "#getTokenSpecial %s \n", owner % 2);
    // 0 is REQ
    // 1 is WB
    return owner % 2;
  }

	std::deque<ReqStructType> _reqList;
	std::deque<ReqStructType> _respList;
	std::deque<ReqStructType> _wbList;
};

inline std::ostream &operator<<(std::ostream &out, const PerfectSwitch &obj) {
  obj.print(out);
  out << std::flush;
  return out;
}

#endif // __MEM_RUBY_NETWORK_SIMPLE_PERFECTSWITCH_HH__
