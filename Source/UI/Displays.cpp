#include "Displays.h"
#include "DSP/DspMath.h"
#include "Voices/Oscillator.h"
#include <complex>

namespace nedd::ui
{
// =============================================================================================
// EnvelopeDisplay
// =============================================================================================
namespace
{
    constexpr float kMaxEnvTime = 20.0f;
    constexpr float kTimeScale = 200.0f;
}

EnvelopeDisplay::EnvelopeDisplay (EditorContext& c, int index, juce::Colour col) : ctx (c), envIndex (index), colour (col)
{
    ctx.addListener (this);
    setTooltip ("Drag the nodes: peak = attack time, middle = decay time and sustain level, end = release time.");
}

EnvelopeDisplay::~EnvelopeDisplay() { ctx.removeListener (this); }

float EnvelopeDisplay::timeToWidth (float seconds)
{
    if (seconds <= 0.0f)
        return 0.0f;
    return juce::jlimit (0.03f, 1.0f, std::log10 (1.0f + seconds * kTimeScale) / std::log10 (1.0f + kMaxEnvTime * kTimeScale));
}

float EnvelopeDisplay::widthToTime (float fraction)
{
    fraction = juce::jlimit (0.0f, 1.0f, fraction);
    return (std::pow (10.0f, fraction * std::log10 (1.0f + kMaxEnvTime * kTimeScale)) - 1.0f) / kTimeScale;
}

EnvelopeDisplay::Geometry EnvelopeDisplay::computeGeometry() const
{
    Geometry geo;
    geo.area = getLocalBounds().toFloat().reduced (10.0f, 12.0f);
    geo.segmentWidth = geo.area.getWidth() * 0.86f / 5.0f;
    const float s = geo.segmentWidth;

    geo.xDelay = geo.area.getX() + s * timeToWidth (valueOf (EnvField::Delay));
    geo.xAttack = geo.xDelay + std::max (2.0f, s * timeToWidth (valueOf (EnvField::Attack)));
    geo.xHold = geo.xAttack + s * timeToWidth (valueOf (EnvField::Hold));
    geo.xDecay = geo.xHold + std::max (2.0f, s * timeToWidth (valueOf (EnvField::Decay)));
    geo.xSustainEnd = geo.xDecay + geo.area.getWidth() * 0.14f;
    geo.xRelease = geo.xSustainEnd + std::max (2.0f, s * timeToWidth (valueOf (EnvField::Release)));
    return geo;
}

void EnvelopeDisplay::editorTick()
{
    std::array<float, 10> values {};
    for (int f = 0; f < (int) EnvField::Count; ++f)
        values[(size_t) f] = valueOf ((EnvField) f);

    bool changed = values != lastValues;
    lastValues = values;

    if (envIndex == pid::ampEnv)
    {
        for (int v = 0; v < kPhysicalVoices; ++v)
        {
            const auto& t = ctx.telemetry.voices[(size_t) v];
            const float level = t.active.load (std::memory_order_relaxed) ? t.ampEnv.load (std::memory_order_relaxed) : -1.0f;
            changed = changed || std::abs (level - voiceLevels[(size_t) v]) > 0.002f;
            voiceLevels[(size_t) v] = level;
        }
    }

    if (changed)
        repaint();
}

void EnvelopeDisplay::paint (juce::Graphics& g)
{
    const auto geo = computeGeometry();
    const auto& a = geo.area;
    auto y = [&a] (float level) { return a.getBottom() - level * a.getHeight(); };

    // grid
    g.setColour (colours::outline.withAlpha (0.6f));
    for (float level : { 0.25f, 0.5f, 0.75f })
        g.drawHorizontalLine ((int) y (level), a.getX(), a.getRight());

    const float sustain = valueOf (EnvField::Sustain);
    juce::Path path;
    path.startNewSubPath (a.getX(), y (0.0f));
    path.lineTo (geo.xDelay, y (0.0f));

    auto curveSegment = [&path, &y] (float x0, float x1, float from, float to, float curve)
    {
        constexpr int steps = 24;
        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            path.lineTo (x0 + (x1 - x0) * t, y (from + (to - from) * dsp::tensionCurve (t, curve)));
        }
    };

