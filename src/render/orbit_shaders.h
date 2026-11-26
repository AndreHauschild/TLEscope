#pragma once

// GLSL for the instanced orbit renderer: `layout(location = N)` macros stringified by ORBIT_SHADER_STR(); tuning from orbit_dir_constants.h; `#version` stays first via split raw-string below.

#include "orbit_dir_constants.h"

#define ORBIT_ATTR_LOC_RING    0 // vec2  per-vertex
#define ORBIT_ATTR_LOC_TAN     1 // vec2  per-vertex
#define ORBIT_ATTR_LOC_SIDE    2 // float per-vertex
#define ORBIT_ATTR_LOC_CENTER  3 // vec3  per-instance
#define ORBIT_ATTR_LOC_BX      4 // vec3  per-instance
#define ORBIT_ATTR_LOC_BY      5 // vec3  per-instance
#define ORBIT_ATTR_LOC_COLOR   6 // vec4  per-instance
#define ORBIT_ATTR_LOC_ECC     7 // float per-instance
#define ORBIT_ATTR_LOC_PHASE0  8 // float per-instance

namespace OrbitShaderAttrib {
inline constexpr int RING     = ORBIT_ATTR_LOC_RING;
inline constexpr int TAN      = ORBIT_ATTR_LOC_TAN;
inline constexpr int SIDE     = ORBIT_ATTR_LOC_SIDE;
inline constexpr int CENTER   = ORBIT_ATTR_LOC_CENTER;
inline constexpr int BX       = ORBIT_ATTR_LOC_BX;
inline constexpr int BY       = ORBIT_ATTR_LOC_BY;
inline constexpr int COLOR    = ORBIT_ATTR_LOC_COLOR;
inline constexpr int ECC      = ORBIT_ATTR_LOC_ECC;
inline constexpr int PHASE0   = ORBIT_ATTR_LOC_PHASE0;
} // namespace OrbitShaderAttrib

#define ORBIT_SHADER_STR_(x) #x
#define ORBIT_SHADER_STR(x) ORBIT_SHADER_STR_(x)

namespace OrbitShaders {

inline constexpr const char *vsOrbit = R"GLSL(#version 330
#define ORBIT_ATTR_RING    )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_RING) R"GLSL(
#define ORBIT_ATTR_TAN     )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_TAN) R"GLSL(
#define ORBIT_ATTR_SIDE    )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_SIDE) R"GLSL(
#define ORBIT_ATTR_CENTER  )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_CENTER) R"GLSL(
#define ORBIT_ATTR_BX      )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_BX) R"GLSL(
#define ORBIT_ATTR_BY      )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_BY) R"GLSL(
#define ORBIT_ATTR_COLOR   )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_COLOR) R"GLSL(
#define ORBIT_ATTR_E       )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_ECC) R"GLSL(
#define ORBIT_ATTR_PHASE0  )GLSL" ORBIT_SHADER_STR(ORBIT_ATTR_LOC_PHASE0) R"GLSL(

layout(location = ORBIT_ATTR_RING)   in vec2  aRing;
layout(location = ORBIT_ATTR_TAN)    in vec2  aTan;
layout(location = ORBIT_ATTR_SIDE)   in float aSide;
layout(location = ORBIT_ATTR_CENTER) in vec3  iCenter;
layout(location = ORBIT_ATTR_BX)     in vec3  iBx;
layout(location = ORBIT_ATTR_BY)     in vec3  iBy;
layout(location = ORBIT_ATTR_COLOR)  in vec4  iColor;
layout(location = ORBIT_ATTR_E)      in float iE;
layout(location = ORBIT_ATTR_PHASE0) in float iPhase0;

uniform mat4  uMVP;
uniform float uTangentEps;
uniform float uThicknessPx;
uniform vec2  uViewport;

out vec3  vWorld;
out vec4  vColor;
out float vM;
out float vPhase0;

void main() {
    // ellipse point in draw space: unit circle mapped by the basis, then offset
    vec3 world = iCenter + aRing.x * iBx + aRing.y * iBy;

    // project a nearby point displaced along the tangent to get the screen direction
    vec4 c0 = uMVP * vec4(world, 1.0);
    vec3 wt = aTan.x * iBx + aTan.y * iBy;
    vec4 c1 = uMVP * vec4(world + uTangentEps * wt, 1.0);

    vec2 s0 = c0.xy / c0.w;
    vec2 s1 = c1.xy / c1.w;

    // pixel space keeps the perpendicular isotropic; convert half-thickness to NDC
    vec2 pix_dir = (s1 - s0) * (uViewport * 0.5);
    float plen = length(pix_dir);
    vec2 ndir = (plen > 1e-6) ? (pix_dir / plen) : vec2(1.0, 0.0);
    vec2 perp = vec2(-ndir.y, ndir.x);

    vec2 half_ndc = perp * (aSide * uThicknessPx * 0.5) * (2.0 / uViewport);
    gl_Position = vec4(c0.xy + half_ndc * c0.w, c0.z, c0.w);

    // direction-ramp phase: M = E - e sin E, the same phase the CPU uses
    float E = atan(aRing.y, aRing.x);
    vM = E - iE * sin(E);
    vPhase0 = iPhase0;

    vWorld = world;
    vColor = iColor;
}
)GLSL";

