#pragma once

#include "EditorContext.h"

namespace nedd::ui
{
/**
    Playable MPE keyboard for users without an MPE controller (and for multi-touch screens).

    Each touch gets its own MIDI channel (2..16) exactly like an MPE controller:
      - vertical position when pressing sets velocity
      - dragging sideways bends that note only (per-note pitch bend)
      - dragging vertically sends slide (CC74) for that note
      - the mouse wheel changes pressure of the held note
    Messages go through the same MPE input path as external MIDI.
*/
class ExpressionKeyboard : public juce::Component, public juce::SettableTooltipClient, private EditorContext::Listener
{
public:
    explicit ExpressionKeyboard (EditorContext& ctx);
    ~ExpressionKeyboard() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setLowestNote (int note) { lowestNote = note; resized(); repaint(); }
    int getLowestNote() const noexcept { return lowestNote; }

private:
    struct Touch
    {
        bool active = false;
        int source = -1;
        int note = 60;
        int channel = 2;
        float startX = 0.0f;
        float pressure = 0.5f;
    };

    void editorTick() override;
    int noteAt (juce::Point<float> p) const;
    juce::Rectangle<float> keyBounds (int note) const;
    bool isBlack (int note) const;
    int allocateChannel();
    void send (const juce::MidiMessage& m);
    Touch* touchFor (int source);
    float slideFor (float y) const;

    EditorContext& ctx;
    int lowestNote = 36;
    int numWhiteKeys = 29;
    float whiteWidth = 20.0f;
    std::array<Touch, 10> touches {};
    std::array<uint32_t, 17> channelAge {};
    uint32_t ageCounter = 0;
    std::array<float, 128> sounding {};
    std::array<juce::Colour, 128> soundingColour {};
};

} // namespace nedd::ui
