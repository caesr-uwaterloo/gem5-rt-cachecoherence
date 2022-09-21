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

#include "base/intmath.hh"
#include "base/random.hh"
#include "cpu/testers/rubytest/Check.hh"
#include "cpu/testers/rubytest/CheckTable.hh"
#include "debug/RubyTest.hh"

#include <iostream>
#include <string>
#include <fstream>

CheckTable::CheckTable(int _num_writers, int _num_readers, RubyTester *_tester)
    : m_num_writers(_num_writers), m_num_readers(_num_readers),
      m_tester_ptr(_tester) {

  Address address;
  if (TRACE == 0) {
    physical_address_t physical = 0;
    const int size1 = 32;
    const int size2 = 100;

    // The first set is to get some false sharing
    physical = 1000;
    for (int i = 0; i < size1; i++) {
      // Setup linear addresses
      address.setAddress(physical);
      addCheck(address, 0);
      physical += CHECK_SIZE;
    }

    // The next two sets are to get some limited false sharing and
    // cache conflicts
    physical = 1000;
    for (int i = 0; i < size2; i++) {
      // Setup linear addresses
      address.setAddress(physical);
      addCheck(address, 0);
      physical += 256;
    }

    physical = 1000 + CHECK_SIZE;
    for (int i = 0; i < size2; i++) {
      // Setup linear addresses
      address.setAddress(physical);
      addCheck(address, 0);
      physical += 256;
    }
  } 
	else {
    DPRINTF(RubyTest, "Trace Path: %s\n", m_tester_ptr->getTracePath());

    /* trace based simulation */
    for (unsigned int i = 0; i < getNumberOfCores(); i++) {
      vindex[i] = 0;
    }

    for (unsigned int i = 0; i < getNumberOfCores(); i++) {
      prevVindex[i] = -1;
    }

    for (unsigned int i = 0; i<getNumberOfCores(); i++) {
        traceVec.push_back(std::vector<traceEntry>(0));
        checkVec.push_back(std::vector<Check*>(0));
    }

    for (int coreID = 0; coreID < getNumberOfCores(); coreID++) {
      std::string fName = m_tester_ptr->getTracePath() + "/" + "trace" +
                          std::to_string(coreID) + ".trc";
      std::ifstream myFile(fName.c_str());
      int total = 0;
			std::string fline;
      if (myFile.is_open()) {
        while (getline(myFile, fline)) {
          total++;

					// Address | Acces type (Rd, Wr) | Warp ID (for GPU agent) | Issue cycle | [ Coalesced? ]
          // When the last field is not specified, the entries default to uncoalesced (0)
					std::vector<std::string> parsedString;
					std::string delimiter = ",";
					size_t pos = fline.find(delimiter);
					while(pos != std::string::npos) {
						std::string token = fline.substr(0, pos);
						parsedString.push_back(token);
						fline.erase(0, pos + delimiter.length());
						pos = fline.find(delimiter);
					}
					parsedString.push_back(fline);

					std::string s = parsedString[0];
					std::string type = parsedString[1];
					std::string cyc = parsedString[2];
					std::string wID = "0";
					std::string coalesced = "0";
					//std::string wID = parsedString[2];
          //std::string coalesced = "0";
          //if(parsedString.size() == 5) {
          //coalesced = parsedString[4];
          //}

          DPRINTF(RubyTest, "coreID=%d, Type: %s warpID: %s, Cycle: %s, Coalesced?: %s\n", coreID, type,
                  wID, cyc, coalesced);

          unsigned long memSchedule =
              (unsigned long)strtol(cyc.c_str(), NULL, 10);
          DPRINTF(RubyTest, "Memory schedule :%s\n", memSchedule);

          Addr physical = (unsigned long)strtol(s.c_str(), NULL, 16);
          physical = physical & 0x8FFFFFFF;
          address.setAddress(physical);

          unsigned int warpID = (unsigned int)strtol(wID.c_str(), NULL, 10);
          bool isCoalesced = (bool)strtol(coalesced.c_str(), NULL, 10);

          if (type == "WR") {
            traceEntry te = { address, false, memSchedule, warpID,
                              isCoalesced };
            traceVec[coreID].push_back(te);
          } else {
            traceEntry te = { address, true, memSchedule, warpID, isCoalesced };
            traceVec[coreID].push_back(te);
          }

          DPRINTF(RubyTest, "Address: %s\n", address);
          addCheck(address, coreID);
        }
      }
      DPRINTF(RubyTest, "Total request for core%i : %i\n", coreID, total);
    }
  }
}

