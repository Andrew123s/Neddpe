#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace nedd
{
/**
    A band-limited, mip-mapped wavetable.

    Each frame is stored at kNumMips resolutions of harmonic content: mip m contains harmonics
    1 .. (kMaxHarmonics >> m). The oscillator selects the mip whose highest harmonic stays
    below the alias limit for the current pitch, so scanning and pitch sweeps stay alias-free
    without oversampling.
*/
class Wavetable
{
public:
    static constexpr int kFrameSize = 2048;
    static constexpr int kFrameStride = kFrameSize + 1;   // one guard sample for interpolation
    static constexpr int kMaxHarmonics = kFrameSize / 2;
    static constexpr int kNumMips = 10;

    juce::String name;
    int numFrames = 0;
    std::vector<float> samples;

    const float* getFrame (int frame, int mip) const noexcept
    {
        return samples.data() + ((size_t) frame * kNumMips + (size_t) mip) * kFrameStride;
    }

    float* getFrameForWriting (int frame, int mip) noexcept
    {
        return samples.data() + ((size_t) frame * kNumMips + (size_t) mip) * kFrameStride;
    }

    /** Chooses a mip level for a phase increment (cycles per sample).
        At the base rate, harmonics above Nyquist are allowed as long as their alias folds back
        above ~18 kHz. When oversampled (strict), everything stays below the oversampled Nyquist
        frequency so the decimation filter can remove it cleanly. */
    static int selectMip (float increment, float sampleRate, bool strict = false) noexcept
    {
        const float allowedTop = strict ? sampleRate * 0.5f : std::max (sampleRate * 0.5f, sampleRate - 18000.0f);
        const float f0 = std::max (std::abs (increment) * sampleRate, 1.0e-3f);
        const float maxHarmonic = allowedTop / f0;

        int mip = 0;
        while (mip < kNumMips - 1 && (float) (kMaxHarmonics >> mip) > maxHarmonic)
            ++mip;
        return mip;
    }

    /** phase in [0, 1), position in [0, 1] across frames. */
    float sample (float phase, float position, int mip) const noexcept
    {
        const float framePos = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
        const int frameA = std::min ((int) framePos, numFrames - 1);
        const int frameB = std::min (frameA + 1, numFrames - 1);
        const float frameFrac = framePos - (float) frameA;

        const float pos = phase * (float) kFrameSize;
        const int index = juce::jlimit (0, kFrameSize - 1, (int) pos);
        const float frac = pos - (float) index;

        const float* a = getFrame (frameA, mip);
        const float sa = a[index] + frac * (a[index + 1] - a[index]);

        if (frameFrac <= 0.0f || frameA == frameB)
            return sa;

        const float* b = getFrame (frameB, mip);
        const float sb = b[index] + frac * (b[index + 1] - b[index]);
        return sa + frameFrac * (sb - sa);
    }
};

/** Names of the built-in wavetables, in bank order (parameter choice list: append only). */
juce::StringArray getWavetableNames();

/**
    Builds a band-limited, mip-mapped wavetable from single-cycle frames of kFrameSize samples
    each (e.g. an imported wavetable file). DC is removed and each frame is normalised.
    Message thread only (allocates and runs FFTs).
*/
Wavetable buildWavetableFromFrames (const juce::String& name, const float* frames, int numFrames);

/** The factory wavetable bank, generated procedurally once per process and shared by all instances. */
class WavetableBank
{
public:
    static const WavetableBank& getInstance();

    int size() const noexcept { return (int) tables.size(); }
    const Wavetable& get (int index) const noexcept { return tables[(size_t) juce::jlimit (0, size() - 1, index)]; }

private:
    WavetableBank();
    std::vector<Wavetable> tables;
};

} // namespace nedd
