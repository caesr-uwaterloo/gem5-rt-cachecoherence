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

#include <algorithm>

#include "base/cast.hh"
#include "base/random.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/simple/PerfectSwitch.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/network/simple/SimpleNetwork.hh"
#include "mem/ruby/network/simple/Switch.hh"
#include "mem/ruby/slicc_interface/NetworkMessage.hh"
#include "mem/ruby/system/System.hh"

#include "mem/ruby/network/Network.hh"

#include "sim/debug.hh"

#include <iostream>
#include <sstream>
#include <string>
#include <iterator>

#include "mem/protocol/RequestMsg.hh"
#include "mem/protocol/ResponseMsg.hh"

using namespace std;

const int PRIORITY_SWITCH_LIMIT = 128;

// Operator for helper class
bool operator<(const LinkOrder &l1, const LinkOrder &l2) {
  return (l1.m_value < l2.m_value);
}

bool decreasingSort(const ReqStructType& a, const ReqStructType& b) {
	return (a.pos > b.pos);
}

PerfectSwitch::PerfectSwitch(SwitchID sid, Switch *sw, uint32_t virt_nets)
    : Consumer(sw), slackUtil(numCores, -1){
  m_switch_id = sid;
  m_round_robin_start = 0;
  m_wakeups_wo_switch = 0;
  m_virtual_networks = virt_nets;

	currPhase = COLLECTION;
}

void PerfectSwitch::init(SimpleNetwork *network_ptr) {
  m_network_ptr = network_ptr;

  for (int i = 0; i < m_virtual_networks; ++i) {
    m_pending_message_count.push_back(0);
  }

	DPRINTF(RubyNetwork, "M_VIRTUAL_NETWORKS: %d, m_in.size(): %d\n", m_virtual_networks, m_in.size());
  m_pending_message_count_mtx.resize(m_virtual_networks,
                                     vector<int>(m_in.size(), 0));

  DPRINTF(RubyNetwork, "init: switch_id:%s \n", m_switch_id);
  if (m_switch_id == (numCores+1)) {
    DPRINTF(RubyNetwork, "init: switch_id:%s schedule %s\n", m_switch_id,
            SLOT_WIDTH);
		DPRINTF(RubyNetwork, "SCHEDULE EVENT 1 SLOT WIDTH\n");

		// If arb is conventional TDM
    scheduleEvent(Cycles(SLOT_WIDTH));

		// if arb is dep arbiter, schedule next cycle
		//scheduleEvent(Cycles(1));

    m_is_slack_slot = false;
    m_srt_idx = 0;
    m_slot_owner = getHRTCoreOfIndex(getSlotOwnerNew());

    for (int i = 0; i < getNumberOfCores(); i++) {
      m_token.push_back(false);
      DPRINTF(RubyNetwork, "init: m_token[%s]:%s\n", i, m_token[i]);
    }
  }
}

void PerfectSwitch::addInPort(const vector<MessageBuffer *> &in) {
  NodeID port = m_in.size();
  m_in.push_back(in);

  for (int i = 0; i < in.size(); ++i) {
    if (in[i] != nullptr) {
      in[i]->setConsumer(this);

      string desc =
          csprintf("[Queue from port %s %s %s to PerfectSwitch]",
                   to_string(m_switch_id), to_string(port), to_string(i));

      in[i]->setDescription(desc);
      in[i]->setIncomingLink(port);
      in[i]->setVnet(i);
    }
  }
}

void PerfectSwitch::addOutPort(const vector<MessageBuffer *> &out,
                               const NetDest &routing_table_entry) {
  // Setup link order
  LinkOrder l;
  l.m_value = 0;
  l.m_link = m_out.size();
  m_link_order.push_back(l);

  // Add to routing table
  m_out.push_back(out);
  m_routing_table.push_back(routing_table_entry);
}

PerfectSwitch::~PerfectSwitch() {}

void PerfectSwitch::operateVnet(int vnet) {
  MsgPtr msg_ptr;
  NetworkMessage *net_msg_ptr = NULL;

  // This is for round-robin scheduling
  int incoming = m_round_robin_start;
  DPRINTF(RubyNetwork, "operateVnet(vnet) : vnet %s \n", vnet);
  DPRINTF(RubyNetwork, "m_round_robin_start: %s \n", m_round_robin_start);
  DPRINTF(RubyNetwork, "m_in.size(): %s \n", m_in.size());

  m_round_robin_start++;
  if (m_round_robin_start >= m_in.size()) {
    m_round_robin_start = 0;
  }

  DPRINTF(RubyNetwork, "m_pending_message_count[vnet]: %s %s \n", vnet,
          m_pending_message_count[vnet]);
  if (m_pending_message_count[vnet] > 0) {
    // for all input ports, use round robin scheduling
    for (int counter = 0; counter < m_in.size(); counter++) {
      DPRINTF(RubyNetwork, "Counter: %d, incoming: %d, m_in.size(): %d\n",
              counter, incoming, m_in.size());
      // Round robin scheduling
      incoming++;
      if (incoming >= m_in.size()) {
        incoming = 0;
      }

      // temporary vectors to store the routing results
      vector<LinkID> output_links;
      vector<NetDest> output_link_destinations;

      DPRINTF(RubyNetwork, "Size of m_in[incoming] %d %d %d", incoming, vnet,
              m_in[incoming].size());

      // Is there a message waiting?
			// TODO: Why is the comparison with vnet? Shouldn't it be 0?
      if (m_in[incoming].size() <= vnet) {
        continue;
      }

      MessageBuffer *buffer = m_in[incoming][vnet];
      if (buffer == nullptr) {
        continue;
      }

#ifdef PRED_ARB
		while (buffer->isReady()) {
#else
		bool isReady = false;
    isReady = buffer->isReady();
		bool isBusWon = false;
    while (isReady) {
#endif
        DPRINTF(RubyNetwork, "incoming: %d\n", incoming);

        // Peek at message
        msg_ptr = buffer->peekMsgPtr();
        net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
        DPRINTF(RubyNetwork, "Message: %s\n", (*net_msg_ptr));

        output_links.clear();
        output_link_destinations.clear();
        NetDest msg_dsts = net_msg_ptr->getInternalDestination();

        // Unfortunately, the token-protocol sends some
        // zero-destination messages, so this assert isn't valid
        // assert(msg_dsts.count() > 0);
        //

        DPRINTF(RubyNetwork, "msg_dsts %s \n", msg_dsts);
        DPRINTF(RubyNetwork, "m_link_order.size(): %s\n", m_link_order.size());
        assert(m_link_order.size() == m_routing_table.size());
        assert(m_link_order.size() == m_out.size());

        if (m_network_ptr->getAdaptiveRouting()) {
          if (m_network_ptr->isVNetOrdered(vnet)) {
            // Don't adaptively route
            DPRINTF(RubyNetwork, "This network is ordered\n");
            for (int out = 0; out < m_out.size(); out++) {
              m_link_order[out].m_link = out;
              m_link_order[out].m_value = 0;
            }
          } else {
            // Find how clogged each link is
            for (int out = 0; out < m_out.size(); out++) {
              int out_queue_length = 0;
              for (int v = 0; v < m_virtual_networks; v++) {
                out_queue_length += m_out[out][v]->getSize();
              }
              int value = (out_queue_length << 8) | random_mt.random(0, 0xff);
              m_link_order[out].m_link = out;
              m_link_order[out].m_value = value;
            }

            // Look at the most empty link first
            sort(m_link_order.begin(), m_link_order.end());
          }
        }

        for (int i = 0; i < m_routing_table.size(); i++) {
          // pick the next link to look at
          int link = m_link_order[i].m_link;
          NetDest dst = m_routing_table[link];

					DPRINTF(RubyNetwork, "msg_dsts: %d, dst: %d\n", msg_dsts.getSize(), dst.getSize());

          if (!msg_dsts.intersectionIsNotEmpty(dst))
            continue;

          // Remember what link we're using
          DPRINTF(RubyNetwork, "link: %s dst: %s msg_dsts: %s\n", link, dst,
                  msg_dsts);

          output_links.push_back(link);

          // Need to remember which destinations need this message in
          // another vector.  This Set is the intersection of the
          // routing_table entry and the current destination set.  The
          // intersection must not be empty, since we are inside "if"

          output_link_destinations.push_back(msg_dsts.AND(dst));
        }

        // Check for resources - for all outgoing queues
        bool enough = true;
        for (int i = 0; i < output_links.size(); i++) {
          int outgoing = output_links[i];

          if (!m_out[outgoing][vnet]->areNSlotsAvailable(1))
            enough = false;

          DPRINTF(RubyNetwork, "Checking if node is blocked ..."
                               "outgoing: %d, vnet: %d, enough: %d\n",
                  outgoing, vnet, enough);
        }

        // There were not enough resources
        if (!enough) {
          scheduleEvent(Cycles(1));
          DPRINTF(RubyNetwork, "Can't deliver message since a node "
                               "is blocked\n");
          DPRINTF(RubyNetwork, "Message: %s\n", (*net_msg_ptr));
          break; // go to next incoming port
        }

        MsgPtr unmodified_msg_ptr;

        if (output_links.size() > 1) {
          // If we are sending this message down more than one link
          // (size>1), we need to make a copy of the message so each
          // branch can have a different internal destination we need
          // to create an unmodified MsgPtr because the MessageBuffer
          // enqueue func will modify the message

          // This magic line creates a private copy of the message
          unmodified_msg_ptr = msg_ptr->clone();
        }

        // Dequeue msg
        buffer->dequeue();
        m_pending_message_count[vnet]--;

        // The idea is to not allow self router operations.
        // So, for this check if incoming == outgoing for all router nodes
        // excluding crossbar
        // For cross bar, iterate over all the nodes and send the msg_ptr
        DPRINTF(RubyNetwork, "M switch id: %d\n", m_switch_id);
        for (int i = 0; i < output_links.size(); i++) {
          int outgoing = output_links[i];

          if (i > 0) {
            // create a private copy of the unmodified message
            msg_ptr = unmodified_msg_ptr->clone();
          }

          // Change the internal destination set of the message so it
          // knows which destinations this link is responsible for.
          net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
          net_msg_ptr->getInternalDestination() = output_link_destinations[i];

          DPRINTF(RubyNetwork,
                  "Last enqueued time: %d, delayed ticks: %d, getTime: %d\n",
                  msg_ptr->getLastEnqueueTime(), msg_ptr->getDelayedTicks(),
                  msg_ptr->getTime());

          // Enqeue msg
          // The value in the comparison should reflect the number of cores +
          // memory
          // 6 means, 4 cores with 1 shared L2 and memory
          //
          if (m_switch_id < getNumberOfCores() + 1) {
            if (incoming != outgoing) {
              DPRINTF(RubyNetwork, "Enqueuing net msg from "
                                   "inport[%d][%d] to outport [%d][%d].\n",
                      incoming, vnet, outgoing, vnet);

              m_out[outgoing][vnet]->enqueue(msg_ptr);
            }
          } else {
            DPRINTF(RubyNetwork, "Enqueuing net msg from "
                                 "inport[%d][%d] to outport [%d][%d].\n",
                    incoming, vnet, outgoing, vnet);

            m_out[outgoing][vnet]->enqueue(msg_ptr);
          }
        }
#ifdef PRED_ARB
      }
#else
			isReady = (m_switch_id == (numCores+1))? false: buffer->isReady();
			isBusWon = true;
		}
     if(isBusWon && (m_switch_id == (numCores+1)) ){        
			for(int counter = 0; counter < m_in.size(); counter++){          
				MessageBuffer *buffer = m_in[counter][vnet];          
				if (buffer == nullptr) {            
					continue;          
				}          
				if(buffer->isReady()) scheduleEvent(Cycles(1));        
			}        
			return;      
		} 
#endif
    }
  }
}

