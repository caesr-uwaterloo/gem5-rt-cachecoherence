# Gem5 Real-Time Cache Coherence Repository

## This repo holds the Gem5 source code for the following works on multi-core Real-time cache coherence mechanisms
1. A Systematic Approach to Achieving Tight Worst-Case Latency and High-Performance Under Predictable Cache Coherence at RTAS 2021
2. Designing Predictable Cache Coherence Protocols for Multi-Core Real-Time Systems at IEEE Transactions on Computers 20202
3. CARP: A Data Communication Mechanism for Multi-core Mixed-Criticality Systems at RTSS 2019
4. Automatic Construction of Predictable and High-Performance Cache Coherence Protocols for Multi-Core Real-Time Systems IEEE Transactions on Computer Aided Design 2021

## Protocol list
1. CARP - Mixed criticality snooping bus-based cache coherence protocol (RTSS 2019)
2. MSI - Conventional snooping bus-based MSI protocol
3. PMSI - Predictable snopping bus-based MSI protocol (IEEE TC 2020)
4. MESI - Conventinoal snooping bus-based MESI protocol
5. PMESI - Predictable snooping bus-based MESI protocol (IEEE TC 2020)
6. PMSI_C2C - PMSI with cache-to-cache transfers (RTAS 2021)
7. PMESI_C2C - PMESI with cache-to-cache transfers (RTAS 2021)
8. MOESI - Conventional snooping bus-based MOESI protocol
9. PMOESI - Predictable snooping bus-based MOESI protocol
10. MESIF - Conventional snooping bus-based MESIF protocol (IEEE TCAD 2021)
11. PMESIF - Predictable snooping bus-based MESIF protocol (IEEE TCAD 2021)
12. LPMI - Linear predictable snooping bus-based MI protocol (RTAS 2021)
13. LPMSI - Linear PMSI (RTAS 2021)
14. LPMESI - Linear PMESI (RTAS 2021)
15. LPMOESI - Linear PMOESI (RTAS 2021)

## Usage notes
1. The build-all.sh script will build all the above flavors of the real-time cache coherence protocols for different core counts (2, 4, and 8 cores)
2. `src/cpu/testers/rubytest/Trace.hh` controls several parameters of the real-time arbitration policy. One can control the arbitration schedule, the allocation of time slots to cores for mixed-criticality scheduling, and the type of arbitration policy (round-robin, TDM, slack slot scheduling).

## Contact
For any questions, please contact Anirudh Kaushik at amkaushi@uwaterloo.ca
