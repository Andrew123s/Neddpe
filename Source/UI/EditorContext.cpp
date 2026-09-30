#include "EditorContext.h"

namespace nedd::ui
{
EditorContext::EditorContext (NeddPEAudioProcessor& p)
    : processor (p), state (p.getState()), telemetry (p.getShared().telemetry)
{
    processor.readParameters (snapshot);
    routingCache.build (snapshot);
}

void EditorContext::tick()
{
    ++frame;
    peakLeft = telemetry.peakLeft.exchange (0.0f, std::memory_order_relaxed);
    peakRight = telemetry.peakRight.exchange (0.0f, std::memory_order_relaxed);
    processor.readParameters (snapshot);
    routingCache.build (snapshot);
    listeners.call ([] (Listener& l) { l.editorTick(); });
}

juce::Range<float> EditorContext::modulationRange (ModDest dest) const
{
    float lo = 0.0f, hi = 0.0f;
    for (int r = 0; r < routingCache.numRoutes; ++r)
    {
        const auto& route = routingCache.routes[(size_t) r];
        if (route.dest != dest)
            continue;
        if (route.polarity == ModPolarity::Bipolar)
        {
            lo -= std::abs (route.amount);
            hi += std::abs (route.amount);
        }
        else if (route.amount > 0.0f)
            hi += route.amount;
        else
            lo += route.amount;
    }
    return { lo, hi };
}

bool EditorContext::hasActiveVoice() const
{
    return telemetry.focusVoice.load (std::memory_order_relaxed) >= 0;
}

float EditorContext::liveModulation (ModDest dest) const
{
    return telemetry.destModulation[(size_t) dest].load (std::memory_order_relaxed);
}

void EditorContext::setParam (int index, float plainValue, const juce::String& undoName)
{
    if (undoName.isNotEmpty())
        processor.getUndoManager().beginNewTransaction (undoName);
    processor.setParameterPlain (index, plainValue);
    snapshot[index] = plainValue;
}

int EditorContext::addModulation (ModSource source, ModDest dest, float amount)
{
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const bool empty = snapshot.getInt (pid::mod (s, ModSlotField::Source)) == (int) ModSource::None
                        || snapshot.getInt (pid::mod (s, ModSlotField::Dest)) == (int) ModDest::None;
        if (! empty)
            continue;

        processor.getUndoManager().beginNewTransaction ("Add modulation");
        const bool bipolar = getModSourceInfo (source).bipolar;
        setParam (pid::mod (s, ModSlotField::Source), (float) source);
        setParam (pid::mod (s, ModSlotField::Dest), (float) dest);
        setParam (pid::mod (s, ModSlotField::Amount), amount);
        setParam (pid::mod (s, ModSlotField::Curve), (float) ModCurve::Linear);
        setParam (pid::mod (s, ModSlotField::Polarity), (float) (bipolar ? ModPolarity::Bipolar : ModPolarity::Unipolar));
        routingCache.build (snapshot);
        return s;
    }
    return -1;
}

void EditorContext::removeModulationSlot (int slot)
{
    processor.getUndoManager().beginNewTransaction ("Remove modulation");
    setParam (pid::mod (slot, ModSlotField::Source), (float) ModSource::None);
    setParam (pid::mod (slot, ModSlotField::Dest), (float) ModDest::None);
    setParam (pid::mod (slot, ModSlotField::Amount), 0.0f);
    routingCache.build (snapshot);
}

} // namespace nedd::ui
