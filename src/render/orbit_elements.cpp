// Pure analytic Kepler math: no GL and no SGP4.

// types.h first: it sequences windows.h before raylib.h on Windows.
#include "../core/types.h"           // DRAW_SCALE, MU, Satellite

#include "orbit_elements.h"
#include "../core/astro_constants.h" // ASTRO_MU_SGP4

#include <raymath.h>                 // Vector3* helpers (types.h pulls raylib.h only)
#include <cmath>

// Mu in km^3/s^2; falls back to the SGP4 constant if types.h has no MU.
#ifndef MU
#define MU ASTRO_MU_SGP4
#endif

static const float kMu = (float)MU;

// Any unit vector perpendicular to w; used when the orbit is circularish and the eccentricity vector carries no usable direction.
static Vector3 any_unit_perp(Vector3 w)
{
    // Pick the axis least aligned with w, then Gram-Schmidt it.
    float ax = fabsf(w.x), ay = fabsf(w.y), az = fabsf(w.z);
    Vector3 axis;
    if (ax <= ay && ax <= az)
        axis = (Vector3){1.0f, 0.0f, 0.0f};
    else if (ay <= az)
        axis = (Vector3){0.0f, 1.0f, 0.0f};
    else
        axis = (Vector3){0.0f, 0.0f, 1.0f};

    Vector3 perp = Vector3CrossProduct(w, axis);
    float len = Vector3Length(perp);
    if (len <= 1e-12f)
        return (Vector3){0.0f, 1.0f, 0.0f};
    return Vector3Scale(perp, 1.0f / len);
}

// Wrap an angle (rad) into [-pi, pi].
static float wrap_pi(float a)
{
    const float two_pi = 2.0f * (float)PI;
    a = fmodf(a + (float)PI, two_pi);
    if (a < 0.0f)
        a += two_pi;
    return a - (float)PI;
}

// Shared core: fills the draw-unit ellipse basis.
static bool build_elements(Vector3 r, Vector3 v, OrbitInstance *out)
{
    if (!out)
        return false;
    if (!isfinite(r.x) || !isfinite(r.y) || !isfinite(r.z) ||
        !isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z))
        return false;

    const float rmag = Vector3Length(r);
    if (!(rmag > 1e-6f))
        return false;

    const float v2 = Vector3LengthSqr(v);

    // Specific angular momentum.
    Vector3 h = Vector3CrossProduct(r, v);
    const float h2 = Vector3LengthSqr(h);
    if (!(h2 > 1e-12f))
        return false; // rectilinear / degenerate orbit plane

    // vis-viva: a = 1 / (2/r - v^2/mu)
    const float inv_a = 2.0f / rmag - v2 / kMu;
    if (!(inv_a > 0.0f) || !isfinite(inv_a))
        return false; // parabolic / hyperbolic or degenerate

    const float a = 1.0f / inv_a;
    if (!(a > 0.0f) || !isfinite(a))
        return false;

    // Eccentricity vector: e = ((v^2 - mu/r) r - (r.v) v) / mu
    const float rv = Vector3DotProduct(r, v);
    Vector3 evec = Vector3Scale(
        Vector3Subtract(Vector3Scale(r, v2 - kMu / rmag), Vector3Scale(v, rv)),
        1.0f / kMu);
    const float e = Vector3Length(evec);
    if (!isfinite(e) || e >= 1.0f)
        return false; // non-elliptic

    // Mean motion gate (matches the existing mean_motion > 0 guard).
    const float n = sqrtf(kMu / (a * a * a));
    if (!(n > 0.0f) || !isfinite(n))
        return false;

    const float b = a * sqrtf(fmaxf(0.0f, 1.0f - e * e));

    Vector3 what = Vector3Normalize(h);
    Vector3 ehat = (e > 1e-6f) ? Vector3Scale(evec, 1.0f / e) : any_unit_perp(what);
    Vector3 qhat = Vector3CrossProduct(what, ehat);

    // Semi-major axis vector is along ehat; the semi-minor along qhat.
    Vector3 bx = Vector3Scale(ehat, a);
    Vector3 by = Vector3Scale(qhat, b);

    // Ellipse center sits one focus-to-center offset behind the focus.
    Vector3 center = Vector3Scale(ehat, -a * e);

    // Phase: with r + a e ehat = a cos(E) ehat + b sin(E) qhat, cos(E) = ((r.ehat) + a e) / a and sin(E) = (r.qhat) / b.
    float cosE = (Vector3DotProduct(r, ehat) + a * e) / a;
    float sinE = (b > 1e-6f) ? (Vector3DotProduct(r, qhat) / b) : 0.0f;
    float E = atan2f(sinE, cosE);
    float M = E - e * sinf(E);
    float M0 = wrap_pi(M); // mean anomaly at the state, in [-pi, pi]

    out->center = Vector3Scale(center, 1.0f / DRAW_SCALE);
    out->bx = Vector3Scale(bx, 1.0f / DRAW_SCALE);
    out->by = Vector3Scale(by, 1.0f / DRAW_SCALE);
    out->color = (Vector4){1.0f, 1.0f, 1.0f, 1.0f};
    out->e = e;
    out->phase0 = -M0 / (2.0f * (float)PI);

    return true;
}

bool OrbitElementsFromState(Vector3 r_km, Vector3 v_km_s, OrbitInstance *out)
{
    return build_elements(r_km, v_km_s, out);
}

float OrbitProjectedRadiusPx(const OrbitInstance &o, const Vector3 &cam_pos,
                             float fovy_deg, int viewport_h)
{
    if (!(fovy_deg > 0.0f) || viewport_h <= 0)
        return 0.0f;

    // a = |bx| (draw units); use apoapsis a(1+e) as the bounding radius.
    float a_draw = Vector3Length(o.bx);
    if (!(a_draw > 0.0f) || !isfinite(a_draw))
        return 0.0f;

    float radius_draw = a_draw * (1.0f + o.e);

    float distance = Vector3Distance(cam_pos, o.center);
    if (!(distance > 1e-6f) || !isfinite(distance))
        return 0.0f;

    float focal = (float)viewport_h / (2.0f * tanf(fovy_deg * DEG2RAD * 0.5f));
    return radius_draw / distance * focal;
}

int OrbitLodBucket(float radius_px)
{
    if (!(radius_px >= 16.0f))
        return 0;
    if (radius_px < 64.0f)
        return 1;
    if (radius_px < 256.0f)
        return 2;
    return 3;
}

bool OrbitElementsFromSatellite(const Satellite *s, OrbitInstance *out)
{
    if (!s || !out)
        return false;
    return OrbitElementsFromState(s->current_pos, s->current_vel, out);
}
