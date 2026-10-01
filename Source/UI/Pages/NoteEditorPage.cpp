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

    /** Ramer-Douglas-Peucker simplification of a drawn line, in pixel space. */
    void simplifyPolyline (const std::vector<juce::Point<float>>& in, size_t first, size_t last, float tolerance, std::vector<bool>& keep)
    {
        if (last <= first + 1)
            return;
        const juce::Line<float> chord (in[first], in[last]);
        float worst = -1.0f;
        size_t index = first;
        for (size_t i = first + 1; i < last; ++i)
        {
            juce::Point<float> nearest;
            const float d = chord.getDistanceFromPoint (in[i], nearest);
            if (d > worst) { worst = d; index = i; }
        }
        if (worst > tolerance)
        {
            keep[index] = true;
            simplifyPolyline (in, first, index, tolerance, keep);
            simplifyPolyline (in, index, last, tolerance, keep);
        }
    }

    /** Clip lengths the length box offers, in beats. */
    constexpr double kLengthChoices[] = { 4.0, 8.0, 16.0, 32.0, 64.0 };

    /** Grows the clip so `beat` is inside it (to the next length choice). */
    void ensureClipCovers (NoteClip& clip, double beat)
    {
        if (beat <= clip.lengthBeats)
            return;
        for (double choice : kLengthChoices)
            if (choice >= beat - 1.0e-9)
            {
                clip.lengthBeats = std::max (clip.lengthBeats, choice);
                return;
            }
        clip.lengthBeats = kLengthChoices[std::size (kLengthChoices) - 1];
    }

    // Expression layers drawn around each note line (toggled from the legend).
    enum Layer { LayerVelocity = 0, LayerRelease, LayerPressure, LayerSlide, kNumLayers };
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
        editing = false;
        if (name.isEmpty())
            return;
        ctx.processor.setClip (before);
        ctx.processor.setClip (working, name);
    }

    void cancel()
    {
        if (! editing)
            return;
        editing = false;
        ctx.processor.setClip (before);
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

    /** Selected notes, or every note when nothing is selected. */
    std::vector<int> targets() const
    {
        if (! selection.empty())
            return { selection.begin(), selection.end() };
        std::vector<int> all;
        for (const auto& n : ctx.processor.getClip().notes)
            all.push_back (n.uid);
        return all;
    }

    void flash (const juce::String& message)
    {
        if (onMessage)
            onMessage (message);
    }

    std::set<int> selection;
    double grid = 0.25;
    double lastClickBeat = 0.0;         // where PASTE puts notes when the clip is not playing
    Tool tool = Tool::Draw;
    Dimension dimension = Dimension::Pressure;
    TimeAxis axis;
    std::array<bool, kNumLayers> layers { true, true, true, true };
    std::function<void (const juce::String&)> onMessage;

    EditorContext& ctx;

private:
    NoteClip working, before;
    bool editing = false;
};

