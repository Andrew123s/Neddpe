#include "Pages.h"

namespace nedd::ui
{
// =============================================================================================
// Header: preset navigation and generators
// =============================================================================================
class PresetTools : public juce::Component, private EditorContext::Listener
{
public:
    explicit PresetTools (EditorContext& c) : ctx (c)
    {
        for (auto* b : { &prev, &next, &name, &save, &random, &mutate })
            addAndMakeVisible (b);

        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        name.setTooltip ("Open the preset browser");
        save.setTooltip ("Save: overwrites your user preset, or opens Save As for factory sounds");
        random.setTooltip ("Intelligent randomise: choose what to randomise");
        mutate.setTooltip ("Mutate: a controlled variation of the current sound (repeat to keep evolving; history on the PRESETS page)");

        prev.onClick = [this] { ctx.processor.getPresetManager().loadNext (-1); };
        next.onClick = [this] { ctx.processor.getPresetManager().loadNext (1); };
        name.onClick = [this] { if (ctx.showPage) ctx.showPage ((int) PageId::Presets); };
        save.onClick = [this]
        {
            auto& pm = ctx.processor.getPresetManager();
            const int current = pm.getCurrentIndex();
            if (juce::isPositiveAndBelow (current, (int) pm.getEntries().size()) && ! pm.getEntries()[(size_t) current].factory)
            {
                const auto& e = pm.getEntries()[(size_t) current];
                pm.saveUser (e.name, e.category, e.author, e.description);
            }
            else if (ctx.showPage)
                ctx.showPage ((int) PageId::Presets);
        };
        random.onClick = [this]
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("Randomise");
            const auto modes = PatchRandomizer::getModeNames();
            for (int i = 0; i < modes.size(); ++i)
                menu.addItem (i + 1, modes[i]);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&random), [this] (int r)
            {
                if (r > 0)
                    ctx.processor.randomise ((PatchRandomizer::Mode) (r - 1));
            });
        };
        mutate.onClick = [this] { ctx.processor.mutate (0.35f); };
        ctx.addListener (this);
    }

    ~PresetTools() override { ctx.removeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 4);
        prev.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (2);
        mutate.setBounds (r.removeFromRight (70));
        r.removeFromRight (4);
        random.setBounds (r.removeFromRight (70));
        r.removeFromRight (4);
        save.setBounds (r.removeFromRight (54));
        r.removeFromRight (6);
        next.setBounds (r.removeFromRight (26));
        r.removeFromRight (2);
        name.setBounds (r);
    }

private:
    void editorTick() override
    {
        const auto text = ctx.processor.getCurrentPresetName() + "   /   " + ctx.processor.getCurrentPresetCategory();
        if (text != name.getButtonText())
            name.setButtonText (text);
    }

    EditorContext& ctx;
    juce::TextButton prev { "<" }, next { ">" }, name, save { "SAVE" }, random { "RANDOM" }, mutate { "MUTATE" };
};

std::unique_ptr<juce::Component> createPresetTools (EditorContext& ctx) { return std::make_unique<PresetTools> (ctx); }

// =============================================================================================
// Header: A/B morph
// =============================================================================================
class MorphTools : public juce::Component, private EditorContext::Listener
{
public:
    explicit MorphTools (EditorContext& c)
        : ctx (c), enable (c, pid::global (GlobalField::MorphOn), "MORPH"), position (c, pid::global (GlobalField::MorphPosition), colours::modulation)
    {
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        for (auto* comp : std::initializer_list<juce::Component*> { &enable, &position, &store, &swap })
            addAndMakeVisible (comp);
        store.setTooltip ("Store the current sound as morph target B. A is always the live patch.");
        swap.setTooltip ("Swap the live patch (A) with B");
        store.onClick = [this] { ctx.processor.captureMorphTarget(); };
        swap.onClick = [this] { ctx.processor.swapMorphAB(); };
        ctx.addListener (this);
    }

    ~MorphTools() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.setFont (displayFont (12.0f));
        g.setColour (colours::text);
        g.drawText ("A", aLabel, juce::Justification::centred);
        g.setColour (hasTarget ? colours::modulation : colours::textFaint);
        g.drawText ("B", bLabel, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 6);
        enable.setBounds (r.removeFromLeft (74));
        swap.setBounds (r.removeFromRight (40));
        r.removeFromRight (4);
        store.setBounds (r.removeFromRight (62));
        r.removeFromRight (4);
        aLabel = r.removeFromLeft (12);
        bLabel = r.removeFromRight (12);
        position.setBounds (r);
    }

private:
    void editorTick() override
    {
        const bool now = ctx.processor.hasMorphTarget();
        swap.setEnabled (now);
        if (now != hasTarget)
        {
            hasTarget = now;
            repaint();
        }
    }

    EditorContext& ctx;
    ParamToggle enable;
    ParamSlider position;
    juce::TextButton store { "STORE B" }, swap { "A<>B" };
    juce::Rectangle<int> aLabel, bLabel;
    bool hasTarget = false;
};