    curveSegment (geo.xDelay, geo.xAttack, 0.0f, 1.0f, valueOf (EnvField::AttackCurve));
    path.lineTo (geo.xHold, y (1.0f));
    curveSegment (geo.xHold, geo.xDecay, 1.0f, sustain, valueOf (EnvField::DecayCurve));
    path.lineTo (geo.xSustainEnd, y (sustain));
    curveSegment (geo.xSustainEnd, geo.xRelease, sustain, 0.0f, valueOf (EnvField::ReleaseCurve));

    juce::Path fill (path);
    fill.lineTo (geo.xRelease, y (0.0f));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, a.getY(), colour.withAlpha (0.02f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto node = [&g, this] (float x, float yy, Node which)
    {
        const bool active = dragging == which || hover == which;
        const float r = active ? 5.5f : 4.0f;
        g.setColour (colours::background);
        g.fillEllipse (x - r, yy - r, r * 2.0f, r * 2.0f);
        g.setColour (active ? colours::text : colour);
        g.drawEllipse (x - r, yy - r, r * 2.0f, r * 2.0f, 1.5f);
    };
    node (geo.xAttack, y (1.0f), Node::Attack);
    node (geo.xDecay, y (sustain), Node::Decay);
    node (geo.xRelease, y (0.0f), Node::Release);

    // Level of each sounding note (amp envelope only)
    if (envIndex == pid::ampEnv)
    {
        for (int v = 0; v < kPhysicalVoices; ++v)
        {
            if (voiceLevels[(size_t) v] < 0.0f)
                continue;
            const auto& t = ctx.telemetry.voices[(size_t) v];
            g.setColour (noteColour (t.noteId.load (std::memory_order_relaxed), t.noteNumber.load (std::memory_order_relaxed)));
            g.fillRect (juce::Rectangle<float> (a.getX() - 8.0f, y (voiceLevels[(size_t) v]) - 1.0f, 6.0f, 2.0f));
        }
    }

    // Times
    g.setColour (colours::textFaint);
    g.setFont (font (10.5f));
    auto timeText = [] (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + "ms" : juce::String (s, 2) + "s"; };
    g.drawText ("A " + timeText (valueOf (EnvField::Attack)) + "   D " + timeText (valueOf (EnvField::Decay)) + "   S "
                    + juce::String (juce::roundToInt (sustain * 100.0f)) + "%   R " + timeText (valueOf (EnvField::Release)),
                getLocalBounds().reduced (10, 0).removeFromBottom (13), juce::Justification::centredRight);
}

EnvelopeDisplay::Node EnvelopeDisplay::hitTest (juce::Point<float> p) const
{
    const auto geo = computeGeometry();
    const auto& a = geo.area;
    auto y = [&a] (float level) { return a.getBottom() - level * a.getHeight(); };
    const std::pair<Node, juce::Point<float>> nodes[] = {
        { Node::Attack, { geo.xAttack, y (1.0f) } },
        { Node::Decay, { geo.xDecay, y (valueOf (EnvField::Sustain)) } },
        { Node::Release, { geo.xRelease, y (0.0f) } },
    };

    Node best = Node::None;
    float bestDistance = 12.0f;
    for (const auto& [node, pos] : nodes)
    {
        const float d = pos.getDistanceFrom (p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = node;
        }
    }
    return best;
}

void EnvelopeDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == Node::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void EnvelopeDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTest (e.position);
    if (dragging == Node::None)
        return;

    for (auto f : { EnvField::Attack, EnvField::Decay, EnvField::Sustain, EnvField::Release })
        if (auto* p = ctx.processor.getParameterByIndex (pid::env (envIndex, f)))
            p->beginChangeGesture();
}

void EnvelopeDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Node::None)
        return;

    const auto geo = computeGeometry();
    auto set = [this] (EnvField f, float plain)
    {
        if (auto* p = ctx.processor.getParameterByIndex (pid::env (envIndex, f)))
        {
            const auto& def = getParamDef (pid::env (envIndex, f));
            p->setValueNotifyingHost (def.range.convertTo0to1 (juce::jlimit (def.range.start, def.range.end, plain)));
        }
    };

    switch (dragging)
    {
        case Node::Attack:
            set (EnvField::Attack, widthToTime ((e.position.x - geo.xDelay) / geo.segmentWidth));
            break;
        case Node::Decay:
            set (EnvField::Decay, std::max (0.001f, widthToTime ((e.position.x - geo.xHold) / geo.segmentWidth)));
            set (EnvField::Sustain, (geo.area.getBottom() - e.position.y) / geo.area.getHeight());
            break;
        case Node::Release:
            set (EnvField::Release, std::max (0.001f, widthToTime ((e.position.x - geo.xSustainEnd) / geo.segmentWidth)));
            break;
        case Node::None:
            break;
    }
}

void EnvelopeDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Node::None)
        return;
    for (auto f : { EnvField::Attack, EnvField::Decay, EnvField::Sustain, EnvField::Release })
        if (auto* p = ctx.processor.getParameterByIndex (pid::env (envIndex, f)))
            p->endChangeGesture();
    dragging = Node::None;
    repaint();
}

// =============================================================================================
// WaveDisplay
// =============================================================================================
WaveDisplay::WaveDisplay (EditorContext& c, int index) : ctx (c), oscIndex (index)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

WaveDisplay::~WaveDisplay() { ctx.removeListener (this); }

void WaveDisplay::editorTick()
{
    float sig = 0.0f;
    int prime = 1;
    for (auto f : { OscField::On, OscField::Engine, OscField::Wave, OscField::PulseWidth, OscField::Table, OscField::WtPos,
                    OscField::NoiseType, OscField::FmAlgorithm, OscField::Op1Ratio, OscField::Op2Ratio, OscField::OpFine,
                    OscField::Op1Amount, OscField::Op2Amount, OscField::FmFeedback, OscField::GrainSpray, OscField::SampleLoop,
                    OscField::SampleSource })
    {
        sig += ctx.param (pid::osc (oscIndex, f)) * (float) (prime += 7);
    }
    sig += (float) ctx.processor.getAssetsVersion() * 1000.0f;

    float live = -1.0f;
    if (ctx.hasActiveVoice())
    {
        const float mod = ctx.liveModulation (oscDest (ModDest::Osc1WtPos, oscIndex));
        if (mod != 0.0f)
            live = dsp::clamp01 (ctx.param (pid::osc (oscIndex, OscField::WtPos)) + mod);
    }

    if (sig != signature)
    {
        signature = sig;
        rebuild();
        repaint();
    }
    else if (std::abs (live - livePosition) > 0.002f)
    {
        repaint();
    }
    livePosition = live;
}

const Wavetable& WaveDisplay::currentTable() const
{
    const int index = ctx.params().getInt (pid::osc (oscIndex, OscField::Table));
    const auto& slot = ctx.processor.getOscillatorAssets().slots[(size_t) oscIndex];
    if (index == kImportedWavetableChoice && slot.wavetable != nullptr)
        return slot.wavetable->table;
    const auto& bank = WavetableBank::getInstance();
    return bank.get (std::min (index, bank.size() - 1));
}

const SampleData& WaveDisplay::currentSample() const
{
    const auto& slot = ctx.processor.getOscillatorAssets().slots[(size_t) oscIndex];
    return slot.sample != nullptr ? *slot.sample : assets::getBuiltInSample (ctx.params().getInt (pid::osc (oscIndex, OscField::SampleSource)));
}