void PerfectSwitch::operateVnet(int incoming, int vnet, int pos) {
  MsgPtr msg_ptr;
  NetworkMessage *net_msg_ptr = NULL;

  // This is for round-robin scheduling
  DPRINTF(RubyNetwork, "incoming %s vnet %s pos %s\n", incoming, vnet, pos);
  DPRINTF(RubyNetwork, "m_in.size(): %s \n", m_in.size());

  m_round_robin_start++;
  if (m_round_robin_start >= m_in.size()) {
    m_round_robin_start = 0;
  }

  DPRINTF(RubyNetwork, "m_pending_message_count[vnet]: %s %s \n", vnet,
          m_pending_message_count[vnet]);

  DPRINTF(RubyNetwork,
          "m_pending_message_count_mtx[incoming][vnet]: %S %s %s \n", incoming,
          vnet, m_pending_message_count_mtx[incoming][vnet]);

  if (m_pending_message_count[vnet] > 0) {
    DPRINTF(RubyNetwork, "incoming: %d, m_in.size(): %d\n", incoming,
            m_in.size());

    // temporary vectors to store the routing results
    vector<LinkID> output_links;
    vector<NetDest> output_link_destinations;

    DPRINTF(RubyNetwork, "Size of m_in[incoming] %d %d %d", incoming, vnet,
            m_in[incoming].size());

    // Is there a message waiting?
    if (m_in[incoming].size() <= vnet) {
      return;
    }

    MessageBuffer *buffer = m_in[incoming][vnet];
    if (buffer == nullptr) {
      return;
    }

		bool isReady = false;
		if (pos == -1) {
			isReady = buffer->isReady();
		}
		else {
			isReady = buffer->isReady(pos);
		}

		while(isReady) {

      DPRINTF(RubyNetwork, "incoming: %d\n", incoming);

      // Peek at message
      if (pos == -1) {
        msg_ptr = buffer->peekMsgPtr();
      } else {
        msg_ptr = buffer->peekMsgPtr(pos);
      }

      net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
      DPRINTF(RubyNetwork, "Message: %s\n", (*net_msg_ptr));

      output_links.clear();
      output_link_destinations.clear();
      NetDest msg_dsts = net_msg_ptr->getInternalDestination();

      // Unfortunately, the token-protocol sends some
      // zero-destination messages, so this assert isn't valid
      // assert(msg_dsts.count() > 0);
      //

      DPRINTF(RubyNetwork, "msg_dsts %s \n", msg_dsts);
      DPRINTF(RubyNetwork, "m_link_order.size(): %s\n", m_link_order.size());
      assert(m_link_order.size() == m_routing_table.size());
      assert(m_link_order.size() == m_out.size());

      for (int i = 0; i < m_routing_table.size(); i++) {
        // pick the next link to look at
        int link = m_link_order[i].m_link;
        NetDest dst = m_routing_table[link];

        if (!msg_dsts.intersectionIsNotEmpty(dst))
          continue;

        // Remember what link we're using
        DPRINTF(RubyNetwork, "link: %s dst: %s msg_dsts: %s\n", link, dst,
                msg_dsts);

        output_links.push_back(link);

        // Need to remember which destinations need this message in
        // another vector.  This Set is the intersection of the
        // routing_table entry and the current destination set.  The
        // intersection must not be empty, since we are inside "if"
        output_link_destinations.push_back(msg_dsts.AND(dst));
      }

      // Check for resources - for all outgoing queues
      bool enough = true;
      for (int i = 0; i < output_links.size(); i++) {
        int outgoing = output_links[i];

        if (!m_out[outgoing][vnet]->areNSlotsAvailable(1))
          enough = false;

        DPRINTF(RubyNetwork, "Checking if node is blocked ..."
                             "outgoing: %d, vnet: %d, enough: %d\n",
                outgoing, vnet, enough);
      }

      // There were not enough resources
      if (!enough) {
        scheduleEvent(Cycles(1));
        DPRINTF(RubyNetwork, "Can't deliver message since a node "
                             "is blocked\n");
        DPRINTF(RubyNetwork, "Message: %s\n", (*net_msg_ptr));
        break; // go to next incoming port
      }

      MsgPtr unmodified_msg_ptr;

      if (output_links.size() > 1) {
        // If we are sending this message down more than one link
        // (size>1), we need to make a copy of the message so each
        // branch can have a different internal destination we need
        // to create an unmodified MsgPtr because the MessageBuffer
        // enqueue func will modify the message

        // This magic line creates a private copy of the message
        unmodified_msg_ptr = msg_ptr->clone();
      }

      // Dequeue msg
      if (pos == -1) {
        buffer->dequeue();
      } else {
        buffer->dequeue(pos);
      }

      m_pending_message_count_mtx[incoming][vnet]--;

      m_pending_message_count[vnet]--;

      // The idea is to not allow self router operations.
      // So, for this check if incoming == outgoing for all router
      // nodes excluding crossbar

      // For cross bar, iterate over all the nodes and send the msg_ptr
      DPRINTF(RubyNetwork, "M switch id: %d\n", m_switch_id);
      for (int i = 0; i < output_links.size(); i++) {
        int outgoing = output_links[i];

        if (i > 0) {
          // create a private copy of the unmodified message
          msg_ptr = unmodified_msg_ptr->clone();
        }

        // Change the internal destination set of the message so it
        // knows which destinations this link is responsible for.
        net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
        net_msg_ptr->getInternalDestination() = output_link_destinations[i];

        DPRINTF(RubyNetwork,
                "Last enqueued time: %d, delayed ticks: %d, getTime: %d\n",
                msg_ptr->getLastEnqueueTime(), msg_ptr->getDelayedTicks(),
                msg_ptr->getTime());

        // Enqeue msg
        // The value in the comparison should reflect the number of cores +
        // memory
        // 6 means, 4 cores with 1 shared L2 and memory
        //
        if (m_switch_id < getNumberOfCores() + 1) {
          if (incoming != outgoing) {
            DPRINTF(RubyNetwork, "Enqueuing net msg from "
                                 "inport[%d][%d] to outport [%d][%d].\n",
                    incoming, vnet, outgoing, vnet);

            m_out[outgoing][vnet]->enqueue(msg_ptr);
          }
        } else {
          DPRINTF(RubyNetwork, "Enqueuing net msg from "
                               "inport[%d][%d] to outport [%d][%d].\n",
                  incoming, vnet, outgoing, vnet);
          m_out[outgoing][vnet]->enqueue(msg_ptr);
        }
      }
      isReady = false; // buffer->isReady(pos);
    }
  }
}

