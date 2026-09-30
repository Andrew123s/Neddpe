#include "Pages.h"

namespace nedd::ui
{
// =============================================================================================
// Quick per-note routes: shows every route into the filter from an MPE source, with one-click adds.
// =============================================================================================
namespace
{
    class FilterRoutesCard : public Card, private EditorContext::Listener
    {
    public:
        explicit FilterRoutesCard (EditorContext& c) : Card (c, "Per-note filter modulation")
        {
            titleColour = colours::modulation;
            struct Preset { ModSource source; const char* label; };
            const Preset presets[] = { { ModSource::MpePressure, "+ Pressure > Cutoff" }, { ModSource::MpeSlide, "+ Slide > Cutoff" },
                                       { ModSource::Velocity, "+ Velocity > Cutoff" }, { ModSource::KeyPosition, "+ Key > Cutoff" } };
            for (const auto& p : presets)
            {
                auto b = std::make_unique<juce::TextButton> (p.label);
                const auto source = p.source;
                b->onClick = [this, source] { ctx.addModulation (source, ModDest::FilterCutoff, 0.35f); };
                addAndMakeVisible (*b);
                buttons.push_back (std::move (b));
            }
            ctx.addListener (this);
        }

        ~FilterRoutesCard() override { ctx.removeListener (this); }

        void paint (juce::Graphics& g) override
        {
            Card::paint (g);
            auto r = content().withTrimmedTop (68);
            g.setFont (font (12.5f));

            const auto& routing = ctx.routing();
            int shown = 0;
            for (int i = 0; i < routing.numRoutes; ++i)
            {
                const auto& route = routing.routes[(size_t) i];
                if (route.dest < ModDest::FilterCutoff || route.dest > ModDest::FilterMix)
                    continue;
                auto line = r.removeFromTop (20);
                g.setColour (colours::text);
                g.drawText (juce::String (getModSourceInfo (route.source).name) + "  >  " + getModDestInfo (route.dest).name,
                            line, juce::Justification::centredLeft);
                g.setColour (route.amount >= 0.0f ? colours::accent : colours::danger);
                g.drawText (juce::String (juce::roundToInt (route.amount * 100.0f)) + "%", line, juce::Justification::centredRight);
                ++shown;
            }

            if (shown == 0)
            {
                g.setColour (colours::textFaint);
                g.drawText ("No per-note filter routes yet.", r.removeFromTop (20), juce::Justification::centredLeft);
            }

            g.setColour (colours::textFaint);
            g.setFont (font (11.5f));
            g.drawFittedText ("Each note computes its own cutoff: the markers on the response show where every sounding note sits right now. "
                              "Right-click any knob to add or remove routes.",
                              content().removeFromBottom (44), juce::Justification::bottomLeft, 3);
        }

        void resized() override
        {
            auto r = content().removeFromTop (60);
            auto top = r.removeFromTop (26);
            auto bottom = r.withTrimmedTop (4).removeFromTop (26);
            buttons[0]->setBounds (top.removeFromLeft (top.getWidth() / 2 - 3));
            top.removeFromLeft (6);
            buttons[1]->setBounds (top);
            buttons[2]->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
            bottom.removeFromLeft (6);
            buttons[3]->setBounds (bottom);
        }

    private:
        void editorTick() override
        {
            int signature = 0;
            const auto& routing = ctx.routing();
            for (int i = 0; i < routing.numRoutes; ++i)
                signature = signature * 31 + (int) routing.routes[(size_t) i].dest * 7 + (int) routing.routes[(size_t) i].source
                          + juce::roundToInt (routing.routes[(size_t) i].amount * 100.0f);
            if (signature != lastSignature)
            {
                lastSignature = signature;
                repaint();
            }
        }

        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        int lastSignature = 0;
    };
} // namespace

// =============================================================================================
OscillatorPage::OscillatorPage (EditorContext& c) : Page (c)
{
    for (int o = 0; o < kNumOscillators; ++o)
    {
        oscillators[(size_t) o] = std::make_unique<OscillatorPanel> (c, o, false);
        addAndMakeVisible (*oscillators[(size_t) o]);
    }
}

void OscillatorPage::resized()
{
    auto r = getLocalBounds();
    const int w = (r.getWidth() - 2 * metrics::gap) / 3;
    for (auto& o : oscillators)
    {
        o->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (metrics::gap);
    }
}

// =============================================================================================
FilterPage::FilterPage (EditorContext& c)
    : Page (c), filter (c, true), filterEnv (c, pid::filterEnv, "Filter envelope", colours::amber, true)
{
    routes = std::make_unique<FilterRoutesCard> (c);
    addAndMakeVisible (filter);
    addAndMakeVisible (filterEnv);
    addAndMakeVisible (*routes);
}

void FilterPage::resized()
{
    auto r = getLocalBounds();
    filter.setBounds (r.removeFromTop (350));
    r.removeFromTop (metrics::gap);
    filterEnv.setBounds (r.removeFromLeft (r.getWidth() * 3 / 5));
    r.removeFromLeft (metrics::gap);
    routes->setBounds (r);
}

// =============================================================================================
EnvelopePage::EnvelopePage (EditorContext& c)
    : Page (c),
      amp (c, pid::ampEnv, "Amp envelope", colours::accent, true),
      filter (c, pid::filterEnv, "Filter envelope", colours::amber, true),
      mod (c, pid::modEnv, "Mod envelope  (assign in the matrix)", colours::modulation, true)
{
    addAndMakeVisible (amp);
    addAndMakeVisible (filter);
    addAndMakeVisible (mod);
}

void EnvelopePage::resized()
{
    auto r = getLocalBounds();
    amp.setBounds (r.removeFromTop ((r.getHeight() - metrics::gap) / 2));
    r.removeFromTop (metrics::gap);
    filter.setBounds (r.removeFromLeft ((r.getWidth() - metrics::gap) / 2));
    r.removeFromLeft (metrics::gap);
    mod.setBounds (r);
}

} // namespace nedd::ui
