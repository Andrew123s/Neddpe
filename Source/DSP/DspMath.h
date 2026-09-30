#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace nedd::dsp
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kHalfPi = 0.5f * kPi;

inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }
inline float clampBipolar (float x) noexcept { return std::clamp (x, -1.0f, 1.0f); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

inline float semitonesToRatio (float semitones) noexcept { return std::exp2 (semitones * (1.0f / 12.0f)); }
inline float midiNoteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float hzToMidiNote (float hz) noexcept { return 69.0f + 12.0f * std::log2 (std::max (hz, 1.0e-3f) / 440.0f); }

inline float dbToGain (float db) noexcept { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float gain) noexcept { return gain <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (gain); }

/** Rational tanh approximation, accurate to ~0.5% and exactly saturating at |x| >= 3. */
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Equal-power pan gains for pan in [-1, 1]. */
inline void panGains (float pan, float& left, float& right) noexcept
{
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * (kPi * 0.25f);
    left = std::cos (angle);
    right = std::sin (angle);
}

/** Bends a 0..1 value. curve in [-1, 1]: 0 = linear, > 0 = fast rise (log-like), < 0 = slow rise (exp-like). */
inline float tensionCurve (float t, float curve) noexcept
{
    if (std::abs (curve) < 1.0e-3f)
        return t;

    constexpr float kTension = 12.0f;

    if (curve > 0.0f)
    {
        const float a = curve * kTension;
        return t * (1.0f + a) / (1.0f + a * t);
    }

    const float a = -curve * kTension;
    const float u = 1.0f - t;
    return 1.0f - u * (1.0f + a) / (1.0f + a * u);
}

/** Response curve for controller calibration: curve in [-1, 1] maps to a power law 0.25x .. 4x. */
inline float responseCurve (float x, float curve) noexcept
{
    x = clamp01 (x);
    if (std::abs (curve) < 1.0e-3f)
        return x;
    return std::pow (x, std::exp2 (-curve * 2.0f));
}

/** Small, fast, deterministic PRNG usable on the audio thread. */
struct Random32
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9E3779B9u; }

    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** [0, 1) */
    float nextFloat() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    /** [-1, 1) */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }
};

/** Shared lookup tables used by oscillators and LFOs. */
struct SineTable
{
    static constexpr int kSize = 4096;
    std::array<float, kSize + 1> table {};

    SineTable()
    {
        for (int i = 0; i <= kSize; ++i)
            table[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * (double) i / (double) kSize);
    }

    /** phase in cycles, any value (wrapped internally). */
    float operator() (float phase) const noexcept
    {
        phase -= std::floor (phase);
        const float pos = phase * (float) kSize;
        const int index = std::min ((int) pos, kSize - 1);
        const float frac = pos - (float) index;
        return table[(size_t) index] + frac * (table[(size_t) index + 1] - table[(size_t) index]);
    }

    static const SineTable& get()
    {
        static const SineTable instance;
        return instance;
    }
};

inline float fastSin (float phaseCycles) noexcept { return SineTable::get() (phaseCycles); }

/** One-pole smoother whose time constant is set in samples. */
struct OnePole
{
    float value = 0.0f;
    float coefficient = 1.0f;

    void setTime (float timeSeconds, float updateRate) noexcept
    {
        coefficient = timeSeconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (timeSeconds * updateRate));
    }

    float process (float target) noexcept
    {
        value += coefficient * (target - value);
        return value;
    }

    void reset (float v) noexcept { value = v; }
};

} // namespace nedd::dsp
