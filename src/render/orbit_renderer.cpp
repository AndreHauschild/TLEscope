// Instanced orbit ribbon renderer: raw GL draws are bracketed by rlDrawRenderBatchActive() so raylib's queued geometry is not reordered; GL types come from the vendored glad header.

#include "orbit_renderer.h"
#include "orbit_shaders.h"

#include <raymath.h>
#include <rlgl.h>
#include <external/glad.h>

#include <cmath>
#include <cstddef>
#include <cstring>

const int ORBIT_RING_COUNTS[4] = {32, 64, 128, 128};
#define ORBIT_BUCKETS 4
// Capacity doubles on demand to the hard cap, then drops further instances and warns once.
#define ORBIT_MAX_INSTANCES 8192
#define ORBIT_INSTANCE_HARD_CAP 65536

// Attributes bind by offsetof(), so the stride is never hardcoded; this assert guards against padding holes. OrbitInstance is 60 bytes.
static_assert(sizeof(OrbitInstance) == 60, "OrbitInstance layout changed: update attribute offsets/size comment");

// One shared max-size ring VBO; smaller LODs stride into it. Literal so it can size the static scratch arrays below.
#define ORBIT_RING_MAX_VERTICES 256
static_assert(2 * 128 == ORBIT_RING_MAX_VERTICES, "ring VBO size must cover the largest bucket");

// ---------------------------------------------------------------------------

struct OrbitVertex {
    float ring_x, ring_y;
    float tan_x, tan_y;
    float side;
};

static struct {
    bool     ready;
    Shader   shader;
    unsigned int vao[ORBIT_BUCKETS];
    unsigned int ring_vbo;
    unsigned int index_vbo[ORBIT_BUCKETS];
    unsigned int inst_vbo;
    int      inst_capacity; // in instances

    // per-bucket instance scratch (CPU)
    OrbitInstance *inst[ORBIT_BUCKETS];
    int      inst_count[ORBIT_BUCKETS];

    // cull-face state captured at BeginFrame so Flush never aliases raylib's
    bool     cull_was_enabled;
    bool     cull_saved;

    // per-frame uniforms
    Matrix   mvp;
    float    tangent_eps;
    float    thickness_px;
    float    viewport[2];
    Vector3  sun_dir;
    float    earth_r2;
    float    time_sec;
    float    fade;
    float    dir_enabled;
    float    dir_animated;
    float    selected_boost;
    float    sunlit_enabled;

    // dimmed-pass visibility tunables (set via OrbitRendererSetDimVisibility)
    float    dim_alpha_scale;   // default 1.0
    float    dim_thickness_px;  // default 0.0 -> use base thickness
    float    dim_dir_floor;     // default 0.0 -> no override
    float    fav_thickness_px;

    // resolved uniform locations
    int loc_mvp, loc_tangent_eps, loc_thickness_px, loc_viewport;
    int loc_sun_dir, loc_earth_r2, loc_time;
    int loc_dir_enabled, loc_dir_animated, loc_fade, loc_selected_boost;
    int loc_sunlit;
    int loc_dim_alpha_scale, loc_dim_dir_floor, loc_shadow_tint;

    bool cap_warned;
} g;

// ---------------------------------------------------------------------------

// Full-res ring: `samples` samples, two vertices each (side -1/+1), so every LOD bucket strides into one VBO without re-upload.
static void build_ring_vertices(OrbitVertex *verts, int samples)
{
    for (int i = 0; i < samples; i++) {
        float E = 2.0f * (float)PI * (float)i / (float)samples;
        float c = cosf(E), s = sinf(E);
        for (int k = 0; k < 2; k++) {
            OrbitVertex &v = verts[2 * i + k];
            v.ring_x = c;
            v.ring_y = s;
            v.tan_x = -s;
            v.tan_y = c;
            v.side = (k == 0) ? -1.0f : 1.0f;
        }
    }
}