void PerfectSwitch::wakeup() {

#ifdef PRED_ARB
  // for switches other than the bus
  if (m_switch_id != (numCores+1)) {
#else
     if (1) {
#endif
    // Give the highest numbered link priority most of the time
    m_wakeups_wo_switch++;
    int highest_prio_vnet = m_virtual_networks - 1;
    int lowest_prio_vnet = 0;
    int decrementer = 1;

    // invert priorities to avoid starvation seen in the component network
    if (m_wakeups_wo_switch > PRIORITY_SWITCH_LIMIT) {
      m_wakeups_wo_switch = 0;
      highest_prio_vnet = 0;
      lowest_prio_vnet = m_virtual_networks - 1;
      decrementer = -1;
    }

    // For all components incoming queues
    for (int vnet = highest_prio_vnet;
         (vnet * decrementer) >= (decrementer * lowest_prio_vnet);
         vnet -= decrementer) {
						DPRINTF(RubyNetwork, "OPERATE VNET 1\n");
      operateVnet(vnet);
    }
    //scheduleEvent(Cycles(1));
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // for switch == 9, bus arbitration
  ///////////////////////////////////////////////////////////////////////////

	cilkuMCSArb();
 
  // ATOMIC requests
	DPRINTF(RubyNetwork, "OPERATE VNET 2\n");
  operateVnet(5);

  // Cache to cache
	DPRINTF(RubyNetwork, "OPERATE VNET 3\n");
  operateVnet(3);
}

void PerfectSwitch::enqueueOps(int coreID, int pos, Address a, reqEnumType r, bool isRead) {
	ReqStructType rt;
	rt.coreID = coreID;
	rt.isRead = isRead;
	rt.pos = pos;
	rt.a = a;

	if (r == MEM_RESP) {
		_respList.push_back(rt);
	}
	else if (r == MEM_REQ) {
		_reqList.push_back(rt);
	}
	else if (r == WB_RESP) {
		_wbList.push_back(rt);
	}
}

bool PerfectSwitch::isReadReqType(int coreID) {
	MessageBuffer *requestQueue = m_in[coreID][2];
	if (!requestQueue->isReady()) {
		return false;
	}

	RequestMsg *reqmsg = safe_cast<RequestMsg*>(requestQueue->peekMsgPtr().get());
	if (CoherenceRequestType_to_string(reqmsg->m_Type) == "GETS" ||CoherenceRequestType_to_string(reqmsg->m_Type) == "GETSNC" ||  CoherenceRequestType_to_string(reqmsg->m_Type) == "SGETS" || CoherenceRequestType_to_string(reqmsg->m_Type) == "GETI") {
		return true;
	}
	return false;	
}

// TODO: Should have just one scheduling logic for everything; not different functions for cilku and dep arb

void PerfectSwitch::cilkuMCSArb() {

	std::vector<int> temp = getEventqDump();
  std::stringstream result;
  std::sort(temp.begin(), temp.end());
  std::copy(temp.begin(), temp.end(), std::ostream_iterator<int>(result, ","));

  DPRINTF(RubyNetwork, "#wakeup --> %s\n", result.str());
  DPRINTF(RubyNetwork,
          "#wakeup --> getSlotOwnerNew(): %s isStartDemandSlotNew: %s\n",
          getSlotOwnerNew(), isStartDemandSlotNew());

  if (isStartDemandSlotNew() && isHRTCore(getSlotOwnerNew())) {
		DPRINTF(RubyNetwork, "SCHEDULE EVENT 2 SLOT WIDTH\n");
		// ANI: Change this SLOT_WIDTH to slots_in_a_period * latency_collect_requests
    scheduleEvent(Cycles(SLOT_WIDTH));
  }
	else {
		scheduleEvent(Cycles(1));
	}

  //if (isSRTCore(getSlotOwnerNew())) {
  // scheduleEvent(Cycles(1));
  //}

  if (isHRTCore(getSlotOwnerNew())) {
    DPRINTF(RubyNetwork, "===> HRT\n");

    if (isStartDemandSlotNew()) {
      m_slot_owner = getHRTCoreOfIndex(getSlotOwnerNew());
      m_is_slack_slot = false;
			// reset slack owner
			slackUtil[m_slot_owner] = -1;
			DPRINTF(RubyNetwork, "RESETTING SLACK UTIL OWNER %d %d %x\n", m_slot_owner, slackUtil[m_slot_owner], &slackUtil[m_slot_owner]);
      int pos = isTherePendingResponse(numCores, m_slot_owner);
			updateToken(m_slot_owner);
			DPRINTF(RubyNetwork, "CURRENT TOKEN: %d\n", getToken(m_slot_owner));

      DPRINTF(RubyNetwork,
              "HRT startslot slot_owner %s pos %s DemReq %s WBReq %s\n",
              m_slot_owner, pos, isTherePendingDemandReq(m_slot_owner),
              isTherePendingWBReq(m_slot_owner));

      if (isTherePendingDemandReq(m_slot_owner) ||
          isTherePendingWBReq(m_slot_owner) || pos != -1) {
        // HRT owner has request

        for (int i : { 0, 1 }) {
          std::ignore = i;
          // DPRINTF(RubyNetwork, "HRT i %s", i);
					DPRINTF(RubyNetwork, "getToken(m_slot_owner)^i: %d, getToken(m_slot_owner): %d, i: %d\n", getToken(m_slot_owner)^i, getToken(m_slot_owner), i); 
          if ((getToken(m_slot_owner)^i) &&
              (isTherePendingDemandReq(m_slot_owner) || pos != -1)) {
            if (pos != -1) {
              // HRT has a response from memory
							DPRINTF(RubyNetwork, "SERVICING DEMAND REQ : %d %d %d\n", getToken(m_slot_owner), i, getToken(m_slot_owner)^i);
						DPRINTF(RubyNetwork, "OPERATE VNET 14\n");
              operateVnet(numCores, 4, pos);
            } else {
              Address addr = getPendingDemandReqAddr(m_slot_owner);
              removeAllPendingResponseFromMEMToNRT(addr);
							DPRINTF(RubyNetwork, "SERVICING DEMAND REQ : %d %d %d\n", getToken(m_slot_owner), i, getToken(m_slot_owner)^i);
						DPRINTF(RubyNetwork, "OPERATE VNET 15\n");
              operateVnet(m_slot_owner, 2, -1);
            }
            break;
          } 
					else if (isTherePendingWBReq(m_slot_owner)) {
            Address addr = getPendingWBAddr(m_slot_owner);
            removeAllPendingWBToNRT(addr, m_slot_owner);
						DPRINTF(RubyNetwork, "SERVICING WB : %d %d %d\n", getToken(m_slot_owner), i, getToken(m_slot_owner)^i);
						DPRINTF(RubyNetwork, "OPERATE VNET 16\n");
            operateVnet(m_slot_owner, 6, -1);
            break;
          }
        }
      } 
			
			else {
        // slack slot
        DPRINTF(RubyNetwork, "#wakeup slackslot\n");
        m_total_slack++;
#ifdef SLACK_SLOT
        m_is_slack_slot = true;
        bool is_slack_used = false;
        int index = m_srt_idx;

#ifdef DSA_SSA
        //DSA_SSA
        bool pendingWBToHRT = false;
        for(int i : E_CORES){
          if(isTherePendingWBToHRT(i)){
            pendingWBToHRT = true;
          }
        }

        if(pendingWBToHRT){
          DPRINTF(RubyNetwork, "slackslot DSA_SSA\n");
          //check first WBs which HRT is asking for

          do{
            DPRINTF(RubyNetwork, "slackslot srt %s WBToHRT %s\n",
                    getSRTCoreOfIndex(m_srt_idx),
                    isTherePendingWBToHRT(getSRTCoreOfIndex(m_srt_idx)));

            if(isTherePendingWBToHRT(getSRTCoreOfIndex(m_srt_idx))){
						DPRINTF(RubyNetwork, "OPERATE VNET 17\n");
              operateVnet(getSRTCoreOfIndex(m_srt_idx), 6, -1);
              is_slack_used = true;
							slackUtil[m_slot_owner] = getSRTCoreOfIndex(m_srt_idx);
							DPRINTF(RubyNetwork, "setting SLACK UTIL %d\n", slackUtil[m_slot_owner]);
              break;
            }
            increSRTIdxRoundRobin();
          }while(index != m_srt_idx);

          if(is_slack_used){
            m_slot_owner = getSRTCoreOfIndex(m_srt_idx);
            increSRTIdxRoundRobin();
            return;
          }
        }
#endif
        int nrt_resp = isTherePendingResponse(numCores, getNRTCoreOfIndex(0));

        DPRINTF(RubyNetwork,
                "slackslot NRT %s nrt_resp %s DemReq %s WBReq %s\n",
                getNRTCoreOfIndex(0), nrt_resp,
                isTherePendingDemandReq(getNRTCoreOfIndex(0)),
                isTherePendingWBReq(getNRTCoreOfIndex(0)));

        // first check for NRT
        if (isTherePendingDemandReq(getNRTCoreOfIndex(0)) || nrt_resp != -1) {

          // There is a Req from a NRT or response from memory to a NRT
          DPRINTF(RubyNetwork, "slackslot NRT %s pos %s DemReq %s WBReq %s\n",
                  getSRTCoreOfIndex(0), nrt_resp,
                  isTherePendingDemandReq(getNRTCoreOfIndex(0)),
                  isTherePendingWBReq(getNRTCoreOfIndex(0)));

          if (nrt_resp != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 18\n");
            operateVnet(numCores, 4, nrt_resp);
            is_slack_used = true;
						slackUtil[m_slot_owner] = getNRTCoreOfIndex(0);
						DPRINTF(RubyNetwork, "setting SLACK UTIL %d\n", slackUtil[m_slot_owner]);
          } else if (isTherePendingDemandReq(getNRTCoreOfIndex(0))) {
						DPRINTF(RubyNetwork, "OPERATE VNET 19\n");
            operateVnet(getNRTCoreOfIndex(0), 2, -1);
						slackUtil[m_slot_owner] = getNRTCoreOfIndex(0);
						DPRINTF(RubyNetwork, "setting SLACK UTIL %d\n", slackUtil[m_slot_owner]);
            is_slack_used = true;
          }
          m_slot_owner = getNRTCoreOfIndex(0);
        } else {
          DPRINTF(RubyNetwork, "CHECKING FOR WBs in non-critical PWB\n");

          // Check for WB from SRTs and HRTs for NRT requested data
          bool pendingWBToNRT = false;
          int HRTCoreID = -1;
          int SRTCoreID = -1;
          for (int i : AB_CORES) {
            if (isTherePendingWBToNRT(i)) {
              pendingWBToNRT = true;
              HRTCoreID = i;
              DPRINTF(RubyNetwork,
                      "FOUND NON-CRITICAL PWB entry in HRT CORE ID %d %d\n",
                      m_slot_owner, HRTCoreID);
              break;
            }
          }

          if (!pendingWBToNRT) {
            for (int i : CD_CORES) {
              if (isTherePendingWBToNRT(i)) {
                pendingWBToNRT = true;
                SRTCoreID = i;
                DPRINTF(RubyNetwork,
                        "FOUND NON-CRITICAL PWB entry in SRT CORE ID %d, %d\n",
                        m_slot_owner, SRTCoreID);
                break;
              }
            }
          }
          DPRINTF(RubyNetwork,
                  "FOUND NON-CRITICAL PWB entry in HRT CORE ID %s, %d, %d\n",
                  pendingWBToNRT, HRTCoreID, SRTCoreID);

          if (pendingWBToNRT) {
            // Check HRT
            if (HRTCoreID != -1) {
              DPRINTF(RubyNetwork,
                      "FOUND NON-CRITICAL PWB entry in HRT CORE ID %d %d\n",
                      m_slot_owner, HRTCoreID);
              Address addr = getPendingNCWBAddr(getHRTCoreOfIndex(HRTCoreID));
							slackUtil[m_slot_owner] = HRTCoreID;
							DPRINTF(RubyNetwork, "SETTING SLACK UTIL %d %d %d %x\n", m_slot_owner, HRTCoreID, slackUtil[m_slot_owner], &slackUtil[m_slot_owner]);

              is_slack_used = true;
              //m_slot_owner = getHRTCoreOfIndex(HRTCoreID);
							DPRINTF(RubyNetwork, "OPERATE VNET 20\n");
              operateVnet(getHRTCoreOfIndex(HRTCoreID), numCores, -1);
              removeAllPendingCWBToAddress(addr, getHRTCoreOfIndex(HRTCoreID));
              DPRINTF(RubyNetwork,
                      "FOUND NON-CRITICAL PWB entry in HRT CORE ID %d %d\n",
                      m_slot_owner, HRTCoreID);
            } else {
              assert(SRTCoreID != -1);
              DPRINTF(RubyNetwork,
                      "FOUND NON-CRITICAL PWB entry in SRT CORE ID %d %d \n",
                      m_slot_owner, SRTCoreID);
              Address addr = getPendingNCWBAddr(SRTCoreID);
              //m_slot_owner = SRTCoreID;
							slackUtil[m_slot_owner] = SRTCoreID;
							DPRINTF(RubyNetwork, "SETTING SLACK UTIL %d %d %d %x\n", m_slot_owner, SRTCoreID, slackUtil[m_slot_owner], &slackUtil[m_slot_owner]);
              // is_slack_used = true;
							DPRINTF(RubyNetwork, "OPERATE VNET 21\n");
              operateVnet(SRTCoreID, numCores, -1);
              removeAllPendingCWBToAddress(addr, SRTCoreID);
              DPRINTF(RubyNetwork,
                      "FOUND NON-CRITICAL PWB entry in SRT CORE ID %d %d \n",
                      m_slot_owner, SRTCoreID);
            }
          } else {
            // normal SRT demand or WB
            do {
              int pos = isTherePendingResponse(numCores, getSRTCoreOfIndex(m_srt_idx));

              DPRINTF(RubyNetwork,
                      "slackslot srt %s pos %s DemReq %s WBReq %s\n",
                      getSRTCoreOfIndex(m_srt_idx), pos,
                      isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)),
                      isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx)));

              if (isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)) ||
                  isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx)) ||
                  pos != -1) {
                for (int i : { 0, 1 }) {
                  std::ignore = i;
                  // DPRINTF(RubyNetwork, "SRT i %s", i);
                  if (getToken(getSRTCoreOfIndex(m_srt_idx)^i) &&
                      (isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)) ||
                       pos != -1)) {
                    if (pos != -1) {
											DPRINTF(RubyNetwork, "OPERATE VNET 22\n");
                      operateVnet(numCores, 4, pos);
                    } else {
                      Address addr = getPendingDemandReqAddr(getSRTCoreOfIndex(m_srt_idx));
                      removeAllPendingResponseFromMEMToNRT(addr);
											DPRINTF(RubyNetwork, "OPERATE VNET 23\n");
                      operateVnet(getSRTCoreOfIndex(m_srt_idx), 2, -1);
                    }
										slackUtil[m_slot_owner] = getSRTCoreOfIndex(m_srt_idx);
										DPRINTF(RubyNetwork, "SETTING SLACK UTIL %d %d %d %x\n", m_slot_owner, getSRTCoreOfIndex(SRTCoreID), slackUtil[m_slot_owner], &slackUtil[m_slot_owner]);
                    is_slack_used = true;
                    break;
                  } else if (isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx))) {

                    Address addr = getPendingWBAddr(getSRTCoreOfIndex(m_srt_idx));
                    removeAllPendingWBToNRT(addr, getSRTCoreOfIndex(m_srt_idx));
										DPRINTF(RubyNetwork, "OPERATE VNET 24\n");
                    operateVnet(getSRTCoreOfIndex(m_srt_idx), 6, -1);
                    is_slack_used = true;
										slackUtil[m_slot_owner] = getSRTCoreOfIndex(m_srt_idx);
										DPRINTF(RubyNetwork, "setting SLACK UTIL %d\n", slackUtil[m_slot_owner]);

										/*
										// Check if this core is sending C2C to another core
										DPRINTF(RubyNetwork, "CHECKING IF SLOT OWNER IS SENDING VIA C2C\n");
          					int pos = -1;
          					pos = isThereCacheToCachePendingResponseFrom(m_srt_idx, dest_core);
          					if (pos != -1) {
            					DPRINTF(RubyNetwork, "DEST CORE FOUND: %d\n", dest_core);
            					DPRINTF(RubyNetwork, "HRT response cache to cache Sender %s\n",
                    	m_slot_owner);
											DPRINTF(RubyNetwork, "OPERATE VNET 30\n");
            					operateVnet(m_slot_owner, 4, pos);
										}
										*/
                    break;
                  }
                }
                if (is_slack_used)
                  break;
              }
              increSRTIdxRoundRobin();
            } while (index != m_srt_idx);

            if (is_slack_used) {
              m_slot_owner = getSRTCoreOfIndex(m_srt_idx);
              increSRTIdxRoundRobin();
            } else {
              m_unused_slack++;
              DPRINTF(RubyNetwork, "#wakeup slackslot unused\n");
            }
          }
        }
