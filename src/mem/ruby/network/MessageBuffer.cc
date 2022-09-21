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

#include <cassert>

#include "base/cprintf.hh"
#include "base/misc.hh"
#include "base/random.hh"
#include "base/stl_helpers.hh"
#include "debug/RubyQueue.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/system/System.hh"
#include "mem/ruby/slicc_interface/RubyRequest.hh"

#include <iostream>
#include <sstream>
#include <string>
#include <iterator>

using namespace std;
using m5::stl_helpers::operator<<;

MessageBuffer::MessageBuffer(const string &name)
    : m_time_last_time_size_checked(0), m_time_last_time_enqueue(0),
      m_time_last_time_pop(0), m_last_arrival_time(0) {
  m_msg_counter = 0;
  m_consumer = NULL;
  m_sender = NULL;
  m_receiver = NULL;

  m_ordering_set = false;
  m_strict_fifo = false;
  m_max_size = 0;
  m_randomization = false;
  m_size_last_time_size_checked = 0;
  m_size_at_cycle_start = 0;
  m_msgs_this_cycle = 0;
  m_not_avail_count = 0;
  m_priority_rank = 0;
  m_name = name;

  m_stall_msg_map.clear();
  m_input_link_id = 0;
  m_vnet_id = 0;
  issuedOnce = false;
}

unsigned int MessageBuffer::getSize() {
  if (m_time_last_time_size_checked != m_receiver->curCycle()) {
    m_time_last_time_size_checked = m_receiver->curCycle();
    m_size_last_time_size_checked = m_prio_heap.size();
  }

  return m_size_last_time_size_checked;
}

bool MessageBuffer::areNSlotsAvailable(unsigned int n) {

  // fast path when message buffers have infinite size
  if (m_max_size == 0) {
    return true;
  }

  // determine the correct size for the current cycle
  // pop operations shouldn't effect the network's visible size
  // until next cycle, but enqueue operations effect the visible
  // size immediately
  unsigned int current_size = 0;

  if (m_time_last_time_pop < m_sender->clockEdge()) {
    // no pops this cycle - heap size is correct
    current_size = m_prio_heap.size();
  } else {
    if (m_time_last_time_enqueue < m_sender->curCycle()) {
      // no enqueues this cycle - m_size_at_cycle_start is correct
      current_size = m_size_at_cycle_start;
    } else {
      // both pops and enqueues occured this cycle - add new
      // enqueued msgs to m_size_at_cycle_start
      current_size = m_size_at_cycle_start + m_msgs_this_cycle;
    }
  }

  // now compare the new size with our max size
  if (current_size + n <= m_max_size) {
    return true;
  } else {
    DPRINTF(RubyQueue, "n: %d, current_size: %d, heap size: %d, "
                       "m_max_size: %d\n",
            n, current_size, m_prio_heap.size(), m_max_size);
    m_not_avail_count++;
    return false;
  }
}

const Message *
    // MessageBuffer::peek() const
    MessageBuffer::peek() {
  // print(cout);
  DPRINTF(RubyQueue, "\n Peeking at head of message buffer queue.\n");
  // assert(isReady());

  const Message *msg_ptr = m_prio_heap.front().m_msgptr.get();
  assert(msg_ptr);

  DPRINTF(RubyQueue, "Message: %s\n", (*msg_ptr));
  return msg_ptr;
}

// FIXME - move\n me somewhere else
Cycles random_time() {
  Cycles time(1);
  time += Cycles(random_mt.random(0, 3));          // [0...3]
  if (random_mt.random(0, 7) == 0) {               // 1 in 8 chance
    time += Cycles(100 + random_mt.random(1, 15)); // 100 + [1...15]
  }
  return time;
}

