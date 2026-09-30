#include "MpeVisualizer.h"

namespace nedd::ui
{
// =============================================================================================
ExpressionHistory::ExpressionHistory (EditorContext& c) : ctx (c) { ctx.addListener (this); }
ExpressionHistory::~ExpressionHistory() { ctx.removeListener (this); }

void ExpressionHistory::editorTick()
{
    head = (head + 1) % kLength;
    ++writes;

    for (int v = 0; v < kPhysicalVoices; ++v)
    {
        const auto& t = ctx.telemetry.voices[(size_t) v];
        auto& s = samples[(size_t) v][(size_t) head];
        s.active = t.active.load (std::memory_order_relaxed);
        if (! s.active)
            continue;
        s.gate = t.gate.load (std::memory_order_relaxed);
        s.noteId = t.noteId.load (std::memory_order_relaxed);
        s.note = t.noteNumber.load (std::memory_order_relaxed);
        s.bend = t.pitch.load (std::memory_order_relaxed);
        s.pressure = t.pressure.load (std::memory_order_relaxed);
        s.slide = t.slide.load (std::memory_order_relaxed);
        s.velocity = t.velocity.load (std::memory_order_relaxed);
        s.level = t.ampEnv.load (std::memory_order_relaxed);
    }
}

// =============================================================================================
ExpressionField::ExpressionField (EditorContext& c, ExpressionHistory& h) : ctx (c), history (h)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

ExpressionField::~ExpressionField() { ctx.removeListener (this); }

void ExpressionField::editorTick()
{
    // Follow the played register smoothly so gestures stay large on screen.
    float lo = 1000.0f, hi = -1000.0f;
    for (int v = 0; v < kPhysicalVoices; ++v)
    {
        const auto& s = history.get (v, 0);
        if (! s.active) continue;
        lo = std::min (lo, (float) s.note + s.bend);
        hi = std::max (hi, (float) s.note + s.bend);
    }

    float targetLow = 36.0f, targetHigh = 84.0f;
    if (hi >= lo)
    {
        targetLow = std::min (lo - 7.0f, (lo + hi) * 0.5f - 12.0f);
        targetHigh = std::max (hi + 7.0f, (lo + hi) * 0.5f + 12.0f);
    }
    lowNote += (targetLow - lowNote) * 0.08f;
    highNote += (targetHigh - highNote) * 0.08f;
    repaint();
}

void ExpressionField::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto area = bounds.reduced (12.0f, 10.0f).withTrimmedBottom (14.0f).withTrimmedLeft (10.0f);
    auto xFor = [&] (float pitch) { return area.getX() + area.getWidth() * (pitch - lowNote) / (highNote - lowNote); };
    auto yFor = [&] (float slide) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, slide); };

    // Key columns
    for (int n = (int) std::floor (lowNote); n <= (int) std::ceil (highNote); ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float x0 = xFor ((float) n - 0.5f), x1 = xFor ((float) n + 0.5f);
        if (x1 < area.getX() || x0 > area.getRight()) continue;
        g.setColour (black ? colours::panel.darker (0.3f) : colours::panel.brighter (0.03f));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()).getIntersection (area));
        if (pc == 0)
        {
            g.setColour (colours::outlineStrong);
            g.drawVerticalLine ((int) x0, area.getY(), area.getBottom());
            g.setColour (colours::textFaint);
            g.setFont (font (10.0f));
            g.drawText (noteName (n), juce::Rectangle<float> (x0 + 2.0f, area.getBottom() + 1.0f, 30.0f, 12.0f), juce::Justification::left);
        }
    }

    g.setColour (colours::outline.withAlpha (0.7f));
    for (float s : { 0.25f, 0.5f, 0.75f })
        g.drawHorizontalLine ((int) yFor (s), area.getX(), area.getRight());

    g.setFont (displayFont (10.5f));
    g.setColour (colours::slide.withAlpha (0.7f));
    g.drawText ("SLIDE", juce::Rectangle<float> (bounds.getX() + 2.0f, area.getY(), 12.0f, area.getHeight()), juce::Justification::centred);

    // Voices
    constexpr int trailLength = 45;
    bool any = false;
    for (int v = 0; v < kPhysicalVoices; ++v)
    {
        const auto& now = history.get (v, 0);
        if (! now.active)
            continue;
        any = true;

        const auto colour = noteColour (now.noteId, now.note);

        juce::Path trail;
        bool started = false;
        for (int age = trailLength; age >= 0; --age)
        {
            const auto& s = history.get (v, age);
            if (! s.active || s.noteId != now.noteId) { started = false; continue; }
            const juce::Point<float> p (xFor ((float) s.note + s.bend), yFor (s.slide));
            if (! started) { trail.startNewSubPath (p); started = true; } else trail.lineTo (p);
        }
        g.setColour (colour.withAlpha (0.45f));
        g.strokePath (trail, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const juce::Point<float> centre (xFor ((float) now.note + now.bend), yFor (now.slide));
        const float radius = 6.0f + now.pressure * 26.0f;
        const auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        g.setColour (colour.withAlpha (0.12f + 0.3f * now.velocity));
        g.fillEllipse (circle);
        g.setColour (colour.withAlpha (now.gate ? 0.95f : 0.4f));
        g.drawEllipse (circle, now.gate ? 2.0f : 1.0f);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre));

        g.setColour (colours::text.withAlpha (now.gate ? 0.95f : 0.5f));
        g.setFont (font (11.0f, true));
        const juce::String bendText = std::abs (now.bend) >= 0.05f ? (now.bend > 0 ? " +" : " ") + juce::String (now.bend, 2) : juce::String();
        g.drawText (noteName (now.note) + bendText, juce::Rectangle<float> (centre.x - 60.0f, centre.y - radius - 16.0f, 120.0f, 14.0f),
                    juce::Justification::centred);
    }

    if (! any)
    {
        g.setColour (colours::textFaint);
        g.setFont (font (13.0f));
        g.drawText ("Play an MPE controller or the keyboard below. X = pitch  |  Y = slide  |  size = pressure  |  glow = velocity",
                    area, juce::Justification::centred);
    }
}

