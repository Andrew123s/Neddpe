#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #define NEDD_SIMD_SSE2 1
 #include <emmintrin.h>
#else
 #define NEDD_SIMD_SSE2 0
#endif

namespace nedd::simd
{
/**
    Four floats processed together. Used to render unison sub-voices side by side: an
    8-voice unison stack is two f4 groups instead of eight scalar passes.

    SSE2 on x86-64 (always available there); a plain-array fallback elsewhere, which
    compilers auto-vectorise reasonably well. Both paths compute identical results.
*/
struct f4
{
#if NEDD_SIMD_SSE2
    __m128 v;

    f4() noexcept : v (_mm_setzero_ps()) {}
    f4 (__m128 x) noexcept : v (x) {}
    f4 (float x) noexcept : v (_mm_set1_ps (x)) {}

    static f4 load (const float* p) noexcept { return _mm_loadu_ps (p); }
    void store (float* p) const noexcept { _mm_storeu_ps (p, v); }

    friend f4 operator+ (f4 a, f4 b) noexcept { return _mm_add_ps (a.v, b.v); }
    friend f4 operator- (f4 a, f4 b) noexcept { return _mm_sub_ps (a.v, b.v); }
    friend f4 operator* (f4 a, f4 b) noexcept { return _mm_mul_ps (a.v, b.v); }
    friend f4 operator/ (f4 a, f4 b) noexcept { return _mm_div_ps (a.v, b.v); }

    /** Comparisons return an all-bits mask per lane. */
    friend f4 operator< (f4 a, f4 b) noexcept { return _mm_cmplt_ps (a.v, b.v); }
    friend f4 operator> (f4 a, f4 b) noexcept { return _mm_cmpgt_ps (a.v, b.v); }
    friend f4 operator>= (f4 a, f4 b) noexcept { return _mm_cmpge_ps (a.v, b.v); }
    friend f4 operator& (f4 a, f4 b) noexcept { return _mm_and_ps (a.v, b.v); }

    static f4 min (f4 a, f4 b) noexcept { return _mm_min_ps (a.v, b.v); }
    static f4 max (f4 a, f4 b) noexcept { return _mm_max_ps (a.v, b.v); }
    static f4 abs (f4 a) noexcept { return _mm_andnot_ps (_mm_set1_ps (-0.0f), a.v); }
    /** mask ? a : b */
    static f4 select (f4 mask, f4 a, f4 b) noexcept { return _mm_or_ps (_mm_and_ps (mask.v, a.v), _mm_andnot_ps (mask.v, b.v)); }
    /** Rounds towards zero. Valid for |x| < 2^31. */
    static f4 truncate (f4 a) noexcept { return _mm_cvtepi32_ps (_mm_cvttps_epi32 (a.v)); }
    /** Rounds to the nearest integer (ties to even). Valid for |x| < 2^31. */
    static f4 round (f4 a) noexcept { return _mm_cvtepi32_ps (_mm_cvtps_epi32 (a.v)); }

    float sum() const noexcept
    {
        const __m128 shuffled = _mm_shuffle_ps (v, v, _MM_SHUFFLE (2, 3, 0, 1));
        const __m128 pairs = _mm_add_ps (v, shuffled);
        const __m128 high = _mm_movehl_ps (pairs, pairs);
        return _mm_cvtss_f32 (_mm_add_ss (pairs, high));
    }
#else
    float v[4];

    f4() noexcept : v { 0.0f, 0.0f, 0.0f, 0.0f } {}
    f4 (float x) noexcept : v { x, x, x, x } {}

    static f4 load (const float* p) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = p[i]; return r; }
    void store (float* p) const noexcept { for (int i = 0; i < 4; ++i) p[i] = v[i]; }

