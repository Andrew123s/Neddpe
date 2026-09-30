#pragma once

#include "EditorContext.h"

namespace nedd::ui
{
/**
    DAHDSR display with draggable nodes: the attack peak sets attack time, the decay node sets
    decay time and sustain level, the release node sets release time. For the amp envelope the
    current level of every sounding note is shown on the level axis.
*/
class EnvelopeDisplay : public juce::Component, public juce::SettableTooltipClient, private EditorContext::Listener
{
public:
    EnvelopeDisplay (EditorContext& ctx, int envIndex, juce::Colour colour);
    ~EnvelopeDisplay() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Node { None, Attack, Decay, Release };

    struct Geometry
    {
        juce::Rectangle<float> area;
        float xDelay, xAttack, xHold, xDecay, xSustainEnd, xRelease;
        float segmentWidth;
    };

    void editorTick() override;
    Geometry computeGeometry() const;
    Node hitTest (juce::Point<float>) const;
    float valueOf (EnvField f) const { return ctx.param (pid::env (envIndex, f)); }
    static float timeToWidth (float seconds);
    static float widthToTime (float fraction);

    EditorContext& ctx;
    const int envIndex;
    const juce::Colour colour;
    Node dragging = Node::None, hover = Node::None;
    std::array<float, 10> lastValues {};
    std::array<float, (size_t) kPhysicalVoices> voiceLevels {};
};

/** Oscillator view: a single cycle for analog/FM/noise, a stacked 3-D wavetable for WT mode. */
class WaveDisplay : public juce::Component, private EditorContext::Listener
{
public:
    WaveDisplay (EditorContext& ctx, int oscIndex);
    ~WaveDisplay() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    void rebuild();

    EditorContext& ctx;
    const int oscIndex;
    std::vector<float> cycle;
    float signature = -1.0f;
    float livePosition = -1.0f;
};

/** Filter magnitude response with one marker per sounding note at that note's own cutoff. */
class FilterDisplay : public juce::Component, private EditorContext::Listener
{
public:
    explicit FilterDisplay (EditorContext& ctx);
    ~FilterDisplay() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    float magnitudeDb (float frequency, float cutoff, float resonance, FilterType type) const;

    EditorContext& ctx;
    float signature = -1.0f;
    struct Marker { float cutoff; juce::Colour colour; float level; };
    std::vector<Marker> markers;
};

/** Stereo peak meter fed by the engine telemetry. */
class OutputMeter : public juce::Component, private EditorContext::Listener
{
public:
    explicit OutputMeter (EditorContext& ctx);
    ~OutputMeter() override;
    void paint (juce::Graphics&) override;

private:
    void editorTick() override;
    EditorContext& ctx;
    float left = 0.0f, right = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int holdCounterL = 0, holdCounterR = 0;
};

} // namespace nedd::ui