CheckTable::~CheckTable() {
  int size = m_check_vector.size();
  for (int i = 0; i < size; i++)
    delete m_check_vector[i];
}

bool CheckTable::isEnd() {
    // check so that all traces are done
    for(int coreID : SUBSET_OF_CORE) {
        // NOTE, vindex may be the size of the index after being updated
        if (vindex[coreID] < checkVec[coreID].size()) {
            return false;
        }
    }
    return true;
}

void CheckTable::updatePrevIndex(unsigned int coreID) {
	prevVindex[coreID] = vindex[coreID];
}

bool CheckTable::prevReqComplete(unsigned int coreID) {
	if (prevVindex[coreID] == vindex[coreID]) {
		return false;
	}
	return true;
}

void CheckTable::addCheck(const Address &address, int coreID) {
  Check *check_ptr = NULL;
  if (TRACE) {
    check_ptr = new Check(address, Address(100 + checkVec[coreID].size()),
                          m_num_writers, m_num_readers, m_tester_ptr);

		DPRINTF(RubyTest, "COREID: %d\n", coreID);
    m_lookup_map[Address(address.getAddress())] = check_ptr;
    m_vec_lookup_map[coreID][Address(address.getAddress())] = check_ptr;

    checkVec[coreID].push_back(check_ptr);
    DPRINTF(RubyTest, "In addCheck(): id: %d sz: %d\n", coreID, checkVec[coreID].size());
  } 
	else {
    if (floorLog2(CHECK_SIZE) != 0) {
      if (address.bitSelect(0, CHECK_SIZE_BITS - 1) != 0) {
        panic("Check not aligned");
      }
    }

    for (int i = 0; i < CHECK_SIZE; i++) {
      if (m_lookup_map.count(Address(address.getAddress() + i))) {
        // A mapping for this byte already existed, discard the
        // entire check
        return;
      }
    }

    check_ptr = new Check(address, Address(100 + m_check_vector.size()),
                          m_num_writers, m_num_readers, m_tester_ptr);
    for (int i = 0; i < CHECK_SIZE; i++) {
      // Insert it once per byte
      m_lookup_map[Address(address.getAddress() + i)] = check_ptr;
    }
    m_check_vector.push_back(check_ptr);
  }
}

Check *CheckTable::getRandomCheck() {
  assert(m_check_vector.size() > 0);
  return m_check_vector
      [random_mt.random<unsigned>(0, m_check_vector.size() - 1)];
}

Check *CheckTable::getOrderedCheck(unsigned int coreID) {

	if (vindex[coreID] > checkVec[coreID].size() - 1) {
		return NULL;
	}
	
	if (checkVec[coreID].size() > 0) {
		return checkVec[coreID].at(vindex[coreID]);	
	}
	return NULL;
}

bool CheckTable::getOp(unsigned int coreID) {
	return traceVec[coreID].at(vindex[coreID]).readOp;
}

unsigned int CheckTable::getWarpID(unsigned int coreID) {
	return traceVec[coreID].at(vindex[coreID]).warpID;
}

unsigned long CheckTable::getNextTS(unsigned int coreID) {
	return traceVec[coreID].at(vindex[coreID]).cycle;
}

bool CheckTable::getCoalesced(unsigned int coreID) {
  return traceVec[coreID].at(vindex[coreID]).coalesced;
}

Check *CheckTable::getCheck(const Address &address, NodeID proc) {
  DPRINTF(RubyTest, "Looking for check by address: %s", address);

  m5::hash_map<Address, Check *>::iterator i =
      m_vec_lookup_map[proc].find(address);

  if (i == m_vec_lookup_map[proc].end())
    return NULL;

  Check *check = i->second;
  assert(check != NULL);
  return check;
}

Check *CheckTable::getCheck(const Address &address) {
  DPRINTF(RubyTest, "Looking for check by address: %s", address);

  m5::hash_map<Address, Check *>::iterator i = m_lookup_map.find(address);

  if (i == m_lookup_map.end())
    return NULL;

  Check *check = i->second;
  assert(check != NULL);
  return check;
}

void CheckTable::print(std::ostream &out) const {}