void MessageBuffer::enqueue(MsgPtr message, Cycles delta, bool isRMWWrite) {
  assert(m_ordering_set);

  // record current time incase we have a pop that also adjusts my size
  if (m_time_last_time_enqueue < m_sender->curCycle()) {
    m_msgs_this_cycle = 0; // first msg this cycle
    m_time_last_time_enqueue = m_sender->curCycle();
  }

  m_msg_counter++;
  m_msgs_this_cycle++;

  // Calculate the arrival time of the message, that is, the first
  // cycle the message can be dequeued.

  // Ani hack
  assert(delta > 0);

  Tick current_time = m_sender->clockEdge();
  Tick arrival_time = 0;

  DPRINTF(RubyQueue, "Before CURRENT TIME: %s\n", current_time);
  DPRINTF(RubyQueue, "Before m_time_last_time_enqueue: %s\n",
          m_time_last_time_enqueue);
  DPRINTF(RubyQueue, "Before m_sender->curCycle: %s\n", m_sender->curCycle());
  DPRINTF(RubyQueue, "m_last_arrival_time: %s\n", m_last_arrival_time);
  DPRINTF(RubyQueue, "m_sender->clockPeriod: %s\n", m_sender->clockPeriod());
  DPRINTF(RubyQueue, "m_sender: %s\n", m_sender);
  DPRINTF(RubyQueue, "m_receiver: %s\n", m_receiver);
  DPRINTF(RubyQueue, "delta: %s\n", delta);

  if (!RubySystem::getRandomization() || !m_randomization) {
    // No randomization
    arrival_time = current_time + delta * m_sender->clockPeriod();
    DPRINTF(RubyQueue, "NO random ARRIVAL TIME: %s\n", arrival_time);
  } else {
    // Randomization - ignore delta
    if (m_strict_fifo) {
      if (m_last_arrival_time < current_time) {
        m_last_arrival_time = current_time;
      }
      // arrival_time = m_last_arrival_time + random_time() *
      // m_sender->clockPeriod();
      arrival_time = m_last_arrival_time + m_sender->clockPeriod();
      DPRINTF(RubyQueue, "Strict fifo ARRIVAL TIME: %s\n", arrival_time);
    } else {
      arrival_time = current_time + random_time() * m_sender->clockPeriod();
      DPRINTF(RubyQueue, "N strict fifo ARRIVAL TIME: %s\n", arrival_time);
    }
  }

  DPRINTF(RubyQueue, "ARRIVAL TIME: %s, Current time: %s\n", arrival_time,
          current_time);

  // Check the arrival time

  // Ani hack
  assert(arrival_time > current_time);

  if (m_strict_fifo) {
    if (arrival_time < m_last_arrival_time) {
      panic("FIFO ordering violated: %s name: %s current time: %d "
            "delta: %d arrival_time: %d last arrival_time: %d\n",
            *this, m_name, current_time, delta * m_sender->clockPeriod(),
            arrival_time, m_last_arrival_time);
    }
  }

  // If running a cache trace, don't worry about the last arrival checks
  if (!RubySystem::getWarmupEnabled()) {
    m_last_arrival_time = arrival_time;
  }

  // compute the delay cycles and set enqueue time
  Message *msg_ptr = message.get();
  assert(msg_ptr != NULL);

  assert(m_sender->clockEdge() >= msg_ptr->getLastEnqueueTime() &&
         "ensure we aren't dequeued early");

  msg_ptr->updateDelayedTicks(m_sender->clockEdge());
  msg_ptr->setLastEnqueueTime(arrival_time);

  // Insert the message into the priority heap
  MessageBufferNode thisNode(arrival_time, m_msg_counter, message);
  if (isRMWWrite) {
    m_prio_heap.push_front(thisNode);
    push_heap(m_prio_heap.begin(), m_prio_heap.end(),
              less<MessageBufferNode>());
  } else {
    m_prio_heap.push_back(thisNode);
    push_heap(m_prio_heap.begin(), m_prio_heap.end(),
              greater<MessageBufferNode>());
  }

  // m_prio_heap.push_back(thisNode);
  // push_heap(m_prio_heap.begin(), m_prio_heap.end(),
  //   greater<MessageBufferNode>());

  DPRINTF(RubyQueue, "Enqueue arrival_time: %lld, Message: %s, Message Ptr: "
                     "%s, buffer type: %s\n",
          arrival_time, *(message.get()), message, name());

  // Schedule the wakeup
  assert(m_consumer != NULL);
  m_consumer->scheduleEventAbsolute(arrival_time);
  m_consumer->storeEventInfo(m_vnet_id);
  m_consumer->storeEventInfo(m_input_link_id, m_vnet_id);
  // DPRINTF(RubyQueue, "###### Printing message enqueue contents \n");
  // print(cout);
}

Cycles MessageBuffer::dequeue(int pos) {
  DPRINTF(RubyQueue, "Popping position\n");
  // assert(isReady());

  // get MsgPtr of the message about to be dequeued
  MsgPtr message = m_prio_heap.at(pos).m_msgptr;

  DPRINTF(RubyQueue, "Popped message: %s", message);

  // get the delay cycles
  message->updateDelayedTicks(m_receiver->clockEdge());
  // Cycles delayCycles = m_receiver->ticksToCycles(message->getDelayedTicks());

  // record previous size and time so the current buffer size isn't
  // adjusted until next cycle
  if (m_time_last_time_pop < m_receiver->clockEdge()) {
    m_size_at_cycle_start = m_prio_heap.size();
    m_time_last_time_pop = m_receiver->clockEdge();
  }

  m_prio_heap.erase(m_prio_heap.begin() + pos);

  return m_sender->ticksToCycles(m_sender->clockEdge());
}