void WaveDisplay::rebuild()
{
    constexpr int kPoints = 256;
    cycle.assign (kPoints, 0.0f);

    const auto engine = ctx.params().getChoice<OscEngine> (pid::osc (oscIndex, OscField::Engine));
    if (engine == OscEngine::Wavetable)
        return;

    if (engine == OscEngine::Granular || engine == OscEngine::Sample)
    {
        // Peak overview of the whole sample.
        const auto& s = currentSample();
        const float* left = s.channel (0);
        const float* right = s.channel (1);
        for (int i = 0; i < kPoints; ++i)
        {
            const int from = (int) ((juce::int64) s.length * i / kPoints);
            const int to = std::max (from + 1, (int) ((juce::int64) s.length * (i + 1) / kPoints));
            float peak = 0.0f;
            for (int n = from; n < std::min (to, s.length); ++n)
                peak = std::max (peak, std::max (std::abs (left[n]), std::abs (right[n])));
            cycle[(size_t) i] = peak;
        }
        return;
    }

    Oscillator osc;
    osc.prepare (48000.0f);
    dsp::Random32 rng;
    osc.noteOn (0.0f, 0.0f, rng);

    const auto& p = ctx.params();
    auto f = [this] (OscField field) { return pid::osc (oscIndex, field); };
    OscillatorBlockParams bp;
    bp.engine = engine;
    bp.wave = p.getChoice<AnalogWave> (f (OscField::Wave));
    bp.noise = p.getChoice<NoiseType> (f (OscField::NoiseType));
    bp.algorithm = p.getChoice<FmAlgorithm> (f (OscField::FmAlgorithm));
    bp.pulseWidth = p[f (OscField::PulseWidth)];
    bp.frequency = 48000.0f / (float) kPoints;
    bp.unison = 1;
    const float fine = p[f (OscField::OpFine)];
    bp.op1Ratio = fmRatioValue (p.getInt (f (OscField::Op1Ratio))) + fine;
    bp.op2Ratio = fmRatioValue (p.getInt (f (OscField::Op2Ratio))) + fine;
    const float a1 = p[f (OscField::Op1Amount)], a2 = p[f (OscField::Op2Amount)];
    bp.op1Index = a1 * a1 * 2.0f;
    bp.op2Index = a2 * a2 * 2.0f;
    bp.feedback = p[f (OscField::FmFeedback)];
    osc.setBlock (bp);

    // Let feedback settle for one cycle, then capture the next.
    for (int i = 0; i < kPoints; ++i)
        osc.tick (0.0f, -1.0f);
    for (int i = 0; i < kPoints; ++i)
        cycle[(size_t) i] = osc.tick (0.0f, -1.0f).mono;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (colours::background.withAlpha (0.6f));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto& p = ctx.params();
    const bool on = p.getBool (pid::osc (oscIndex, OscField::On));
    const auto colour = on ? colours::accent : colours::textFaint;
    const auto engine = p.getChoice<OscEngine> (pid::osc (oscIndex, OscField::Engine));

    if (engine == OscEngine::Granular || engine == OscEngine::Sample)
    {
        paintSample (g, bounds, colour);
        return;
    }

    if (engine == OscEngine::Wavetable)
    {
        const auto& table = currentTable();
        const float position = p[pid::osc (oscIndex, OscField::WtPos)];
        constexpr int layers = 12;
        const auto area = bounds.reduced (8.0f, 8.0f);
        const float depthX = area.getWidth() * 0.18f, depthY = area.getHeight() * 0.32f;
        const float w = area.getWidth() - depthX, h = area.getHeight() - depthY;

        auto drawFrame = [&] (float pos, juce::Colour c, float thickness)
        {
            const float ox = area.getX() + depthX * (1.0f - pos);
            const float oy = area.getY() + depthY * pos;
            juce::Path path;
            for (int i = 0; i <= 96; ++i)
            {
                const float phase = (float) i / 96.0f;
                const float v = table.sample (std::min (phase, 0.9999f), pos, 3);
                const float x = ox + phase * w;
                const float yy = oy + h * 0.5f - v * h * 0.45f;
                if (i == 0) path.startNewSubPath (x, yy); else path.lineTo (x, yy);
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (thickness));
        };

        for (int l = 0; l < layers; ++l)
            drawFrame ((float) l / (float) (layers - 1), colour.withAlpha (0.12f), 1.0f);
        drawFrame (position, colour, 2.0f);
        if (livePosition >= 0.0f)
            drawFrame (livePosition, colours::modulation, 1.5f);
        return;
    }

    if (cycle.empty())
        return;

    const auto area = bounds.reduced (6.0f, 8.0f);
    g.setColour (colours::outline);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    float peak = 0.001f;
    for (auto v : cycle)
        peak = std::max (peak, std::abs (v));

    juce::Path path;
    for (size_t i = 0; i < cycle.size(); ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (cycle.size() - 1);
        const float yy = area.getCentreY() - cycle[i] / peak * area.getHeight() * 0.45f;
        if (i == 0) path.startNewSubPath (x, yy); else path.lineTo (x, yy);
    }
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.8f));
}