#endif
      }
		
    } else {
      // HRT non starting slot
      DPRINTF(RubyNetwork, "HRT non starting slot owner %s\n", m_slot_owner);

			DPRINTF(RubyNetwork, "CHECKING WB\n");
			int sl_owner = m_slot_owner;
			DPRINTF(RubyNetwork, "SLACK UTIL %d %d %x\n", m_slot_owner, slackUtil[m_slot_owner], &slackUtil[m_slot_owner]);
			if (slackUtil[m_slot_owner] != -1) {
				sl_owner = slackUtil[m_slot_owner];	
			}

     	// Why? -> If owner is in M and sees a remote load, wb will not happen at start of slot
     	if (isTherePendingResponse(sl_owner, { "DATA_TO_WB" }) || isTherePendingResponse(sl_owner, { "NO_DATA" }) || 
											isTherePendingResponse(sl_owner, { "DATA_TO_WB_O" })) {
     	  DPRINTF(RubyNetwork, "SLOT OWNER IS DOING A WB\n");
						DPRINTF(RubyNetwork, "OPERATE VNET 25\n");
     	  operateVnet(sl_owner, 4, -1);
     	} else if (isTherePendingDemandReqSpecial(sl_owner)) {
     	  DPRINTF(RubyNetwork, "SLOT OWNER IS PROCESSING A SPECIAL REQ\n");
						DPRINTF(RubyNetwork, "OPERATE VNET 26\n");
     	  operateVnet(sl_owner, 2, -1);
     	} else {
       	int pos = isTherePendingResponse(numCores, sl_owner);
       	if (pos != -1) {
       	  DPRINTF(RubyNetwork, "SLOT OWNER IS GETTING RESPONSE FROM MEMORY\n");
       	  DPRINTF(RubyNetwork, "HRT response slot owner %s\n", sl_owner);
					DPRINTF(RubyNetwork, "OPERATE VNET 27\n");
       	  operateVnet(numCores, 4, pos);
       	}
			
			/*
			//TODO: Revisit this slack logic..
			else if (isTherePendingResponse(numCores)) {
         DPRINTF(RubyNetwork,
                 "SLACK SLOT OWNER IS GETTING RESPONSE FROM MEMORY\n");
         DPRINTF(RubyNetwork, "HRT response non slot owner %s\n",
                 sl_owner);
         scheduleEvent(Cycles(1));
       } 
			*/
			
				else {
         	DPRINTF(RubyNetwork, "CHECKING IF SLOT OWNER IS RECIEVING VIA C2C\n");
         	int sender_core, dest_core;
         	int pos =
             isThereCacheToCachePendingResponseTo(sl_owner, sender_core);
         	if (pos != -1) {
           	DPRINTF(RubyNetwork, "SENDER CORE FOUND: %d\n", sender_core);
           	DPRINTF(RubyNetwork, "HRT response cache to cache %s\n",
           	        sl_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 28\n");
           	operateVnet(sender_core, 4, pos);
           	DPRINTF(RubyNetwork, "SENDER CORE FOUND: %d\n", sender_core);
           	int Data2WBPos = isTherePendingResponseToMemory(sender_core);
           	DPRINTF(RubyNetwork, "CHECKING WB FOR CACHE TO CACHE TXN %d\n",
                   Data2WBPos);
         	  if (Data2WBPos != -1) {
         	    DPRINTF(RubyNetwork, "DOING WB FOR CACHE TO CACHE TXN \n");
							DPRINTF(RubyNetwork, "OPERATE VNET 29\n");
         	    operateVnet(sender_core, 4, Data2WBPos);
         	  }
         	}

         	DPRINTF(RubyNetwork, "CHECKING IF SLOT OWNER IS SENDING VIA C2C\n");
         	pos = -1;
         	pos = isThereCacheToCachePendingResponseFrom(sl_owner, dest_core);
         	if (pos != -1) {
           	DPRINTF(RubyNetwork, "DEST CORE FOUND: %d\n", dest_core);
           	DPRINTF(RubyNetwork, "HRT response cache to cache Sender %s\n",
           	        sl_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 30\n");
           	operateVnet(sl_owner, 4, pos);
           	DPRINTF(RubyNetwork, "SENDER CORE FOUND: %d\n", sl_owner);
           	int Data2WBPos = isTherePendingResponseToMemory(sl_owner);
           	DPRINTF(RubyNetwork, "CHECKING WB FOR CACHE TO CACHE TXN %d\n",
           	        Data2WBPos);
           	if (Data2WBPos != -1) {
           	  DPRINTF(RubyNetwork, "DOING WB FOR CACHE TO CACHE TXN \n");
						DPRINTF(RubyNetwork, "OPERATE VNET 31\n");
           	  operateVnet(sl_owner, 4, Data2WBPos);
           	}
         	}

					// Other cores can send data in any slot if there is no communication with shared memory
					DPRINTF(RubyNetwork, "SEND TO OTHERS");
    			for (int i = 0; i < getNumberOfCores(); i++) {
						pos = -1;
						pos = isThereCacheToCachePendingResponseFrom(i, dest_core);
						if (pos != -1) {
							if (isTherePendingResponseToMemory(i) == -1) {
								// can immediately schedule
						DPRINTF(RubyNetwork, "OPERATE VNET 32\n");
								operateVnet(i, 4, pos);
							}
						}
					}
       	}
     	}
			
    	scheduleEvent(Cycles(1));
    }
  } else {
    // SRT slot frame
    DPRINTF(RubyNetwork, "===> SRT\n");

    if ((isCurrentSRTReserveSlot() && isStartDemandSlotNew())) {
      DPRINTF(RubyNetwork, "===> SRT Reserve Slot\n");
    }

    if (!isCurrentSRTReserveSlot() ||
        (isCurrentSRTReserveSlot() && isStartDemandSlotNew())) {
      // non-reserve slot or reserve slot and SRT starting slot

      int index = m_srt_idx;

      DPRINTF(RubyNetwork, "SRT cutCycle - last_time_used :%s \n",
              g_system_ptr->curCycle() - m_last_time_srt_slot_used);
      if (g_system_ptr->curCycle() - m_last_time_srt_slot_used >= SLOT_WIDTH) {
        bool isUsed = false;
        do {

					updateToken(getSRTCoreOfIndex(m_srt_idx));
          int pos = isTherePendingResponse(numCores, getSRTCoreOfIndex(m_srt_idx));

          DPRINTF(RubyNetwork, "SRT m_srt_idx:%s m_slot_owner:%s pos:%s\n",
                  getSRTCoreOfIndex(m_srt_idx), getSRTCoreOfIndex(m_srt_idx),
                  pos);
          DPRINTF(RubyNetwork, "SRT DemReq:%s WBReq:%s \n",
                  isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)),
                  isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx)));
          if (isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)) ||
              isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx)) || pos != -1) {

            for (int i : { 0, 1 }) {
              std::ignore = i;
              // DPRINTF(RubyNetwork, "SRT i %s", i);
              if (getToken(getSRTCoreOfIndex(m_srt_idx)^i) &&
                  (isTherePendingDemandReq(getSRTCoreOfIndex(m_srt_idx)) ||
                   pos != -1)) {
                if (pos != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 33\n");
                  operateVnet(numCores, 4, pos);
                } else {
                  Address addr = getPendingDemandReqAddr(getSRTCoreOfIndex(m_srt_idx));
                  removeAllPendingResponseFromMEMToNRT(addr);
						DPRINTF(RubyNetwork, "OPERATE VNET 34\n");
                  operateVnet(getSRTCoreOfIndex(m_srt_idx), 2, -1);
                }
                isUsed = true;
                break;
              } else if (isTherePendingWBReq(getSRTCoreOfIndex(m_srt_idx))) {
                Address addr = getPendingWBAddr(getSRTCoreOfIndex(m_srt_idx));
                removeAllPendingWBToNRT(addr, getSRTCoreOfIndex(m_srt_idx));
						DPRINTF(RubyNetwork, "OPERATE VNET 35\n");
                operateVnet(getSRTCoreOfIndex(m_srt_idx), 6, -1);
                isUsed = true;
              }
            }
            if (isUsed)
              break;
          }
          increSRTIdxRoundRobin();
					DPRINTF(RubyNetwork, "STUCK HERE.... %d %d\n", index, m_srt_idx);
        } while (index != m_srt_idx);
					
				DPRINTF(RubyNetwork, "Not STUCK HERE.... %d %d\n", index, m_srt_idx);

        if (isUsed) {
          m_last_time_srt_slot_used = g_system_ptr->curCycle();
          m_slot_owner = getSRTCoreOfIndex(m_srt_idx);
          increSRTIdxRoundRobin();
        }
      } else {
        // SRT frame after SRT core sends Req on SRT frame
        if (isTherePendingResponse(m_slot_owner, { "DATA_TO_WB" })) {
						DPRINTF(RubyNetwork, "OPERATE VNET 36\n");
          operateVnet(m_slot_owner, 4, -1);
        } else if (isTherePendingDemandReqSpecial(m_slot_owner)) {
						DPRINTF(RubyNetwork, "OPERATE VNET 37\n");
          operateVnet(m_slot_owner, 2, -1);
        } else {
          int pos = isTherePendingResponse(numCores, m_slot_owner);
          DPRINTF(RubyNetwork, "SRT m_srt_idx:%s m_slot_owner:%s pos:%s\n",
                  getSRTCoreOfIndex(m_srt_idx), m_slot_owner, pos);
          if (pos != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 38\n");
            operateVnet(numCores, 4, pos);
          } else {
            int sender_core, dest_core;
            int pos =
                isThereCacheToCachePendingResponseTo(m_slot_owner, sender_core);
            if (pos != -1) {
              DPRINTF(RubyNetwork, "SRT response cache to cache %s\n",
                      m_slot_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 39\n");
              operateVnet(sender_core, 4, pos);
              int Data2WBPos = isTherePendingResponseToMemory(sender_core);
              if (Data2WBPos != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 40\n");
                operateVnet(sender_core, 4, Data2WBPos);
              }
            }

            DPRINTF(RubyNetwork, "CHECKING IF SLOT OWNER IS SENDING VIA C2C\n");
            pos = -1;
            pos =
                isThereCacheToCachePendingResponseFrom(m_slot_owner, dest_core);
            if (pos != -1) {
              DPRINTF(RubyNetwork, "DEST CORE FOUND: %d\n", dest_core);
              DPRINTF(RubyNetwork, "HRT response cache to cache Sender %s\n",
                      m_slot_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 41\n");
              operateVnet(m_slot_owner, 4, pos);
              DPRINTF(RubyNetwork, "SENDER CORE FOUND: %d\n", m_slot_owner);
              int Data2WBPos = isTherePendingResponseToMemory(m_slot_owner);
              DPRINTF(RubyNetwork, "CHECKING WB FOR CACHE TO CACHE TXN %d\n",
                      Data2WBPos);
              if (Data2WBPos != -1) {
                DPRINTF(RubyNetwork, "DOING WB FOR CACHE TO CACHE TXN \n");
						DPRINTF(RubyNetwork, "OPERATE VNET 42\n");
                operateVnet(m_slot_owner, 4, Data2WBPos);
              }
            }
          }
        }
      }
    } else {
      // reserve slot and non starting slot
      if (isTherePendingResponse(m_slot_owner, { "DATA_TO_WB" })) {
						DPRINTF(RubyNetwork, "OPERATE VNET 43\n");
        operateVnet(m_slot_owner, 4, -1);
      } else if (isTherePendingDemandReqSpecial(m_slot_owner)) {
						DPRINTF(RubyNetwork, "OPERATE VNET 44\n");
        operateVnet(m_slot_owner, 2, -1);
      } else {
        int pos = isTherePendingResponse(numCores, m_slot_owner);
        DPRINTF(RubyNetwork,
                "SRT Reserve m_srt_idx:%s m_slot_owner:%s pos:%s\n",
                getSRTCoreOfIndex(m_srt_idx), m_slot_owner, pos);
        if (pos != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 45\n");
          operateVnet(numCores, 4, pos);
        } else {
          int sender_core, dest_core;
          int pos =
              isThereCacheToCachePendingResponseTo(m_slot_owner, sender_core);
          if (pos != -1) {
            DPRINTF(RubyNetwork, "SRT response cache to cache %s\n",
                    m_slot_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 46\n");
            operateVnet(sender_core, 4, pos);
            int Data2WBPos = isTherePendingResponseToMemory(sender_core);
            if (Data2WBPos != -1) {
						DPRINTF(RubyNetwork, "OPERATE VNET 47\n");
              operateVnet(sender_core, 4, Data2WBPos);
            }
          }

          DPRINTF(RubyNetwork, "CHECKING IF SLOT OWNER IS SENDING VIA C2C\n");
          pos = -1;
          pos = isThereCacheToCachePendingResponseFrom(m_slot_owner, dest_core);
          if (pos != -1) {
            DPRINTF(RubyNetwork, "DEST CORE FOUND: %d\n", dest_core);
            DPRINTF(RubyNetwork, "HRT response cache to cache Sender %s\n",
                    m_slot_owner);
						DPRINTF(RubyNetwork, "OPERATE VNET 48\n");
            operateVnet(m_slot_owner, 4, pos);
            DPRINTF(RubyNetwork, "SENDER CORE FOUND: %d\n", m_slot_owner);
            int Data2WBPos = isTherePendingResponseToMemory(m_slot_owner);
            DPRINTF(RubyNetwork, "CHECKING WB FOR CACHE TO CACHE TXN %d\n",
                    Data2WBPos);
            if (Data2WBPos != -1) {
              DPRINTF(RubyNetwork, "DOING WB FOR CACHE TO CACHE TXN \n");
						DPRINTF(RubyNetwork, "OPERATE VNET 49\n");
              operateVnet(m_slot_owner, 4, Data2WBPos);
            }
          }
        }
      }
    }
  }
}