Cycles MessageBuffer::dequeue() {
  DPRINTF(RubyQueue, "Popping\n");
  // assert(isReady());

  // get MsgPtr of the message about to be dequeued
  MsgPtr message = NULL;
  message = m_prio_heap.front().m_msgptr;
  DPRINTF(RubyQueue, "Popped message: %s", message);

  // get the delay cycles
  message->updateDelayedTicks(m_receiver->clockEdge());
  // Cycles delayCycles = m_receiver->ticksToCycles(message->getDelayedTicks());

  // record previous size and time so the current buffer size isn't
  // adjusted until next cycle
  if (m_time_last_time_pop < m_receiver->clockEdge()) {
    m_size_at_cycle_start = m_prio_heap.size();
    m_time_last_time_pop = m_receiver->clockEdge();
  }


  pop_heap(m_prio_heap.begin(), m_prio_heap.end(),
            greater<MessageBufferNode>());
  m_prio_heap.pop_back();

  return m_sender->ticksToCycles(m_sender->clockEdge());
}

Cycles MessageBuffer::getCurrentCycles() {
  return m_sender->ticksToCycles(m_sender->clockEdge());
}

void MessageBuffer::clear() {
  m_prio_heap.clear();

  m_msg_counter = 0;
  m_time_last_time_enqueue = Cycles(0);
  m_time_last_time_pop = 0;
  m_size_at_cycle_start = 0;
  m_msgs_this_cycle = 0;
}

void MessageBuffer::recycle() {
  DPRINTF(RubyQueue, "Recycling.\n");
  // assert(isReady());
  MessageBufferNode node = m_prio_heap.front();
  pop_heap(m_prio_heap.begin(), m_prio_heap.end(),
           greater<MessageBufferNode>());

  DPRINTF(RubyQueue, "Changing node time. Before: %s\n", node.m_time);
  node.m_time = m_receiver->clockEdge(m_recycle_latency);
  DPRINTF(RubyQueue, "Changing node time. After: %s\n", node.m_time);
  m_prio_heap.back() = node;
  push_heap(m_prio_heap.begin(), m_prio_heap.end(),
            greater<MessageBufferNode>());
  m_consumer->scheduleEventAbsolute(m_receiver->clockEdge(m_recycle_latency));
}

void MessageBuffer::reanalyzeList(vector<MsgNodeIDPairType> &lt,
                                  Tick nextTick) {

  DPRINTF(RubyQueue, "ANALYZE LIST SIZE: %d\n", lt.size());
  while (!lt.empty()) {
    m_msg_counter++;
    MessageBufferNode msgNode(nextTick, (*(lt.front().first)).getTime(), m_msg_counter, lt.front().first);
    //MessageBufferNode msgNode((*(lt.front().first)).getTime(), m_msg_counter, lt.front().first);
		DPRINTF(RubyQueue, "MSG BUFFER NODE: %s TIME: %s\n", msgNode, (*(lt.front().first)).getTime());

    m_prio_heap.push_back(msgNode);
    push_heap(m_prio_heap.begin(), m_prio_heap.end(),
              greater<MessageBufferNode>());

    m_consumer->scheduleEventAbsolute(nextTick);
    lt.erase(lt.begin()); 
  }
}

void MessageBuffer::clearMessages(const Address &addr) {
  DPRINTF(RubyQueue, "Clearing messages");
  m_stall_msg_map.erase(addr);
}

void MessageBuffer::reanalyzeMessages(const Address &addr) {
  DPRINTF(RubyQueue, "ReanalyzeMessages\n");
  assert(m_stall_msg_map.count(addr) > 0);
  // Tick nextTick = m_receiver->clockEdge(Cycles(1));
  Tick nextTick = m_receiver->clockEdge(Cycles(0));

  //
  // Put all stalled messages associated with this address back on the
  // prio heap
  //
  reanalyzeList(m_stall_msg_map[addr], nextTick);
  m_stall_msg_map.erase(addr);
}

void MessageBuffer::reanalyzeAllMessages() {
  DPRINTF(RubyQueue, "ReanalyzeAllMessages\n");
  Tick nextTick = m_receiver->clockEdge(Cycles(1));

  //
  // Put all stalled messages associated with this address back on the
  // prio heap
  //
  for (StallMsgMapType::iterator map_iter = m_stall_msg_map.begin();
       map_iter != m_stall_msg_map.end(); ++map_iter) {
    reanalyzeList(map_iter->second, nextTick);
  }
  m_stall_msg_map.clear();
}

void MessageBuffer::stallMessage(const Address &addr) {
  DPRINTF(RubyQueue, "Stalling due to %s\n", addr);
  // assert(isReady());
  assert(addr.getOffset() == 0);
  MsgPtr message = m_prio_heap.front().m_msgptr;

  dequeue();

  //
  // Note: no event is scheduled to analyze the map at a later time.
  // Instead the controller is responsible to call reanalyzeMessages when
  // these addresses change state.
  //
  (m_stall_msg_map[addr]).push_back(std::make_pair(message, 0));
}

