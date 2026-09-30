#include "HelpText.h"
#include "Parameters/ParameterDefs.h"
#include <unordered_map>

namespace nedd::ui
{
juce::String helpForParam (int paramIndex)
{
    static const std::unordered_map<std::string, const char*> help {
        { "flt_cutoff", "Base cutoff frequency. Each note adds its own modulation on top (pressure, slide, envelope, key tracking), so notes in a chord can sit at different brightness." },
        { "flt_reso", "Resonance. The ladder mode self-oscillates near the top of the range." },
        { "flt_drive", "Pre-filter saturation, applied per note." },
        { "flt_keytrack", "How far the cutoff follows the note's pitch, including its MPE pitch bend." },
        { "flt_env", "Filter envelope depth (+/- 8 octaves at 100%)." },
        { "flt_vel", "How much velocity scales the filter envelope depth." },
        { "flt_mix", "Blend between the unfiltered and filtered signal." },
        { "amp_level", "Per-note output level." },
        { "amp_vel", "Velocity sensitivity of the note level." },
        { "amp_pressure", "MPE Pressure controls the loudness of an individual note. At 100% a note is silent until pressed." },
        { "mpe_bendrange", "Per-note pitch-bend range. Must match your controller (48 is the MPE default; LinnStrument often uses 24)." },
        { "mpe_masterbend", "Range of the master-channel pitch wheel, which bends every note of the zone." },
        { "mpe_pitchsens", "Scales the MPE Pitch source. 100% tracks your finger exactly." },
        { "mpe_prescurve", "Pressure response curve. Positive values make light touches count more." },
        { "mpe_slidecurve", "Slide (CC74) response curve." },
        { "mpe_velcurve", "Velocity response curve." },
        { "mpe_smooth", "Smooths per-note expression to remove zipper noise from 7-bit controllers." },
        { "mpe_mode", "MPE zone layout. 'Off (Legacy)' treats every channel as its own expression channel, which also suits ordinary keyboards." },
        { "voice_poly", "Maximum simultaneous notes. Stolen notes fade out over 3 ms." },
        { "voice_glide", "Portamento time." },
        { "quality", "Control-rate resolution for modulation (Eco 64 / Normal 32 / High 16 / Ultra 8 samples) and distortion oversampling." },
        { "morph_pos", "Blend from the live patch (A) to the stored B snapshot. Route MPE Slide to 'Morph A/B' to morph each note independently." },
        { "scale_bendq", "Pulls per-note pitch bends toward the nearest scale degree." },
        { "delay_send", "Per-note send into the delay. Modulate it from pressure or velocity for notes that echo only when played harder." },
        { "reverb_send", "Per-note send into the reverb." },
        { "osc_oversampling", "Runs the oscillators of a note at 2x or 4x the sample rate and filters the result back down, so cross-FM, "
                              "ring modulation, hard sync and extreme FM-engine settings do not fold harmonics back as aliasing. Auto "
                              "oversamples 2x only the notes that use cross-FM, ring modulation or sync (decided when the note starts). "
                              "2x / 4x apply to every note and cost about 2x / 4x the oscillator CPU." },
    };

    const auto& def = getParamDef (paramIndex);
    const auto it = help.find (def.id.toStdString());

    if (it != help.end())
        return def.name + "\n" + it->second;

    const auto id = def.id;
    if (id.endsWith ("_wtpos"))  return def.name + "\nWavetable engine: position in the table. Granular: where in the sample grains are read. "
                                                   "Sample: the start point. Route MPE Slide here to change each note separately.";
    if (id.endsWith ("_fm"))     return def.name + "\nPhase modulation from the selected FM source oscillator (oversampled in Auto mode to limit aliasing).";
    if (id.endsWith ("_root"))   return def.name + "\nThe key at which the sample plays at its recorded pitch.";
    if (id.endsWith ("_loop"))   return def.name + "\nSample engine: loop from the start point to the end with a short cross-fade, or play once.";
    if (id.endsWith ("_gsize"))  return def.name + "\nLength of each grain. Modulate with the 'Grain Size' destination.";
    if (id.endsWith ("_gdensity")) return def.name + "\nGrains started per second (up to 32 overlap). Modulate with the 'Grain Density' destination.";
    if (id.endsWith ("_gspray")) return def.name + "\nRandom scatter of each grain's read position around POSITION.";
    if (id.endsWith ("_gpitch")) return def.name + "\nRandom pitch offset per grain, up to this many semitones either way.";
    if (id.endsWith ("_unison")) return def.name + "\nNumber of stacked, detuned copies of the oscillator.";
    if (id.endsWith ("_ring"))   return def.name + "\nRing modulation with the previous oscillator.";
    if (id.endsWith ("_sync"))   return def.name + "\nHard-syncs this oscillator to the previous one. Each reset is band-limited (PolyBLEP).";
    if (id.endsWith ("_route"))  return def.name + "\nSend this oscillator through the filter, or straight to the amp.";
    if (id.startsWith ("macro")) return def.name + "\nPerformance macro: a modulation source you can route anywhere in the matrix and automate from the DAW.";

    return def.name;
}

} // namespace nedd::ui
