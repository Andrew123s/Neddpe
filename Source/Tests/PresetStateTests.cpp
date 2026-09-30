#include "TestHelpers.h"
#include "PluginProcessor/PluginProcessor.h"

namespace nedd::test
{
namespace
{
    /** Loads a preset state into a harness (params + structured data) and plays an expressive chord. */
    float playPreset (const PresetState& state, bool& finite, float& lateLevel)
    {
        auto harness = std::make_unique<EngineHarness>();
        auto& h = *harness;
        h.params = state.params;
        h.shared.pattern.publish (std::make_unique<SequencerPattern> (state.pattern));
        h.shared.arpPattern.publish (std::make_unique<ArpPattern> (state.arpPattern));
        auto shapes = std::make_unique<dsp::LfoCustomShapes> (state.lfoShapes);
        h.shared.lfoShapes.publish (std::move (shapes));
        if (state.hasMorphTarget)
        {
            auto target = std::make_unique<MorphTarget>();
            target->plain = state.morphTarget;
            for (const auto& d : getParamDefs())
                target->normalised[(size_t) d.index] = d.range.convertTo0to1 (state.morphTarget[d.index]);
            h.shared.morphTarget.publish (std::move (target));
        }
        h.transport.hostPlaying = true;
        h.transport.hostTransportAvailable = true;

        for (int n = 0; n < 3; ++n)
        {
            h.pressure (2 + n, 0.6f);
            h.slide (2 + n, 0.5f);
            h.noteOn (2 + n, 48 + n * 7, 0.8f);
        }
        float peak = h.renderSeconds (1.5);
        for (int n = 0; n < 3; ++n)
            h.noteOff (2 + n, 48 + n * 7);
        peak = std::max (peak, h.renderSeconds (0.5));
        lateLevel = h.renderSeconds (0.1);
        finite = ! h.sawNonFinite;
        return peak;
    }
} // namespace

class PresetTests : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets and state", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Factory library: at least 30 presets covering every category");
        {
            const auto& library = getFactoryPresets();
            expect (library.size() >= 30, juce::String ((int) library.size()) + " presets");
            for (const auto& categoryName : getPresetCategories())
            {
                if (categoryName == "User") continue;
                const auto count = std::count_if (library.begin(), library.end(), [&] (const FactoryPreset& p) { return categoryName == p.category; });
                expect (count >= 3, categoryName + " has " + juce::String ((int) count));
            }
        }

        beginTest ("Every factory preset plays audibly, finitely and within full scale");
        {
            const auto& library = getFactoryPresets();
            for (int i = 0; i < (int) library.size(); ++i)
            {
                bool finite = true;
                float late = 0.0f;
                const float peak = playPreset (makeFactoryPreset (i), finite, late);
                expect (finite, juce::String (library[(size_t) i].name) + " finite");
                expect (peak > 0.02f && peak <= 1.0f, juce::String (library[(size_t) i].name) + " peak " + juce::String (peak));
            }
        }

        beginTest ("Preset serialisation round trip");
        {
            auto s = makeFactoryPreset (0);
            s.macroNames[1] = "WOBBLE";
            s.pattern.steps[3].pressure = 0.33f;
            s.arpPattern.steps[2] = 5;
            s.lfoShapes.points[1] = { { 0.0f, 1.0f }, { 0.5f, -1.0f }, { 1.0f, 1.0f } };
            s.hasMorphTarget = true;
            s.morphTarget = s.params;
            s.morphTarget[pid::filter (FilterField::Cutoff)] = 12345.0f;

            const auto xml = s.toValueTree().createXml();
            const auto restored = PresetState::fromValueTree (juce::ValueTree::fromXml (*xml));

            bool paramsEqual = true;
            for (const auto& d : getParamDefs())
                paramsEqual = paramsEqual && std::abs (restored.params[d.index] - s.params[d.index]) < 1.0e-4f;
            expect (paramsEqual, "all parameters restored");
            expectEquals (restored.macroNames[1], juce::String ("WOBBLE"));
            expectWithinAbsoluteError (restored.pattern.steps[3].pressure, 0.33f, 1.0e-4f);
            expectEquals (restored.arpPattern.steps[2], 5);
            expectEquals ((int) restored.lfoShapes.points[1].size(), 3);
            expect (restored.hasMorphTarget);
            expectWithinAbsoluteError (restored.morphTarget[pid::filter (FilterField::Cutoff)], 12345.0f, 0.5f);
            expectEquals (restored.name, s.name);
        }

        beginTest ("Missing parameters fall back to defaults (older presets keep loading)");
        {
            juce::ValueTree tree ("NeddPE");
            juce::ValueTree params ("PARAMS");
            juce::ValueTree p ("PARAM");
            p.setProperty ("id", "flt_cutoff", nullptr);
            p.setProperty ("value", 440.0f, nullptr);
            params.appendChild (p, nullptr);
            juce::ValueTree unknown ("PARAM");
            unknown.setProperty ("id", "removed_param", nullptr);
            unknown.setProperty ("value", 3.0f, nullptr);
            params.appendChild (unknown, nullptr);
            tree.appendChild (params, nullptr);

            const auto s = PresetState::fromValueTree (tree);
            expectWithinAbsoluteError (s.params[pid::filter (FilterField::Cutoff)], 440.0f, 1.0e-3f);
            expectWithinAbsoluteError (s.params[pid::amp (AmpField::Level)], getParamDef (pid::amp (AmpField::Level)).defaultValue, 1.0e-6f);
        }

        beginTest ("Randomiser: every mode yields a valid, audible patch; MPE setup untouched");
        {
            const PresetState base;
            for (int mode = 0; mode < 7; ++mode)
                for (uint32_t seed = 1; seed <= 6; ++seed)
                {
                    const auto r = PatchRandomizer::randomise (base, (PatchRandomizer::Mode) mode, seed * 97u + (uint32_t) mode);
                    expect (r.params.getBool (pid::osc (0, OscField::On)));
                    expectWithinAbsoluteError (r.params[pid::global (GlobalField::MpeBendRange)], base.params[pid::global (GlobalField::MpeBendRange)], 1.0e-6f);
                    expectWithinAbsoluteError (r.params[pid::global (GlobalField::MpeMode)], base.params[pid::global (GlobalField::MpeMode)], 1.0e-6f);
                    expectEquals (r.params.getInt (pid::mod (0, ModSlotField::Source)), (int) ModSource::MpePitch, "per-note pitch route kept");

                    bool inRange = true;
                    for (const auto& d : getParamDefs())
                        inRange = inRange && r.params[d.index] >= d.range.start - 1.0e-4f && r.params[d.index] <= d.range.end + 1.0e-4f;
                    expect (inRange, "all values within their ranges");

                    if (seed <= 2)
                    {
                        bool finite = true;
                        float late = 0.0f;
                        const float peak = playPreset (r, finite, late);
                        expect (finite && peak > 0.005f && peak <= 1.0f, "mode " + juce::String (mode) + " seed " + juce::String ((int) seed) + " peak " + juce::String (peak));
                    }
                }

            // Full randomisation always gives pressure and slide a job.
            const auto full = PatchRandomizer::randomise (base, PatchRandomizer::Mode::Full, 1234u);
            bool pressure = false, slide = false;
            for (int s = 0; s < kNumModSlots; ++s)
            {
                const auto src = full.params.getChoice<ModSource> (pid::mod (s, ModSlotField::Source));
                pressure = pressure || src == ModSource::MpePressure;
                slide = slide || src == ModSource::MpeSlide;
            }
            expect (pressure && slide, "MPE-first randomisation");
        }

        beginTest ("Mutation makes small changes proportional to the amount");
        {
            const auto base = makeFactoryPreset (8);
            auto distance = [&base] (const PresetState& s)
            {
                float sum = 0.0f;
                for (const auto& d : getParamDefs())
                    if (d.morphable && d.type == ParamType::Float)
                        sum += std::abs (d.range.convertTo0to1 (s.params[d.index]) - d.range.convertTo0to1 (base.params[d.index]));
                return sum;
            };
            const float small = distance (PatchRandomizer::mutate (base, 0.1f, 7u));
            const float large = distance (PatchRandomizer::mutate (base, 1.0f, 7u));
            expect (small > 0.0f && small < large, "small " + juce::String (small) + " < large " + juce::String (large));
        }

        beginTest ("Processor state restores completely in a new instance (DAW reopen)");
        {
            auto a = std::make_unique<NeddPEAudioProcessor>();
            a->applyState (makeFactoryPreset (30), {});
            a->setParameterPlain (pid::macro (2), 0.77f);
            a->setMacroName (0, "Custom");
            NoteClip clip;
            ClipNote note;
            note.noteNumber = 67;
            note.pressure = { { 0.0f, 0.2f }, { 1.0f, 0.9f } };
            clip.addNote (note);
            a->setClip (clip);
            auto tuning = a->getTuning();
            tuning.userScale[1] = true;
            a->setTuning (tuning);
            a->setClipLoop (false);

            juce::MemoryBlock block;
            a->getStateInformation (block);

            auto b = std::make_unique<NeddPEAudioProcessor>();
            b->setStateInformation (block.getData(), (int) block.getSize());

            ParamSnapshot pa, pb;
            a->readParameters (pa);
            b->readParameters (pb);
            bool equal = true;
            for (const auto& d : getParamDefs())
                equal = equal && std::abs (pa[d.index] - pb[d.index]) < 1.0e-4f;
            expect (equal, "every parameter restored");
            expectEquals (b->getMacroName (0), juce::String ("CUSTOM"));
            expectEquals ((int) b->getClip().notes.size(), 1);
            expectWithinAbsoluteError (evaluateCurve (b->getClip().notes[0].pressure, 1.0f, 0.0f), 0.9f, 1.0e-3f);
            expect (b->getTuning().userScale[1]);
            expect (! b->getClipLoop());
            expect (b->hasMorphTarget(), "morph B restored");
            expectEquals (b->getCurrentPresetName(), a->getCurrentPresetName());
        }

        beginTest ("Loading a preset is one undo step, including structured data");
        {
            auto p = std::make_unique<NeddPEAudioProcessor>();
            p->getUndoManager().clearUndoHistory();
            const float cutoffBefore = p->getParameterByIndex (pid::filter (FilterField::Cutoff))->convertFrom0to1 (
                p->getParameterByIndex (pid::filter (FilterField::Cutoff))->getValue());
            const auto patternBefore = p->getPattern();

            p->applyState (makeFactoryPreset (7), "Load preset");   // Acid Line: different cutoff and seq pattern flag
            auto modified = p->getPattern();
            modified.steps[0].note = 30;
            p->setPattern (modified);
            p->getUndoManager().beginNewTransaction();

            p->getUndoManager().undo();
            const float cutoffAfter = p->getParameterByIndex (pid::filter (FilterField::Cutoff))->convertFrom0to1 (
                p->getParameterByIndex (pid::filter (FilterField::Cutoff))->getValue());
            expectWithinAbsoluteError (cutoffAfter, cutoffBefore, 0.5f);
            expect (p->getPattern() == patternBefore, "pattern restored by the same undo step");
            expectEquals (p->getCurrentPresetName(), juce::String ("Init"));
        }

        beginTest ("Knob gestures become single undo steps; grouped edits undo together");
        {
            auto p = std::make_unique<NeddPEAudioProcessor>();
            auto& um = p->getUndoManager();
            um.clearUndoHistory();
            auto* cutoff = p->getParameterByIndex (pid::filter (FilterField::Cutoff));
            const float original = cutoff->getValue();

            cutoff->beginChangeGesture();
            cutoff->setValueNotifyingHost (0.2f);
            cutoff->setValueNotifyingHost (0.3f);
            cutoff->endChangeGesture();
            expectWithinAbsoluteError (cutoff->getValue(), 0.3f, 1.0e-6f);
            um.undo();
            expectWithinAbsoluteError (cutoff->getValue(), original, 1.0e-6f, "drag undone in one step");
            um.redo();
            expectWithinAbsoluteError (cutoff->getValue(), 0.3f, 1.0e-6f, "and redone");

            // Host automation (no gesture) is not recorded.
            um.clearUndoHistory();
            cutoff->setValueNotifyingHost (0.9f);
            expect (! um.canUndo(), "automation does not create undo steps");

            // A matrix edit touching several parameters is one step.
            p->setParametersUndoable ({ { pid::mod (6, ModSlotField::Source), (float) ModSource::Lfo2 },
                                        { pid::mod (6, ModSlotField::Dest), (float) ModDest::Pan },
                                        { pid::mod (6, ModSlotField::Amount), 0.5f } }, "Add route");
            um.undo();
            ParamSnapshot snapshot;
            p->readParameters (snapshot);
            expectEquals (snapshot.getInt (pid::mod (6, ModSlotField::Source)), 0);
            expectEquals (snapshot.getInt (pid::mod (6, ModSlotField::Dest)), 0);
        }
    }
};

static PresetTests presetTests;

} // namespace nedd::test