void MessageBuffer::stallMessage(const Address &addr, int nodeID) {
  DPRINTF(RubyQueue, "Stalling due to %s\n", addr);
  // assert(isReady());
  assert(addr.getOffset() == 0);
  MsgPtr message = m_prio_heap.front().m_msgptr;

  dequeue();

  //
  // Note: no event is scheduled to analyze the map at a later time.
  // Instead the controller is responsible to call reanalyzeMessages when
  // these addresses change state.
  //
  (m_stall_msg_map[addr]).push_back(std::make_pair(message, nodeID));
}

void MessageBuffer::prioritizeAndCancelMessages(const Address &addr) {
  // A core running A-B core is sending a getM.
  // Go through the stall msg map of addr and remove stalled C-E cores

  std::vector<MsgNodeIDPairType> stallVec = m_stall_msg_map[addr];
  std::vector<MsgNodeIDPairType> copyStallVec;
  for (unsigned int i = 0; i < stallVec.size(); i++) {
    if (stallVec.at(i).second < 4) {
      copyStallVec.push_back(stallVec.at(i));
    }
  }

  m_stall_msg_map[addr] = copyStallVec;
}

void MessageBuffer::print(ostream &out) const {
  ccprintf(out, "[MessageBuffer: ");
  if (m_consumer != NULL) {
    ccprintf(out, " consumer-yes ");
  }

  deque<MessageBufferNode> copy(m_prio_heap);
  // sort_heap(copy.begin(), copy.end(), greater<MessageBufferNode>());
  ccprintf(out, "%s] %s", copy, m_name);
}

bool MessageBuffer::isReady() {
  DPRINTF(RubyQueue, "Size of m_prio_heap: %s\n", m_prio_heap.size());
  return ((m_prio_heap.size() > 0) &&
          (m_prio_heap.front().m_time <= m_receiver->clockEdge()));
}

bool MessageBuffer::isReady(int pos) {
  DPRINTF(RubyQueue, "Size of m_prio_heap is ready pos: %s\n",
          m_prio_heap.size());
  fflush(stdout);
  return ((m_prio_heap.size() > 0) &&
          (m_prio_heap.at(pos).m_time <= m_receiver->clockEdge()));
}

bool MessageBuffer::functionalRead(Packet *pkt) {
  // Check the priority heap and read any messages that may
  // correspond to the address in the packet.
  for (unsigned int i = 0; i < m_prio_heap.size(); ++i) {
    Message *msg = m_prio_heap[i].m_msgptr.get();
    if (msg->functionalRead(pkt))
      return true;
  }

  // Read the messages in the stall queue that correspond
  // to the address in the packet.
  for (StallMsgMapType::iterator map_iter = m_stall_msg_map.begin();
       map_iter != m_stall_msg_map.end(); ++map_iter) {

    for (std::vector<MsgNodeIDPairType>::iterator it =
             (map_iter->second).begin();
         it != (map_iter->second).end(); ++it) {

      Message *msg = (*it).first.get();
      if (msg->functionalRead(pkt))
        return true;
    }
  }
  return false;
}

uint32_t MessageBuffer::functionalWrite(Packet *pkt) {
  uint32_t num_functional_writes = 0;
  DPRINTF(RubyQueue, "FW MessageBuffer: %s\n", this);

  // Check the priority heap and write any messages that may
  // correspond to the address in the packet.
  for (unsigned int i = 0; i < m_prio_heap.size(); ++i) {
    Message *msg = m_prio_heap[i].m_msgptr.get();
    if (msg->functionalWrite(pkt)) {
      num_functional_writes++;
    }
  }

  // Check the stall queue and write any messages that may
  // correspond to the address in the packet.
  for (StallMsgMapType::iterator map_iter = m_stall_msg_map.begin();
       map_iter != m_stall_msg_map.end(); ++map_iter) {

    for (std::vector<MsgNodeIDPairType>::iterator it =
             (map_iter->second).begin();
         it != (map_iter->second).end(); ++it) {

      Message *msg = (*it).first.get();
      if (msg->functionalWrite(pkt)) {
        num_functional_writes++;
      }
    }
  }

  return num_functional_writes;
}

void MessageBuffer::removeMessage(int pos) {
  // std::cout<<"\nbefore removeMessage"<< m_prio_heap

  DPRINTF(RubyQueue, "Before removeMessage: %s\n", m_prio_heap.size());

  m_prio_heap.erase(m_prio_heap.begin() + pos);

  DPRINTF(RubyQueue, "after removeMessage: %s\n", m_prio_heap.size());
}
