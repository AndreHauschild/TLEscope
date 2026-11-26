#pragma once

// Shared orbit direction constants: OrbitDirectionIntensity() in tools_common.cpp and the GLSL shader in orbit_shaders.h (via ORBIT_SHADER_STR()) must agree; trailing `f` is valid in both.
#define ORBIT_DIR_MIN          0.18f /* base alpha multiplier for dimmed/unselected orbits */
#define ORBIT_DIR_MIN_SELECTED 0.62f /* base alpha for the selected orbit; must stay clearly above ORBIT_DIR_MIN so the selection is identifiable at every phase */
#define ORBIT_DIR_TRAIL_LEN    0.30  /* highlighted trail length behind the satellite, in orbits */
#define ORBIT_DIR_ANIM_TAU     0.10  /* animated pulse tail length, in orbits */
#define ORBIT_DIR_ANIM_SPEED   0.30  /* animated pulse sweeps per second */
#define ORBIT_DIR_WIDTH_MAX    2.5f  /* thickest line-width multiplier (2D only) */
#define ORBIT_SELECTED_WIDTH_PX 2.6f /* selected orbit ribbon width; favourites use ORBIT_FAVOURITE_WIDTH_PX */
#define ORBIT_FAVOURITE_WIDTH_PX 1.8f
