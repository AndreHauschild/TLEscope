#pragma once

// Raw-GL draws are bracketed by rlDrawRenderBatchActive() so raylib's batch is not reordered; Flush() draws and clears buckets, and ORBIT_PASS_HIGHLIGHT is never flushed.

#include "orbit_elements.h"
#include "raylib.h"

// Pass mask bits (passed to OrbitRendererFlush).
#define ORBIT_PASS_DIMMED    (1u << 0) // depth test ON,  depth write OFF (Earth-occluded)
#define ORBIT_PASS_HIGHLIGHT (1u << 1) // depth test OFF, depth write OFF (over the Earth)
#define ORBIT_PASS_FAVOURITE (1u << 2) // depth test OFF, depth write OFF (over the Earth)

// Ring sample counts per LOD bucket: {32, 64, 128, 128}.
extern const int ORBIT_RING_COUNTS[4];

bool OrbitRendererInit(void);
void OrbitRendererShutdown(void);

void OrbitRendererBeginFrame(Camera3D cam, int vpW, int vpH, Vector3 sun_dir_world,
                             float earth_r_draw, float time_sec);

// Queue one ellipse for a LOD bucket (0..3). Colour alpha is baked into inst.
void OrbitRendererAddInstance(int bucket, const OrbitInstance &inst);

// Per-frame toggles, after BeginFrame and before Flush.
void OrbitRendererSetDirection(float enabled, float animated);
void OrbitRendererSetSunlit(float enabled);

// Dimmed-pass tunables only: favourites/highlight unaffected; thickness_px <= 0 keeps default, dir_floor <= 0 disables override, defaults 1.0 / 0.0 / 0.0 change nothing.
void OrbitRendererSetDimVisibility(float alpha_scale, float thickness_px, float dir_floor);

void OrbitRendererSetFavouriteVisibility(float thickness_px);

void OrbitRendererFlush(unsigned pass_mask);

// Draw one short non-instanced polyline via raylib's batched line path.
void OrbitRendererDrawHighlightCPUPolyline(const Vector3 *pts, const Vector4 *cols, int n,
                                           float thickness_px);
