#ifndef PROPAGATOR_H
#define PROPAGATOR_H

#include "types.h"

#include <atomic>
#include <cstdint>

// -- SGP4 call instrumentation: counts actual SGP4 propagations only ---------
extern std::atomic<uint64_t> g_sgp4_call_count;

// relaxed load of the current counter value
uint64_t sgp4_call_count_get();

// mean elements at a given time; angles/rates in SGP4 native units, sma_km in km
typedef struct {
    double sma_km;
    double ecc;
    double mean_motion;
    double incl;
    double raan;
    double argp;
    double mean_anom;
    bool   valid;
} SatPropElements;

// init SGP4 once and populate prop from the epoch state
bool sat_prop_init(Satellite *sat);

// ECI position (km) in the app axis convention (x, z, -y)
Vector3 sat_prop_position(const Satellite *sat, double unix);
// ECI velocity (km/s) in the app axis convention
Vector3 sat_prop_velocity(const Satellite *sat, double unix);
// both at once, avoids recomputing the Kepler solve
void sat_prop_state(const Satellite *sat, double unix, Vector3 *out_pos, Vector3 *out_vel);

// current-time mean elements; deep-space sats use the full-SGP4 fallback
void sat_prop_elements(const Satellite *sat, double unix, SatPropElements *out);

void sat_prop_set_short_period(bool enabled);

#endif /* PROPAGATOR_H */
