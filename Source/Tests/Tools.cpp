#include "Parameters/ParameterDefs.h"
#include "Presets/FactoryPresets.h"
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