// =============================================================================================
VoiceLanes::VoiceLanes (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

VoiceLanes::~VoiceLanes() { ctx.removeListener (this); }
void VoiceLanes::editorTick() { repaint(); }

void VoiceLanes::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 4.0f);
    bounds.reduce (8.0f, 6.0f);

    struct Column { const char* name; juce::Colour colour; };
    const Column columns[] = { { "PITCH", colours::pitch }, { "PRESS", colours::pressure }, { "SLIDE", colours::slide },
                               { "VEL", colours::velocity }, { "R.VEL", colours::releaseVel }, { "AMP", colours::text }, { "MOD", colours::modulation } };
    constexpr int numColumns = 7;

    auto header = bounds.removeFromTop (16.0f);
    const float labelWidth = 74.0f;
    const float columnWidth = (bounds.getWidth() - labelWidth) / (float) numColumns;

    g.setFont (displayFont (10.5f));
    g.setColour (colours::textFaint);
    g.drawText ("NOTE  VOICE", header.withWidth (labelWidth), juce::Justification::centredLeft);
    for (int c = 0; c < numColumns; ++c)
    {
        g.setColour (columns[c].colour.withAlpha (0.8f));
        g.drawText (columns[c].name, juce::Rectangle<float> (header.getX() + labelWidth + columnWidth * (float) c, header.getY(), columnWidth, 16.0f),
                    juce::Justification::centred);
    }

    struct Row { int voice; int64_t start; };
    std::vector<Row> rows;
    for (int v = 0; v < kPhysicalVoices; ++v)
        if (ctx.telemetry.voices[(size_t) v].active.load (std::memory_order_relaxed))
            rows.push_back ({ v, ctx.telemetry.voices[(size_t) v].startSample.load (std::memory_order_relaxed) });
    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.start < b.start; });

    const float rowHeight = 17.0f;
    const float bendRange = std::max (1.0f, ctx.param (pid::global (GlobalField::MpeBendRange)));

    for (const auto& row : rows)
    {
        if (bounds.getHeight() < rowHeight)
            break;
        auto r = bounds.removeFromTop (rowHeight);
        const auto& t = ctx.telemetry.voices[(size_t) row.voice];
        const bool gate = t.gate.load (std::memory_order_relaxed);
        const int note = t.noteNumber.load (std::memory_order_relaxed);
        const auto colour = noteColour (t.noteId.load (std::memory_order_relaxed), note);

        g.setColour (colour.withAlpha (gate ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r.getX(), r.getY() + 4.0f, 4.0f, rowHeight - 8.0f, 1.0f);
        g.setColour (gate ? colours::text : colours::textFaint);
        g.setFont (font (11.5f, true));
        g.drawText (noteName (note), juce::Rectangle<float> (r.getX() + 8.0f, r.getY(), 36.0f, rowHeight), juce::Justification::centredLeft);
        g.setFont (monoFont (10.5f));
        g.setColour (colours::textFaint);
        g.drawText ("#" + juce::String (row.voice + 1), juce::Rectangle<float> (r.getX() + 44.0f, r.getY(), 30.0f, rowHeight), juce::Justification::centredLeft);

        const float values[numColumns] = {
            t.pitch.load (std::memory_order_relaxed) / bendRange,
            t.pressure.load (std::memory_order_relaxed),
            t.slide.load (std::memory_order_relaxed),
            t.velocity.load (std::memory_order_relaxed),
            t.releaseVelocity.load (std::memory_order_relaxed),
            t.ampEnv.load (std::memory_order_relaxed),
            std::min (1.0f, t.modActivity.load (std::memory_order_relaxed)),
        };

        for (int c = 0; c < numColumns; ++c)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + labelWidth + columnWidth * (float) c, r.getY(), columnWidth, rowHeight).reduced (4.0f, 5.0f);
            g.setColour (colours::control);
            g.fillRoundedRectangle (cell, 2.0f);
            g.setColour (columns[c].colour.withAlpha (gate ? 0.9f : 0.45f));

            if (c == 0)   // bipolar pitch bar from the centre
            {
                const float v = juce::jlimit (-1.0f, 1.0f, values[c]);
                const float mid = cell.getCentreX();
                const float w = std::abs (v) * cell.getWidth() * 0.5f;
                g.fillRoundedRectangle (v >= 0.0f ? cell.withX (mid).withWidth (std::max (1.0f, w)) : cell.withX (mid - w).withWidth (w), 2.0f);
            }
            else
            {
                g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * juce::jlimit (0.0f, 1.0f, values[c])), 2.0f);
            }
        }
    }

    if (rows.empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (font (12.0f));
        g.drawText ("No active voices", bounds, juce::Justification::centred);
    }
}