void WaveDisplay::paintSample (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto& p = ctx.params();
    const auto engine = p.getChoice<OscEngine> (pid::osc (oscIndex, OscField::Engine));
    const auto area = bounds.reduced (6.0f, 8.0f);
    if (cycle.empty())
        return;

    float peak = 0.001f;
    for (auto v : cycle)
        peak = std::max (peak, v);

    juce::Path wave;
    const float centre = area.getCentreY();
    const float half = area.getHeight() * 0.45f;
    wave.startNewSubPath (area.getX(), centre);
    for (size_t i = 0; i < cycle.size(); ++i)
        wave.lineTo (area.getX() + area.getWidth() * (float) i / (float) (cycle.size() - 1), centre - cycle[i] / peak * half);
    for (size_t i = cycle.size(); i-- > 0;)
        wave.lineTo (area.getX() + area.getWidth() * (float) i / (float) (cycle.size() - 1), centre + cycle[i] / peak * half);
    wave.closeSubPath();
    g.setColour (colour.withAlpha (0.35f));
    g.fillPath (wave);

    const float position = p[pid::osc (oscIndex, OscField::WtPos)];
    auto xFor = [&area] (float pos) { return area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, pos); };

    if (engine == OscEngine::Granular)
    {
        // Region the grains are read from: position +- spray.
        const float spray = p[pid::osc (oscIndex, OscField::GrainSpray)] * 0.15f;
        const float x0 = xFor (position - spray), x1 = xFor (position + spray);
        g.setColour (colour.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), std::max (2.0f, x1 - x0), area.getHeight()));
    }
    else if (p.getBool (pid::osc (oscIndex, OscField::SampleLoop)))
    {
        g.setColour (colour.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (xFor (position), area.getY(), area.getRight() - xFor (position), area.getHeight()));
    }

    g.setColour (colour);
    g.drawVerticalLine ((int) xFor (position), area.getY(), area.getBottom());
    if (livePosition >= 0.0f)
    {
        g.setColour (colours::modulation);
        g.drawVerticalLine ((int) xFor (livePosition), area.getY(), area.getBottom());
    }
}

