#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "DSP/Wavetable.h"
#include "Modulation/ModTypes.h"
#include <memory>

namespace nedd
{
/**
    An audio sample for the Sample and Granular engines. Immutable once built.

    Each channel is stored with kGuard zero samples before and after the audio so cubic
    interpolation can read one sample either side without bounds checks.
*/
struct SampleData
{
    static constexpr int kGuard = 4;

    juce::String name;
    double sampleRate = 48000.0;
    int length = 0;                    // frames of audio (excluding guards)
    bool stereo = false;
    std::vector<float> left, right;    // length + 2 * kGuard each; right is empty for mono

    // Encoded copy for saving (computed once, so saving a project never re-encodes).
    juce::String storedEncoding, storedData;

    const float* channel (int ch) const noexcept { return (ch == 0 || ! stereo ? left.data() : right.data()) + kGuard; }

    /** 4-point Hermite interpolation. pos must be in [-1, length]. */
    static float readCubic (const float* data, double pos) noexcept
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - (double) i);
        const float xm1 = data[i - 1], x0 = data[i], x1 = data[i + 1], x2 = data[i + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    /** Builds from channel data (1 or 2 channels). */
    static std::shared_ptr<const SampleData> create (const juce::String& name, double sampleRate,
                                                     const float* left, const float* right, int length);
};

/** An imported wavetable: the source frames (kept for saving) and the band-limited table built from them. */
struct UserWavetable
{
    juce::String name;
    int numFrames = 0;
    std::vector<float> frames;         // numFrames * Wavetable::kFrameSize source samples
    Wavetable table;
    juce::String storedEncoding, storedData;

    static std::shared_ptr<const UserWavetable> create (const juce::String& name, std::vector<float> frames);
};

/**
    User content for the three oscillators: an imported wavetable and an imported sample per
    oscillator. Copying is cheap (shared, immutable content).

    Part of the sound (PresetState): saved in presets and projects, restored by undo. The engine
    receives a copy through a RealtimeExchange, so the audio thread never frees any of it.
*/
struct OscillatorAssets
{
    struct Slot
    {
        std::shared_ptr<const UserWavetable> wavetable;
        std::shared_ptr<const SampleData> sample;
    };

    std::array<Slot, (size_t) kNumOscillators> slots;

    bool operator== (const OscillatorAssets& other) const noexcept
    {
        for (size_t i = 0; i < slots.size(); ++i)
            if (slots[i].wavetable != other.slots[i].wavetable || slots[i].sample != other.slots[i].sample)
                return false;
        return true;
    }
    bool operator!= (const OscillatorAssets& other) const noexcept { return ! operator== (other); }

    juce::ValueTree toValueTree() const;
    static OscillatorAssets fromValueTree (const juce::ValueTree& tree);
};

namespace assets
{
    /** Longest sample accepted by import (seconds). */
    constexpr double kMaxSampleSeconds = 60.0;
    /** Most frames accepted in an imported wavetable. */
    constexpr int kMaxWavetableFrames = 256;

    /** Reads an audio file as a sample. Returns nullptr and sets error on failure. Message thread. */
    std::shared_ptr<const SampleData> loadSample (const juce::File& file, juce::String& error);

    /**
        Reads an audio file as a wavetable. Frame size detection: a Serum-style "clm " marker
        if present, otherwise the first of 2048, 1024, 512, 256 or 4096 that divides the length;
        a file shorter than 4096 samples that divides by none of them is one single cycle.
        Frames are resampled to 2048 samples. Message thread.
    */
    std::shared_ptr<const UserWavetable> loadWavetable (const juce::File& file, juce::String& error);

    /** File patterns for the import dialogs. */
    juce::String getAudioFileWildcard();

    /**
        The source the Granular and Sample engines play when no sample has been imported: a
        4-second evolving vowel texture pitched at middle C (note 60), generated at start-up.
    */
    const SampleData& getBuiltInSample();
}

} // namespace nedd
