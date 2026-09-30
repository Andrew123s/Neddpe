#pragma once

#include "ExpressionKeyboard.h"
#include "ParamControls.h"
#include "Pages/Pages.h"

namespace nedd::ui
{
/** Top bar: identity, patch name, undo/redo, the preset/randomise tools, morph and output. */
class HeaderBar : public juce::Component, private EditorContext::Listener
{
public:
    explicit HeaderBar (EditorContext& ctx);
    ~HeaderBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Extra header tools added by later subsystems (presets, randomiser, morph). */
    void setTools (juce::Component* presetTools, juce::Component* morphTools);

private:
    void editorTick() override;

    EditorContext& ctx;
    juce::TextButton undo { "UNDO" }, redo { "REDO" };
    OutputMeter meter;
    juce::Component* presetTools = nullptr;
    juce::Component* morphTools = nullptr;
    juce::String patchName;
    float cpu = 0.0f;
};

/** Vertical page navigation. */
class NavRail : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void addPage (int id, const juce::String& name) { items.push_back ({ id, name }); repaint(); }
    void setCurrent (int id) { current = id; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    int itemAt (juce::Point<int>) const;
    struct Item { int id; juce::String name; };
    std::vector<Item> items;
    int current = 0, hover = -1;
    static constexpr int itemHeight = 34;
};

/** Always-visible performance strip: four named macros and the expression keyboard. */
class PerformanceStrip : public juce::Component, private EditorContext::Listener
{
public:
    explicit PerformanceStrip (EditorContext& ctx);
    ~PerformanceStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void editorTick() override;

    EditorContext& ctx;
    std::array<std::unique_ptr<ParamKnob>, (size_t) kNumMacros> macros;
    std::array<juce::Label, (size_t) kNumMacros> names;
    ExpressionKeyboard keyboard;
    juce::TextButton octaveDown { "<" }, octaveUp { ">" };
};

} // namespace nedd::ui