std::unique_ptr<juce::Component> createMorphTools (EditorContext& ctx) { return std::make_unique<MorphTools> (ctx); }

// =============================================================================================
// Presets page
// =============================================================================================
class PresetsPage : public Page, private juce::ListBoxModel, private EditorContext::Listener
{
public:
    explicit PresetsPage (EditorContext& c) : Page (c)
    {
        categories.add ("All");
        categories.add ("Favourites");
        categories.addArray (getPresetCategories());

        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, colours::panel);
        historyModel.owner = this;
        history.setModel (&historyModel);
        history.setRowHeight (22);
        history.setColour (juce::ListBox::backgroundColourId, colours::panel);

        search.setTextToShowWhenEmpty ("Search presets...", colours::textFaint);
        search.onTextChange = [this] { refilter(); };

        saveCategory.addItemList (getPresetCategories(), 1);
        saveCategory.setSelectedId (getPresetCategories().size(), juce::dontSendNotification);
        saveName.setTextToShowWhenEmpty ("Preset name", colours::textFaint);
        saveAuthor.setTextToShowWhenEmpty ("Author", colours::textFaint);
        saveDescription.setTextToShowWhenEmpty ("Description", colours::textFaint);
        saveDescription.setMultiLine (true);

        for (auto* comp : std::initializer_list<juce::Component*> { &list, &search, &load, &favourite, &morphB, &remove, &saveName, &saveCategory,
                                                                    &saveAuthor, &saveDescription, &saveAs, &init, &mutateButton, &mutateAmount,
                                                                    &history, &rescan })
            addAndMakeVisible (comp);

        const auto modes = PatchRandomizer::getModeNames();
        const char* shortLabels[] = { "FULL", "OSC", "FILTER", "MOD", "MPE", "FX", "TEXTURE" };
        for (int i = 0; i < modes.size(); ++i)
        {
            auto b = std::make_unique<juce::TextButton> (shortLabels[i]);
            b->onClick = [this, i] { ctx.processor.randomise ((PatchRandomizer::Mode) i); refreshHistory(); };
            b->setTooltip ("Randomise " + modes[i].toLowerCase() + " with musical constraints");
            addAndMakeVisible (*b);
            randomButtons.push_back (std::move (b));
        }

        mutateAmount.setRange (0.05, 1.0, 0.01);
        mutateAmount.setValue (0.35, juce::dontSendNotification);
        mutateAmount.setSliderStyle (juce::Slider::LinearHorizontal);
        mutateAmount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        mutateAmount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        mutateAmount.setTooltip ("How far each mutation moves from the current sound");

