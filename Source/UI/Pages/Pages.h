#pragma once

#include "UI/MpeVisualizer.h"
#include "UI/Panels.h"

namespace nedd::ui
{
/** Navigation targets, in rail order. */
enum class PageId : int
{
    Main = 0, Oscillators, Filter, Envelopes, Lfo, Mpe, Matrix, Effects, Arp, Sequencer, NoteEditor, Presets, Settings, Count
};

class Page : public juce::Component
{
public:
    explicit Page (EditorContext& c) : ctx (c) {}

protected:
    EditorContext& ctx;
};

// ---------------------------------------------------------------------------------------------
class MpeQuickCard;
class OutputCard;

class MainPage : public Page
{
public:
    explicit MainPage (EditorContext& ctx);
    ~MainPage() override;
    void resized() override;

private:
    std::array<std::unique_ptr<OscillatorPanel>, 3> oscillators;
    FilterPanel filter;
    EnvelopePanel ampEnv;
    std::unique_ptr<MpeQuickCard> mpe;
    std::unique_ptr<juce::Component> effects;
    std::unique_ptr<OutputCard> output;
};

class OscillatorPage : public Page
{
public:
    explicit OscillatorPage (EditorContext& ctx);
    void resized() override;

private:
    std::array<std::unique_ptr<OscillatorPanel>, 3> oscillators;
};

class FilterPage : public Page
{
public:
    explicit FilterPage (EditorContext& ctx);
    void resized() override;

private:
    FilterPanel filter;
    EnvelopePanel filterEnv;
    std::unique_ptr<juce::Component> routes;
};

class EnvelopePage : public Page
{
public:
    explicit EnvelopePage (EditorContext& ctx);
    void resized() override;

private:
    EnvelopePanel amp, filter, mod;
};

// ---------------------------------------------------------------------------------------------
class ExpressionRoutingPanel;
class CalibrationPanel;

class MpePage : public Page
{
public:
    explicit MpePage (EditorContext& ctx);
    ~MpePage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void setView (bool calibration);

    ExpressionHistory history;
    ExpressionField field;
    VoiceLanes lanes;
    ExpressionTimeline timeline;
    std::unique_ptr<ExpressionRoutingPanel> routing;
    std::unique_ptr<CalibrationPanel> calibration;
    juce::TextButton performanceButton { "PERFORMANCE" }, calibrationButton { "CALIBRATION" };
    bool showingCalibration = false;
};

// ---------------------------------------------------------------------------------------------
class MatrixRow;
class ModInspector;

class MatrixPage : public Page
{
public:
    explicit MatrixPage (EditorContext& ctx);
    ~MatrixPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    std::vector<std::unique_ptr<MatrixRow>> rows;
    std::unique_ptr<ModInspector> inspector;
};

// ---------------------------------------------------------------------------------------------
class SettingsPage : public Page
{
public:
    explicit SettingsPage (EditorContext& ctx, std::function<void (float)> setUiScale);
    ~SettingsPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Factories for pages whose classes are private to their translation unit
std::unique_ptr<Page> createLfoPage (EditorContext& ctx);
std::unique_ptr<Page> createEffectsPage (EditorContext& ctx);
std::unique_ptr<juce::Component> createEffectsOverview (EditorContext& ctx);
std::unique_ptr<Page> createSequencerPage (EditorContext& ctx);
std::unique_ptr<Page> createArpPage (EditorContext& ctx);
std::unique_ptr<Page> createNoteEditorPage (EditorContext& ctx);
std::unique_ptr<Page> createPresetsPage (EditorContext& ctx);
std::unique_ptr<juce::Component> createPresetTools (EditorContext& ctx);
std::unique_ptr<juce::Component> createMorphTools (EditorContext& ctx);

} // namespace nedd::ui