// =============================================================================================
ExpressionTimeline::ExpressionTimeline (EditorContext& c, ExpressionHistory& h) : ctx (c), history (h)
{
    ctx.addListener (this);
    setInterceptsMouseClicks (false, false);
}

ExpressionTimeline::~ExpressionTimeline() { ctx.removeListener (this); }
void ExpressionTimeline::editorTick() { repaint(); }

void ExpressionTimeline::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 4.0f);
    bounds.reduce (8.0f, 6.0f);

    struct Lane { const char* name; juce::Colour colour; };
    const Lane lanes[] = { { "PITCH", colours::pitch }, { "PRESSURE", colours::pressure }, { "SLIDE", colours::slide } };
    const float laneHeight = bounds.getHeight() / 3.0f;
    const float bendRange = std::max (1.0f, ctx.param (pid::global (GlobalField::MpeBendRange)));
    const float labelWidth = 64.0f;

    for (int l = 0; l < 3; ++l)
    {
        auto lane = juce::Rectangle<float> (bounds.getX(), bounds.getY() + laneHeight * (float) l, bounds.getWidth(), laneHeight).reduced (0.0f, 2.0f);
        g.setColour (lanes[l].colour.withAlpha (0.75f));
        g.setFont (displayFont (10.5f));
        g.drawText (lanes[l].name, lane.removeFromLeft (labelWidth), juce::Justification::centredLeft);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (lane, 3.0f);
        if (l == 0)
        {
            g.setColour (colours::outline);
            g.drawHorizontalLine ((int) lane.getCentreY(), lane.getX(), lane.getRight());
        }

        for (int v = 0; v < kPhysicalVoices; ++v)
        {
            juce::Path path;
            bool started = false;
            uint32_t id = 0;
            int note = 60;

            for (int age = ExpressionHistory::kLength - 1; age >= 0; --age)
            {
                const auto& s = history.get (v, age);
                if (! s.active || (started && s.noteId != id))
                {
                    if (started)
                    {
                        g.setColour (noteColour (id, note).withAlpha (0.85f));
                        g.strokePath (path, juce::PathStrokeType (1.5f));
                        path.clear();
                    }
                    started = false;
                    if (! s.active)
                        continue;
                }

                const float value = l == 0 ? 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, s.bend / bendRange)
                                           : (l == 1 ? s.pressure : s.slide);
                const float x = lane.getRight() - lane.getWidth() * (float) age / (float) (ExpressionHistory::kLength - 1);
                const float y = lane.getBottom() - 2.0f - (lane.getHeight() - 4.0f) * value;
                if (! started) { path.startNewSubPath (x, y); started = true; id = s.noteId; note = s.note; }
                else path.lineTo (x, y);
            }

            if (started)
            {
                g.setColour (noteColour (id, note).withAlpha (0.85f));
                g.strokePath (path, juce::PathStrokeType (1.5f));
            }
        }
    }
}

} // namespace nedd::ui
