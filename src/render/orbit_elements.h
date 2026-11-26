#pragma once

#include <cstdint>

// types.h must come first: it sequences windows.h before raylib.h on Windows, and Satellite is an anonymous struct typedef so it cannot be forward-declared.
#include "../core/types.h"

// 60 bytes: 3*12 (center/bx/by) + 16 (color) + 2*4 (e/phase0), no implicit padding; stride is sizeof(OrbitInstance), offsets use offsetof().
struct OrbitInstance {
    Vector3 center;  // ellipse center, draw units          @0
    Vector3 bx;      // semi-major axis vector, draw units   @12
    Vector3 by;      // semi-minor axis vector, draw units   @24
    Vector4 color;   // rgba                                 @36
    float   e;       // eccentricity                         @52
    float   phase0;  // -M0/(2*pi), in [-0.5, 0.5]           @56
};

// Build Kepler elements from osculating state (km, km/s, ECI); returns false for degenerate input.
bool OrbitElementsFromState(Vector3 r_km, Vector3 v_km_s, OrbitInstance *out);

// Builds from the satellite's per-frame state, so no extra SGP4 is needed.
bool OrbitElementsFromSatellite(const Satellite *s, OrbitInstance *out);

float OrbitProjectedRadiusPx(const OrbitInstance &o, const Vector3 &cam_pos,
                             float fovy_deg, int viewport_h);
int    OrbitLodBucket(float radius_px); // 32/64/128/128 -> 0..3