// n quads -> n*6 indices, wrapping the last quad back to sample 0.
static void build_indices(unsigned short *idx, int n, int stride)
{
    int o = 0;
    for (int i = 0; i < n; i++) {
        int cur  = i * stride;
        int next = ((i + 1) % n) * stride;
        unsigned short a0 = (unsigned short)(2 * cur);
        unsigned short a1 = (unsigned short)(2 * cur + 1);
        unsigned short b0 = (unsigned short)(2 * next);
        unsigned short b1 = (unsigned short)(2 * next + 1);
        idx[o++] = a0; idx[o++] = a1; idx[o++] = b0;
        idx[o++] = b1; idx[o++] = b0; idx[o++] = a1;
    }
}

// Ring attrs divisor 0, instance attrs divisor 1; the element VBO is bound last so the VAO captures it.
static void configure_vao(unsigned int vao, unsigned int ring_vbo, unsigned int elem_vbo,
                          unsigned int inst_vbo)
{
    rlEnableVertexArray(vao);

    // per-vertex ring attributes
    rlEnableVertexBuffer(ring_vbo);
    rlEnableVertexAttribute(OrbitShaderAttrib::RING);
    rlSetVertexAttribute(OrbitShaderAttrib::RING, 2, GL_FLOAT, false, sizeof(OrbitVertex),
                         (int)offsetof(OrbitVertex, ring_x));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::RING, 0);

    rlEnableVertexAttribute(OrbitShaderAttrib::TAN);
    rlSetVertexAttribute(OrbitShaderAttrib::TAN, 2, GL_FLOAT, false, sizeof(OrbitVertex),
                         (int)offsetof(OrbitVertex, tan_x));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::TAN, 0);

    rlEnableVertexAttribute(OrbitShaderAttrib::SIDE);
    rlSetVertexAttribute(OrbitShaderAttrib::SIDE, 1, GL_FLOAT, false, sizeof(OrbitVertex),
                         (int)offsetof(OrbitVertex, side));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::SIDE, 0);

    // per-instance attributes
    rlEnableVertexBuffer(inst_vbo);
    const int stride = (int)sizeof(OrbitInstance);
    rlEnableVertexAttribute(OrbitShaderAttrib::CENTER);
    rlSetVertexAttribute(OrbitShaderAttrib::CENTER, 3, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, center));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::CENTER, 1);

    rlEnableVertexAttribute(OrbitShaderAttrib::BX);
    rlSetVertexAttribute(OrbitShaderAttrib::BX, 3, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, bx));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::BX, 1);

    rlEnableVertexAttribute(OrbitShaderAttrib::BY);
    rlSetVertexAttribute(OrbitShaderAttrib::BY, 3, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, by));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::BY, 1);

    rlEnableVertexAttribute(OrbitShaderAttrib::COLOR);
    rlSetVertexAttribute(OrbitShaderAttrib::COLOR, 4, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, color));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::COLOR, 1);

    rlEnableVertexAttribute(OrbitShaderAttrib::ECC);
    rlSetVertexAttribute(OrbitShaderAttrib::ECC, 1, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, e));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::ECC, 1);

    rlEnableVertexAttribute(OrbitShaderAttrib::PHASE0);
    rlSetVertexAttribute(OrbitShaderAttrib::PHASE0, 1, GL_FLOAT, false, stride,
                         (int)offsetof(OrbitInstance, phase0));
    rlSetVertexAttributeDivisor(OrbitShaderAttrib::PHASE0, 1);

    // element buffer binding is captured by the VAO
    rlEnableVertexBufferElement(elem_vbo);

    rlDisableVertexArray();
    rlDisableVertexBuffer();
    rlDisableVertexBufferElement();
}

// ---------------------------------------------------------------------------

