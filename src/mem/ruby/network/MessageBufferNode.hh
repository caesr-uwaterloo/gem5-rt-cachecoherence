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

#ifndef __MEM_RUBY_BUFFERS_MESSAGEBUFFERNODE_HH__
#define __MEM_RUBY_BUFFERS_MESSAGEBUFFERNODE_HH__

#include <iostream>

#include "mem/ruby/slicc_interface/Message.hh"

class MessageBufferNode {
public:
  MessageBufferNode() : m_time(0), i_time(0), m_msg_counter(0) {}

  MessageBufferNode(const Tick time, uint64_t counter, const MsgPtr &msgptr)
      : m_time(time), i_time(time), m_msg_counter(counter), m_msgptr(msgptr) {}

  MessageBufferNode(const Tick time, const Tick itime, uint64_t counter, const MsgPtr &msgptr)
      : m_time(time), i_time(itime), m_msg_counter(counter), m_msgptr(msgptr) {}

  void print(std::ostream &out) const;

public:
  Tick m_time;
	Tick i_time;
  uint64_t m_msg_counter; // FIXME, should this be a 64-bit value?
  MsgPtr m_msgptr;
};

inline bool operator>(const MessageBufferNode &n1,
                      const MessageBufferNode &n2) {
  if (n1.m_time == n2.m_time) {
		/*
		if (n1.i_time != n2.i_time) {
			return (n1.i_time > n2.i_time);
		}
		*/
    assert(n1.m_msg_counter != n2.m_msg_counter);
    return n1.m_msg_counter > n2.m_msg_counter;
  } else {
    return n1.m_time > n2.m_time;
  }
}

inline bool operator<(const MessageBufferNode &n1,
                      const MessageBufferNode &n2) {
  if (n1.m_time == n2.m_time) {
		/*
		if (n1.i_time != n2.i_time) {
			return (n1.i_time < n2.i_time);
		}
		*/
    assert(n1.m_msg_counter != n2.m_msg_counter);
    return n1.m_msg_counter < n2.m_msg_counter;
  } else {
    return n1.m_time < n2.m_time;
  }
}

inline std::ostream &operator<<(std::ostream &out,
                                const MessageBufferNode &obj) {
  obj.print(out);
  out << std::flush;
  return out;
}

#endif // __MEM_RUBY_BUFFERS_MESSAGEBUFFERNODE_HH__
