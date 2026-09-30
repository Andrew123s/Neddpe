#pragma once

#include "EditorContext.h"

namespace nedd::ui
{
/** Rolling per-voice history of expression, sampled from the engine telemetry every frame. */
class ExpressionHistory : private EditorContext::Listener
{
public:
    static constexpr int kLength = 150;   // ~5 s at 30 fps

    struct Sample
    {
        bool active = false;
        bool gate = false;
        uint32_t noteId = 0;
        int note = 60;
        float bend = 0.0f;       // semitones
        float pressure = 0.0f;
        float slide = 0.0f;
        float velocity = 0.0f;
        float level = 0.0f;
    };

    explicit ExpressionHistory (EditorContext& ctx);
    ~ExpressionHistory() override;

    /** age 0 = newest. */
    const Sample& get (int voice, int age) const noexcept
    {
        const int index = (head - age + kLength * 2) % kLength;
        return samples[(size_t) voice][(size_t) index];
    }

    int getWriteCount() const noexcept { return writes; }

private:
    void editorTick() override;

    EditorContext& ctx;
    std::array<std::array<Sample, (size_t) kLength>, (size_t) kPhysicalVoices> samples {};
    int head = 0;
    int writes = 0;
};

/**
    The expression field: every sounding note is a live object.
    X = sounding pitch (key + per-note bend), Y = slide, size = pressure, brightness = velocity,
    with a fading trail of the last 1.5 s so gestures are visible, not just positions.
*/
class ExpressionField : public juce::Component, private EditorContext::Listener
{
public:
    ExpressionField (EditorContext& ctx, ExpressionHistory& history);
    ~ExpressionField() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    EditorContext& ctx;
    ExpressionHistory& history;
    float lowNote = 36.0f, highNote = 84.0f;
};

/** One row per sounding voice with bars for every dimension, voice slot and modulation activity. */
class VoiceLanes : public juce::Component, private EditorContext::Listener
{
public:
    explicit VoiceLanes (EditorContext& ctx);
    ~VoiceLanes() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    EditorContext& ctx;
};

/** Scrolling pitch / pressure / slide lanes: X = time, Y = expression, one line per note. */
class ExpressionTimeline : public juce::Component, private EditorContext::Listener
{
public:
    ExpressionTimeline (EditorContext& ctx, ExpressionHistory& history);
    ~ExpressionTimeline() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    EditorContext& ctx;
    ExpressionHistory& history;
};

} // namespace nedd::ui