bool OrbitRendererInit(void)
{
    if (g.ready)
        return true;

    memset(&g, 0, sizeof(g));

    g.shader = LoadShaderFromMemory(OrbitShaders::vsOrbit, OrbitShaders::fsOrbit);
    if (g.shader.id == 0) {
        TraceLog(LOG_ERROR, "ORBIT: shader compile/link failed");
        return false;
    }

    g.loc_mvp             = GetShaderLocation(g.shader, "uMVP");
    g.loc_tangent_eps     = GetShaderLocation(g.shader, "uTangentEps");
    g.loc_thickness_px    = GetShaderLocation(g.shader, "uThicknessPx");
    g.loc_viewport        = GetShaderLocation(g.shader, "uViewport");
    g.loc_sun_dir         = GetShaderLocation(g.shader, "uSunDir");
    g.loc_earth_r2        = GetShaderLocation(g.shader, "uEarthR2");
    g.loc_time            = GetShaderLocation(g.shader, "uTime");
    g.loc_dir_enabled     = GetShaderLocation(g.shader, "uDirEnabled");
    g.loc_dir_animated    = GetShaderLocation(g.shader, "uDirAnimated");
    g.loc_fade            = GetShaderLocation(g.shader, "uFade");
    g.loc_selected_boost  = GetShaderLocation(g.shader, "uSelectedBoost");
    g.loc_sunlit          = GetShaderLocation(g.shader, "uSunlit");
    g.loc_dim_alpha_scale = GetShaderLocation(g.shader, "uDimAlphaScale");
    g.loc_dim_dir_floor   = GetShaderLocation(g.shader, "uDimDirFloor");
    g.loc_shadow_tint     = GetShaderLocation(g.shader, "uShadowTint");

    // shared ring VBO (static)
    static OrbitVertex ring[ORBIT_RING_MAX_VERTICES];
    build_ring_vertices(ring, ORBIT_RING_COUNTS[3]);
    g.ring_vbo = rlLoadVertexBuffer(ring, (int)sizeof(ring), false);

    // Sized to the hard cap once so the CPU lists can grow without rebinding.
    g.inst_vbo = rlLoadVertexBuffer(NULL, ORBIT_INSTANCE_HARD_CAP * (int)sizeof(OrbitInstance), true);
    g.inst_capacity = ORBIT_MAX_INSTANCES;

    for (int b = 0; b < ORBIT_BUCKETS; b++) {
        int n = ORBIT_RING_COUNTS[b];
        int stride = ORBIT_RING_COUNTS[ORBIT_BUCKETS - 1] / n; // 128/n -> 4,2,1,1
        static unsigned short idx[ORBIT_RING_MAX_VERTICES * 3]; // 128 samples -> 768 indices
        build_indices(idx, n, stride);
        g.index_vbo[b] = rlLoadVertexBufferElement(idx, n * 6 * (int)sizeof(unsigned short), false);

        g.vao[b] = rlLoadVertexArray();
        configure_vao(g.vao[b], g.ring_vbo, g.index_vbo[b], g.inst_vbo);

        g.inst[b] = (OrbitInstance *)MemAlloc(sizeof(OrbitInstance) * ORBIT_MAX_INSTANCES);
        g.inst_count[b] = 0;
    }

    g.fade = 1.0f;
    g.dir_enabled = 1.0f;
    g.dir_animated = 0.0f;
    g.selected_boost = 0.0f;
    g.sunlit_enabled = 0.0f;
    g.dim_alpha_scale = 1.0f;
    g.dim_thickness_px = 0.0f;
    g.dim_dir_floor = 0.0f;
    g.fav_thickness_px = 0.0f;
    g.ready = true;
    return true;
}

void OrbitRendererShutdown(void)
{
    if (!g.ready)
        return;

    for (int b = 0; b < ORBIT_BUCKETS; b++) {
        rlUnloadVertexArray(g.vao[b]);
        rlUnloadVertexBuffer(g.index_vbo[b]);
        if (g.inst[b]) MemFree(g.inst[b]);
    }
    rlUnloadVertexBuffer(g.ring_vbo);
    rlUnloadVertexBuffer(g.inst_vbo);
    UnloadShader(g.shader);

    memset(&g, 0, sizeof(g));
}

// ---------------------------------------------------------------------------