bool PerfectSwitch::isTherePendingDemandReq(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingDemandReq incoming %s\n", incoming);
  bool status = false;
  if (incoming == -1)
    return false;
  MessageBuffer *requestQueue = m_in[incoming][2];
  if (!requestQueue->isReady()) {
    return false;
  }

  RequestMsg *reqmsg =
      safe_cast<RequestMsg *>(requestQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#isTherePendingDemandReq reqmsg: %s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "GETM" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETS" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETSNC" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "SGETS" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETMR" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETMO" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETMA" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "UPG" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETI") {

    // I have pending request
    status = true;
  }
  // DPRINTF(RubyNetwork, "#isTherePendingRequestInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#isTherePendingDemandReq status %s\n", status);
  return status;
}

bool PerfectSwitch::isTherePendingDemandReqSpecial(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingDemandReqSpecial incoming %s\n",
          incoming);
  bool status = false;
  MessageBuffer *requestQueue = m_in[incoming][2];
  if (!requestQueue->isReady()) {
    return false;
  }

  NetworkMessage *netmsg =
      safe_cast<NetworkMessage *>(requestQueue->peekMsgPtr().get());
  DPRINTF(RubyNetwork, "#isTherePendingDemandReqSpecial netmsg: %s %s\n",
          *netmsg, netmsg->getDestination().isSidePacket());

  if (netmsg->getDestination().isSidePacket()) {
    // I have pending request
    status = true;
  }
  DPRINTF(RubyNetwork, "#isTherePendingDemandReqSpecial status %s\n", status);
  return status;
}

