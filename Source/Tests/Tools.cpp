#include "Parameters/ParameterDefs.h"
#include "Presets/FactoryPresets.h"
#include "PluginProcessor/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

namespace nedd::test
{
namespace
{
    const char* groupName (ParamGroup g)
    {
        switch (g)
        {
            case ParamGroup::Oscillator: return "Oscillators";
            case ParamGroup::Filter:     return "Filter";
            case ParamGroup::Amp:        return "Amp";
            case ParamGroup::Envelope:   return "Envelopes";
            case ParamGroup::Lfo:        return "LFOs";
            case ParamGroup::Matrix:     return "Modulation matrix";
            case ParamGroup::Macro:      return "Macros";
            case ParamGroup::Voice:      return "Voicing";
            case ParamGroup::Mpe:        return "MPE";
            case ParamGroup::Tuning:     return "Tuning";
            case ParamGroup::Morph:      return "Morph";
            case ParamGroup::Master:     return "Master";
            case ParamGroup::Effects:    return "Effects";
            case ParamGroup::Arp:        return "Arpeggiator";
            case ParamGroup::Sequencer:  return "Sequencer";
        }
        return "Other";
    }

    const char* typeName (ParamType t)
    {
        switch (t)
        {
            case ParamType::Float:  return "float";
            case ParamType::Int:    return "int";
            case ParamType::Bool:   return "bool";
            case ParamType::Choice: return "choice";
        }
        return "";
    }
} // namespace

/** Writes docs/PARAMETERS.md from the central parameter table, so the reference can never drift. */
int dumpParameters (const juce::File& file)
{
    juce::String md;
    md << "# NeddPE parameter reference\n\n"
       << "Generated from `Source/Parameters/ParameterDefs.cpp` by `NeddPETests --dump-params`. Do not edit by hand.\n\n"
       << "- **ID** is the stable identifier stored in DAW projects and presets. IDs are never renamed.\n"
       << "- **Auto**: automatable by the host. Matrix routing choices are deliberately not automatable.\n"
       << "- **Morph**: included in A/B morphing and mutation. **Scope**: `voice` parameters are evaluated per note.\n\n"
       << "Total: " << pid::count << " parameters.\n";

    ParamGroup current = (ParamGroup) -1;
    for (const auto& d : getParamDefs())
    {
        // Collapse the repeated oscillator / envelope / LFO / matrix blocks into their first instance.
        const bool repeated = (d.group == ParamGroup::Oscillator && ! d.id.startsWith ("osc1_"))
                           || (d.group == ParamGroup::Lfo && d.id.startsWith ("lfo") && ! d.id.startsWith ("lfo1_"))
                           || (d.group == ParamGroup::Matrix && ! d.id.startsWith ("mod1_"))
                           || (d.group == ParamGroup::Envelope && ! d.id.startsWith ("aenv_"));
        if (repeated)
            continue;

        if (d.group != current)
        {
            current = d.group;
            md << "\n## " << groupName (d.group) << "\n\n";
            if (d.group == ParamGroup::Oscillator) md << "Shown for OSC 1 (`osc1_`); OSC 2 and 3 use `osc2_` / `osc3_`.\n\n";
            if (d.group == ParamGroup::Envelope)   md << "Shown for the amp envelope (`aenv_`); filter and mod envelopes use `fenv_` / `menv_`.\n\n";
            if (d.group == ParamGroup::Lfo)        md << "Shown for LFO 1 (`lfo1_`); LFO 2 and 3 use `lfo2_` / `lfo3_`.\n\n";
            if (d.group == ParamGroup::Matrix)     md << "Shown for slot 1 (`mod1_`); slots 2-16 use `mod2_` ... `mod16_`.\n\n";
            md << "| ID | Name | Type | Range | Default | Auto | Morph | Scope |\n|---|---|---|---|---|---|---|---|\n";
        }

        juce::String range;
        if (d.type == ParamType::Choice)
            range = d.choices.joinIntoString (" / ");
        else if (d.type == ParamType::Bool)
            range = "off / on";
        else
            range = (d.formatter ? d.formatter (d.range.start) : juce::String (d.range.start)) + " .. "
                  + (d.formatter ? d.formatter (d.range.end) : juce::String (d.range.end));

        const auto def = d.formatter ? d.formatter (d.defaultValue) : juce::String (d.defaultValue);
        md << "| `" << d.id << "` | " << d.name << " | " << typeName (d.type) << " | " << range.replace ("|", "/") << " | " << def
           << " | " << (d.automatable ? "yes" : "no") << " | " << (d.morphable ? "yes" : "") << " | " << (d.perVoice ? "voice" : "global") << " |\n";
    }

    md << "\n## Modulation sources\n\n| # | Source | Range | Per note |\n|---|---|---|---|\n";
    for (int s = 1; s < kNumModSources; ++s)
    {
        const auto& info = getModSourceInfo ((ModSource) s);
        md << "| " << s << " | " << info.name << " | " << (info.bipolar ? "-1..1" : "0..1") << " | " << (info.perVoice ? "yes" : "") << " |\n";
    }

    md << "\n## Modulation destinations\n\n| # | Destination | +100% equals | Scope |\n|---|---|---|---|\n";
    for (int d = 1; d < kNumModDests; ++d)
    {
        const auto& info = getModDestInfo ((ModDest) d);
        md << "| " << d << " | " << info.name << " | " << juce::String (info.range, 0) << " " << info.unit << " | "
           << (info.scope == ModScope::Voice ? "per note" : "global") << " |\n";
    }

    file.getParentDirectory().createDirectory();
    const bool ok = file.replaceWithText (md, false, false, "\n");
    std::cout << (ok ? "wrote " : "FAILED to write ") << file.getFullPathName() << "\n";
    return ok ? 0 : 1;
}

/** Writes every factory preset as a .neddpe file (the same format as user presets). */
/**
    Renders every factory preset to a WAV preview: a four-note MPE chord held for four seconds
    while each note's pressure swells and its slide moves, then released into the tail.
    Prints peak and RMS so silent or clipping presets stand out.
*/
int renderPresets (const juce::File& directory)
{
    directory.createDirectory();
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    constexpr double holdSeconds = 4.0, tailSeconds = 3.0;
    const int notes[] = { 48, 55, 62, 64 };
    int problems = 0;

    const auto& library = getFactoryPresets();
    for (int i = 0; i < (int) library.size(); ++i)
    {
        auto owner = std::make_unique<NeddPEAudioProcessor>();   // large: keep it off the stack
        auto& processor = *owner;
        processor.setPlayConfigDetails (0, 2, rate, block);
        processor.prepareToPlay (rate, block);
        processor.applyState (makeFactoryPreset (i), {});

        const int totalBlocks = (int) ((holdSeconds + tailSeconds) * rate / block);
        juce::AudioBuffer<float> out (2, totalBlocks * block);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;

        for (int b = 0; b < totalBlocks; ++b)
        {
            const double t = (double) b * block / rate;
            midi.clear();
            if (b == 0)
                for (int n = 0; n < 4; ++n)
                    midi.addEvent (juce::MidiMessage::noteOn (2 + n, notes[n], 0.75f), 0);
            if (t < holdSeconds && b % 4 == 0)
            {
                for (int n = 0; n < 4; ++n)
                {
                    const double x = t / holdSeconds;
                    const float pressure = (float) std::sin (juce::MathConstants<double>::pi * x) * (0.6f + 0.1f * (float) n);
                    const float slide = (float) (0.5 + 0.45 * std::sin (2.0 * juce::MathConstants<double>::pi * (x * 1.5 + 0.25 * n)));
                    midi.addEvent (juce::MidiMessage::channelPressureChange (2 + n, juce::jlimit (0, 127, (int) (pressure * 127.0f))), 0);
                    midi.addEvent (juce::MidiMessage::controllerEvent (2 + n, 74, juce::jlimit (0, 127, (int) (slide * 127.0f))), 0);
                }
            }
            if (b == (int) (holdSeconds * rate / block))
                for (int n = 0; n < 4; ++n)
                    midi.addEvent (juce::MidiMessage::noteOff (2 + n, notes[n], 0.5f), 0);

            buffer.clear();
            processor.processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, b * block, buffer, c, 0, block);
            processor.getShared().lfoShapes.collectGarbage();
        }
        processor.releaseResources();

        float peak = 0.0f;
        double sum = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < out.getNumSamples(); ++s)
            {
                const float v = out.getSample (c, s);
                peak = std::max (peak, std::abs (v));
                sum += (double) v * v;
            }
        const double rms = std::sqrt (sum / (2.0 * out.getNumSamples()));
        const auto& preset = library[(size_t) i];
        const bool bad = peak < 0.01f || peak > 0.999f;
        problems += bad ? 1 : 0;
        std::cout << (bad ? "[!!] " : "     ") << juce::String (preset.category).paddedRight (' ', 16) << juce::String (preset.name).paddedRight (' ', 22)
                  << " peak " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB   rms "
                  << juce::String (juce::Decibels::gainToDecibels ((float) rms), 1) << " dB\n";

        const auto file = directory.getChildFile (juce::File::createLegalFileName (juce::String (preset.category) + " - " + preset.name) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }

    std::cout << problems << " preset(s) silent or clipping\n";
    return problems == 0 ? 0 : 1;
}

int exportPresets (const juce::File& directory)
{
    int written = 0;
    const auto& library = getFactoryPresets();
    for (int i = 0; i < (int) library.size(); ++i)
    {
        const auto state = makeFactoryPreset (i);
        const auto dir = directory.getChildFile (juce::File::createLegalFileName (state.category));
        dir.createDirectory();
        const auto file = dir.getChildFile (juce::File::createLegalFileName (state.name) + ".neddpe");
        if (auto xml = state.toValueTree().createXml(); xml != nullptr && xml->writeTo (file))
            ++written;
    }
    std::cout << "exported " << written << " of " << library.size() << " factory presets to " << directory.getFullPathName() << "\n";
    return written == (int) library.size() ? 0 : 1;
}

} // namespace nedd::test
