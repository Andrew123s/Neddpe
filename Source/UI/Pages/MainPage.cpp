#include "Pages.h"

namespace nedd::ui
{
class MpeQuickCard : public Card
{
public:
    explicit MpeQuickCard (EditorContext& c)
        : Card (c, "MPE"),
          history (c),
          field (c, history),
          bendRange (c, pid::global (GlobalField::MpeBendRange), "BEND RNG", colours::pitch),
          pitchSens (c, pid::global (GlobalField::PitchSensitivity), "PITCH", colours::pitch),
          pressureAmp (c, pid::amp (AmpField::Pressure), "PRS>AMP", colours::pressure),
          pressureCurve (c, pid::global (GlobalField::PressureCurve), "PRS CRV", colours::pressure),
          slideCurve (c, pid::global (GlobalField::SlideCurve), "SLD CRV", colours::slide)
    {
        titleColour = colours::slide;
        for (auto* comp : std::initializer_list<juce::Component*> { &field, &bendRange, &pitchSens, &pressureAmp, &pressureCurve, &slideCurve, &open })
            addAndMakeVisible (comp);
        open.onClick = [this] { if (ctx.showPage) ctx.showPage ((int) PageId::Mpe); };
    }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (panelTitleHeight).reduced (8, 3);
        open.setBounds (strip.removeFromRight (86));
        auto r = content();
        field.setBounds (r.removeFromTop (r.getHeight() - 66));
        r.removeFromTop (4);
        layoutRow (r, { &bendRange, &pitchSens, &pressureAmp, &pressureCurve, &slideCurve }, 60);
    }

private:
    ExpressionHistory history;
    ExpressionField field;
    ParamKnob bendRange, pitchSens, pressureAmp, pressureCurve, slideCurve;
    juce::TextButton open { "MPE VIEW" };
};

class OutputCard : public Card, private EditorContext::Listener
{
public:
    explicit OutputCard (EditorContext& c)
        : Card (c, "Output"),
          volume (c, pid::global (GlobalField::MasterVolume), "VOLUME"),
          polyphony (c, pid::global (GlobalField::Polyphony), "VOICES"),
          glide (c, pid::global (GlobalField::Glide), "GLIDE"),
          meter (c),
          mode (c, pid::global (GlobalField::VoiceMode)),
          quality (c, pid::global (GlobalField::Quality))
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &volume, &polyphony, &glide, &meter, &mode, &quality })
            addAndMakeVisible (comp);
        ctx.addListener (this);
    }

    ~OutputCard() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        Card::paint (g);
        g.setColour (colours::textDim);
        g.setFont (monoFont (11.5f));
        g.drawText (juce::String (activeVoices) + " voices", statusArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = content();
        auto meterArea = r.removeFromRight (18);
        meter.setBounds (meterArea.withTrimmedBottom (4));
        r.removeFromRight (6);
        volume.setBounds (r.removeFromTop (70));
        mode.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        quality.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto row = r.removeFromTop (60);
        polyphony.setBounds (row.removeFromLeft (row.getWidth() / 2));
        glide.setBounds (row);
        statusArea = r.removeFromTop (18);
    }

private:
    void editorTick() override
    {
        const int now = ctx.telemetry.activeVoices.load (std::memory_order_relaxed);
        if (now != activeVoices)
        {
            activeVoices = now;
            repaint (statusArea);
        }
    }

    ParamKnob volume, polyphony, glide;
    OutputMeter meter;
    ParamChoice mode, quality;
    juce::Rectangle<int> statusArea;
    int activeVoices = -1;
};

MainPage::MainPage (EditorContext& c)
    : Page (c), filter (c, false), ampEnv (c, pid::ampEnv, "Amp envelope", colours::accent, false)
{
    for (int o = 0; o < kNumOscillators; ++o)
    {
        oscillators[(size_t) o] = std::make_unique<OscillatorPanel> (c, o, true);
        addAndMakeVisible (*oscillators[(size_t) o]);
    }
    mpe = std::make_unique<MpeQuickCard> (c);
    output = std::make_unique<OutputCard> (c);
    effects = createEffectsOverview (c);
    addAndMakeVisible (*effects);
    addAndMakeVisible (filter);
    addAndMakeVisible (ampEnv);
    addAndMakeVisible (*mpe);
    addAndMakeVisible (*output);
}

MainPage::~MainPage() = default;

void MainPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (330);
    r.removeFromTop (metrics::gap);

    const int filterWidth = 330;
    const int oscWidth = (top.getWidth() - filterWidth - 3 * metrics::gap) / 3;
    for (auto& o : oscillators)
    {
        o->setBounds (top.removeFromLeft (oscWidth));
        top.removeFromLeft (metrics::gap);
    }
    filter.setBounds (top);

    output->setBounds (r.removeFromRight (176));
    r.removeFromRight (metrics::gap);

    if (effects != nullptr)
    {
        effects->setBounds (r.removeFromRight (330));
        r.removeFromRight (metrics::gap);
    }

    ampEnv.setBounds (r.removeFromLeft ((r.getWidth() - metrics::gap) / 2));
    r.removeFromLeft (metrics::gap);
    mpe->setBounds (r);
}

} // namespace nedd::ui
