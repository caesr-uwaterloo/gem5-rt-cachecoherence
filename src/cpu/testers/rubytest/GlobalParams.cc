#include "cpu/testers/rubytest/GlobalParams.hh"

#include <cassert>

global_params::ArbitrationScheme global_params::arbitration_scheme;
int global_params::coalesced_cacheline;
int global_params::region_bits;

bool global_params::_isPredictableArbitration(ArbitrationScheme as) {
  switch(as) {
    case TDM_1:
    case CARP:
      return true;
  }
  // probably we don't need the non-predictable arbitrations right now
  assert(false);
}

bool global_params::isPredictableArbitration() {
  return _isPredictableArbitration(global_params::arbitration_scheme);
}
