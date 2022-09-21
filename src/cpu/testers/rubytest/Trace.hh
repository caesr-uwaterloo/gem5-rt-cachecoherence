#ifndef __TRACE_HH__
#define __TRACE_HH__

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>

// TRACE = 1 reads from trace.trc file. If not there, seg fault may happen,
// TRACE = 0 uses normal ruby tester when using ruby tester config file
#define TRACE 1 

// DEADLOCK_CHECK 1 enables deadlock checking logic. Useful for checking protocol correctness
#define DEADLOCK_CHECK 1

// PRED_ARB enables predictable arbitration
#define PRED_ARB 

// SLOT_WIDTH 50 sets slot width of TDM arbitration to 50 cycles
#define SLOT_WIDTH 50

// MAX_NPROC 8 sets a system configuration with 8 cores
#define MAX_NPROC 16

// Enables slack slots
#define SLACK_SLOT

const unsigned int numCores = 16;

////////////////////////////////////////////////////////////////////////////////
// When assigning the cores, |TOTAL cores| must be equal to
// |HRT| + |SRT| + |NRT|. A TDM period is the sum of HRT cores and the total
// number of dedicated SRT SLOTS. Total SRT dedicated slots must be less than
// total number of SRT cores
// +----------------------+---------------------+
// |TDM Slots for AB cores|RR slots for CD cores|
// +----------------------+---------------------+


// The set of HRT processors
const unsigned int AB_CORES[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

// The set of SRT processors
const unsigned int CD_CORES[] = {4, 5, 6};

// The set of NRT processors
const unsigned int E_CORES[] = {7};

// Defines the slot allocation of AB cores. E.g, if {0,0,1,0,1} defined, then
// arbiter allocates dedicated AB slots in the following order:
// core0, core0, core1, core0, core1
// add slots for the gpu here
const unsigned int AB_SLOT_ALLOCATION[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

// Total RR slots for CD cores + 1 for a reserve slot
const unsigned int TOTAL_DEDICATED_CD_SLOTS = 3;

const unsigned int SUBSET_OF_CORE[] = { 0, 1, 2, 3, 4, 6, 7};

inline int getNumberOfCores() {
	return numCores;
}

inline int getNumberOfHRTCores() {
	return sizeof(AB_CORES) / sizeof(unsigned int);
}

inline int getNumberOfAllocatedSlots(int coreID) {
	int slots = 0;
	for (int i = 0; i<getNumberOfHRTCores(); i++) {
		if (AB_SLOT_ALLOCATION[i] == coreID) {
			slots++;
		}
	}	
	return slots;
}

// helper macro to get total number of SRT cores
inline int getNumberOfSRTCores() {
  return sizeof(CD_CORES) / sizeof(unsigned int);
}

// helper macro to get total number of NRT cores
inline int getNumberOfNRTCores() {
  return sizeof(E_CORES) / sizeof(unsigned int);
}

// helper macro to get total number of dedicated slots
inline int getTotalDedicatedSlots() {
  return sizeof(AB_SLOT_ALLOCATION) / sizeof(unsigned int) +
         TOTAL_DEDICATED_CD_SLOTS;
}

// helper macro to get total number of dedicated slots
inline int getTotalDedicatedHRTSlots() {
  return sizeof(AB_SLOT_ALLOCATION) / sizeof(unsigned int);
}

inline bool isHRTCore(unsigned int core) {
  for (int i = 0; i < getNumberOfHRTCores(); i++)
    if (AB_CORES[i] == core)
      return true;
  return false;
}

inline bool isSRTCore(unsigned int core) {
  if (getNumberOfSRTCores() == 0)
    return false;

  for (int i = 0; i < getNumberOfSRTCores(); i++)
    if (CD_CORES[i] == core)
      return true;
  return false;
}

inline bool isNRTCore(unsigned int core) {
  for (int i = 0; i < getNumberOfNRTCores(); i++)
    if (E_CORES[i] == core)
      return true;
  return false;
}

inline int getHRTCoreOfIndex(int indexHRT) {
  if (getNumberOfHRTCores() > indexHRT)
    return AB_CORES[indexHRT];
  else
    return -1;
}

inline int getSRTCoreOfIndex(int indexSRT) {
  if (getNumberOfSRTCores() == 0)
    return -1;

  if (getNumberOfSRTCores() > indexSRT)
    return CD_CORES[indexSRT];
  else
    return -1;
}

inline int getNRTCoreOfIndex(int indexNRT) {
  if (getNumberOfNRTCores() == 0)
    return -1;
  if (getNumberOfNRTCores() > indexNRT)
    return E_CORES[indexNRT];
  else
    return -1;
}

inline int getIndexOfSRTCore(int core) {
  for (int index = 0; index < getNumberOfSRTCores(); index++) {
    if (CD_CORES[index] == core)
      return index;
  }
  return -1;
}

#endif