        load.onClick = [this] { loadSelected(); };
        favourite.onClick = [this]
        {
            if (selected >= 0) { ctx.processor.getPresetManager().toggleFavourite (selected); refilter(); }
        };
        morphB.onClick = [this]
        {
            if (selected >= 0 && ctx.processor.getPresetManager().loadIntoMorphTarget (selected))
                ctx.setParam (pid::global (GlobalField::MorphOn), 1.0f, "Enable morph");
        };
        remove.onClick = [this]
        {
            if (selected < 0) return;
            const auto& e = ctx.processor.getPresetManager().getEntries()[(size_t) selected];
            if (e.factory) return;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                "Move \"" + e.name + "\" to the recycle bin?", "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this] (int result)
                                                {
                                                    if (result == 1)
                                                    {
                                                        ctx.processor.getPresetManager().deleteUser (selected);
                                                        selected = -1;
                                                        refilter();
                                                    }
                                                }));
        };
        saveAs.onClick = [this] { saveCurrent(); };
        init.onClick = [this] { ctx.processor.resetToInitPatch(); };
        mutateButton.onClick = [this] { ctx.processor.mutate ((float) mutateAmount.getValue()); refreshHistory(); };
        rescan.onClick = [this] { ctx.processor.getPresetManager().rescan(); refilter(); };

        ctx.addListener (this);
        refilter();
    }

    ~PresetsPage() override
    {
        ctx.removeListener (this);
        list.setModel (nullptr);
        history.setModel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, categoryArea.toFloat(), "Categories");
        drawPanel (g, listArea.toFloat(), "Library  (" + juce::String ((int) filtered.size()) + ")");
        drawPanel (g, detailArea.toFloat(), "Preset", colours::accent);
        drawPanel (g, generatorArea.toFloat(), "Sound generator", colours::modulation);

        // Categories
        for (int i = 0; i < categories.size(); ++i)
        {
            const auto r = categoryRow (i);
            const bool active = categories[i] == category;
            if (active)
            {
                g.setColour (colours::raised);
                g.fillRoundedRectangle (r.toFloat(), 3.0f);
                g.setColour (colours::accent);
                g.fillRect (r.withWidth (3));
            }
            g.setColour (active ? colours::text : colours::textDim);
            g.setFont (displayFont (13.0f));
            g.drawText (categories[i].toUpperCase(), r.withTrimmedLeft (10), juce::Justification::centredLeft);
        }

        // Details
        auto d = detailText;
        const auto& entries = ctx.processor.getPresetManager().getEntries();
        if (juce::isPositiveAndBelow (selected, (int) entries.size()))
        {
            const auto& e = entries[(size_t) selected];
            g.setColour (colours::text);
            g.setFont (font (18.0f, true));
            g.drawText (e.name, d.removeFromTop (26), juce::Justification::centredLeft, true);
            g.setColour (colours::textDim);
            g.setFont (font (12.5f));
            g.drawText (e.category + "   " + (e.factory ? juce::String ("Factory") : "User") + (e.author.isNotEmpty() ? "   by " + e.author : juce::String()),
                        d.removeFromTop (18), juce::Justification::centredLeft, true);
            d.removeFromTop (6);
            g.setColour (colours::text.withAlpha (0.85f));
            g.setFont (font (13.0f));
            g.drawFittedText (e.description, d, juce::Justification::topLeft, 5);
        }
        else
        {
            g.setColour (colours::textFaint);
            g.setFont (font (13.0f));
            g.drawText ("Select a preset", d, juce::Justification::topLeft);
        }

        g.setColour (colours::textDim);
        g.setFont (displayFont (11.5f));
        g.drawText ("SAVE CURRENT SOUND", saveLabel, juce::Justification::centredLeft);
        g.drawText ("RANDOMISE", randomLabel, juce::Justification::centredLeft);
        g.drawText ("MUTATE", mutateLabel, juce::Justification::centredLeft);
        g.drawText ("HISTORY  (click to go back)", historyLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        categoryArea = r.removeFromLeft (170);
        r.removeFromLeft (metrics::gap);
        generatorArea = r.removeFromRight (300);
        r.removeFromRight (metrics::gap);
        detailArea = r.removeFromRight (300);
        r.removeFromRight (metrics::gap);
        listArea = r;

        auto l = listArea.withTrimmedTop (panelTitleHeight).reduced (8, 6);
        auto top = l.removeFromTop (26);
        rescan.setBounds (top.removeFromRight (70));
        top.removeFromRight (6);
        search.setBounds (top);
        l.removeFromTop (6);
        list.setBounds (l);

        auto d = detailArea.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        detailText = d.removeFromTop (130);
        auto row = d.removeFromTop (28);
        load.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        favourite.setBounds (row);
        d.removeFromTop (6);
        row = d.removeFromTop (28);
        morphB.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        remove.setBounds (row);
        d.removeFromTop (16);
        saveLabel = d.removeFromTop (18);
        saveName.setBounds (d.removeFromTop (26));
        d.removeFromTop (6);
        row = d.removeFromTop (26);
        saveCategory.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        saveAuthor.setBounds (row);
        d.removeFromTop (6);
        saveDescription.setBounds (d.removeFromTop (60));
        d.removeFromTop (6);
        row = d.removeFromTop (28);
        saveAs.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        init.setBounds (row);

        auto gA = generatorArea.withTrimmedTop (panelTitleHeight).reduced (10, 6);
        randomLabel = gA.removeFromTop (18);
        const int perRow = 3;
        for (size_t i = 0; i < randomButtons.size(); i += (size_t) perRow)
        {
            auto line = gA.removeFromTop (26);
            const int w = line.getWidth() / perRow;
            for (size_t j = i; j < std::min (randomButtons.size(), i + (size_t) perRow); ++j)
                randomButtons[j]->setBounds (line.removeFromLeft (w).reduced (2, 0));
            gA.removeFromTop (4);
        }
        gA.removeFromTop (8);
        mutateLabel = gA.removeFromTop (18);
        row = gA.removeFromTop (26);
        mutateButton.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (6);
        mutateAmount.setBounds (row);
        gA.removeFromTop (12);
        historyLabel = gA.removeFromTop (18);
        history.setBounds (gA);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < categories.size(); ++i)
            if (categoryRow (i).contains (e.getPosition()))
            {
                category = categories[i];
                refilter();
                repaint();
            }
    }