void OrbitRendererBeginFrame(Camera3D cam, int vpW, int vpH, Vector3 sun_dir_world,
                             float earth_r_draw, float time_sec)
{
    if (!g.ready)
        return;

    // raylib's batch MVP is modelview * projection (rlgl.h:3038) with a usually-identity per-object transform; follow that exact ordering.
    Matrix modelview = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();
    g.mvp = MatrixMultiply(modelview, projection);

    // remember the incoming cull state so the ribbon pass can force it off then restore raylib's setting
    g.cull_was_enabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    g.cull_saved = true;

    // Displacement used to finite-difference the screen-space tangent, scaled off the camera distance so it is neither sub-pixel nor huge.
    float dist = Vector3Distance(cam.position, cam.target);
    g.tangent_eps = 0.005f * (dist > 1e-3f ? dist : 1e-3f);

    g.thickness_px = 1.5f;
    g.viewport[0] = (float)vpW;
    g.viewport[1] = (float)vpH;
    g.sun_dir = sun_dir_world;
    g.earth_r2 = earth_r_draw * earth_r_draw;

    // Animated pulse time must be time-of-day scaled like the CPU path (GetTime).
    g.time_sec = time_sec;

    // Defaults; the public setters override them.
    g.fade = 1.0f;
    g.dir_enabled = 1.0f;
    g.dir_animated = 0.0f;
    g.selected_boost = 0.0f;
    g.sunlit_enabled = 0.0f;

    for (int b = 0; b < ORBIT_BUCKETS; b++)
        g.inst_count[b] = 0;
}

void OrbitRendererAddInstance(int bucket, const OrbitInstance &inst)
{
    if (!g.ready || bucket < 0 || bucket >= ORBIT_BUCKETS)
        return;

    // grow the per-bucket CPU list on demand (a fleet can exceed the start size)
    if (g.inst_count[bucket] >= g.inst_capacity)
    {
        if (g.inst_capacity >= ORBIT_INSTANCE_HARD_CAP)
        {
            if (!g.cap_warned)
            {
                TraceLog(LOG_WARNING,
                         "ORBIT: instance cap %d reached, dropping further orbits",
                         ORBIT_INSTANCE_HARD_CAP);
                g.cap_warned = true;
            }
            return;
        }
        int new_cap = g.inst_capacity * 2;
        if (new_cap > ORBIT_INSTANCE_HARD_CAP)
            new_cap = ORBIT_INSTANCE_HARD_CAP;
        for (int b = 0; b < ORBIT_BUCKETS; b++)
        {
            OrbitInstance *grown =
                (OrbitInstance *)MemRealloc(g.inst[b], sizeof(OrbitInstance) * new_cap);
            if (!grown)
                return; /* allocation failed: stop adding this frame */
            g.inst[b] = grown;
        }
        g.inst_capacity = new_cap;
    }

    g.inst[bucket][g.inst_count[bucket]++] = inst;
}

void OrbitRendererSetDirection(float enabled, float animated)
{
    g.dir_enabled = enabled;
    g.dir_animated = animated;
}

void OrbitRendererSetSunlit(float enabled)
{
    g.sunlit_enabled = enabled;
}

void OrbitRendererSetDimVisibility(float alpha_scale, float thickness_px, float dir_floor)
{
    g.dim_alpha_scale = alpha_scale;
    g.dim_thickness_px = thickness_px;
    g.dim_dir_floor = dir_floor;
}

void OrbitRendererSetFavouriteVisibility(float thickness_px)
{
    g.fav_thickness_px = thickness_px;
}

