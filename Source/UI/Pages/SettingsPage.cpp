#include "Pages.h"
#include "UI/HelpText.h"

namespace nedd::ui
{
struct SettingsPage::Impl : private EditorContext::Listener
{
    Impl (SettingsPage& o, EditorContext& c, std::function<void (float)> scaleSetter)
        : owner (o), ctx (c), setUiScale (std::move (scaleSetter)),
          mpeMode (c, pid::global (GlobalField::MpeMode)),
          bendRange (c, pid::global (GlobalField::MpeBendRange), "BEND RANGE", colours::pitch),
          masterBend (c, pid::global (GlobalField::MasterBendRange), "MASTER BEND", colours::pitch),
          pitchSens (c, pid::global (GlobalField::PitchSensitivity), "PITCH SENS", colours::pitch),
          smoothing (c, pid::global (GlobalField::ExpressionSmoothing), "SMOOTHING"),
          voiceMode (c, pid::global (GlobalField::VoiceMode)),
          glideMode (c, pid::global (GlobalField::GlideMode)),
          polyphony (c, pid::global (GlobalField::Polyphony), "POLYPHONY"),
          glide (c, pid::global (GlobalField::Glide), "GLIDE"),
          scale (c, pid::global (GlobalField::ScaleType)),
          root (c, pid::global (GlobalField::ScaleRoot)),
          bendQuantize (c, pid::global (GlobalField::BendQuantize), "PITCH QNT", colours::pitch),
          customTuning (c, pid::global (GlobalField::CustomTuning), "Use custom tuning table"),
          quality (c, pid::global (GlobalField::Quality)),
          oversampling (c, pid::global (GlobalField::OscOversampling)),
          midiOut (c, pid::global (GlobalField::MidiOut), "Send generated notes as MPE MIDI"),
          limiter (c, pid::fx (FxField::LimiterOn), "Output limiter")
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &mpeMode, &bendRange, &masterBend, &pitchSens, &smoothing, &voiceMode, &glideMode,
                                                                    &polyphony, &glide, &scale, &root, &bendQuantize, &customTuning, &quality,
                                                                    &oversampling, &qualityCaption, &oversamplingCaption,
                                                                    &midiOut, &limiter, &loadScala, &resetTuning })
            owner.addAndMakeVisible (comp);
        quality.setTooltip (helpForParam (pid::global (GlobalField::Quality)));
        oversampling.setTooltip (helpForParam (pid::global (GlobalField::OscOversampling)));

        const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i)
        {
            auto& b = degree[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setToggleState (ctx.processor.getTuning().userScale[(size_t) i], juce::dontSendNotification);
            b.onClick = [this, i]
            {
                auto t = ctx.processor.getTuning();
                t.userScale[(size_t) i] = degree[(size_t) i].getToggleState();
                ctx.processor.setTuning (t);
            };
            b.setTooltip ("Degree of the 'User' scale");
            owner.addAndMakeVisible (b);
        }

        const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = scaleButtons[(size_t) i];
            b.setButtonText (juce::String (juce::roundToInt (scales[i] * 100.0f)) + "%");
            const float s = scales[i];
            b.onClick = [this, s] { if (setUiScale) setUiScale (s); };
            owner.addAndMakeVisible (b);
        }

        loadScala.onClick = [this] { chooseScala(); };
        resetTuning.onClick = [this]
        {
            auto t = ctx.processor.getTuning();
            t.resetTo12Tet();
            ctx.processor.setTuning (t);
            owner.repaint();
        };

        ctx.addListener (this);
        rebuildLearnList();
    }

    ~Impl() override { ctx.removeListener (this); }

    void chooseScala()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a Scala tuning", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.scl");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            auto t = ctx.processor.getTuning();
            const auto error = t.loadScala (file.loadFileAsString(), 60);
            if (error.isNotEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load tuning", error);
                return;
            }
            ctx.processor.setTuning (t);
            ctx.setParam (pid::global (GlobalField::CustomTuning), 1.0f, "Load tuning");
            owner.repaint();
        });
    }

    void editorTick() override
    {
        if (ctx.getFrameCounter() % 15 != 0)
            return;
        juce::String signature;
        for (int i = 0; i < pid::count; ++i)
            if (const int cc = ctx.processor.getMidiMappingFor (i); cc >= 0)
                signature << cc << ">" << i << ";";
        if (signature != learnSignature)
            rebuildLearnList();
    }

    void rebuildLearnList()
    {
        learnRows.clear();
        learnSignature.clear();
        for (int i = 0; i < pid::count; ++i)
        {
            const int cc = ctx.processor.getMidiMappingFor (i);
            if (cc < 0)
                continue;
            learnSignature << cc << ">" << i << ";";
            LearnRow row;
            row.text = "CC " + juce::String (cc) + "   " + getParamDef (i).name;
            row.clear = std::make_unique<juce::TextButton> ("Clear");
            const int index = i;
            row.clear->onClick = [this, index] { juce::MessageManager::callAsync ([this, index] { ctx.processor.clearMidiMapping (index); }); };
            owner.addAndMakeVisible (*row.clear);
            learnRows.push_back (std::move (row));
        }

        if (owner.impl != nullptr)   // not during construction
        {
            owner.resized();
            owner.repaint();
        }
    }

    struct LearnRow
    {
        juce::String text;
        std::unique_ptr<juce::TextButton> clear;
        juce::Rectangle<int> area;
    };

    SettingsPage& owner;
    EditorContext& ctx;
    std::function<void (float)> setUiScale;

    ParamChoice mpeMode;
    ParamKnob bendRange, masterBend, pitchSens, smoothing;
    ParamChoice voiceMode, glideMode;
    ParamKnob polyphony, glide;
    ParamChoice scale, root;
    ParamKnob bendQuantize;
    ParamToggle customTuning;
    ParamChoice quality, oversampling;
    Caption qualityCaption { "CPU QUALITY" }, oversamplingCaption { "OSCILLATOR OVERSAMPLING (ANTI-ALIASING)" };
    ParamToggle midiOut, limiter;
    juce::TextButton loadScala { "Load Scala (.scl)..." }, resetTuning { "Reset to 12-TET" };
    std::array<juce::TextButton, 12> degree;
    std::array<juce::TextButton, 4> scaleButtons;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<LearnRow> learnRows;
    juce::String learnSignature;

    // Layout rectangles for painted cards and labels
    juce::Rectangle<int> mpeCard, voiceCard, tuningCard, engineCard, uiCard, learnCard;
    juce::Rectangle<int> tuningNameArea, userScaleLabel, learnListArea, voiceModeLabel, glideModeLabel;
};

