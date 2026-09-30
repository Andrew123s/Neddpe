#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Synth/SynthEngine.h"

namespace nedd::test
{
/** Drives a SynthEngine offline: send MIDI, render blocks, inspect voices. */
struct EngineHarness
{
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlock = 256;

    EngineShared shared;
    SynthEngine engine { shared };
    ParamSnapshot params;
    TransportInfo transport;
    juce::AudioBuffer<float> buffer { 2, kBlock };
    juce::MidiBuffer pending;
    double lastPeak = 0.0;
    bool sawNonFinite = false;

    EngineHarness()
    {
        params.setToDefaults();
        engine.prepare (kSampleRate, kBlock);
        transport.sampleRate = kSampleRate;
        transport.bpm = 120.0;
        transport.beatsPerSample = transport.bpm / 60.0 / kSampleRate;
    }

    void set (int index, float plain) { params[index] = plain; }

    void midi (const juce::MidiMessage& m, int offset = 0) { pending.addEvent (m, offset); }

    void noteOn (int ch, int note, float velocity = 0.8f) { midi (juce::MidiMessage::noteOn (ch, note, velocity)); }
    void noteOff (int ch, int note, float releaseVelocity = 0.5f) { midi (juce::MidiMessage::noteOff (ch, note, releaseVelocity)); }
    void pitchBend (int ch, float normalised) { midi (juce::MidiMessage::pitchWheel (ch, juce::jlimit (0, 16383, (int) std::lround (8192.0f + normalised * 8191.0f)))); }
    void pressure (int ch, float value) { midi (juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, (int) std::lround (value * 127.0f)))); }
    void slide (int ch, float value) { midi (juce::MidiMessage::controllerEvent (ch, 74, juce::jlimit (0, 127, (int) std::lround (value * 127.0f)))); }

    /** Renders numBlocks blocks; returns the peak absolute sample. */
    float render (int numBlocks = 1)
    {
        float peak = 0.0f;
        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            engine.process (buffer, pending, params, transport);
            pending.clear();
            transport.ppqAtBlockStart += transport.beatsPerSample * kBlock;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float s = buffer.getSample (c, i);
                    if (! std::isfinite (s)) sawNonFinite = true;
                    peak = std::max (peak, std::abs (s));
                }
        }
        lastPeak = peak;
        return peak;
    }

    float renderSeconds (double seconds) { return render (std::max (1, (int) std::ceil (seconds * kSampleRate / kBlock))); }

    const Voice* voiceForNote (int note) const
    {
        const auto& vm = engine.getVoiceManager();
        for (int i = 0; i < kPhysicalVoices; ++i)
        {
            const auto& v = vm.getVoice (i);
            if (v.isActive() && ! v.isStealing() && v.getState().noteNumber == note)
                return &v;
        }
        return nullptr;
    }

    int soundingVoices() const { return engine.getVoiceManager().getNumSoundingNotes(); }
};

} // namespace nedd::test