// `dimmed` selects the dimmed overrides; other passes get neutral defaults.
static void apply_uniforms(unsigned pass_mask)
{
    const bool dimmed = (pass_mask & ORBIT_PASS_DIMMED) != 0 &&
                        (pass_mask & (ORBIT_PASS_HIGHLIGHT | ORBIT_PASS_FAVOURITE)) == 0;
    const bool favourite = (pass_mask & ORBIT_PASS_FAVOURITE) != 0;

    // ribbon width: dimmed pass may override, all other passes keep the base
    float thickness = g.thickness_px;
    if (dimmed && g.dim_thickness_px > 0.0f)
        thickness = g.dim_thickness_px;
    if (favourite && g.fav_thickness_px > 0.0f)
        thickness = g.fav_thickness_px;

    // dim-only overrides; neutral for every other pass
    float dim_alpha_scale = dimmed ? g.dim_alpha_scale : 1.0f;
    float dim_dir_floor   = dimmed ? g.dim_dir_floor   : 0.0f;
    float selected_boost  = favourite ? 1.0f : g.selected_boost;
    // original shadow tint for favourites/highlight; dimmed lightens it whenever sunlit shading is
    // actually enabled on this pass, so the shadowed dimmed arcs stay legible. Keyed off the sunlit
    // toggle rather than the alpha scale: alpha no longer carries a dim override.
    Vector3 shadow_tint = {0.35f, 0.40f, 0.55f};
    if (dimmed && g.sunlit_enabled > 0.5f) {
        // lighten the eclipse multiply so the shadowed dimmed arcs stay legible
        shadow_tint.x = 0.55f;
        shadow_tint.y = 0.60f;
        shadow_tint.z = 0.72f;
    }

    rlSetUniformMatrix(g.loc_mvp, g.mvp);
    rlSetUniform(g.loc_tangent_eps, &g.tangent_eps, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_thickness_px, &thickness, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_viewport, g.viewport, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(g.loc_sun_dir, &g.sun_dir, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(g.loc_earth_r2, &g.earth_r2, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_time, &g.time_sec, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_dir_enabled, &g.dir_enabled, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_dir_animated, &g.dir_animated, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_fade, &g.fade, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_selected_boost, &selected_boost, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_sunlit, &g.sunlit_enabled, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_dim_alpha_scale, &dim_alpha_scale, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_dim_dir_floor, &dim_dir_floor, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(g.loc_shadow_tint, &shadow_tint, RL_SHADER_UNIFORM_VEC3, 1);
}

void OrbitRendererFlush(unsigned pass_mask)
{
    if (!g.ready)
        return;

    // Any queued raylib geometry must land before we touch VAO/shader state.
    rlDrawRenderBatchActive();

    // Every orbit pass is body-occluded: selected/favourite used to draw over the Earth,
    // which made them inconsistent with the dimmed pass and indistinguishable from it.
    // Depth test is now uniform; depth write stays off so alpha blending works.
    rlEnableDepthTest();
    rlDisableDepthMask();

    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);

    // ribbons are double-sided: a back-facing arc must still draw
    rlDisableBackfaceCulling();

    rlEnableShader(g.shader.id);
    // a dimmed-only flush gets the dim overrides; favourite/highlight never do
    apply_uniforms(pass_mask);

    // nudge the ribbons toward the eye in depth only, so an arc grazing a body limb cannot bleed through it
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    // One bucket at a time: all uploads write at offset 0, so a full upload would leave only the last bucket's data live.
    for (int b = 0; b < ORBIT_BUCKETS; b++) {
        int count = g.inst_count[b];
        if (count <= 0)
            continue;
        int bytes = count * (int)sizeof(OrbitInstance);
        rlUpdateVertexBuffer(g.inst_vbo, g.inst[b], bytes, 0);

        rlEnableVertexArray(g.vao[b]);
        int n = ORBIT_RING_COUNTS[b];
        rlDrawVertexArrayElementsInstanced(0, n * 6, NULL, count);
        rlDisableVertexArray();
    }

    glDisable(GL_POLYGON_OFFSET_FILL);

    rlDisableVertexArray();
    rlDisableShader();

    // restore the state raylib expects after our pass
    rlEnableDepthTest();
    rlEnableDepthMask();
    if (!g.cull_saved || g.cull_was_enabled)
        rlEnableBackfaceCulling();
    else
        rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();

    // queues are consumed; next AddInstance() group starts empty
    for (int b = 0; b < ORBIT_BUCKETS; b++)
        g.inst_count[b] = 0;
}

// Highlight polyline: raylib's batched DrawLine3D, one call per segment with the segment-start colour; only one orbit uses it, so batching is irrelevant.
void OrbitRendererDrawHighlightCPUPolyline(const Vector3 *pts, const Vector4 *cols, int n,
                                           float thickness_px)
{
    if (!pts || n < 2)
        return;

    rlDrawRenderBatchActive();
    rlSetLineWidth(thickness_px > 0.0f ? thickness_px : 1.0f);

    for (int i = 1; i < n; i++) {
        Color c = WHITE;
        if (cols) {
            const Vector4 &v = cols[i - 1];
            c.r = (unsigned char)(v.x * 255.0f);
            c.g = (unsigned char)(v.y * 255.0f);
            c.b = (unsigned char)(v.z * 255.0f);
            c.a = (unsigned char)(v.w * 255.0f);
        }
        DrawLine3D(pts[i - 1], pts[i], c);
    }

    rlSetLineWidth(1.0f);
    rlDrawRenderBatchActive();
}