SettingsPage::SettingsPage (EditorContext& c, std::function<void (float)> setUiScale) : Page (c)
{
    impl = std::make_unique<Impl> (*this, c, std::move (setUiScale));
}

SettingsPage::~SettingsPage() = default;

void SettingsPage::paint (juce::Graphics& g)
{
    auto& i = *impl;
    drawPanel (g, i.mpeCard.toFloat(), "MPE & input", colours::slide);
    drawPanel (g, i.voiceCard.toFloat(), "Voicing");
    drawPanel (g, i.tuningCard.toFloat(), "Tuning & scale", colours::pitch);
    drawPanel (g, i.engineCard.toFloat(), "Engine");
    drawPanel (g, i.uiCard.toFloat(), "Interface size");
    drawPanel (g, i.learnCard.toFloat(), "MIDI learn  (right-click any knob > MIDI Learn)", colours::amber);

    g.setFont (font (12.5f));
    g.setColour (colours::textDim);
    g.drawText ("Tuning: " + ctx.processor.getTuning().tuningName, i.tuningNameArea, juce::Justification::centredLeft, true);
    g.drawText ("User scale degrees:", i.userScaleLabel, juce::Justification::centredLeft);
    g.drawText ("Voice mode", i.voiceModeLabel, juce::Justification::centredLeft);
    g.drawText ("Glide", i.glideModeLabel, juce::Justification::centredLeft);

    if (i.learnRows.empty())
    {
        g.setColour (colours::textFaint);
        g.drawText ("No MIDI mappings. Right-click a knob, choose MIDI Learn, then move a controller.", i.learnListArea.removeFromTop (24),
                    juce::Justification::centredLeft);
    }
    g.setColour (colours::text);
    for (const auto& row : i.learnRows)
        g.drawText (row.text, row.area, juce::Justification::centredLeft, true);
}

