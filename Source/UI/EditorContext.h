#pragma once

#include "Modulation/ModMatrix.h"
#include "PluginProcessor/PluginProcessor.h"
#include "Theme.h"

namespace nedd::ui
{
/**
    Shared state for every UI component: the processor, a frame-rate tick, and a message-thread
    copy of the parameters and routing so widgets can explain their modulation.
*/
class EditorContext
{
public:
    explicit EditorContext (NeddPEAudioProcessor& p);

    NeddPEAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    EngineTelemetry& telemetry;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editorTick() = 0;
    };

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Called by the editor at ~30 Hz. */
    void tick();

    const ParamSnapshot& params() const noexcept { return snapshot; }
    const ModRouting& routing() const noexcept { return routingCache; }
    float param (int index) const noexcept { return snapshot[index]; }

    /** Range of possible offsets for a destination from all routes, in destination units. */
    juce::Range<float> modulationRange (ModDest dest) const;
    /** Current offset for a destination (focus voice for per-voice destinations). */
    float liveModulation (ModDest dest) const;
    /** Whether any active note currently exists (so the live offset is meaningful). */
    bool hasActiveVoice() const;

    /** Adds a route in the first free matrix slot. Returns the slot, or -1 if the matrix is full. */
    int addModulation (ModSource source, ModDest dest, float amount);
    void removeModulationSlot (int slot);

    void setParam (int index, float plainValue, const juce::String& undoName = {});

    std::function<void (int pageIndex)> showPage;
    std::function<void()> refreshPreset;

    uint64_t getFrameCounter() const noexcept { return frame; }

    /** Output peaks since the previous frame (read once per frame so several meters can share them). */
    float getPeakLeft() const noexcept { return peakLeft; }
    float getPeakRight() const noexcept { return peakRight; }

private:
    ParamSnapshot snapshot;
    ModRouting routingCache;
    juce::ListenerList<Listener> listeners;
    uint64_t frame = 0;
    float peakLeft = 0.0f, peakRight = 0.0f;
};

} // namespace nedd::ui
