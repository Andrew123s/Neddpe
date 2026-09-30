#pragma once

#include "DspMath.h"
#include "Parameters/ParameterDefs.h"

namespace nedd::dsp
{
/** User-drawn LFO shapes, one table per LFO, values in [-1, 1]. Published to the audio thread as a whole. */
struct LfoCustomShapes
{
    static constexpr int kTableSize = 256;
    static constexpr int kMaxPoints = 32;

    struct Point { float x, y; };   // x in [0,1], y in [-1,1]

    std::array<std::vector<Point>, kNumLfos> points;
    std::array<std::array<float, kTableSize>, kNumLfos> tables {};

    LfoCustomShapes()
    {
        for (auto& p : points)
            p = { { 0.0f, -1.0f }, { 0.25f, 1.0f }, { 0.5f, -0.2f }, { 0.75f, 0.6f }, { 1.0f, -1.0f } };
        rebuildTables();
    }

    void rebuildTables()
    {
        for (size_t l = 0; l < points.size(); ++l)
        {
            auto pts = points[l];
            std::sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
            if (pts.empty())
                pts = { { 0.0f, 0.0f }, { 1.0f, 0.0f } };

            for (int i = 0; i < kTableSize; ++i)
            {
                const float x = (float) i / (float) (kTableSize - 1);
                float y = pts.front().y;

                if (x >= pts.back().x)
                    y = pts.back().y;
                else
                    for (size_t p = 1; p < pts.size(); ++p)
                        if (x <= pts[p].x)
                        {
                            const float span = std::max (1.0e-6f, pts[p].x - pts[p - 1].x);
                            y = lerp (pts[p - 1].y, pts[p].y, (x - pts[p - 1].x) / span);
                            break;
                        }

                tables[l][(size_t) i] = std::clamp (y, -1.0f, 1.0f);
            }
        }
    }
};

/** One LFO instance (per voice, or the free-running global instance). Evaluated at control rate. */
class Lfo
{
public:
    struct Settings
    {
        LfoShape shape = LfoShape::Sine;
        float phaseOffset = 0.0f;
        float fadeSeconds = 0.0f;
        const float* customTable = nullptr;   // LfoCustomShapes::kTableSize entries
    };

    void reset (float startPhase, uint32_t seed) noexcept
    {
        phase = startPhase - std::floor (startPhase);
        rng.seed (seed);
        held = rng.nextBipolar();
        randomFrom = held;
        randomTo = rng.nextBipolar();
        elapsed = 0.0f;
    }

    /** Forces the phase (tempo sync to the host position). */
    void setPhase (float newPhase) noexcept
    {
        const float wrapped = newPhase - std::floor (newPhase);
        if (wrapped < phase - 0.5f)
            onWrap();
        phase = wrapped;
    }

    /** Advances the phase by rateHz for numSamples; returns the bipolar output (before depth). */
    float advance (const Settings& s, float rateHz, int numSamples, float sampleRate) noexcept
    {
        phase += rateHz * (float) numSamples / sampleRate;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            onWrap();
        }

        elapsed += (float) numSamples / sampleRate;
        return evaluate (s);
    }

    /** Output with the current phase (no advance). */
    float evaluate (const Settings& s) const noexcept
    {
        const float p = wrap (phase + s.phaseOffset);
        float v = 0.0f;

        switch (s.shape)
        {
            case LfoShape::Sine:         v = fastSin (p); break;
            case LfoShape::Triangle:     v = 4.0f * std::abs (wrap (p + 0.75f) - 0.5f) - 1.0f; break;
            case LfoShape::Saw:          v = 2.0f * p - 1.0f; break;
            case LfoShape::ReverseSaw:   v = 1.0f - 2.0f * p; break;
            case LfoShape::Square:       v = p < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::SampleHold:   v = held; break;
            case LfoShape::SmoothRandom:
            {
                const float t = 0.5f - 0.5f * std::cos (kPi * phase);
                v = lerp (randomFrom, randomTo, t);
                break;
            }
            case LfoShape::Custom:
            {
                if (s.customTable == nullptr) break;
                const float pos = p * (float) (LfoCustomShapes::kTableSize - 1);
                const int i = std::min ((int) pos, LfoCustomShapes::kTableSize - 2);
                v = lerp (s.customTable[i], s.customTable[i + 1], pos - (float) i);
                break;
            }
        }

        if (s.fadeSeconds > 0.0f && elapsed < s.fadeSeconds)
            v *= elapsed / s.fadeSeconds;

        return v;
    }

    float getPhase() const noexcept { return phase; }

private:
    static float wrap (float x) noexcept { return x - std::floor (x); }

    void onWrap() noexcept
    {
        held = rng.nextBipolar();
        randomFrom = randomTo;
        randomTo = rng.nextBipolar();
    }

    float phase = 0.0f;
    float held = 0.0f, randomFrom = 0.0f, randomTo = 0.0f;
    float elapsed = 0.0f;
    Random32 rng;
};

} // namespace nedd::dsp