// =============================================================================================
// FilterDisplay
// =============================================================================================
FilterDisplay::FilterDisplay (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

FilterDisplay::~FilterDisplay() { ctx.removeListener (this); }

void FilterDisplay::editorTick()
{
    float sig = 0.0f;
    for (auto f : { FilterField::On, FilterField::Type, FilterField::Cutoff, FilterField::Resonance })
        sig = sig * 31.0f + ctx.param (pid::filter (f));

    std::vector<Marker> now;
    for (int v = 0; v < kPhysicalVoices; ++v)
    {
        const auto& t = ctx.telemetry.voices[(size_t) v];
        if (! t.active.load (std::memory_order_relaxed))
            continue;
        now.push_back ({ t.cutoffHz.load (std::memory_order_relaxed),
                         noteColour (t.noteId.load (std::memory_order_relaxed), t.noteNumber.load (std::memory_order_relaxed)),
                         t.ampEnv.load (std::memory_order_relaxed) });
    }

    const bool markersChanged = now.size() != markers.size()
        || ! std::equal (now.begin(), now.end(), markers.begin(), [] (const Marker& a, const Marker& b) { return std::abs (a.cutoff - b.cutoff) < 1.0f; });

    if (sig != signature || markersChanged)
    {
        signature = sig;
        markers = std::move (now);
        repaint();
    }
}

float FilterDisplay::magnitudeDb (float frequency, float cutoff, float resonance, FilterType type) const
{
    using C = std::complex<float>;
    const float w = frequency / cutoff;
    const C s (0.0f, w);

    auto lp2 = [&s] (float k) { return C (1.0f) / (s * s + k * s + 1.0f); };
    auto hp2 = [&s] (float k) { return (s * s) / (s * s + k * s + 1.0f); };

    const float k = 2.0f - 1.96f * resonance;
    C h;
    switch (type)
    {
        case FilterType::LowPass12:  h = lp2 (k); break;
        case FilterType::HighPass12: h = hp2 (k); break;
        case FilterType::BandPass:   h = (k * s) / (s * s + k * s + 1.0f); break;
        case FilterType::Notch:      h = (s * s + 1.0f) / (s * s + k * s + 1.0f); break;
        case FilterType::LowPass24:  h = lp2 (1.8478f) * lp2 (std::min (0.7654f, k)); break;
        case FilterType::HighPass24: h = hp2 (1.8478f) * hp2 (std::min (0.7654f, k)); break;
        case FilterType::Ladder:
        {
            const float feedback = resonance * 3.95f;
            const C g = C (1.0f) / (C (1.0f) + s);
            const C g4 = g * g * g * g;
            h = g4 * (1.0f + feedback * 0.55f) / (C (1.0f) + feedback * g4);
            break;
        }
    }
    return juce::Decibels::gainToDecibels (std::abs (h), -60.0f);
}

void FilterDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (colours::background.withAlpha (0.6f));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto area = bounds.reduced (6.0f, 6.0f);
    constexpr float minDb = -36.0f, maxDb = 24.0f;
    auto xFor = [&area] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yFor = [&area] (float db) { return area.getY() + area.getHeight() * (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb); };

    g.setFont (font (9.5f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (colours::outline);
        g.drawVerticalLine ((int) xFor (hz), area.getY(), area.getBottom());
        g.setColour (colours::textFaint);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xFor (hz) + 2.0f, area.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::left);
    }
    g.setColour (colours::outline);
    g.drawHorizontalLine ((int) yFor (0.0f), area.getX(), area.getRight());

    const auto& p = ctx.params();
    const bool on = p.getBool (pid::filter (FilterField::On));
    const auto type = p.getChoice<FilterType> (pid::filter (FilterField::Type));
    const float cutoff = p[pid::filter (FilterField::Cutoff)];
    const float reso = p[pid::filter (FilterField::Resonance)];

    juce::Path path;
    constexpr int points = 160;
    for (int i = 0; i <= points; ++i)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) points);
        const float y = yFor (on ? magnitudeDb (hz, cutoff, reso, type) : 0.0f);
        if (i == 0) path.startNewSubPath (xFor (hz), y); else path.lineTo (xFor (hz), y);
    }

    juce::Path fill (path);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    const auto colour = on ? colours::accent : colours::textFaint;
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f), 0.0f, area.getY(), colour.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Per-note cutoffs: each sounding note's own filter position (MPE pressure, slide, envelope...).
    for (const auto& m : markers)
    {
        const float x = xFor (juce::jlimit (20.0f, 20000.0f, m.cutoff));
        g.setColour (m.colour.withAlpha (0.25f + 0.6f * m.level));
        g.drawLine (x, area.getY() + 4.0f, x, area.getBottom(), 1.5f);
        g.fillEllipse (x - 3.0f, area.getY(), 6.0f, 6.0f);
    }
}

// =============================================================================================
// OutputMeter
// =============================================================================================
OutputMeter::OutputMeter (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

OutputMeter::~OutputMeter() { ctx.removeListener (this); }

void OutputMeter::editorTick()
{
    const float pl = ctx.getPeakLeft();
    const float pr = ctx.getPeakRight();
    left = std::max (pl, left * 0.8f);
    right = std::max (pr, right * 0.8f);

    if (pl >= holdL) { holdL = pl; holdCounterL = 45; } else if (--holdCounterL <= 0) holdL *= 0.9f;
    if (pr >= holdR) { holdR = pr; holdCounterR = 45; } else if (--holdCounterR <= 0) holdR *= 0.9f;
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float barWidth = (bounds.getWidth() - 3.0f) * 0.5f;
    auto toY = [&bounds] (float gain)
    {
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f);
        return bounds.getBottom() - bounds.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (bounds.getX() + (float) c * (barWidth + 3.0f), bounds.getY(), barWidth, bounds.getHeight());
        g.setColour (colours::control);
        g.fillRoundedRectangle (bar, 2.0f);

        const float level = c == 0 ? left : right;
        const float y = toY (level);
        g.setGradientFill (juce::ColourGradient (colours::danger, 0.0f, bounds.getY(), colours::accent, 0.0f, bounds.getY() + bounds.getHeight() * 0.35f, false));
        g.fillRoundedRectangle (bar.withTop (y), 2.0f);

        const float hold = c == 0 ? holdL : holdR;
        g.setColour (hold > 0.98f ? colours::danger : colours::text);
        g.fillRect (bar.getX(), toY (hold), barWidth, 1.5f);
    }
}

} // namespace nedd::ui