private:
    juce::Rectangle<int> categoryRow (int i) const
    {
        return categoryArea.withTrimmedTop (panelTitleHeight + 6).reduced (6, 0).withHeight (28).translated (0, i * 30);
    }

    void refilter()
    {
        filtered = ctx.processor.getPresetManager().filter (category, search.getText());
        list.updateContent();
        list.repaint();
        repaint();
    }

    void refreshHistory()
    {
        history.updateContent();
        history.repaint();
    }

    void loadSelected()
    {
        if (selected >= 0)
            ctx.processor.getPresetManager().load (selected);
    }

    void saveCurrent()
    {
        auto& pm = ctx.processor.getPresetManager();
        const auto nameText = saveName.getText().trim().isNotEmpty() ? saveName.getText() : ctx.processor.getCurrentPresetName();
        const int index = pm.saveUser (nameText, saveCategory.getText(), saveAuthor.getText(), saveDescription.getText());
        if (index < 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save", pm.getLastError());
            return;
        }
        selected = index;
        refilter();
    }

    // ---- ListBoxModel
    int getNumRows() override { return (int) filtered.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) filtered.size()))
            return;
        const auto& e = ctx.processor.getPresetManager().getEntries()[(size_t) filtered[(size_t) row]];
        const bool current = filtered[(size_t) row] == ctx.processor.getPresetManager().getCurrentIndex();
        if (rowIsSelected || current)
        {
            g.setColour (rowIsSelected ? colours::accentSoft : colours::control);
            g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, (float) width - 4.0f, (float) height - 2.0f), 6.0f);
        }
        g.setColour (e.favourite ? colours::amber : colours::textFaint.withAlpha (0.4f));
        g.setFont (font (14.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")), 6, 0, 18, height, juce::Justification::centred);
        g.setColour (current ? colours::accent : colours::text);
        g.setFont (font (13.5f, current));
        g.drawText (e.name, 30, 0, width / 2 - 30, height, juce::Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.setFont (font (12.0f));
        g.drawText (e.category, width / 2, 0, width / 4, height, juce::Justification::centredLeft, true);
        g.setColour (e.factory ? colours::textFaint : colours::slide.withAlpha (0.8f));
        g.drawText (e.factory ? "FACTORY" : "USER", width * 3 / 4, 0, width / 4 - 8, height, juce::Justification::centredRight);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        selected = juce::isPositiveAndBelow (lastRowSelected, (int) filtered.size()) ? filtered[(size_t) lastRowSelected] : -1;
        const auto& entries = ctx.processor.getPresetManager().getEntries();
        if (selected >= 0)
        {
            remove.setEnabled (! entries[(size_t) selected].factory);
            saveName.setText (entries[(size_t) selected].factory ? juce::String() : entries[(size_t) selected].name, false);
        }
        repaint();
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, (int) filtered.size()))
        {
            selected = filtered[(size_t) row];
            loadSelected();
        }
    }

    void returnKeyPressed (int) override { loadSelected(); }

    struct HistoryModel : public juce::ListBoxModel
    {
        PresetsPage* owner = nullptr;
        int getNumRows() override { return (int) owner->ctx.processor.getMutationHistory().entries.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
        {
            const auto& h = owner->ctx.processor.getMutationHistory();
            if (! juce::isPositiveAndBelow (row, (int) h.entries.size())) return;
            const bool current = row == h.current;
            if (current) { g.setColour (colours::raised); g.fillRect (0, 0, width, height); }
            g.setColour (current ? colours::modulation : colours::textDim);
            g.setFont (font (12.5f, current));
            g.drawText (juce::String (row + 1) + ".  " + h.entries[(size_t) row].name, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            owner->ctx.processor.recallMutation (row);
            owner->history.repaint();
        }
    };

    void editorTick() override
    {
        const int historySize = (int) ctx.processor.getMutationHistory().entries.size();
        const int current = ctx.processor.getPresetManager().getCurrentIndex();
        if (historySize != lastHistorySize || current != lastCurrent)
        {
            lastHistorySize = historySize;
            lastCurrent = current;
            refreshHistory();
            list.repaint();
        }
    }

    juce::StringArray categories;
    juce::String category { "All" };
    std::vector<int> filtered;
    int selected = -1;
    int lastHistorySize = -1, lastCurrent = -2;

    juce::ListBox list, history;
    HistoryModel historyModel;
    juce::TextEditor search, saveName, saveAuthor, saveDescription;
    juce::ComboBox saveCategory;
    juce::TextButton load { "LOAD" }, favourite { "FAVOURITE" }, morphB { "LOAD AS MORPH B" }, remove { "DELETE" },
                     saveAs { "SAVE AS" }, init { "INIT PATCH" }, mutateButton { "MUTATE" }, rescan { "RESCAN" };
    juce::Slider mutateAmount;
    std::vector<std::unique_ptr<juce::TextButton>> randomButtons;
    juce::Rectangle<int> categoryArea, listArea, detailArea, generatorArea, detailText, saveLabel, randomLabel, mutateLabel, historyLabel;
};

std::unique_ptr<Page> createPresetsPage (EditorContext& ctx) { return std::make_unique<PresetsPage> (ctx); }

} // namespace nedd::ui