bool PerfectSwitch::isTherePendingWBReq(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingWBReq incoming %s\n", incoming);

  if (incoming == -1)
    return false;
  bool status = false;
  MessageBuffer *WBQueue = m_in[incoming][6];
  if (!WBQueue->isReady()) {
    return false;
  }

  RequestMsg *reqmsg = safe_cast<RequestMsg *>(WBQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#isTherePendingWBReq reqmsg:%s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "PUTM") {
    // I have pending request
    status = true;
  }
  // DPRINTF(RubyNetwork, "#isTherePendingWBInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#isTherePendingWBReq status %s\n", status);
  return status;
}

bool PerfectSwitch::isTherePendingWBToNRT(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingWBReq incoming %s\n", incoming);

  if (incoming == -1)
    return false;
  bool status = false;
  MessageBuffer *WBQueue = m_in[incoming][numCores];
  if (!WBQueue->isReady()) {
    return false;
  }

  RequestMsg *reqmsg = safe_cast<RequestMsg *>(WBQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#isTherePendingWBReq reqmsg:%s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "PUTM") {
    // I have pending request
    DPRINTF(RubyNetwork, "FOUND NON-CRITICAL PWB entry \n");
    status = true;
  }
  // DPRINTF(RubyNetwork, "#isTherePendingWBInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#isTherePendingWBReq status %s\n", status);
  return status;
}