void SettingsPage::resized()
{
    auto& i = *impl;
    auto r = getLocalBounds();
    auto left = r.removeFromLeft ((r.getWidth() - 2 * metrics::gap) / 3);
    r.removeFromLeft (metrics::gap);
    auto middle = r.removeFromLeft ((r.getWidth() - metrics::gap) / 2);
    r.removeFromLeft (metrics::gap);
    auto right = r;

    // MPE
    i.mpeCard = left.removeFromTop (250);
    left.removeFromTop (metrics::gap);
    {
        auto c = i.mpeCard.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        i.mpeMode.setBounds (c.removeFromTop (24));
        c.removeFromTop (10);
        layoutRow (c.removeFromTop (74), { &i.bendRange, &i.masterBend }, 90);
        layoutRow (c.removeFromTop (74), { &i.pitchSens, &i.smoothing }, 90);
    }

    // Voicing
    i.voiceCard = left;
    {
        auto c = i.voiceCard.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        auto modeRow = c.removeFromTop (24);
        i.voiceModeLabel = modeRow.removeFromLeft (90);
        i.voiceMode.setBounds (modeRow);
        c.removeFromTop (6);
        auto glideRow = c.removeFromTop (24);
        i.glideModeLabel = glideRow.removeFromLeft (90);
        i.glideMode.setBounds (glideRow);
        c.removeFromTop (10);
        layoutRow (c.removeFromTop (74), { &i.polyphony, &i.glide }, 90);
    }

    // Tuning
    i.tuningCard = middle.removeFromTop (380);
    middle.removeFromTop (metrics::gap);
    {
        auto c = i.tuningCard.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        auto row = c.removeFromTop (24);
        i.scale.setBounds (row.removeFromLeft (row.getWidth() - 80));
        row.removeFromLeft (6);
        i.root.setBounds (row);
        c.removeFromTop (8);
        i.bendQuantize.setBounds (c.removeFromTop (74).withSizeKeepingCentre (90, 74));
        c.removeFromTop (4);
        i.userScaleLabel = c.removeFromTop (20);
        auto degrees = c.removeFromTop (26);
        const int w = degrees.getWidth() / 12;
        for (auto& b : i.degree)
            b.setBounds (degrees.removeFromLeft (w).reduced (1, 0));
        c.removeFromTop (14);
        i.customTuning.setBounds (c.removeFromTop (24));
        c.removeFromTop (6);
        i.tuningNameArea = c.removeFromTop (20);
        c.removeFromTop (6);
        auto buttons = c.removeFromTop (28);
        i.loadScala.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
        buttons.removeFromLeft (6);
        i.resetTuning.setBounds (buttons);
    }

    // Engine
    i.engineCard = middle;
    {
        auto c = i.engineCard.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        i.qualityCaption.setBounds (c.removeFromTop (16));
        i.quality.setBounds (c.removeFromTop (24));
        c.removeFromTop (8);
        i.oversamplingCaption.setBounds (c.removeFromTop (16));
        i.oversampling.setBounds (c.removeFromTop (24));
        c.removeFromTop (10);
        i.midiOut.setBounds (c.removeFromTop (24));
        c.removeFromTop (6);
        i.limiter.setBounds (c.removeFromTop (24));
    }

    // Interface
    i.uiCard = right.removeFromTop (80);
    right.removeFromTop (metrics::gap);
    {
        auto c = i.uiCard.withTrimmedTop (panelTitleHeight).reduced (10, 8);
        const int w = c.getWidth() / 4;
        for (auto& b : i.scaleButtons)
            b.setBounds (c.removeFromLeft (w).reduced (3, 0));
    }

    // MIDI learn
    i.learnCard = right;
    {
        auto c = i.learnCard.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        i.learnListArea = c;
        for (auto& row : i.learnRows)
        {
            auto line = c.removeFromTop (26);
            row.clear->setBounds (line.removeFromRight (60).reduced (0, 2));
            row.area = line;
        }
    }
}

} // namespace nedd::ui
