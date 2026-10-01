#pragma once

#include "ModTypes.h"
#include <array>
#include <atomic>

namespace nedd
{
constexpr int kMaxVoices = 32;          // user-selectable polyphony limit
constexpr int kPhysicalVoices = 40;     // extra voices let stolen notes fade out without clicks

/**
    Lock-free telemetry written by the audio thread and read by the UI at frame rate.
    Individual fields may be from slightly different blocks; that is fine for display.
*/
struct VoiceTelemetry
{
    std::atomic<bool> active { false };
    std::atomic<bool> gate { false };
    std::atomic<uint32_t> noteId { 0 };
    std::atomic<int> noteNumber { 60 };
    std::atomic<int> channel { 1 };
    std::atomic<int> origin { 0 };
    std::atomic<float> pitch { 0.0f };          // semitones incl. per-note bend
    std::atomic<float> pressure { 0.0f };       // after the calibration curve
    std::atomic<float> slide { 0.0f };
    std::atomic<float> velocity { 0.0f };
    std::atomic<float> releaseVelocity { 0.0f };
    std::atomic<float> rawPressure { 0.0f };    // before the calibration curve
    std::atomic<float> rawSlide { 0.0f };
    std::atomic<float> ampEnv { 0.0f };
    std::atomic<float> cutoffHz { 1000.0f };
    std::atomic<float> modActivity { 0.0f };    // summed magnitude of per-voice modulation
    std::atomic<int64_t> startSample { 0 };
};

struct EngineTelemetry
{
    std::array<VoiceTelemetry, (size_t) kPhysicalVoices> voices;
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> focusVoice { -1 };         // most recently started voice
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };

    // Modulation: values of the focus voice (per-voice destinations) and the global evaluation.
    std::array<std::atomic<float>, (size_t) kNumModSlots> slotContribution {};
    std::array<std::atomic<float>, (size_t) kNumModDests> destModulation {};
    std::array<std::atomic<float>, (size_t) kNumModSources> sourceValue {};

    // Last incoming raw MIDI expression, for the calibration page.
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<float> lastPitchBend { 0.0f };
    std::atomic<float> lastPressure { 0.0f };
    std::atomic<float> lastSlide { 0.0f };
    std::atomic<int> midiActivity { 0 };

    std::atomic<double> ppq { 0.0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<bool> hostTransportAvailable { false };   // the host provides a song position (not the standalone app)
    std::atomic<int> arpStep { -1 };
    std::atomic<int> seqStep { -1 };
    std::atomic<double> clipPosition { 0.0 };
    std::atomic<bool> clipPlaying { false };
    std::atomic<bool> clipRecording { false };
    std::atomic<int> arpHeld { 0 };
    std::atomic<float> cpuLoad { 0.0f };
};

} // namespace nedd
