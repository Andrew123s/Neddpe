#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DSP/DspMath.h"
#include "Parameters/ParameterDefs.h"

namespace nedd::fx
{
/** Linear per-sample ramp towards a per-block target (removes zipper noise from block-rate parameters). */
struct Ramp
{
    float current = 0.0f, step = 0.0f;
    bool initialised = false;

    void setTarget (float target, int numSamples) noexcept
    {
        if (! initialised) { current = target; initialised = true; }
        step = numSamples > 0 ? (target - current) / (float) numSamples : 0.0f;
    }

    float next() noexcept { current += step; return current; }
};

/** Fractional delay line with linear interpolation. Sized once in prepare(). */
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign ((size_t) maxSamples + 4, 0.0f);
        writeIndex = 0;
    }

    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        if (++writeIndex >= (int) buffer.size())
            writeIndex = 0;
    }

    /** delay in samples (>= 1) behind the most recently pushed sample. */
    float read (float delaySamples) const noexcept
    {
        // Integer indexing: float position arithmetic near the wrap point can round to
        // exactly `size` and read out of bounds.
        const int size = (int) buffer.size();
        delaySamples = std::clamp (delaySamples, 1.0f, (float) (size - 2));
        const int whole = (int) delaySamples;
        const float frac = delaySamples - (float) whole;

        int newer = writeIndex - whole;       // sample at delay `whole`
        if (newer < 0) newer += size;
        int older = newer - 1;                // sample at delay `whole + 1`
        if (older < 0) older += size;

        return buffer[(size_t) newer] + frac * (buffer[(size_t) older] - buffer[(size_t) newer]);
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};

/** Transposed direct form II biquad. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void set (const std::array<float, 6>& c) noexcept
    {
        const float inv = 1.0f / c[3];
        b0 = c[0] * inv; b1 = c[1] * inv; b2 = c[2] * inv;
        a1 = c[4] * inv; a2 = c[5] * inv;
    }

    float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
};

/** DC blocker (first-order high-pass at ~10 Hz). */
struct DcBlocker
{
    float x1[2] {}, y1[2] {};
    float r = 0.9995f;

    void prepare (float sampleRate) noexcept { r = 1.0f - (2.0f * dsp::kPi * 10.0f / sampleRate); }
    float process (float x, int ch) noexcept
    {
        const float y = x - x1[ch] + r * y1[ch];
        x1[ch] = x;
        y1[ch] = y;
        return y;
    }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0f; }
};

// ---------------------------------------------------------------------------------------------

/** Waveshaping distortion, oversampled by the CPU-quality setting to keep aliasing down. */
class Distortion
{
public:
    void prepare (float sampleRate, int maxBlock);
    void reset();
    void process (float* l, float* r, int n, DistortionType type, float drive, float tone, float mix, int oversamplingFactorLog2) noexcept;

private:
    static float shape (float x, DistortionType type) noexcept;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;   // 2x, 4x, 8x
    Ramp driveRamp, mixRamp;
    float toneState[2] {};
    float sampleRate = 44100.0f;
    DcBlocker dc;
    std::vector<float> dryL, dryR;
};

/** Gentle tape-style saturation with a warmth (low tilt / high roll-off) control. */
class Saturation
{
public:
    void prepare (float newSampleRate) { sampleRate = newSampleRate; reset(); }
    void reset() { lp[0] = lp[1] = 0.0f; dc.reset(); dc.prepare (sampleRate); }
    void process (float* l, float* r, int n, float drive, float warmth, float mix) noexcept;

private:
    float sampleRate = 44100.0f;
    float lp[2] {};
    DcBlocker dc;
    Ramp driveRamp, mixRamp;
};

/** Bit-depth and sample-rate reduction. */
class Bitcrusher
{
public:
    void reset() { held[0] = held[1] = 0.0f; counter = 0.0f; }
    void process (float* l, float* r, int n, float bits, float downsample, float mix) noexcept;

private:
    float held[2] {};
    float counter = 0.0f;
    Ramp mixRamp;
};

/** Chorus and flanger share this stereo modulated delay. */
class ModulatedDelay
{
public:
    enum class Mode { Chorus, Flanger };
    explicit ModulatedDelay (Mode m) : mode (m) {}

    void prepare (float sampleRate);
    void reset();
    void process (float* l, float* r, int n, float rateHz, float depth, float feedback, float mix) noexcept;

private:
    Mode mode;
    float sampleRate = 44100.0f;
    DelayLine lines[2];
    float phase = 0.0f;
    float feedbackState[2] {};
    Ramp mixRamp;
};

/** Six-stage stereo phaser with feedback. */
class Phaser
{
public:
    void prepare (float newSampleRate) { sampleRate = newSampleRate; reset(); }
    void reset();
    void process (float* l, float* r, int n, float rateHz, float depth, float feedback, float mix) noexcept;

private:
    static constexpr int kStages = 6;
    float sampleRate = 44100.0f;
    float phase = 0.0f;
    float x1[2][kStages] {}, y1[2][kStages] {};
    float last[2] {};
    float coefficient[2] {};
    int counter = 0;
    Ramp mixRamp;
};

/** Stereo / ping-pong delay fed by the per-voice delay sends. */
class StereoDelay
{
public:
    void prepare (float sampleRate);
    void reset();
    /** Reads the send bus (inL/inR) and adds the delayed signal to outL/outR. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                  float delaySamples, float feedback, float damping, bool pingPong, float returnLevel) noexcept;

private:
    float sampleRate = 44100.0f;
    DelayLine lines[2];
    float smoothedDelay = -1.0f;
    float damp[2] {};
    Ramp returnRamp, feedbackRamp;
};

/** 8-line feedback-delay-network reverb with damping, pre-delay and slow modulation. */
class FdnReverb
{
public:
    void prepare (float sampleRate);
    void reset();
    void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                  float size, float damping, float predelayMs, float width, float returnLevel) noexcept;

private:
    static constexpr int kLines = 8;
    float sampleRate = 44100.0f;
    DelayLine predelay[2];
    DelayLine lines[kLines];
    float lowpass[kLines] {};
    float baseLength[kLines] {};
    float modPhase = 0.0f;
    Ramp returnRamp;
};

/** Low shelf / peak / high shelf equaliser. */
class Equalizer
{
public:
    void prepare (float newSampleRate) { sampleRate = newSampleRate; reset(); }
    void reset() { low.reset(); mid.reset(); high.reset(); lastSignature = -1.0f; }
    void process (float* l, float* r, int n, float lowGainDb, float midFreq, float midGainDb, float highGainDb) noexcept;

private:
    float sampleRate = 44100.0f;
    Biquad low, mid, high;
    float lastSignature = -1.0f;
};

/** Stereo-linked feed-forward compressor with a 6 dB soft knee. */
class Compressor
{
public:
    void prepare (float newSampleRate) { sampleRate = newSampleRate; reset(); }
    void reset() { envelopeDb = -120.0f; }
    void process (float* l, float* r, int n, float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupDb) noexcept;
    float getGainReductionDb() const noexcept { return lastReduction; }

private:
    float sampleRate = 44100.0f;
    float envelopeDb = -120.0f;
    float lastReduction = 0.0f;
};

} // namespace nedd::fx