// =============================================================================================
/**
    The roll draws every note as a line: its sounding pitch over time (bends and glides
    included), with nodes where the pitch line changes direction. Around the line, translucent
    envelopes show velocity (attack triangle), pressure (above), slide (below) and release
    velocity (end triangle).

    DRAW: press on empty space and drag. The note starts where you press; dragging right
    lengthens it and moving up or down draws its pitch line (Shift: keep it flat, Ctrl: snap
    to semitones, Alt: no grid). Drag a node to reshape the line, drag the line to move the
    note, drag the end node to resize. Double-click the line to add a node, a node to remove it.
*/
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

    static constexpr int kKeysWidth = 46;
    static constexpr int kRulerHeight = 20;

    void centreOnNotes()
    {
        const auto& clip = session.clip();
        if (clip.notes.empty())
            return;
        int lo = 127, hi = 0;
        for (const auto& n : clip.notes) { lo = std::min (lo, n.noteNumber); hi = std::max (hi, n.noteNumber); }
        const int visible = visibleRows();
        lowNote = juce::jlimit (0, std::max (0, 127 - visible), (lo + hi) / 2 - visible / 2);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& clip = session.clip();
        const auto grid = gridArea();

        g.fillAll (colours::panel);
        paintRowsAndKeys (g, grid);
        paintGrid (g, grid, clip);

        g.saveState();
        g.reduceClipRegion (grid.withTrimmedTop (kRulerHeight));

        // Unselected notes first, selected ones on top.
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& n : clip.notes)
                if ((session.selection.count (n.uid) > 0) == (pass == 1))
                    paintNote (g, n, pass == 1, false);

        if (session.ctx.processor.isRecorderActive())
            for (const auto& n : session.ctx.processor.getRecordingPreview().notes)
                paintNote (g, n, false, true);

        if (banding)
        {
            g.setColour (colours::accent.withAlpha (0.08f));
            g.fillRoundedRectangle (band.toFloat(), 4.0f);
            g.setColour (colours::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (band.toFloat(), 4.0f, 1.0f);
        }
        g.restoreState();

        paintPlayhead (g, grid);
        paintLegend (g);

        if (clip.notes.empty() && ! session.ctx.processor.isRecorderActive())
        {
            g.setColour (colours::textDim);
            g.setFont (font (14.0f));
            g.drawFittedText ("DRAW: press and drag to draw a note line. Move up or down while dragging to bend its pitch.\n"
                              "Or press REC and play your controller.",
                              grid.withTrimmedTop (kRulerHeight).reduced (40), juce::Justification::centred, 3);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            const double beat = session.axis.xToBeat (e.position.x);
            session.axis.pixelsPerBeat = juce::jlimit (6.0, 400.0, session.axis.pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            session.axis.scrollBeat = std::max (0.0, beat - (e.position.x - (float) session.axis.left) / session.axis.pixelsPerBeat);
        }
        else if (e.mods.isAltDown())
        {
            rowHeight = juce::jlimit (8, 26, rowHeight + (w.deltaY > 0 ? 2 : -2));
        }
        else if (e.mods.isShiftDown())
            session.axis.scrollBeat = std::max (0.0, session.axis.scrollBeat - w.deltaY * 8.0 * 40.0 / session.axis.pixelsPerBeat);
        else
            lowNote = juce::jlimit (0, std::max (0, 127 - visibleRows()), lowNote + (w.deltaY > 0 ? 3 : -3));
        if (onChanged) onChanged();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (legendHit (e.position) >= 0)
        {
            const int layer = legendHit (e.position);
            session.layers[(size_t) layer] = ! session.layers[(size_t) layer];
            repaint();
            return;
        }

        if (! gridArea().withTrimmedTop (kRulerHeight).contains (e.getPosition()))
        {
            // Click in the ruler: move the paste position there.
            if (gridArea().contains (e.getPosition()))
            {
                session.lastClickBeat = std::max (0.0, snap (session.axis.xToBeat (e.position.x), e.mods, false));
                repaint();
            }
            return;
        }

        downBeat = session.axis.xToBeat (e.position.x);
        downPitch = yToPitch (e.position.y);
        session.lastClickBeat = std::max (0.0, snap (downBeat, e.mods, true));
        modified = false;

        if (e.mods.isPopupMenu())
        {
            showNoteMenu (e);
            return;
        }

        const auto node = hitNode (e.position);
        const auto hit = node.uid != 0 ? Hit { node.uid, node.node == kEndNode } : hitNote (e.position);
        const bool pitchNode = node.uid != 0 && node.node >= 1 && node.node != kEndNode;

        if (session.tool == Tool::Erase)
        {
            session.begin();
            if (pitchNode)
                removeNode (node.uid, node.node);
            else
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

            if (pitchNode)
            {
                // Dragging a pitch node of one note.
                session.selection = { node.uid };
                dragUid = node.uid;
                dragNode = node.node;
                mode = Mode::Node;
            }
            else
            {
                mode = hit.edge ? Mode::Resize : Mode::Move;
            }
        }
        else if (session.tool == Tool::Draw)
        {
            session.begin();
            ClipNote n;
            n.noteNumber = juce::jlimit (0, 127, (int) std::floor (downPitch + 0.5f));
            n.start = std::max (0.0, snap (downBeat, e.mods, true));
            n.length = std::max (session.grid, 1.0 / 16.0);
            n.velocity = 0.8f;
            n.pressure = { { 0.0f, 0.55f } };
            dragUid = session.edit().addNote (n);
            ensureClipCovers (session.edit(), n.end());
            session.edit().sortByStart();
            session.selection = { dragUid };
            drawn.clear();
            drawn.push_back ({ 0.0f, 0.0f });
            session.preview();
            mode = Mode::DrawLine;
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
            case Mode::DrawLine:  dragDrawLine (e, beat); break;
            case Mode::Node:      dragNodeTo (e, beat); break;
            case Mode::Move:
            {
                const double delta = snap (beat, e.mods, false) - snap (downBeat, e.mods, false);
                const int noteDelta = (int) std::round (yToPitch (e.position.y) - downPitch);
                auto& clip = session.edit();
                for (auto& n : clip.notes)
                {
                    if (! session.selection.count (n.uid)) continue;
                    if (const auto* o = original.findNote (n.uid))
                    {
                        n.start = std::max (0.0, o->start + delta);
                        n.noteNumber = juce::jlimit (0, 127, o->noteNumber + noteDelta);
                        ensureClipCovers (clip, n.end());
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
                        // Pitch nodes beyond the new end move with it.
                        for (auto& p : n.pitch)
                            p.time = std::min (p.time, (float) n.length);
                        ensureClipCovers (clip, n.end());
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
                    if (noteArea (n).intersects (band.toFloat()))
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

    void mouseUp (const juce::MouseEvent& e) override
    {
        switch (mode)
        {
            case Mode::DrawLine:
                finishDrawLine (e);
                session.commit ("Draw note");
                break;
            case Mode::Node:   session.commit (modified ? "Edit pitch line" : juce::String()); break;
            case Mode::Move:   session.commit (modified ? "Move notes" : juce::String()); break;
            case Mode::Resize: session.commit (modified ? "Resize notes" : juce::String()); break;
            case Mode::Erase:  session.commit (modified ? "Erase" : juce::String()); break;
            case Mode::Band:
            case Mode::None:
                if (session.isEditing()) session.commit ({});
                break;
        }

        mode = Mode::None;
        banding = false;
        dragUid = 0;
        if (onChanged) onChanged();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (session.tool == Tool::Erase)
            return;

        const auto node = hitNode (e.position);
        if (node.uid != 0 && node.node >= 1 && node.node != kEndNode)
        {
            const int uid = node.uid, index = node.node;
            session.apply ("Remove pitch node", [uid, index] (NoteClip& c)
            {
                if (auto* n = c.findNote (uid))
                    if (index < (int) n->pitch.size())
                        n->pitch.erase (n->pitch.begin() + index);
            });
            if (onChanged) onChanged();
            return;
        }

        const auto hit = hitNote (e.position);
        if (hit.uid != 0)
        {
            // Add a node on the line under the cursor, at the pitch the cursor points to.
            const int uid = hit.uid;
            const double beat = session.axis.xToBeat (e.position.x);
            const float pitch = yToPitch (e.position.y);
            session.apply ("Add pitch node", [uid, beat, pitch] (NoteClip& c)
            {
                auto* n = c.findNote (uid);
                if (n == nullptr)
                    return;
                const float t = (float) juce::jlimit (0.0, n->length, beat - n->start);
                if (n->pitch.empty())
                    n->pitch.push_back ({ 0.0f, 0.0f });
                n->pitch.push_back ({ t, pitch - (float) n->noteNumber });
                std::sort (n->pitch.begin(), n->pitch.end(), [] (const ExprPoint& a, const ExprPoint& b) { return a.time < b.time; });
            });
            session.selection = { uid };
            if (onChanged) onChanged();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (legendHit (e.position) >= 0)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }
        const auto node = hitNode (e.position);
        if (node.uid != 0)
        {
            setMouseCursor (node.node == kEndNode ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
            return;
        }
        const auto hit = hitNote (e.position);
        if (hit.uid != 0)
            setMouseCursor (hit.edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor (session.tool == Tool::Draw ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
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
        session.axis.pixelsPerBeat = (double) std::max (100, gridArea().getWidth() - 12) / beats;
        session.axis.scrollBeat = 0.0;
        centreOnNotes();
        if (onChanged) onChanged();
        repaint();
    }

    /** Scrolls so the given beat range is visible. */
    void reveal (juce::Range<double> beats)
    {
        const double visibleBeats = (double) gridArea().getWidth() / session.axis.pixelsPerBeat;
        if (beats.getEnd() > session.axis.scrollBeat + visibleBeats || beats.getStart() < session.axis.scrollBeat)
            session.axis.scrollBeat = std::max (0.0, beats.getStart() - visibleBeats * 0.1);
        if (onChanged) onChanged();
        repaint();
    }

private:
    enum class Mode { None, DrawLine, Node, Move, Resize, Band, Erase };
    struct Hit { int uid = 0; bool edge = false; };
    struct NodeHit { int uid = 0; int node = -1; };
    static constexpr int kEndNode = 100000;

    // ---- geometry ------------------------------------------------------------------------------
    int visibleRows() const { return std::max (1, (getHeight() - kRulerHeight) / rowHeight); }
    float pitchToY (float pitch) const { return (float) getHeight() - (pitch - (float) lowNote + 0.5f) * (float) rowHeight; }
    float yToPitch (float y) const { return (float) lowNote - 0.5f + ((float) getHeight() - y) / (float) rowHeight; }

    juce::Point<float> linePoint (const ClipNote& n, float t) const
    {
        const float bend = evaluateCurve (n.pitch, t, 0.0f);
        return { session.axis.beatToX (n.start + t), pitchToY ((float) n.noteNumber + bend) };
    }

    /** Points of the drawn pitch line (dense enough to follow curves). */
    std::vector<juce::Point<float>> lineOf (const ClipNote& n) const
    {
        std::vector<juce::Point<float>> pts;
        const float widthPx = (float) (n.length * session.axis.pixelsPerBeat);
        const int steps = juce::jlimit (1, 400, (int) (widthPx / 3.0f));
        for (int i = 0; i <= steps; ++i)
            pts.push_back (linePoint (n, (float) n.length * (float) i / (float) steps));
        return pts;
    }

    /** Bounding area of a note including its line and a margin (for band selection and culling). */
    juce::Rectangle<float> noteArea (const ClipNote& n) const
    {
        juce::Rectangle<float> r;
        bool first = true;
        for (const auto& p : lineOf (n))
        {
            r = first ? juce::Rectangle<float> (p, p) : r.getUnion (juce::Rectangle<float> (p, p));
            first = false;
        }
        return r.expanded (2.0f, (float) rowHeight * 0.5f);
    }

    std::vector<std::pair<int, juce::Point<float>>> nodesOf (const ClipNote& n) const
    {
        std::vector<std::pair<int, juce::Point<float>>> nodes;
        nodes.push_back ({ 0, linePoint (n, 0.0f) });
        for (int i = 0; i < (int) n.pitch.size(); ++i)
            if (n.pitch[(size_t) i].time > 1.0e-4f && n.pitch[(size_t) i].time < (float) n.length - 1.0e-4f)
                nodes.push_back ({ i, linePoint (n, n.pitch[(size_t) i].time) });
        nodes.push_back ({ kEndNode, linePoint (n, (float) n.length) });
        return nodes;
    }

    NodeHit hitNode (juce::Point<float> p) const
    {
        const auto& clip = session.clip();
        for (int pass = 0; pass < 2; ++pass)   // selected notes take priority
            for (auto it = clip.notes.rbegin(); it != clip.notes.rend(); ++it)
            {
                if ((session.selection.count (it->uid) > 0) != (pass == 0))
                    continue;
                for (const auto& [index, pt] : nodesOf (*it))
                    if (pt.getDistanceFrom (p) <= 6.5f)
                        return { it->uid, index == 0 ? 0 : index };
            }
        return {};
    }

    Hit hitNote (juce::Point<float> p) const
    {
        const auto& clip = session.clip();
        for (auto it = clip.notes.rbegin(); it != clip.notes.rend(); ++it)
        {
            const auto pts = lineOf (*it);
            for (size_t i = 0; i + 1 < pts.size(); ++i)
            {
                const juce::Line<float> seg (pts[i], pts[i + 1]);
                juce::Point<float> nearest;
                if (seg.getDistanceFromPoint (p, nearest) <= std::max (5.0f, (float) rowHeight * 0.45f))
                {
                    const float endX = pts.back().x;
                    return { it->uid, p.x > endX - 7.0f && endX - pts.front().x > 14.0f };
                }
            }
        }
        return {};
    }

    double snap (double beat, const juce::ModifierKeys& mods, bool floorToGrid) const
    {
        if (mods.isAltDown())
            return beat;
        return floorToGrid ? std::floor (beat / session.grid) * session.grid : std::round (beat / session.grid) * session.grid;
    }

    // ---- drawing -------------------------------------------------------------------------------
    void dragDrawLine (const juce::MouseEvent& e, double beat)
    {
        auto* n = session.edit().findNote (dragUid);
        if (n == nullptr)
            return;

        const float t = (float) std::max (0.0, beat - n->start);
        float bend = e.mods.isShiftDown() ? 0.0f : yToPitch (e.position.y) - (float) n->noteNumber;
        if (e.mods.isCommandDown())
            bend = std::round (bend);
        bend = juce::jlimit (-48.0f, 48.0f, bend);

        // Moving back to the left erases the end of the line drawn so far.
        while (drawn.size() > 1 && drawn.back().time > t)
            drawn.pop_back();
        if (t > drawn.back().time + 1.0e-4f)
            drawn.push_back ({ t, bend });

        n->length = std::max ((double) t, 1.0 / 32.0);
        n->pitch = drawn;
        ensureClipCovers (session.edit(), n->end());
        session.preview();
    }

    void finishDrawLine (const juce::MouseEvent& e)
    {
        auto* n = session.edit().findNote (dragUid);
        if (n == nullptr)
            return;

        // The end snaps to the grid (at least one grid step long unless Alt is held).
        const double rawEnd = n->end();
        const double end = e.mods.isAltDown() ? rawEnd : std::max (n->start + session.grid, std::ceil (rawEnd / session.grid - 1.0e-6) * session.grid);
        n->length = std::max (1.0 / 32.0, end - n->start);

        // Simplify the hand-drawn line into a few nodes.
        std::vector<juce::Point<float>> px;
        for (const auto& p : drawn)
            px.push_back ({ p.time * (float) session.axis.pixelsPerBeat, p.value * (float) rowHeight });
        ExprCurve curve;
        if (px.size() >= 2)
        {
            std::vector<bool> keep (px.size(), false);
            keep.front() = keep.back() = true;
            simplifyPolyline (px, 0, px.size() - 1, 2.5f, keep);
            for (size_t i = 0; i < drawn.size(); ++i)
                if (keep[i])
                    curve.push_back (drawn[i]);
        }

        bool flat = true;
        for (const auto& p : curve)
            flat = flat && std::abs (p.value) < 0.08f;
        n->pitch = flat ? ExprCurve() : curve;
        ensureClipCovers (session.edit(), n->end());
        session.preview();
    }

    void dragNodeTo (const juce::MouseEvent& e, double beat)
    {
        auto* n = session.edit().findNote (dragUid);
        const auto* o = original.findNote (dragUid);
        if (n == nullptr || o == nullptr || dragNode >= (int) o->pitch.size())
            return;

        n->pitch = o->pitch;
        auto& p = n->pitch[(size_t) dragNode];
        const float prev = dragNode > 0 ? n->pitch[(size_t) dragNode - 1].time + 1.0e-3f : 0.0f;
        const float next = dragNode + 1 < (int) n->pitch.size() ? n->pitch[(size_t) dragNode + 1].time - 1.0e-3f : (float) n->length;
        const double snapped = e.mods.isAltDown() ? beat : std::round (beat / (session.grid * 0.5)) * session.grid * 0.5;
        p.time = juce::jlimit (prev, std::max (prev, next), (float) (snapped - n->start));
        float bend = yToPitch (e.position.y) - (float) n->noteNumber;
        if (e.mods.isCommandDown()) bend = std::round (bend);
        p.value = juce::jlimit (-48.0f, 48.0f, bend);
        modified = true;
        session.preview();
    }

    void removeNode (int uid, int index)
    {
        if (auto* n = session.edit().findNote (uid))
            if (index > 0 && index < (int) n->pitch.size())
            {
                n->pitch.erase (n->pitch.begin() + index);
                modified = true;
                session.preview();
            }
    }

    void eraseAt (juce::Point<float> p)
    {
        const auto hit = hitNote (p);
        if (hit.uid == 0)
            return;
        session.edit().removeNote (hit.uid);
        session.selection.erase (hit.uid);
        modified = true;
        session.preview();
    }

    void showNoteMenu (const juce::MouseEvent& e)
    {
        const auto hit = hitNote (e.position);
        if (hit.uid != 0 && ! session.selection.count (hit.uid))
            session.selection = { hit.uid };
        if (session.selection.empty())
            return;

        juce::PopupMenu m;
        m.addSectionHeader (juce::String ((int) session.selection.size()) + (session.selection.size() == 1 ? " note" : " notes"));
        m.addItem (1, "Flatten pitch line");
        m.addItem (2, "Pressure: swell");
        m.addItem (3, "Pressure: fade");
        m.addItem (4, "Slide: rise");
        m.addItem (5, "Clear pressure and slide");
        m.addSeparator();
        m.addItem (6, "Delete");

        juce::Component::SafePointer<PianoRoll> safeThis (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            auto& s = safeThis->session;
            const auto uids = s.selection;
            const juce::String names[] = { "", "Flatten pitch", "Pressure swell", "Pressure fade", "Slide rise", "Clear expression", "Delete notes" };
            s.apply (names[result], [&] (NoteClip& c)
            {
                for (int uid : uids)
                {
                    if (result == 6) { c.removeNote (uid); continue; }
                    auto* n = c.findNote (uid);
                    if (n == nullptr) continue;
                    const float len = (float) n->length;
                    switch (result)
                    {
                        case 1: n->pitch.clear(); break;
                        case 2: n->pressure = { { 0.0f, 0.15f }, { len * 0.6f, 0.95f }, { len, 0.7f } }; break;
                        case 3: n->pressure = { { 0.0f, 0.9f }, { len, 0.05f } }; break;
                        case 4: n->slide = { { 0.0f, 0.0f }, { len, 1.0f } }; break;
                        case 5: n->pressure.clear(); n->slide.clear(); break;
                        default: break;
                    }
                }
            });
            if (result == 6) s.selection.clear();
            if (safeThis->onChanged) safeThis->onChanged();
        });
    }

    // ---- painting ------------------------------------------------------------------------------
    void paintRowsAndKeys (juce::Graphics& g, juce::Rectangle<int> grid)
    {
        for (int row = 0; row <= visibleRows() + 1; ++row)
        {
            const int note = lowNote + row;
            if (note > 127) break;
            const int pc = note % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const float y = pitchToY ((float) note) - (float) rowHeight * 0.5f;
            if (black)
            {
                g.setColour (colours::accentSoft.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> ((float) grid.getX(), y, (float) grid.getWidth(), (float) rowHeight));
            }

            // Keys
            const auto key = juce::Rectangle<float> (2.0f, y + 0.5f, (float) kKeysWidth - 6.0f, (float) rowHeight - 1.0f);
            g.setColour (black ? colours::keyBlack : colours::keyWhite);
            g.fillRoundedRectangle (black ? key.withTrimmedRight (12.0f) : key, 2.5f);
            if (! black)
            {
                g.setColour (colours::outline);
                g.drawRoundedRectangle (key, 2.5f, 0.8f);
            }
            if (pc == 0)
            {
                g.setColour (colours::textDim);
                g.setFont (font (9.5f));
                g.drawText (noteName (note), key.withTrimmedRight (3.0f), juce::Justification::centredRight);
                g.setColour (colours::outlineStrong.withAlpha (0.7f));
                g.drawHorizontalLine ((int) (y + (float) rowHeight), (float) grid.getX(), (float) grid.getRight());
            }
        }
    }

    void paintGrid (juce::Graphics& g, juce::Rectangle<int> grid, const NoteClip& clip)
    {
        const auto& axis = session.axis;
        g.setColour (colours::control);
        g.fillRect (juce::Rectangle<int> (grid.getX(), 0, grid.getWidth(), kRulerHeight));
        g.setColour (colours::outline);
        g.drawHorizontalLine (kRulerHeight, (float) grid.getX(), (float) grid.getRight());

        const double firstBeat = std::floor (axis.scrollBeat / session.grid) * session.grid;
        for (double b = firstBeat; axis.beatToX (b) < (float) grid.getRight(); b += session.grid)
        {
            const float x = axis.beatToX (b);
            if (x < (float) grid.getX()) continue;
            const bool bar = std::abs (std::fmod (b, 4.0)) < 1.0e-6;
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (bar ? colours::outlineStrong : (beat ? colours::outline : colours::outline.withAlpha (0.45f)));
            g.drawVerticalLine ((int) x, (float) kRulerHeight, (float) grid.getBottom());
            if (bar)
            {
                g.setColour (colours::textDim);
                g.setFont (displayFont (10.5f));
                g.drawText (juce::String ((int) (b / 4.0) + 1), juce::Rectangle<float> (x + 4.0f, 3.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

        // Paste position marker
        const float pasteX = axis.beatToX (session.lastClickBeat);
        if (pasteX >= (float) grid.getX() && pasteX <= (float) grid.getRight())
        {
            juce::Path marker;
            marker.addTriangle (pasteX - 4.0f, 2.0f, pasteX + 4.0f, 2.0f, pasteX, 8.0f);
            g.setColour (colours::modulation.withAlpha (0.7f));
            g.fillPath (marker);
        }

        // Clip end
        const float endX = axis.beatToX (clip.lengthBeats);
        if (endX < (float) grid.getRight())
        {
            g.setColour (colours::background.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (endX, (float) kRulerHeight, (float) grid.getRight() - endX, (float) (grid.getHeight() - kRulerHeight)));
            g.setColour (colours::amber);
            g.drawVerticalLine ((int) endX, 0.0f, (float) grid.getBottom());
        }
    }

    void paintNote (juce::Graphics& g, const ClipNote& n, bool selected, bool recording)
    {
        const auto area = noteArea (n);
        const auto visible = gridArea().toFloat();
        if (area.getRight() + 40.0f < visible.getX() || area.getX() > visible.getRight())
            return;

        const auto pts = lineOf (n);
        const float row = (float) rowHeight;
        const auto lineColour = recording ? colours::danger : (selected ? colours::accent : colours::accent.withAlpha (0.75f));

        auto envelope = [&] (const ExprCurve& curve, float heightRows, float direction, juce::Colour c)
        {
            if (curve.empty())
                return;
            juce::Path p;
            p.startNewSubPath (pts.front());
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const float t = (float) n.length * (float) i / (float) (pts.size() - 1);
                const float v = juce::jlimit (0.0f, 1.0f, evaluateCurve (curve, t, 0.0f));
                p.lineTo (pts[i].x, pts[i].y - direction * v * heightRows * row);
            }
            for (size_t i = pts.size(); i-- > 0;)
                p.lineTo (pts[i]);
            p.closeSubPath();
            g.setColour (c.withAlpha (selected ? 0.28f : 0.18f));
            g.fillPath (p);
            juce::Path edge;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const float t = (float) n.length * (float) i / (float) (pts.size() - 1);
                const float v = juce::jlimit (0.0f, 1.0f, evaluateCurve (curve, t, 0.0f));
                const juce::Point<float> q (pts[i].x, pts[i].y - direction * v * heightRows * row);
                if (i == 0) edge.startNewSubPath (q); else edge.lineTo (q);
            }
            g.setColour (c.withAlpha (selected ? 0.75f : 0.45f));
            g.strokePath (edge, juce::PathStrokeType (1.0f));
        };

        if (session.layers[LayerSlide])    envelope (n.slide, 1.6f, -1.0f, colours::slide);
        if (session.layers[LayerPressure]) envelope (n.pressure, 2.4f, 1.0f, colours::pressure);

        if (session.layers[LayerVelocity])
        {
            // Attack triangle: height = velocity, decaying back to the line.
            const float x0 = pts.front().x;
            const float width = std::min ((float) (session.axis.pixelsPerBeat * 0.5), pts.back().x - x0);
            const float h = n.velocity * 3.0f * row;
            juce::Path tri;
            tri.addTriangle (x0, pts.front().y, x0, pts.front().y - h, x0 + std::max (4.0f, width), linePoint (n, (float) (width / session.axis.pixelsPerBeat)).y);
            g.setColour (colours::velocity.withAlpha (selected ? 0.4f : 0.26f));
            g.fillPath (tri);
            g.setColour (colours::velocity.withAlpha (0.8f));
            g.strokePath (tri, juce::PathStrokeType (1.0f));
        }

        if (session.layers[LayerRelease] && n.releaseVelocity > 0.0f)
        {
            const auto end = pts.back();
            const float width = std::min ((float) (session.axis.pixelsPerBeat * 0.25), end.x - pts.front().x);
            const float h = n.releaseVelocity * 1.6f * row;
            juce::Path tri;
            tri.addTriangle (end.x - std::max (3.0f, width), end.y, end.x, end.y - h, end.x, end.y);
            g.setColour (colours::releaseVel.withAlpha (selected ? 0.4f : 0.25f));
            g.fillPath (tri);
        }

        // The pitch line itself, with a soft glow when selected.
        juce::Path line;
        line.startNewSubPath (pts.front());
        for (size_t i = 1; i < pts.size(); ++i)
            line.lineTo (pts[i]);
        if (selected)
        {
            g.setColour (colours::accent.withAlpha (0.18f));
            g.strokePath (line, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (lineColour);
        g.strokePath (line, juce::PathStrokeType (selected ? 2.8f : 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Nodes
        for (const auto& [index, pt] : nodesOf (n))
        {
            const bool startNode = index == 0;
            const float size = startNode ? 10.0f : (index == kEndNode ? 7.0f : 8.0f);
            const auto r = juce::Rectangle<float> (size, size).withCentre (pt);
            g.setColour (colours::panel);
            g.fillEllipse (r);
            if (selected || startNode)
                g.setGradientFill (roseGradient (r, recording ? 0.5f : 1.0f));
            else
                g.setColour (colours::accent.withAlpha (0.55f));
            if (startNode || selected)
                g.fillEllipse (r.reduced (startNode ? 2.5f : 2.0f));
            g.setColour (lineColour);
            g.drawEllipse (r, 1.4f);
        }
    }

    void paintPlayhead (juce::Graphics& g, juce::Rectangle<int> grid)
    {
        auto& t = session.ctx.telemetry;
        if (! t.clipPlaying.load())
            return;
        const float x = session.axis.beatToX (t.clipPosition.load());
        if (x < (float) grid.getX() || x > (float) grid.getRight())
            return;
        const auto c = t.clipRecording.load() ? colours::danger : colours::accent;
        g.setColour (c.withAlpha (0.8f));
        g.drawLine (x, (float) kRulerHeight, x, (float) grid.getBottom(), 1.5f);
        juce::Path head;
        head.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 9.0f);
        g.setColour (c);
        g.fillPath (head);
    }

    juce::Rectangle<float> legendBounds (int layer) const
    {
        const float w = 92.0f, h = 17.0f;
        return { (float) getWidth() - w - 10.0f, (float) kRulerHeight + 8.0f + (float) layer * (h + 3.0f), w, h };
    }

    int legendHit (juce::Point<float> p) const
    {
        for (int l = 0; l < kNumLayers; ++l)
            if (legendBounds (l).contains (p))
                return l;
        return -1;
    }

    void paintLegend (juce::Graphics& g)
    {
        const juce::String names[] = { "Velocity", "Release Vel", "Pressure", "Slide" };
        const juce::Colour cols[] = { colours::velocity, colours::releaseVel, colours::pressure, colours::slide };
        auto box = legendBounds (0).getUnion (legendBounds (kNumLayers - 1)).expanded (5.0f);
        g.setColour (colours::panel.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (box, 8.0f, 1.0f);

        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto r = legendBounds (l);
            const bool on = session.layers[(size_t) l];
            const auto swatch = r.withWidth (12.0f).withSizeKeepingCentre (12.0f, 10.0f);
            g.setColour (cols[l].withAlpha (on ? 0.85f : 0.2f));
            g.fillRoundedRectangle (swatch, 3.0f);
            g.setColour (on ? colours::text : colours::textFaint);
            g.setFont (font (11.5f));
            g.drawText (names[l], r.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
        }
    }

    void editorTick() override
    {
        const bool playing = session.ctx.telemetry.clipPlaying.load();
        const bool recording = session.ctx.processor.isRecorderActive();
        const int version = session.ctx.processor.getClipVersion();
        if (playing || recording || version != lastVersion || wasPlaying != playing)
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
    ExprCurve drawn;
    Mode mode = Mode::None;
    double downBeat = 0.0;
    float downPitch = 60.0f;
    int lowNote = 48;
    int rowHeight = 13;
    int dragUid = 0, dragNode = -1;
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
        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

        const auto dim = session.dimension;
        const auto colour = dimensionColour (dim);
        g.setColour (colours::outline);
        for (float v : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine ((int) valueToY (dim == Dimension::Pitch ? (v - 0.5f) * 2.0f * pitchRange : v), (float) area.getX(), (float) area.getRight());

        // Lane title and scale
        const juce::String titles[] = { "PITCH", "PRESS", "SLIDE", "VEL" };
        g.setColour (colour);
        g.setFont (displayFont (10.5f));
        g.drawText (titles[(int) dim], juce::Rectangle<int> (6, area.getY() + area.getHeight() / 2 - 8, PianoRoll::kKeysWidth - 8, 16), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (font (10.0f));
        g.drawText (dim == Dimension::Pitch ? "+" + juce::String ((int) pitchRange) : "100%", juce::Rectangle<int> (2, area.getY(), PianoRoll::kKeysWidth - 6, 12), juce::Justification::right);
        g.drawText (dim == Dimension::Pitch ? "-" + juce::String ((int) pitchRange) : "0", juce::Rectangle<int> (2, area.getBottom() - 12, PianoRoll::kKeysWidth - 6, 12), juce::Justification::right);

        g.saveState();
        g.reduceClipRegion (area);
        const auto& clip = session.clip();
        const bool anySelected = ! session.selection.empty();

        for (const auto& n : clip.notes)
        {
            const bool selected = session.selection.count (n.uid) > 0;
            const float alpha = ! anySelected || selected ? 1.0f : 0.35f;
            const float x0 = session.axis.beatToX (n.start);
            const float x1 = session.axis.beatToX (n.end());
            if (x1 < (float) area.getX() || x0 > (float) area.getRight())
                continue;

            if (dim == Dimension::Velocity)
            {
                const float y = valueToY (n.velocity);
                g.setColour (colour.withAlpha (0.8f * alpha));
                g.drawLine (x0, (float) area.getBottom(), x0, y, 2.5f);
                g.setColour (colours::panel);
                g.fillEllipse (x0 - 4.5f, y - 4.5f, 9.0f, 9.0f);
                g.setColour (colour.withAlpha (alpha));
                g.drawEllipse (x0 - 4.5f, y - 4.5f, 9.0f, 9.0f, 2.0f);
                continue;
            }

            const auto& curve = curveOf (n, dim);
            juce::Path path, fill;
            const int steps = std::max (2, (int) ((x1 - x0) / 2.0f));
            const float baseY = valueToY (0.0f);
            fill.startNewSubPath (x0, baseY);
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                const float v = evaluateCurve (curve, t * (float) n.length, 0.0f);
                const juce::Point<float> p (x0 + t * (x1 - x0), valueToY (v));
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
                fill.lineTo (p);
            }
            fill.lineTo (x1, baseY);
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.16f * alpha));
            g.fillPath (fill);
            g.setColour (colour.withAlpha (alpha));
            g.strokePath (path, juce::PathStrokeType (selected ? 2.2f : 1.6f));
            g.setColour (colour.withAlpha (0.25f * alpha));
            g.drawVerticalLine ((int) x0, (float) area.getY(), (float) area.getBottom());

            if (selected)
                for (const auto& pt : curve)
                {
                    const auto r = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ session.axis.beatToX (n.start + pt.time), valueToY (pt.value) });
                    g.setColour (colours::panel);
                    g.fillEllipse (r);
                    g.setColour (colour);
                    g.drawEllipse (r, 1.5f);
                }
        }
        g.restoreState();

        if (clip.notes.empty())
        {
            g.setColour (colours::textFaint);
            g.setFont (font (12.0f));
            const char* fullNames[] = { "pitch", "pressure", "slide", "velocity" };
            g.drawText (juce::String ("Draw notes above, then paint their ") + fullNames[(int) dim] + " here.", area, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        session.begin();
        changed = false;
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
        const juce::String names[] = { "Draw pitch", "Draw pressure", "Draw slide", "Edit velocity" };
        session.commit (changed ? names[(int) session.dimension] : juce::String());
        if (! changed)
            session.flash ("Paint inside a note's time span to change its " + names[(int) session.dimension].fromFirstOccurrenceOf (" ", false, false) + ".");
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
    juce::Rectangle<int> laneArea() const { return getLocalBounds().withTrimmedLeft (PianoRoll::kKeysWidth).reduced (0, 6); }

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

    /** Notes under the stroke. Where notes overlap in time, selected notes are edited and others left alone. */
    std::vector<int> targetsAt (double beat) const
    {
        std::vector<int> all, selected;
        for (const auto& n : session.clip().notes)
            if (beat >= n.start && beat <= n.end())
            {
                all.push_back (n.uid);
                if (session.selection.count (n.uid))
                    selected.push_back (n.uid);
            }
        return selected.empty() ? all : selected;
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
                if (x >= std::min (from.x, to.x) - 6.0f && x <= std::max (from.x, to.x) + 6.0f)
                {
                    n.velocity = juce::jlimit (0.02f, 1.0f, yToValue (to.y));
                    changed = true;
                }
            }
            session.preview();
            repaint();
            return;
        }

        // Draw a continuous stroke between the previous and current mouse positions.
        const float x0 = std::min (from.x, to.x), x1 = std::max (from.x, to.x);
        const double beatA = session.axis.xToBeat (x0), beatB = session.axis.xToBeat (x1);
        const double pointSpacing = 2.0 / session.axis.pixelsPerBeat;

        std::set<int> targets;
        for (int uid : targetsAt (beatA)) targets.insert (uid);
        for (int uid : targetsAt (beatB)) targets.insert (uid);
        for (const auto& n : clip.notes)
            if (n.start >= beatA && n.end() <= beatB)
                targets.insert (n.uid);

        for (auto& n : clip.notes)
        {
            if (! targets.count (n.uid))
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
            changed = true;
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
    bool changed = false;
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
        session.onMessage = [this] (const juce::String& m) { showMessage (m); };

        for (auto* comp : std::initializer_list<juce::Component*> { &roll, &lane, &record, &overdub, &play, &stop, &loop, &sync, &drawTool, &selectTool,
                                                                    &eraseTool, &grid, &quantize, &humanize, &duplicate, &erase, &copy, &paste, &clear,
                                                                    &length, &fit })
            addAndMakeVisible (comp);

        record.setColour (juce::TextButton::textColourOffId, colours::danger);
        record.setTooltip ("Record what you play (click again to stop recording; playback continues)");
        record.onClick = [this]
        {
            if (ctx.processor.isRecorderActive())
            {
                ctx.processor.clipStopRecording();
                showMessage ("Recording stopped");
            }
            else
            {
                ctx.processor.clipRecord (overdub.getToggleState());
                showMessage (overdub.getToggleState() ? "Recording (overdub): play your controller or the keyboard below"
                                                      : "Recording: play your controller or the keyboard below");
            }
        };
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
        sync.onClick = [this]
        {
            ctx.processor.setClipSyncToHost (sync.getToggleState());
            if (! sync.getToggleState())
                showMessage ("Host sync off: PLAY / STOP control the clip");
            else if (ctx.telemetry.hostTransportAvailable.load())
                showMessage ("Host sync on: the clip plays when your DAW plays, locked to its bars");
            else
                showMessage ("Host sync on, but this app has no DAW transport: PLAY / STOP still work here");
        };

        for (auto* b : { &drawTool, &selectTool, &eraseTool })
            b->setClickingTogglesState (false);
        drawTool.setTooltip ("Draw: press and drag to draw a note line; move up/down to bend it (Shift: flat, Ctrl: semitones, Alt: off-grid)");
        selectTool.setTooltip ("Select: drag a box to select, drag notes or nodes to edit them");
        eraseTool.setTooltip ("Erase: click or drag over notes (or a node) to remove them");
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
            const double beats = kLengthChoices[juce::jlimit (0, 4, length.getSelectedId() - 1)];
            if (std::abs (beats - ctx.processor.getClip().lengthBeats) > 1.0e-6)
                session.apply ("Clip length", [beats] (NoteClip& clip) { clip.lengthBeats = beats; });
            roll.fitToClip();
        };

        quantize.setTooltip ("Snap the selected notes (or all notes) to the grid: starts and ends");
        humanize.setTooltip ("Add small random timing and velocity changes to the selected notes (or all notes)");
        duplicate.setTooltip ("Copy the selected notes (or all notes) right after themselves (Ctrl+D)");
        copy.setTooltip ("Copy the selected notes (Ctrl+C)");
        paste.setTooltip ("Paste at the marker in the ruler (click the ruler to move it), or at the playhead while playing (Ctrl+V)");

        quantize.onClick = [this] { quantizeNotes(); };
        humanize.onClick = [this] { humanizeNotes(); };
        duplicate.onClick = [this] { duplicateSelection(); };
        erase.onClick = [this] { deleteSelection(); };
        copy.onClick = [this] { copySelection(); };
        paste.onClick = [this] { pasteClipboard(); };
        clear.onClick = [this]
        {
            session.apply ("Clear clip", [] (NoteClip& clip) { clip.notes.clear(); });
            session.selection.clear();
            showMessage ("Clip cleared (Ctrl+Z to undo)");
        };
        fit.onClick = [this] { roll.fitToClip(); };

        const std::pair<Dimension, const char*> dims[] = { { Dimension::Pitch, "PITCH" }, { Dimension::Pressure, "PRESSURE" },
                                                           { Dimension::Slide, "SLIDE" }, { Dimension::Velocity, "VELOCITY" } };
        for (const auto& [d, name] : dims)
        {
            auto b = std::make_unique<juce::TextButton> (name);
            const auto dim = d;
            b->onClick = [this, dim] { setDimension (dim); };
            b->setTooltip ("Show and paint this expression in the lane below");
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
        drawPanel (g, editArea.toFloat());

        const bool flashing = juce::Time::getMillisecondCounter() < messageUntil;
        g.setColour (flashing ? colours::accent : colours::textDim);
        g.setFont (flashing ? font (12.0f) : monoFont (11.5f));
        g.drawFittedText (flashing ? message : status, statusArea, juce::Justification::centredRight, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        toolbarArea = r.removeFromTop (80);
        r.removeFromTop (8);
        editArea = r;

        auto t = toolbarArea.reduced (10, 8);
        auto row1 = t.removeFromTop (28);
        t.removeFromTop (6);
        auto row2 = t.removeFromTop (28);

        auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (5); };
        place (row1, record, 76);
        place (row1, overdub, 78);
        place (row1, play, 56);
        place (row1, stop, 56);
        place (row1, loop, 56);
        place (row1, sync, 92);
        row1.removeFromLeft (12);
        place (row1, drawTool, 60);
        place (row1, selectTool, 66);
        place (row1, eraseTool, 60);
        row1.removeFromLeft (12);
        place (row1, grid, 74);
        place (row1, length, 86);
        place (row1, fit, 46);
        statusArea = row1;

        place (row2, quantize, 88);
        place (row2, humanize, 88);
        place (row2, duplicate, 88);
        place (row2, erase, 66);
        place (row2, copy, 58);
        place (row2, paste, 58);
        place (row2, clear, 80);
        row2.removeFromLeft (16);
        for (auto& b : dimensionButtons)
            place (row2, *b, 84);

        auto inner = editArea.reduced (8);
        lane.setBounds (inner.removeFromBottom (150));
        inner.removeFromBottom (6);
        roll.setBounds (inner);
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
        if (! cmd && key.getKeyCode() == 'D') { setTool (Tool::Draw); return true; }
        if (! cmd && key.getKeyCode() == 'S') { setTool (Tool::Select); return true; }
        if (! cmd && key.getKeyCode() == 'E') { setTool (Tool::Erase); return true; }
        if (! cmd && key.getKeyCode() == 'Q') { quantizeNotes(); return true; }
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

    static juce::String count (size_t n) { return juce::String ((int) n) + (n == 1 ? " note" : " notes"); }

    void quantizeNotes()
    {
        const auto uids = session.targets();
        if (uids.empty()) { showMessage ("Nothing to quantize: draw or record some notes first"); return; }
        session.apply ("Quantize", [&] (NoteClip& clip) { ClipTools::quantize (clip, uids, session.grid, 1.0f, true); });
        showMessage ("Quantized " + count (uids.size()) + " to " + grid.getText() + (session.selection.empty() ? " (all notes)" : ""));
    }

    void humanizeNotes()
    {
        const auto uids = session.targets();
        if (uids.empty()) { showMessage ("Nothing to humanize: draw or record some notes first"); return; }
        session.apply ("Humanize", [&] (NoteClip& clip)
        {
            ClipTools::humanize (clip, uids, session.grid * 0.3, 0.15f, (uint32_t) juce::Time::currentTimeMillis());
        });
        showMessage ("Humanized " + count (uids.size()) + ": timing +-" + juce::String (session.grid * 0.3, 2) + " beats, velocity +-15%");
    }

    void deleteSelection()
    {
        if (session.selection.empty()) { showMessage ("Select notes to delete (SELECT tool, or click a note)"); return; }
        const auto uids = std::vector<int> (session.selection.begin(), session.selection.end());
        session.apply ("Delete notes", [&uids] (NoteClip& clip) { for (int uid : uids) clip.removeNote (uid); });
        session.selection.clear();
        showMessage ("Deleted " + count (uids.size()));
        refresh();
    }

    void copySelection()
    {
        clipboard.clear();
        for (const auto& n : ctx.processor.getClip().notes)
            if (session.selection.empty() || session.selection.count (n.uid))
                clipboard.push_back (n);
        if (clipboard.empty())
            showMessage ("Nothing to copy");
        else
            showMessage ("Copied " + count (clipboard.size()) + (session.selection.empty() ? " (all)" : "")
                         + ". Click the ruler where they should go, then PASTE");
    }

    void pasteClipboard()
    {
        if (clipboard.empty()) { showMessage ("Clipboard is empty: COPY some notes first"); return; }
        double first = std::numeric_limits<double>::max();
        for (const auto& n : clipboard) first = std::min (first, n.start);
        const double at = ctx.telemetry.clipPlaying.load() ? std::floor (ctx.telemetry.clipPosition.load() / session.grid) * session.grid
                                                           : session.lastClickBeat;
        std::vector<int> created;
        double lastEnd = at;
        session.apply ("Paste notes", [&] (NoteClip& clip)
        {
            for (auto n : clipboard)
            {
                n.start = n.start - first + at;
                lastEnd = std::max (lastEnd, n.end());
                created.push_back (clip.addNote (n));
            }
            ensureClipCovers (clip, lastEnd);
            clip.sortByStart();
        });
        session.selection = { created.begin(), created.end() };
        session.lastClickBeat = std::ceil (lastEnd / session.grid - 1.0e-6) * session.grid;   // the next paste follows on
        roll.reveal ({ at, lastEnd });
        showMessage ("Pasted " + count (created.size()) + " at bar " + juce::String (at / 4.0 + 1.0, 2));
        refresh();
    }

    void duplicateSelection()
    {
        const auto uids = session.targets();
        if (uids.empty()) { showMessage ("Nothing to duplicate: draw or record some notes first"); return; }
        const auto span = ClipTools::span (ctx.processor.getClip(), uids);
        const double offset = std::max (session.grid, std::ceil (span.getLength() / session.grid - 1.0e-6) * session.grid);
        std::vector<int> created;
        session.apply ("Duplicate notes", [&] (NoteClip& clip)
        {
            created = ClipTools::duplicate (clip, uids, offset);
            ensureClipCovers (clip, span.getEnd() + offset);
        });
        session.selection = { created.begin(), created.end() };
        roll.reveal ({ span.getStart() + offset, span.getEnd() + offset });
        showMessage ("Duplicated " + count (uids.size()) + (ctx.processor.getClip().lengthBeats > span.getEnd() + offset - 1.0e-6 ? "" : "; clip extended"));
        refresh();
    }

    void showMessage (const juce::String& m)
    {
        message = m;
        messageUntil = juce::Time::getMillisecondCounter() + 3500;
        repaint (statusArea);
    }

    void refresh()
    {
        roll.repaint();
        lane.repaint();
        updateStatus();
    }

    void syncLengthBox()
    {
        const double beats = ctx.processor.getClip().lengthBeats;
        for (int i = 0; i < 5; ++i)
            if (std::abs (beats - kLengthChoices[i]) < 1.0e-6)
                length.setSelectedId (i + 1, juce::dontSendNotification);
    }

    void updateStatus()
    {
        const auto& clip = ctx.processor.getClip();
        status = juce::String ((int) clip.notes.size()) + " notes  " + juce::String ((int) session.selection.size()) + " selected  "
               + juce::String (clip.lengthBeats / 4.0, 0) + " bars";
        repaint (statusArea);
    }

    void editorTick() override
    {
        const bool recording = ctx.telemetry.clipRecording.load() || ctx.processor.isRecorderActive();
        const bool playing = ctx.telemetry.clipPlaying.load();
        const bool hostDriven = sync.getToggleState() && ctx.telemetry.hostTransportAvailable.load();
        record.setToggleState (recording, juce::dontSendNotification);
        record.setButtonText (recording ? "STOP REC" : "REC");
        play.setToggleState (playing && ! recording, juce::dontSendNotification);
        play.setEnabled (! hostDriven);
        stop.setEnabled (! hostDriven || recording);
        play.setTooltip (hostDriven ? "Following your DAW: press play in the DAW (turn HOST SYNC off to use PLAY)" : "Play the clip (Space)");
        sync.setTooltip (ctx.telemetry.hostTransportAvailable.load() ? "Follow the DAW transport: the clip plays with the song, locked to its bars"
                                                                     : "Follows a DAW transport when NeddPE runs as a plugin (the standalone app has none)");

        if (juce::Time::getMillisecondCounter() < messageUntil + 100)
            repaint (statusArea);

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
    juce::Rectangle<int> toolbarArea, editArea, statusArea;
    juce::String status, message;
    juce::uint32 messageUntil = 0;
    int lastVersion = -1;
};

std::unique_ptr<Page> createNoteEditorPage (EditorContext& ctx) { return std::make_unique<NoteEditorPage> (ctx); }

} // namespace nedd::ui