inline constexpr const char *fsOrbit = R"GLSL(#version 330
#define ORBIT_DIR_MIN          )GLSL" ORBIT_SHADER_STR(ORBIT_DIR_MIN) R"GLSL(
#define ORBIT_DIR_MIN_SELECTED )GLSL" ORBIT_SHADER_STR(ORBIT_DIR_MIN_SELECTED) R"GLSL(
#define ORBIT_DIR_TRAIL_LEN    )GLSL" ORBIT_SHADER_STR(ORBIT_DIR_TRAIL_LEN) R"GLSL(
#define ORBIT_DIR_ANIM_TAU     )GLSL" ORBIT_SHADER_STR(ORBIT_DIR_ANIM_TAU) R"GLSL(
#define ORBIT_DIR_ANIM_SPEED   )GLSL" ORBIT_SHADER_STR(ORBIT_DIR_ANIM_SPEED) R"GLSL(

in vec3  vWorld;
in vec4  vColor;
in float vM;
in float vPhase0;

uniform float uTime;
uniform float uDirEnabled;
uniform float uDirAnimated;
uniform float uFade;
uniform float uSunlit;
uniform float uSelectedBoost;
uniform vec3  uSunDir;
uniform float uEarthR2;
// Dimmed-pass overrides. Dimmed sets these; favourite/highlight upload the
// neutral defaults so their appearance is unchanged.
uniform float uDimAlphaScale; // multiplies fragment alpha (1.0 = unchanged)
uniform float uDimDirFloor;   // override for ORBIT_DIR_MIN, 0 = disabled
uniform vec3  uShadowTint;    // eclipse multiply colour

out vec4 finalColor;

#define ORBIT_TWO_PI 6.283185307179586

// shortest signed distance around the loop, matching orbit_dir_wrap() in C++
float orbit_wrap(float p) {
    return p - floor(p + 0.5);
}

// GLSL port of OrbitDirectionIntensity() (src/ui/tools/tools_common.cpp)
float orbitDirIntensity(float phase) {
    float s = orbit_wrap(phase);
    float g;

    if (uDirAnimated > 0.5) {
        float pulse = uTime * ORBIT_DIR_ANIM_SPEED;
        float d = orbit_wrap(s - pulse);       // signed distance from the pulse
        g = (d > 0.0) ? 0.0 : exp(d / ORBIT_DIR_ANIM_TAU);
    } else {
        if (s >= 0.0) {
            g = 0.0;                            // future: base level
        } else {
            float x = -s / ORBIT_DIR_TRAIL_LEN; // 0 at satellite, 1 at trail end
            g = (x >= 1.0) ? 0.0 : 1.0 - x * x * (3.0 - 2.0 * x);
        }
    }

    float fl = (uSelectedBoost > 0.5) ? ORBIT_DIR_MIN_SELECTED : ORBIT_DIR_MIN;
    // dimmed-pass floor override; never affects the selected/highlight ramp
    if (uDimDirFloor > 0.0 && uSelectedBoost < 0.5) fl = max(ORBIT_DIR_MIN, uDimDirFloor);
    return fl + (1.0 - fl) * g;
}

void main() {
    vec3 rgb = vColor.rgb;
    // uDimAlphaScale is 1.0 for every non-dimmed pass; dimmed raises it
    float alpha = vColor.a * uFade * uDimAlphaScale;

    // direction-of-motion ramp (phase is the signed fraction of an orbit)
    if (uDirEnabled > 0.5) {
        float ph = vPhase0 + vM / ORBIT_TWO_PI;
        alpha *= orbitDirIntensity(ph);
    }

    // eclipse test, mirroring is_sat_eclipsed() in src/core/astro.cpp
    if (uSunlit > 0.5) {
        vec3 sd = normalize(uSunDir);
        float dp = dot(vWorld, sd);
        float perp2 = dot(vWorld, vWorld) - dp * dp;
        bool lit = (dp > 0.0) || (perp2 >= uEarthR2);
        if (!lit) {
            // cool multiply reproduces the CPU path's dim-orbit colour drop
            rgb *= uShadowTint;
        }
    }

    if (alpha < 0.004) discard;
    finalColor = vec4(rgb, alpha);
}
)GLSL";

#undef ORBIT_SHADER_STR_
#undef ORBIT_SHADER_STR

} // namespace OrbitShaders
