#include "PluginEditor.h"

namespace nedd
{
using namespace ui;

NeddPEEditor::NeddPEEditor (NeddPEAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), ctx (p)
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    pageList = {
        { (int) PageId::Main, "MAIN" },
        { (int) PageId::Oscillators, "OSCILLATORS" },
        { (int) PageId::Filter, "FILTER" },
        { (int) PageId::Envelopes, "ENVELOPES" },
        { (int) PageId::Lfo, "LFO" },
        { (int) PageId::Mpe, "MPE" },
        { (int) PageId::Matrix, "MOD MATRIX" },
        { (int) PageId::Effects, "EFFECTS" },
        { (int) PageId::Arp, "ARPEGGIATOR" },
        { (int) PageId::Sequencer, "SEQUENCER" },
        { (int) PageId::NoteEditor, "NOTE EDITOR" },
        { (int) PageId::Presets, "PRESETS" },
        { (int) PageId::Settings, "SETTINGS" },
    };

    for (const auto& [id, name] : pageList)
        nav.addPage (id, name);
    nav.onSelect = [this] (int id) { showPage (id); };
    ctx.showPage = [this] (int id) { showPage (id); };

    addAndMakeVisible (root);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &nav, &strip })
        root.addAndMakeVisible (c);

    presetTools = createPresetTools (ctx);
    morphTools = createMorphTools (ctx);
    header.setTools (presetTools.get(), morphTools.get());

    showPage ((int) PageId::Main);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) metrics::designWidth / (double) metrics::designHeight);
    setResizeLimits (metrics::designWidth * 3 / 4, metrics::designHeight * 3 / 4, metrics::designWidth * 3 / 2, metrics::designHeight * 3 / 2);
    const float scale = processor.getEditorScale();
    setSize (juce::roundToInt ((float) metrics::designWidth * scale), juce::roundToInt ((float) metrics::designHeight * scale));

    startTimerHz (30);
}

NeddPEEditor::~NeddPEEditor()
{
    stopTimer();
    pages.clear();
    presetTools.reset();
    morphTools.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

std::vector<int> NeddPEEditor::getPageIds() const
{
    std::vector<int> ids;
    for (const auto& entry : pageList)
        ids.push_back (entry.first);
    return ids;
}

std::unique_ptr<Page> NeddPEEditor::createPage (int id)
{
    switch ((PageId) id)
    {
        case PageId::Main:        return std::make_unique<MainPage> (ctx);
        case PageId::Oscillators: return std::make_unique<OscillatorPage> (ctx);
        case PageId::Filter:      return std::make_unique<FilterPage> (ctx);
        case PageId::Envelopes:   return std::make_unique<EnvelopePage> (ctx);
        case PageId::Mpe:         return std::make_unique<MpePage> (ctx);
        case PageId::Matrix:      return std::make_unique<MatrixPage> (ctx);
        case PageId::Settings:    return std::make_unique<SettingsPage> (ctx, [this] (float s) { setUiScale (s); });
        case PageId::Lfo:         return createLfoPage (ctx);
        case PageId::Effects:     return createEffectsPage (ctx);
        case PageId::Arp:         return createArpPage (ctx);
        case PageId::Sequencer:   return createSequencerPage (ctx);
        case PageId::NoteEditor:  return createNoteEditorPage (ctx);
        case PageId::Presets:     return createPresetsPage (ctx);
        case PageId::Count:
            break;
    }
    return nullptr;
}

void NeddPEEditor::showPage (int id)
{
    if (pages.find (id) == pages.end())
    {
        auto page = createPage (id);
        if (page == nullptr)
            return;
        root.addChildComponent (*page);
        pages[id] = std::move (page);
    }

    for (auto& [pageId, page] : pages)
        page->setVisible (pageId == id);

    currentPage = id;
    nav.setCurrent (id);
    resized();
}

void NeddPEEditor::setUiScale (float scale)
{
    processor.setEditorScale (scale);
    setSize (juce::roundToInt ((float) metrics::designWidth * scale), juce::roundToInt ((float) metrics::designHeight * scale));
}

void NeddPEEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void NeddPEEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;   // called during construction, before setSize()

    const float scale = (float) getWidth() / (float) metrics::designWidth;
    root.setTransform (juce::AffineTransform::scale (scale));
    root.setBounds (0, 0, metrics::designWidth, metrics::designHeight);

    auto r = juce::Rectangle<int> (0, 0, metrics::designWidth, metrics::designHeight);
    header.setBounds (r.removeFromTop (metrics::headerHeight));
    strip.setBounds (r.removeFromBottom (metrics::bottomHeight));
    nav.setBounds (r.removeFromLeft (metrics::navWidth));

    const auto content = r.reduced (metrics::gap + 2, metrics::gap + 2);
    if (auto it = pages.find (currentPage); it != pages.end())
        it->second->setBounds (content);
}

void NeddPEEditor::timerCallback()
{
    ctx.tick();
}

void NeddPEEditor::tickForTesting (int frames)
{
    for (int i = 0; i < frames; ++i)
        ctx.tick();
}

} // namespace nedd