    template <typename Fn>
    static f4 map2 (f4 a, f4 b, Fn fn) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = fn (a.v[i], b.v[i]); return r; }

    static float maskOf (bool b) noexcept
    {
        const unsigned bits = b ? 0xffffffffu : 0u;
        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return f;
    }
    static bool isSet (float m) noexcept { unsigned bits; std::memcpy (&bits, &m, sizeof (bits)); return bits != 0; }

    friend f4 operator+ (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return x + y; }); }
    friend f4 operator- (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return x - y; }); }
    friend f4 operator* (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return x * y; }); }
    friend f4 operator/ (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return x / y; }); }
    friend f4 operator< (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return maskOf (x < y); }); }
    friend f4 operator> (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return maskOf (x > y); }); }
    friend f4 operator>= (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return maskOf (x >= y); }); }
    friend f4 operator& (f4 a, f4 b) noexcept { return map2 (a, b, [] (float m, float y) { return isSet (m) ? y : 0.0f; }); }

    static f4 min (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return std::min (x, y); }); }
    static f4 max (f4 a, f4 b) noexcept { return map2 (a, b, [] (float x, float y) { return std::max (x, y); }); }
    static f4 abs (f4 a) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = std::abs (a.v[i]); return r; }
    static f4 select (f4 mask, f4 a, f4 b) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = isSet (mask.v[i]) ? a.v[i] : b.v[i]; return r; }
    static f4 truncate (f4 a) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = (float) (int) a.v[i]; return r; }
    static f4 round (f4 a) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = std::nearbyint (a.v[i]); return r; }

    float sum() const noexcept { return (v[0] + v[1]) + (v[2] + v[3]); }
#endif
};

/** Wraps phases into [0, 1). */
inline f4 wrap01 (f4 x) noexcept
{
    const f4 t = x - f4::truncate (x);
    return t + (f4 (1.0f) & (t < f4 (0.0f)));
}

/**
    sin (2 pi x) for x in cycles, any value. Folds into a quarter period and evaluates a
    degree-11 Taylor polynomial: max error ~6e-8, well below the old 4096-point table.
*/
inline f4 sinCycles (f4 x) noexcept
{
    f4 y = x - f4::round (x);                                        // [-0.5, 0.5]
    y = f4::select (y > f4 (0.25f), f4 (0.5f) - y, y);                // fold to [-0.25, 0.25]
    y = f4::select (y < f4 (-0.25f), f4 (-0.5f) - y, y);
    const f4 z = y * f4 (6.28318530717958647692f);
    const f4 z2 = z * z;
    f4 p (-2.5052108385441718775e-8f);
    p = p * z2 + f4 (2.7557319223985890653e-6f);
    p = p * z2 + f4 (-1.9841269841269841270e-4f);
    p = p * z2 + f4 (8.3333333333333333333e-3f);
    p = p * z2 + f4 (-1.6666666666666666667e-1f);
    return z + z * z2 * p;
}

/** Scalar version of sinCycles() with identical results. */
inline float sinCycles (float x) noexcept
{
    // Round half away from zero; differs from the SIMD ties-to-even only at exact .5, where both give 0.
    float y = x - (float) (int) (x + (x >= 0.0f ? 0.5f : -0.5f));
    if (y > 0.25f) y = 0.5f - y;
    if (y < -0.25f) y = -0.5f - y;
    const float z = y * 6.28318530717958647692f;
    const float z2 = z * z;
    float p = -2.5052108385441718775e-8f;
    p = p * z2 + 2.7557319223985890653e-6f;
    p = p * z2 + -1.9841269841269841270e-4f;
    p = p * z2 + 8.3333333333333333333e-3f;
    p = p * z2 + -1.6666666666666666667e-1f;
    return z + z * z2 * p;
}

/**
    Two-sided PolyBLEP residual for a unit falling edge at phase 0, for four lanes.
    dt is the per-lane phase increment (always positive).
*/
inline f4 polyBlep (f4 t, f4 dt) noexcept
{
    const f4 after = t < dt;
    const f4 before = t > (f4 (1.0f) - dt);
    const f4 xa = t / dt;
    const f4 ra = xa + xa - xa * xa - f4 (1.0f);
    const f4 xb = (t - f4 (1.0f)) / dt;
    const f4 rb = xb * xb + xb + xb + f4 (1.0f);
    return f4::select (after, ra, f4::select (before, rb, f4 (0.0f)));
}

} // namespace nedd::simd