/*
bool PerfectSwitch::isTherePendingWBToHRT(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingWBToHRT incoming %s\n", incoming);

  bool status = false;
  MessageBuffer *WBQueue = m_in[incoming][6];
  if (!WBQueue->isReady()) {
    return false;
  }

  RequestMsg *reqmsg = safe_cast<RequestMsg *>(WBQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#isTherePendingWBToHRT reqmsg:%s %s %s\n", reqmsg,
          reqmsg->m_Dependent, CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "PUTM" &&
      isHRTCore(reqmsg->m_Dependent.getNum())) {
    // I have pending request
    status = true;
  }
  // DPRINTF(RubyNetwork, "#isTherePendingWBInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#isTherePendingWBToHRT status %s\n", status);
  return status;
}
*/


bool PerfectSwitch::isTherePendingResponse(int incoming,
                                           std::vector<string> types) {
  DPRINTF(RubyNetwork, "#isTherePendingResponse incoming %s\n", incoming);
  bool status = false;
  MessageBuffer *responseQueue = m_in[incoming][4];
  if (!responseQueue->isReady()) {
    return false;
  }
  ResponseMsg *resmsg =
      safe_cast<ResponseMsg *>(responseQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#isTherePendingResponse resmsg:%s %s\n", resmsg,
          CoherenceResponseType_to_string(resmsg->m_Type));

  for (string str : types) {
    if (CoherenceResponseType_to_string(resmsg->m_Type) == str) {
      status = true;
    }
  }
  DPRINTF(RubyNetwork, "#isTherePendingResponse status %s\n", status);
  return status;
}

// check if there is pending response on vnet 4 destined to current owner
int PerfectSwitch::isTherePendingResponse(int incoming, int destination) {
  DPRINTF(RubyNetwork, "#isTherePendingResponse incoming %s destination %s\n",
          incoming, destination);

  if (destination == -1) {
    return -1;
  }

  MessageBuffer *responseQueue = m_in[incoming][4];
  MachineID ownerID = { MachineType_L1Cache, (unsigned int)destination };

  int pos = -1;
  for (int i = 0; i < responseQueue->getQSize(); i++) {
    if (responseQueue->isReady(i)) {
      MsgPtr msg_ptr = responseQueue->peekMsgPtr(i);
      NetworkMessage *net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
      DPRINTF(RubyNetwork, "ownerID: %s, destination: %s, isElement %s\n",
              ownerID, net_msg_ptr->getDestination(),
              net_msg_ptr->getDestination().isElement(ownerID));

      if (net_msg_ptr->getDestination().isElement(ownerID)) {
        DPRINTF(RubyNetwork, "index %i ...PENDING RESPONSE FOUND!\n", i);
        pos = i;
        break;
      } else {
        DPRINTF(RubyNetwork, "...PENDING RESPONSE NOT FOUND!\n");
      }
    }
  }
  DPRINTF(RubyNetwork, "#isTherePendingResponse pos %s\n", pos);
  return pos;
}

int PerfectSwitch::isTherePendingResponseToMemory(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingResponseToMemory incoming %s\n",
          incoming);

  if (incoming == -1) {
    return -1;
  }

  MessageBuffer *responseQueue = m_in[incoming][4];
  MachineID ownerID = { MachineType_Directory, (NodeID)0 };

  int pos = -1;
  for (int i = 0; i < responseQueue->getQSize(); i++) {
    if (responseQueue->isReady(i)) {
      MsgPtr msg_ptr = responseQueue->peekMsgPtr(i);
      NetworkMessage *net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
      DPRINTF(RubyNetwork, "ownerID: %s, destination: %s, isElement %s\n",
              ownerID, net_msg_ptr->getDestination(),
              net_msg_ptr->getDestination().isElement(ownerID));

      std::vector<NodeID> destVecTmp =
          net_msg_ptr->getDestination().getAllDest();

      for (unsigned int j = 0; j < destVecTmp.size(); j++) {
        DPRINTF(RubyNetwork, "DEST VEC TMP AT J: %d %s\n", j, destVecTmp.at(j));
      }

      if (net_msg_ptr->getDestination().isElement(ownerID)) {
        DPRINTF(RubyNetwork, "index %i ...PENDING RESPONSE FOUND!\n", i);
        pos = i;
        break;
      } else {
        DPRINTF(RubyNetwork, "...PENDING RESPONSE NOT FOUND!\n");
      }
    }
  }
  DPRINTF(RubyNetwork, "#isTherePendingResponse pos %s\n", pos);
  return pos;
}

bool PerfectSwitch::isTherePendingResponse(int incoming) {
  DPRINTF(RubyNetwork, "#isTherePendingResponse incoming %s\n", incoming);

  if (incoming == -1)
    return false;
  MessageBuffer *responseQueue = m_in[incoming][4];

  if (responseQueue->getQSize() && responseQueue->isReady()) {
    MsgPtr msg_ptr = responseQueue->peekMsgPtr();
    NetworkMessage *net_msg_ptr = safe_cast<NetworkMessage *>(msg_ptr.get());
    std::ignore = net_msg_ptr;
    DPRINTF(RubyNetwork, "#isTherePendingResponse destination: %s\n",
            net_msg_ptr->getDestination());
    DPRINTF(RubyNetwork, "...PENDING RESPONSE FOUND!\n");
    return true;
  } else {
    DPRINTF(RubyNetwork, "...PENDING RESPONSE NOT FOUND!\n");
    return false;
  }
}

