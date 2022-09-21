#ifndef __CPU_RUBYTEST_GLOBALPARAMS_HH__
#define __CPU_RUBYTEST_GLOBALPARAMS_HH__

// a batter way would be to ecapsulate the arbitration scheme in another SimObject
namespace global_params {
  enum ArbitrationScheme {
    // TDM_1 is the very first design that we have, in which the GPU broadcast one request in its own slot
    TDM_1,
    CARP
  };

  bool _isPredictableArbitration(ArbitrationScheme as);
  bool isPredictableArbitration();
  
  extern ArbitrationScheme arbitration_scheme;
  extern int coalesced_cacheline;
  extern int region_bits;
}

#endif // __CPU_RUBYTEST_GLOBALPARAMS_HH__
