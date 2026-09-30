#include "Pages.h"
#include "Sequencer/ClipTools.h"
#include <set>

namespace nedd::ui
{
namespace
{
    enum class Tool { Draw = 0, Select, Erase };
    enum class Dimension { Pitch = 0, Pressure, Slide, Velocity };

    /** Shared horizontal mapping of the roll and the expression lane. */
    struct TimeAxis
    {
        int left = 0;
        double pixelsPerBeat = 40.0;
        double scrollBeat = 0.0;

        float beatToX (double beat) const { return (float) (left + (beat - scrollBeat) * pixelsPerBeat); }
        double xToBeat (float x) const { return scrollBeat + (x - (float) left) / pixelsPerBeat; }
    };

    ExprCurve& curveOf (ClipNote& n, Dimension d)
    {
        return d == Dimension::Pitch ? n.pitch : (d == Dimension::Pressure ? n.pressure : n.slide);
    }

    const ExprCurve& curveOf (const ClipNote& n, Dimension d)
    {
        return d == Dimension::Pitch ? n.pitch : (d == Dimension::Pressure ? n.pressure : n.slide);
    }

    juce::Colour dimensionColour (Dimension d)
    {
        switch (d)
        {
            case Dimension::Pitch:    return colours::pitch;
            case Dimension::Pressure: return colours::pressure;
            case Dimension::Slide:    return colours::slide;
            case Dimension::Velocity: return colours::velocity;
        }
        return colours::text;
    }
} // namespace

// =============================================================================================
/** Editing session: a working copy while dragging, committed as one undo step on release. */
class ClipSession
{
public:
    explicit ClipSession (EditorContext& c) : ctx (c) {}

    const NoteClip& clip() const { return editing ? working : ctx.processor.getClip(); }

    void begin()
    {
        before = ctx.processor.getClip();
        working = before;
        editing = true;
    }

    NoteClip& edit() { return working; }
    void preview() { ctx.processor.setClip (working); }

    void commit (const juce::String& name)
    {
        if (! editing)
            return;
        ctx.processor.setClip (before);
        ctx.processor.setClip (working, name);
        editing = false;
    }

    /** One-shot undoable edit. */
    template <typename Fn>
    void apply (const juce::String& name, Fn&& fn)
    {
        auto c = ctx.processor.getClip();
        fn (c);
        ctx.processor.setClip (c, name);
    }

    bool isEditing() const { return editing; }

    std::set<int> selection;
    double grid = 0.25;
    Tool tool = Tool::Draw;
    Dimension dimension = Dimension::Pressure;
    TimeAxis axis;

    std::vector<int> selectedList() const { return { selection.begin(), selection.end() }; }

    EditorContext& ctx;

private:
    NoteClip working, before;
    bool editing = false;
};

// =============================================================================================
class PianoRoll : public juce::Component, private EditorContext::Listener
{
public:
    explicit PianoRoll (ClipSession& s) : session (s)
    {
        session.ctx.addListener (this);
        setWantsKeyboardFocus (false);
    }

    ~PianoRoll() override { session.ctx.removeListener (this); }

    std::function<void()> onChanged;

    static constexpr int kKeysWidth = 44;
    static constexpr int kRulerHeight = 18;
    static constexpr int kRowHeight = 12;