int PerfectSwitch::isThereCacheToCachePendingResponseTo(int incoming,
                                                        int &sender_core) {
  DPRINTF(RubyNetwork, "#isThereCacheToCachePendingResponseTo incoming %s\n",
          incoming);

  if (incoming == -1)
    return false;

  for (int core : AB_CORES) {
    if (core != incoming) {
      int tmp = isTherePendingResponse(core, incoming);
      if (tmp != -1) {
        sender_core = core;
        return tmp;
      }
    }
  }

  for (int core : CD_CORES) {
    if (core != incoming) {
      int tmp = isTherePendingResponse(core, incoming);
      if (tmp != -1) {
        sender_core = core;
        return tmp;
      }
    }
  }

  sender_core = -1;
  return -1;
}

int PerfectSwitch::isThereCacheToCachePendingResponseFrom(int sender_core,
                                                          int &dst_core) {

  DPRINTF(RubyNetwork, "#isThereCacheToCachePendingResponseFrom sendingCore %s\n",
          sender_core);

  if (sender_core == -1)
    return false;

  for (int core : AB_CORES) {
    if (core != sender_core) {
      int tmp = isTherePendingResponse(sender_core, core);
      if (tmp != -1) {
        dst_core = core;
        return tmp;
      }
    }
  }

  for (int core : CD_CORES) {
    if (core != sender_core) {
      int tmp = isTherePendingResponse(sender_core, core);
      if (tmp != -1) {
        dst_core = core;
        return tmp;
      }
    }
  }

	for (int core: E_CORES) {
		if (core != sender_core) {
			int tmp = isTherePendingResponse(sender_core, core);
			if (tmp != -1) {
				dst_core = core;
				return tmp;
			}
		}
	}

  dst_core = -1;
  return -1;

}

Address PerfectSwitch::getPendingDemandReqAddr(int incoming) {
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr incoming %s\n", incoming);
  Address addr = Address();
  if (incoming == -1)
    return addr;
  MessageBuffer *requestQueue = m_in[incoming][2];
  if (!requestQueue->isReady()) {
    return addr;
  }

  RequestMsg *reqmsg =
      safe_cast<RequestMsg *>(requestQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr reqmsg: %s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "GETM" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETS" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETSNC" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "SGETS" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETMO" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "GETMA" ||
      CoherenceRequestType_to_string(reqmsg->m_Type) == "UPG") {

    // I have pending request
    addr = reqmsg->getAddr();
  }
  // DPRINTF(RubyNetwork, "#isTherePendingRequestInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr addr %s\n", addr);
  return addr;
}

Address PerfectSwitch::getPendingWBAddr(int incoming) {
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr incoming %s\n", incoming);
  Address addr = Address();
  if (incoming == -1)
    return addr;
  MessageBuffer *requestQueue = m_in[incoming][6];
  if (!requestQueue->isReady()) {
    return addr;
  }

  RequestMsg *reqmsg =
      safe_cast<RequestMsg *>(requestQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr reqmsg: %s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "PUTM") {
    // I have pending request
    addr = reqmsg->getAddr();
  }
  // DPRINTF(RubyNetwork, "#isTherePendingRequestInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr addr %s\n", addr);
  return addr;
}

Address PerfectSwitch::getPendingNCWBAddr(int incoming) {
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr incoming %s\n", incoming);
  Address addr = Address();
  if (incoming == -1)
    return addr;
  MessageBuffer *requestQueue = m_in[incoming][numCores];
  if (!requestQueue->isReady()) {
    return addr;
  }

  RequestMsg *reqmsg =
      safe_cast<RequestMsg *>(requestQueue->peekMsgPtr().get());

  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr reqmsg: %s %s\n", *reqmsg,
          CoherenceRequestType_to_string(reqmsg->m_Type));

  if (CoherenceRequestType_to_string(reqmsg->m_Type) == "PUTM") {
    // I have pending request
    addr = reqmsg->getAddr();
  }
  // DPRINTF(RubyNetwork, "#isTherePendingRequestInRequestQueue :%s, %s\n",
  //        CoherenceRequestType_to_string(reqmsg->m_Type), status);
  DPRINTF(RubyNetwork, "#getPendingDemandReqAddr addr %s\n", addr);
  return addr;
}


void PerfectSwitch::removeAllPendingWBToNRT(Address addr, int coreID) {
  if (addr == Address(0)) {
    return;
  }

	// TODO: need to change this
  MessageBuffer *NRTWBQueue = m_in[coreID][numCores];

  for (int i = 0; i < NRTWBQueue->getQSize(); i++) {
    RequestMsg *wb_req_msg_ptr =
        safe_cast<RequestMsg *>(NRTWBQueue->peekMsgPtr(i).get());

    if (addr == wb_req_msg_ptr->getAddr()) {
      NRTWBQueue->removeMessage(i);
      break;
    }
  }
}

void PerfectSwitch::removeAllPendingCWBToAddress(Address addr, int coreID) {
  if (addr == Address(0)) {
    return;
  }

  MessageBuffer *CWBQueue = m_in[coreID][6];

  for (int i = 0; i < CWBQueue->getQSize(); i++) {
    RequestMsg *wb_req_msg_ptr =
        safe_cast<RequestMsg *>(CWBQueue->peekMsgPtr(i).get());

    if (addr == wb_req_msg_ptr->getAddr()) {
      CWBQueue->removeMessage(i);
      break;
    }
  }
}

bool PerfectSwitch::removeAllPendingResponseFromMEMToNRT(Address addr) {

  DPRINTF(RubyNetwork, "#removeAllPendingResponseFromMEMToNRT addr %s\n", addr);

  bool rtr = false;

  DPRINTF(RubyNetwork,
          "#removeAllPendingResponseFromMEMToNRT Trace based enabled %s\n",
          addr);
  if (addr == Address(0)) {
    return rtr;
  }

	// PRLUT
  MessageBuffer *responseQueue = m_in[getNumberOfCores()][4];

  for (int i = 0; i < responseQueue->getQSize(); i++) {
    if (responseQueue->isReady(i)) {
      ResponseMsg *res_msg_ptr =
          safe_cast<ResponseMsg *>(responseQueue->peekMsgPtr(i).get());

      if (addr == res_msg_ptr->getAddr()) {
        DPRINTF(
            RubyNetwork,
            "#removeAllPendingResponseFromMEMToNRT res_msg_ptr->getAddr() %s\n",
            res_msg_ptr->getAddr());

        std::vector<NodeID> destIDs =
            res_msg_ptr->getDestination().getAllDest();

        bool remove = false;
        for (int j = 0; j < destIDs.size(); ++j) {
          if (isNRTCore(destIDs[j])) {
            DPRINTF(RubyNetwork,
                    "#removeAllPendingResponseFromMEMToNRT  isNRT(%s) = %s\n",
                    destIDs[j], isNRTCore(destIDs[j]));
            remove = true;
            break;
          }
        }

        if (remove) {
          responseQueue->removeMessage(i);
          rtr = true;
        }
      }
    }
  }
  return rtr;
}

void PerfectSwitch::regStats(string parent) {
  m_total_slack.name(parent + csprintf(".PerfectSwitch%d", m_switch_id) +
                     ".slack_total_slots");
  m_unused_slack.name(parent + csprintf(".PerfectSwitch%d", m_switch_id) +
                      ".slack_unused_slots");

	m_total_collection.name(parent + csprintf(".PerfectSwitch%d", m_switch_id) + ".total_collection");
	m_reordered_opp.name(parent + csprintf(".PerfectSwitch%d", m_switch_id)+".reordering_opportunities");
	m_slack_opp.name(parent + csprintf(".PerfectSwitch%d", m_switch_id)+".slack_opportunities");
}

void PerfectSwitch::storeEventInfo(int info) {
  m_pending_message_count[info]++;
}

void PerfectSwitch::storeEventInfo(int link_id, int vnet) {
  m_pending_message_count_mtx[link_id][vnet]++;
}

void PerfectSwitch::clearStats() {}
void PerfectSwitch::collateStats() {}

void PerfectSwitch::print(std::ostream &out) const {
  out << "[PerfectSwitch " << m_switch_id << "]";
}