    void centreOnNotes()
    {
        const auto& clip = session.clip();
        if (clip.notes.empty())
            return;
        int lo = 127, hi = 0;
        for (const auto& n : clip.notes) { lo = std::min (lo, n.noteNumber); hi = std::max (hi, n.noteNumber); }
        const int visible = visibleRows();
        lowNote = juce::jlimit (0, 127 - visible, (lo + hi) / 2 - visible / 2);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& clip = session.clip();
        const auto& axis = session.axis;
        const auto grid = gridArea();

        g.fillAll (colours::background);

        // Rows
        for (int row = 0; row <= visibleRows(); ++row)
        {
            const int note = lowNote + row;
            if (note > 127) break;
            const int pc = note % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const float y = noteToY (note);
            g.setColour (black ? colours::panel.darker (0.35f) : colours::panel);
            g.fillRect (juce::Rectangle<float> ((float) grid.getX(), y, (float) grid.getWidth(), (float) kRowHeight));

            // Keys
            g.setColour (black ? juce::Colour (0xff1b1e25) : juce::Colour (0xffcfd3db));
            g.fillRect (juce::Rectangle<float> (0.0f, y, (float) kKeysWidth - 2.0f, (float) kRowHeight - 1.0f));
            if (pc == 0)
            {
                g.setColour (juce::Colour (0xff555b68));
                g.setFont (font (9.5f));
                g.drawText (noteName (note), juce::Rectangle<float> (2.0f, y, (float) kKeysWidth - 6.0f, (float) kRowHeight), juce::Justification::centredRight);
                g.setColour (colours::outlineStrong);
                g.drawHorizontalLine ((int) (y + (float) kRowHeight), (float) grid.getX(), (float) grid.getRight());
            }
        }

        // Beat grid and ruler
        g.setColour (colours::panel.brighter (0.05f));
        g.fillRect (juce::Rectangle<int> (grid.getX(), 0, grid.getWidth(), kRulerHeight));
        const double firstBeat = std::floor (axis.scrollBeat / session.grid) * session.grid;
        for (double b = firstBeat; axis.beatToX (b) < (float) grid.getRight(); b += session.grid)
        {
            const float x = axis.beatToX (b);
            if (x < (float) grid.getX()) continue;
            const bool bar = std::abs (std::fmod (b, 4.0)) < 1.0e-6;
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (bar ? colours::outlineStrong : (beat ? colours::outline : colours::outline.withAlpha (0.4f)));
            g.drawVerticalLine ((int) x, (float) kRulerHeight, (float) grid.getBottom());
            if (bar)
            {
                g.setColour (colours::textDim);
                g.setFont (monoFont (10.0f));
                g.drawText (juce::String ((int) (b / 4.0) + 1), juce::Rectangle<float> (x + 3.0f, 2.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

        // Clip end
        const float endX = axis.beatToX (clip.lengthBeats);
        if (endX < (float) grid.getRight())
        {
            g.setColour (colours::background.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));
            g.setColour (colours::amber);
            g.drawVerticalLine ((int) endX, 0.0f, (float) grid.getBottom());
        }

        // Notes, with their expression drawn inside
        g.saveState();
        g.reduceClipRegion (grid);
        for (const auto& n : clip.notes)
        {
            const auto r = noteBounds (n);
            if (r.getRight() < (float) grid.getX() || r.getX() > (float) grid.getRight() || r.getBottom() < (float) grid.getY() || r.getY() > (float) grid.getBottom())
                continue;

            const bool selected = session.selection.count (n.uid) > 0;
            const auto base = colours::accent.interpolatedWith (colours::velocity, 1.0f - n.velocity);
            g.setColour (base.withAlpha (selected ? 0.55f : 0.32f));
            g.fillRoundedRectangle (r, 2.5f);

            // Pressure: strip along the bottom of the note, height = pressure
            juce::Path pressure;
            const int steps = std::max (2, (int) (r.getWidth() / 3.0f));
            pressure.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                const float v = evaluateCurve (n.pressure, t * (float) n.length, 0.0f);
                pressure.lineTo (r.getX() + t * r.getWidth(), r.getBottom() - v * r.getHeight());
            }
            pressure.lineTo (r.getRight(), r.getBottom());
            pressure.closeSubPath();
            g.setColour (colours::pressure.withAlpha (0.55f));
            g.fillPath (pressure);

            // Pitch: the actual sounding pitch, 1 semitone = 1 row
            if (n.pitch.size() > 1 || (n.pitch.size() == 1 && n.pitch[0].value != 0.0f))
            {
                juce::Path pitch;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t = (float) i / (float) steps;
                    const float bend = evaluateCurve (n.pitch, t * (float) n.length, 0.0f);
                    const juce::Point<float> p (r.getX() + t * r.getWidth(), r.getCentreY() - bend * (float) kRowHeight);
                    if (i == 0) pitch.startNewSubPath (p); else pitch.lineTo (p);
                }
                g.setColour (colours::pitch);
                g.strokePath (pitch, juce::PathStrokeType (1.6f));
            }

            g.setColour (selected ? colours::text : base.withAlpha (0.9f));
            g.drawRoundedRectangle (r, 2.5f, selected ? 1.6f : 1.0f);
        }

        if (banding)
        {
            g.setColour (colours::accent.withAlpha (0.12f));
            g.fillRect (band);
            g.setColour (colours::accent.withAlpha (0.7f));
            g.drawRect (band, 1);
        }

        // Playhead
        if (session.ctx.telemetry.clipPlaying.load())
        {
            const float x = axis.beatToX (session.ctx.telemetry.clipPosition.load());
            g.setColour (session.ctx.telemetry.clipRecording.load() ? colours::danger : colours::text);
            g.drawLine (x, 0.0f, x, (float) grid.getBottom(), 1.5f);
        }
        g.restoreState();

        if (clip.notes.empty())
        {
            g.setColour (colours::textFaint);
            g.setFont (font (13.0f));
            g.drawText ("Record a performance (REC) or draw notes. Pressure is drawn inside each note; the blue line is its pitch.",
                        grid, juce::Justification::centred);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            const double beat = session.axis.xToBeat (e.position.x);
            session.axis.pixelsPerBeat = juce::jlimit (6.0, 400.0, session.axis.pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            session.axis.scrollBeat = std::max (0.0, beat - (e.position.x - session.axis.left) / session.axis.pixelsPerBeat);
        }
        else if (e.mods.isShiftDown())
            session.axis.scrollBeat = std::max (0.0, session.axis.scrollBeat - w.deltaY * 8.0 * 40.0 / session.axis.pixelsPerBeat);
        else
            lowNote = juce::jlimit (0, 127 - visibleRows(), lowNote + (w.deltaY > 0 ? 3 : -3));
        if (onChanged) onChanged();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! gridArea().contains (e.getPosition()))
            return;

        const auto hit = hitTest (e.position);
        downBeat = session.axis.xToBeat (e.position.x);
        downNote = yToNote (e.position.y);
        modified = false;

        if (session.tool == Tool::Erase)
        {
            session.begin();
            eraseAt (e.position);
            mode = Mode::Erase;
            return;
        }

        if (hit.uid != 0)
        {
            if (e.mods.isShiftDown())
            {
                if (session.selection.count (hit.uid)) session.selection.erase (hit.uid); else session.selection.insert (hit.uid);
            }
            else if (! session.selection.count (hit.uid))
            {
                session.selection = { hit.uid };
            }
            session.begin();
            original = session.clip();
            mode = hit.edge ? Mode::Resize : Mode::Move;
        }
        else if (session.tool == Tool::Draw)
        {
            session.begin();
            ClipNote n;
            n.noteNumber = juce::jlimit (0, 127, downNote);
            n.start = std::max (0.0, snap (downBeat, e.mods, true));
            n.length = session.grid;
            n.velocity = 0.8f;
            n.pressure = { { 0.0f, 0.4f } };
            const int uid = session.edit().addNote (n);
            session.edit().sortByStart();
            session.selection = { uid };
            original = session.edit();
            session.preview();
            mode = Mode::Resize;
            modified = true;
        }
        else
        {
            if (! e.mods.isShiftDown())
                session.selection.clear();
            mode = Mode::Band;
            banding = true;
            band = { e.getPosition(), e.getPosition() };
        }

        if (onChanged) onChanged();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double beat = session.axis.xToBeat (e.position.x);
        switch (mode)
        {
            case Mode::Move:
            {
                const double delta = snap (beat, e.mods, false) - snap (downBeat, e.mods, false);
                const int noteDelta = yToNote (e.position.y) - downNote;
                auto& clip = session.edit();
                for (auto& n : clip.notes)
                {
                    if (! session.selection.count (n.uid)) continue;
                    if (const auto* o = original.findNote (n.uid))
                    {
                        n.start = std::max (0.0, o->start + delta);
                        n.noteNumber = juce::jlimit (0, 127, o->noteNumber + noteDelta);
                    }
                }
                clip.sortByStart();
                modified = true;
                session.preview();
                break;
            }
            case Mode::Resize:
            {
                auto& clip = session.edit();
                for (auto& n : clip.notes)
                {
                    if (! session.selection.count (n.uid)) continue;
                    if (const auto* o = original.findNote (n.uid))
                    {
                        const double end = std::max (o->start + session.grid * 0.25, snap (beat, e.mods, false));
                        n.length = std::max (1.0 / 64.0, end - o->start);
                    }
                }
                modified = true;
                session.preview();
                break;
            }
            case Mode::Band:
            {
                band = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
                session.selection.clear();
                for (const auto& n : session.clip().notes)
                    if (noteBounds (n).intersects (band.toFloat()))
                        session.selection.insert (n.uid);
                break;
            }
            case Mode::Erase:
                eraseAt (e.position);
                break;
            case Mode::None:
                break;
        }
        if (onChanged) onChanged();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode == Mode::Move && modified)        session.commit ("Move notes");
        else if (mode == Mode::Resize && modified) session.commit (session.tool == Tool::Draw ? "Draw note" : "Resize notes");
        else if (mode == Mode::Erase && modified)  session.commit ("Erase notes");
        else if (session.isEditing())              session.commit ("Select");

        mode = Mode::None;
        banding = false;
        if (onChanged) onChanged();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = hitTest (e.position);
        if (hit.uid != 0 && session.tool == Tool::Select)
        {
            const int uid = hit.uid;
            session.apply ("Delete note", [uid] (NoteClip& c) { c.removeNote (uid); });
            session.selection.erase (uid);
            if (onChanged) onChanged();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = hitTest (e.position);
        setMouseCursor (hit.uid != 0 ? (hit.edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                                     : (session.tool == Tool::Erase ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
    }

    juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedLeft (kKeysWidth); }

    void resized() override
    {
        session.axis.left = kKeysWidth;
        if (! initialised)
        {
            fitToClip();
            initialised = true;
        }
    }

    void fitToClip()
    {
        const double beats = std::max (4.0, session.clip().lengthBeats);
        session.axis.left = kKeysWidth;
        session.axis.pixelsPerBeat = (double) std::max (100, gridArea().getWidth() - 8) / beats;
        session.axis.scrollBeat = 0.0;
        centreOnNotes();
        if (onChanged) onChanged();
        repaint();
    }

private:
    enum class Mode { None, Move, Resize, Band, Erase };
    struct Hit { int uid = 0; bool edge = false; };

    int visibleRows() const { return std::max (1, (getHeight() - kRulerHeight) / kRowHeight); }
    float noteToY (int note) const { return (float) getHeight() - (float) ((note - lowNote + 1) * kRowHeight); }
    int yToNote (float y) const { return lowNote + (int) std::floor (((float) getHeight() - y) / (float) kRowHeight); }

    juce::Rectangle<float> noteBounds (const ClipNote& n) const
    {
        const float x0 = session.axis.beatToX (n.start);
        const float x1 = session.axis.beatToX (n.end());
        return { x0, noteToY (n.noteNumber) + 1.0f, std::max (3.0f, x1 - x0 - 1.0f), (float) kRowHeight - 2.0f };
    }

    Hit hitTest (juce::Point<float> p) const
    {
        const auto& clip = session.clip();
        for (auto it = clip.notes.rbegin(); it != clip.notes.rend(); ++it)
        {
            const auto r = noteBounds (*it);
            if (r.expanded (0.0f, 1.0f).contains (p))
                return { it->uid, p.x > r.getRight() - 6.0f && r.getWidth() > 10.0f };
        }
        return {};
    }

    double snap (double beat, const juce::ModifierKeys& mods, bool floorToGrid) const
    {
        if (mods.isAltDown())
            return beat;
        return floorToGrid ? std::floor (beat / session.grid) * session.grid : std::round (beat / session.grid) * session.grid;
    }

    void eraseAt (juce::Point<float> p)
    {
        const auto hit = hitTest (p);
        if (hit.uid == 0)
            return;
        session.edit().removeNote (hit.uid);
        session.selection.erase (hit.uid);
        modified = true;
        session.preview();
    }

    void editorTick() override
    {
        const bool playing = session.ctx.telemetry.clipPlaying.load();
        const int version = session.ctx.processor.getClipVersion();
        if (playing || version != lastVersion || wasPlaying != playing)
        {
            if (version != lastVersion && lastVersion < 0)
                centreOnNotes();
            lastVersion = version;
            wasPlaying = playing;
            repaint();
        }
    }

    ClipSession& session;
    NoteClip original;
    Mode mode = Mode::None;
    double downBeat = 0.0;
    int downNote = 60;
    int lowNote = 48;
    bool modified = false, banding = false, initialised = false, wasPlaying = false;
    juce::Rectangle<int> band;
    int lastVersion = -1;
};

// =============================================================================================
/** Draws per-note expression curves (or velocities) for the selected dimension. */
class ExpressionLane : public juce::Component, private EditorContext::Listener
{
public:
    explicit ExpressionLane (ClipSession& s) : session (s) { session.ctx.addListener (this); }
    ~ExpressionLane() override { session.ctx.removeListener (this); }

    std::function<void()> onChanged;

    void paint (juce::Graphics& g) override
    {
        const auto area = laneArea();
        g.fillAll (colours::background);
        g.setColour (colours::panel);
        g.fillRect (area);

        const auto dim = session.dimension;
        const auto colour = dimensionColour (dim);
        g.setColour (colours::outline);
        for (float v : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine ((int) valueToY (dim == Dimension::Pitch ? (v - 0.5f) * 2.0f * pitchRange : v), (float) area.getX(), (float) area.getRight());

        g.setColour (colour.withAlpha (0.8f));
        g.setFont (displayFont (11.0f));
        g.drawText (dim == Dimension::Pitch ? "+" + juce::String ((int) pitchRange) + " st" : "100%", juce::Rectangle<int> (2, area.getY(), PianoRoll::kKeysWidth - 4, 14), juce::Justification::right);
        g.drawText (dim == Dimension::Pitch ? "-" + juce::String ((int) pitchRange) + " st" : "0", juce::Rectangle<int> (2, area.getBottom() - 14, PianoRoll::kKeysWidth - 4, 14), juce::Justification::right);

        g.saveState();
        g.reduceClipRegion (area);
        const auto& clip = session.clip();
        const bool anySelected = ! session.selection.empty();

        for (const auto& n : clip.notes)
        {
            const bool selected = session.selection.count (n.uid) > 0;
            const float alpha = ! anySelected || selected ? 0.95f : 0.25f;
            const float x0 = session.axis.beatToX (n.start);
            const float x1 = session.axis.beatToX (n.end());
            if (x1 < (float) area.getX() || x0 > (float) area.getRight())
                continue;

            if (dim == Dimension::Velocity)
            {
                const float y = valueToY (n.velocity);
                g.setColour (colour.withAlpha (alpha));
                g.drawLine (x0, (float) area.getBottom(), x0, y, 2.0f);
                g.fillEllipse (x0 - 3.5f, y - 3.5f, 7.0f, 7.0f);
                continue;
            }

            const auto& curve = curveOf (n, dim);
            juce::Path path;
            const int steps = std::max (2, (int) ((x1 - x0) / 2.0f));
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                const float v = evaluateCurve (curve, t * (float) n.length, 0.0f);
                const juce::Point<float> p (x0 + t * (x1 - x0), valueToY (v));
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            g.setColour (colour.withAlpha (alpha));
            g.strokePath (path, juce::PathStrokeType (selected ? 2.0f : 1.4f));
            g.setColour (colour.withAlpha (alpha * 0.25f));
            g.drawVerticalLine ((int) x0, (float) area.getY(), (float) area.getBottom());

            if (selected)
                for (const auto& pt : curve)
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ session.axis.beatToX (n.start + pt.time), valueToY (pt.value) }));
        }
        g.restoreState();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        session.begin();
        lastPoint = e.position;
        paintAt (e.position, e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        paintAt (lastPoint, e.position);
        lastPoint = e.position;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        session.commit (session.dimension == Dimension::Velocity ? "Edit velocity" : "Draw expression");
        if (onChanged) onChanged();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Double-click resets the curve of the note under the cursor to a flat line at that value.
        if (session.dimension == Dimension::Velocity)
            return;
        const double beat = session.axis.xToBeat (e.position.x);
        const float value = yToValue (e.position.y);
        const auto dim = session.dimension;
        const auto targets = targetsAt (beat);
        session.apply ("Reset expression", [&] (NoteClip& c)
        {
            for (int uid : targets)
                if (auto* n = c.findNote (uid))
                    curveOf (*n, dim) = { { 0.0f, value } };
        });
    }

private:
    juce::Rectangle<int> laneArea() const { return getLocalBounds().withTrimmedLeft (PianoRoll::kKeysWidth).reduced (0, 4); }

    float valueToY (float v) const
    {
        const auto a = laneArea();
        const float n = session.dimension == Dimension::Pitch ? 0.5f + v / (2.0f * pitchRange) : v;
        return (float) a.getBottom() - juce::jlimit (0.0f, 1.0f, n) * (float) a.getHeight();
    }

    float yToValue (float y) const
    {
        const auto a = laneArea();
        const float n = juce::jlimit (0.0f, 1.0f, ((float) a.getBottom() - y) / (float) a.getHeight());
        return session.dimension == Dimension::Pitch ? (n - 0.5f) * 2.0f * pitchRange : n;
    }

    std::vector<int> targetsAt (double beat) const
    {
        std::vector<int> result;
        const auto& clip = session.clip();
        for (const auto& n : clip.notes)
            if (beat >= n.start && beat <= n.end() && (session.selection.empty() || session.selection.count (n.uid)))
                result.push_back (n.uid);
        if (result.empty() && ! session.selection.empty())
            for (const auto& n : clip.notes)
                if (beat >= n.start && beat <= n.end())
                    result.push_back (n.uid);
        return result;
    }

    void paintAt (juce::Point<float> from, juce::Point<float> to)
    {
        auto& clip = session.edit();
        const auto dim = session.dimension;

        if (dim == Dimension::Velocity)
        {
            for (auto& n : clip.notes)
            {
                const float x = session.axis.beatToX (n.start);
                const bool inRange = x >= std::min (from.x, to.x) - 6.0f && x <= std::max (from.x, to.x) + 6.0f;
                if (inRange && (session.selection.empty() || session.selection.count (n.uid)))
                    n.velocity = juce::jlimit (0.02f, 1.0f, yToValue (to.y));
            }
            session.preview();
            repaint();
            return;
        }

        // Draw a continuous stroke between the previous and current mouse positions.
        const float x0 = std::min (from.x, to.x), x1 = std::max (from.x, to.x);
        const double beatA = session.axis.xToBeat (x0), beatB = session.axis.xToBeat (x1);
        const double pointSpacing = 2.0 / session.axis.pixelsPerBeat;

        for (auto& n : clip.notes)
        {
            if (n.end() < beatA || n.start > beatB)
                continue;
            if (! session.selection.empty() && ! session.selection.count (n.uid))
                continue;

            auto& curve = curveOf (n, dim);
            const float t0 = (float) std::max (0.0, beatA - n.start);
            const float t1 = (float) std::min (n.length, beatB - n.start);
            curve.erase (std::remove_if (curve.begin(), curve.end(), [t0, t1, pointSpacing] (const ExprPoint& p)
                                         { return p.time >= t0 - (float) pointSpacing && p.time <= t1 + (float) pointSpacing; }),
                         curve.end());

            for (double t = t0; t <= t1 + 1.0e-9; t += pointSpacing)
            {
                const float x = session.axis.beatToX (n.start + t);
                const float frac = x1 > x0 ? (x - x0) / (x1 - x0) : 1.0f;
                const float y = from.x <= to.x ? juce::jmap (frac, from.y, to.y) : juce::jmap (frac, to.y, from.y);
                curve.push_back ({ (float) t, yToValue (y) });
            }
            std::sort (curve.begin(), curve.end(), [] (const ExprPoint& a, const ExprPoint& b) { return a.time < b.time; });
        }
        session.preview();
        repaint();
    }

    void editorTick() override
    {
        const int version = session.ctx.processor.getClipVersion();
        if (version != lastVersion) { lastVersion = version; repaint(); }
    }

    ClipSession& session;
    juce::Point<float> lastPoint;
    float pitchRange = 12.0f;
    int lastVersion = -1;
};

// =============================================================================================
class NoteEditorPage : public Page, private EditorContext::Listener
{
public:
    explicit NoteEditorPage (EditorContext& c) : Page (c), session (c), roll (session), lane (session)
    {
        setWantsKeyboardFocus (true);
        roll.onChanged = [this] { lane.repaint(); updateStatus(); };
        lane.onChanged = [this] { roll.repaint(); };

        for (auto* comp : std::initializer_list<juce::Component*> { &roll, &lane, &record, &overdub, &play, &stop, &loop, &sync, &drawTool, &selectTool,
                                                                    &eraseTool, &grid, &quantize, &humanize, &duplicate, &erase, &copy, &paste, &clear,
                                                                    &length, &fit })
            addAndMakeVisible (comp);

        record.setColour (juce::TextButton::textColourOffId, colours::danger);
        record.onClick = [this] { ctx.processor.clipRecord (overdub.getToggleState()); };
        overdub.setClickingTogglesState (true);
        overdub.setToggleState (true, juce::dontSendNotification);
        overdub.setTooltip ("On: new recording is added to the existing notes (looping). Off: replaces the clip.");
        play.onClick = [this] { ctx.processor.clipPlay(); };
        stop.onClick = [this] { ctx.processor.clipStop(); };
        loop.setClickingTogglesState (true);
        loop.setToggleState (ctx.processor.getClipLoop(), juce::dontSendNotification);
        loop.onClick = [this] { ctx.processor.setClipLoop (loop.getToggleState()); };
        sync.setClickingTogglesState (true);
        sync.setToggleState (ctx.processor.getClipSyncToHost(), juce::dontSendNotification);
        sync.setTooltip ("Follow the host transport instead of the PLAY/STOP buttons");
        sync.onClick = [this] { ctx.processor.setClipSyncToHost (sync.getToggleState()); };

        for (auto* b : { &drawTool, &selectTool, &eraseTool })
        {
            b->setClickingTogglesState (false);
            b->setRadioGroupId (0);
        }
        drawTool.onClick = [this] { setTool (Tool::Draw); };
        selectTool.onClick = [this] { setTool (Tool::Select); };
        eraseTool.onClick = [this] { setTool (Tool::Erase); };
        setTool (Tool::Draw);

        grid.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 1);
        grid.setSelectedId (3, juce::dontSendNotification);
        grid.onChange = [this]
        {
            const double values[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
            session.grid = values[juce::jlimit (0, 5, grid.getSelectedId() - 1)];
            roll.repaint();
        };

        length.addItemList ({ "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 1);
        length.onChange = [this]
        {
            const double bars[] = { 1, 2, 4, 8, 16 };
            const double beats = bars[juce::jlimit (0, 4, length.getSelectedId() - 1)] * 4.0;
            if (std::abs (beats - ctx.processor.getClip().lengthBeats) > 1.0e-6)
                session.apply ("Clip length", [beats] (NoteClip& clip) { clip.lengthBeats = beats; });
            roll.fitToClip();
        };

        quantize.onClick = [this] { session.apply ("Quantize", [this] (NoteClip& clip) { ClipTools::quantize (clip, session.selectedList(), session.grid, 1.0f, false); }); };
        humanize.onClick = [this] { session.apply ("Humanize", [this] (NoteClip& clip) { ClipTools::humanize (clip, session.selectedList(), session.grid * 0.12, 0.1f, (uint32_t) juce::Time::currentTimeMillis()); }); };
        duplicate.onClick = [this] { duplicateSelection(); };
        erase.onClick = [this] { deleteSelection(); };
        copy.onClick = [this] { copySelection(); };
        paste.onClick = [this] { pasteClipboard(); };
        clear.onClick = [this] { session.apply ("Clear clip", [] (NoteClip& clip) { clip.notes.clear(); }); session.selection.clear(); };
        fit.onClick = [this] { roll.fitToClip(); };

        const std::pair<Dimension, const char*> dims[] = { { Dimension::Pitch, "PITCH" }, { Dimension::Pressure, "PRESSURE" },
                                                           { Dimension::Slide, "SLIDE" }, { Dimension::Velocity, "VELOCITY" } };
        for (const auto& [d, name] : dims)
        {
            auto b = std::make_unique<juce::TextButton> (name);
            const auto dim = d;
            b->onClick = [this, dim] { setDimension (dim); };
            b->setColour (juce::TextButton::textColourOnId, dimensionColour (d));
            addAndMakeVisible (*b);
            dimensionButtons.push_back (std::move (b));
        }
        setDimension (Dimension::Pressure);

        ctx.addListener (this);
        syncLengthBox();
    }

    ~NoteEditorPage() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, toolbarArea.toFloat());
        g.setColour (colours::textDim);
        g.setFont (monoFont (11.5f));
        g.drawText (status, statusArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        toolbarArea = r.removeFromTop (74);
        r.removeFromTop (6);

        auto t = toolbarArea.reduced (8, 6);
        auto row1 = t.removeFromTop (28);
        t.removeFromTop (6);
        auto row2 = t.removeFromTop (26);

        auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); };
        place (row1, record, 64);
        place (row1, overdub, 80);
        place (row1, play, 60);
        place (row1, stop, 60);
        place (row1, loop, 60);
        place (row1, sync, 96);
        row1.removeFromLeft (14);
        place (row1, drawTool, 64);
        place (row1, selectTool, 70);
        place (row1, eraseTool, 64);
        row1.removeFromLeft (14);
        place (row1, grid, 80);
        place (row1, length, 90);
        place (row1, fit, 50);
        statusArea = row1;

        place (row2, quantize, 90);
        place (row2, humanize, 90);
        place (row2, duplicate, 90);
        place (row2, erase, 70);
        place (row2, copy, 60);
        place (row2, paste, 60);
        place (row2, clear, 80);
        row2.removeFromLeft (20);
        for (auto& b : dimensionButtons)
            place (row2, *b, 90);

        lane.setBounds (r.removeFromBottom (160));
        r.removeFromBottom (4);
        roll.setBounds (r);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool cmd = key.getModifiers().isCommandDown();
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
        if (cmd && key.getKeyCode() == 'C') { copySelection(); return true; }
        if (cmd && key.getKeyCode() == 'V') { pasteClipboard(); return true; }
        if (cmd && key.getKeyCode() == 'D') { duplicateSelection(); return true; }
        if (cmd && key.getKeyCode() == 'A')
        {
            session.selection.clear();
            for (const auto& n : ctx.processor.getClip().notes) session.selection.insert (n.uid);
            refresh();
            return true;
        }
        if (cmd && key.getKeyCode() == 'Z') { ctx.processor.getUndoManager().undo(); return true; }
        if (cmd && key.getKeyCode() == 'Y') { ctx.processor.getUndoManager().redo(); return true; }
        if (key.getKeyCode() == juce::KeyPress::spaceKey)
        {
            if (ctx.telemetry.clipPlaying.load()) ctx.processor.clipStop(); else ctx.processor.clipPlay();
            return true;
        }
        return false;
    }

    void visibilityChanged() override
    {
        if (isShowing())
            grabKeyboardFocus();
    }

private:
    void setTool (Tool tool)
    {
        session.tool = tool;
        drawTool.setToggleState (tool == Tool::Draw, juce::dontSendNotification);
        selectTool.setToggleState (tool == Tool::Select, juce::dontSendNotification);
        eraseTool.setToggleState (tool == Tool::Erase, juce::dontSendNotification);
    }

    void setDimension (Dimension d)
    {
        session.dimension = d;
        for (size_t i = 0; i < dimensionButtons.size(); ++i)
            dimensionButtons[i]->setToggleState ((int) i == (int) d, juce::dontSendNotification);
        lane.repaint();
    }

    void deleteSelection()
    {
        if (session.selection.empty()) return;
        const auto uids = session.selectedList();
        session.apply ("Delete notes", [&uids] (NoteClip& clip) { for (int uid : uids) clip.removeNote (uid); });
        session.selection.clear();
        refresh();
    }

    void copySelection()
    {
        clipboard.clear();
        for (const auto& n : ctx.processor.getClip().notes)
            if (session.selection.count (n.uid))
                clipboard.push_back (n);
    }

    void pasteClipboard()
    {
        if (clipboard.empty()) return;
        double first = std::numeric_limits<double>::max();
        for (const auto& n : clipboard) first = std::min (first, n.start);
        const double at = ctx.telemetry.clipPlaying.load() ? std::floor (ctx.telemetry.clipPosition.load() / session.grid) * session.grid
                                                           : ClipTools::span (ctx.processor.getClip(), session.selectedList()).getEnd();
        std::vector<int> created;
        session.apply ("Paste notes", [&] (NoteClip& clip)
        {
            for (auto n : clipboard)
            {
                n.start = n.start - first + at;
                created.push_back (clip.addNote (n));
            }
            clip.sortByStart();
        });
        session.selection = { created.begin(), created.end() };
        refresh();
    }

    void duplicateSelection()
    {
        if (session.selection.empty()) return;
        const auto uids = session.selectedList();
        const auto span = ClipTools::span (ctx.processor.getClip(), uids);
        const double offset = std::max (session.grid, std::ceil (span.getLength() / session.grid - 1.0e-6) * session.grid);
        std::vector<int> created;
        session.apply ("Duplicate notes", [&] (NoteClip& clip) { created = ClipTools::duplicate (clip, uids, offset); });
        session.selection = { created.begin(), created.end() };
        refresh();
    }

    void refresh()
    {
        roll.repaint();
        lane.repaint();
        updateStatus();
    }

    void syncLengthBox()
    {
        const double bars = ctx.processor.getClip().lengthBeats / 4.0;
        const int ids[] = { 1, 2, 4, 8, 16 };
        for (int i = 0; i < 5; ++i)
            if (std::abs (bars - ids[i]) < 1.0e-6)
                length.setSelectedId (i + 1, juce::dontSendNotification);
    }

    void updateStatus()
    {
        const auto& clip = ctx.processor.getClip();
        status = juce::String ((int) clip.notes.size()) + " notes   " + juce::String ((int) session.selection.size()) + " selected   "
               + juce::String (clip.lengthBeats / 4.0, 0) + " bars";
        repaint (statusArea);
    }

    void editorTick() override
    {
        const bool recording = ctx.telemetry.clipRecording.load() || ctx.processor.isRecorderActive();
        const bool playing = ctx.telemetry.clipPlaying.load();
        record.setToggleState (recording, juce::dontSendNotification);
        play.setToggleState (playing && ! recording, juce::dontSendNotification);

        const int version = ctx.processor.getClipVersion();
        if (version != lastVersion)
        {
            lastVersion = version;
            syncLengthBox();
            updateStatus();
            // Drop selection entries whose notes no longer exist (undo, recording).
            for (auto it = session.selection.begin(); it != session.selection.end();)
                it = ctx.processor.getClip().findNote (*it) == nullptr ? session.selection.erase (it) : std::next (it);
        }
    }

    ClipSession session;
    PianoRoll roll;
    ExpressionLane lane;
    juce::TextButton record { "REC" }, overdub { "OVERDUB" }, play { "PLAY" }, stop { "STOP" }, loop { "LOOP" }, sync { "HOST SYNC" };
    juce::TextButton drawTool { "DRAW" }, selectTool { "SELECT" }, eraseTool { "ERASE" };
    juce::TextButton quantize { "QUANTIZE" }, humanize { "HUMANIZE" }, duplicate { "DUPLICATE" }, erase { "DELETE" },
                     copy { "COPY" }, paste { "PASTE" }, clear { "CLEAR ALL" }, fit { "FIT" };
    juce::ComboBox grid, length;
    std::vector<std::unique_ptr<juce::TextButton>> dimensionButtons;
    std::vector<ClipNote> clipboard;
    juce::Rectangle<int> toolbarArea, statusArea;
    juce::String status;
    int lastVersion = -1;
};

std::unique_ptr<Page> createNoteEditorPage (EditorContext& ctx) { return std::make_unique<NoteEditorPage> (ctx); }

} // namespace nedd::ui
