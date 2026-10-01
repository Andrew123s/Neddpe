#include "ExpressionKeyboard.h"

namespace nedd::ui
{
namespace
{
    constexpr int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
}

ExpressionKeyboard::ExpressionKeyboard (EditorContext& c) : ctx (c)
{
    ctx.addListener (this);
    setTooltip ("Play here without an MPE controller: drag sideways to bend a single note, drag up/down for slide, "
                "use the mouse wheel while holding for pressure. Multi-touch screens play polyphonically.");
}

ExpressionKeyboard::~ExpressionKeyboard()
{
    for (auto& t : touches)
        if (t.active)
            send (juce::MidiMessage::noteOff (t.channel, t.note, 0.5f));
    ctx.removeListener (this);
}

bool ExpressionKeyboard::isBlack (int note) const
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void ExpressionKeyboard::resized()
{
    whiteWidth = (float) getWidth() / (float) numWhiteKeys;
}

juce::Rectangle<float> ExpressionKeyboard::keyBounds (int note) const
{
    const int octave = (note - lowestNote) / 12;
    const int pc = note % 12;
    int whiteIndex = 0;
    for (int i = 0; i < 7; ++i)
        if (kWhiteOffsets[i] <= pc)
            whiteIndex = i;

    const float x = (float) (octave * 7 + whiteIndex) * whiteWidth;
    const float h = (float) getHeight();

    if (! isBlack (note))
        return { x, 0.0f, whiteWidth, h };

    return { x + whiteWidth * 0.68f, 0.0f, whiteWidth * 0.64f, h * 0.6f };
}

int ExpressionKeyboard::noteAt (juce::Point<float> p) const
{
    const int highest = lowestNote + (numWhiteKeys / 7) * 12 + 12;
    for (int n = lowestNote; n <= highest && n < 128; ++n)
        if (isBlack (n) && keyBounds (n).contains (p))
            return n;
    for (int n = lowestNote; n <= highest && n < 128; ++n)
        if (! isBlack (n) && keyBounds (n).contains (p))
            return n;
    return -1;
}

void ExpressionKeyboard::editorTick()
{
    std::array<float, 128> now {};
    for (int v = 0; v < kPhysicalVoices; ++v)
    {
        const auto& t = ctx.telemetry.voices[(size_t) v];
        if (! t.active.load (std::memory_order_relaxed))
            continue;
        const int note = juce::jlimit (0, 127, t.noteNumber.load (std::memory_order_relaxed));
        now[(size_t) note] = std::max (now[(size_t) note], t.ampEnv.load (std::memory_order_relaxed));
        soundingColour[(size_t) note] = noteColour (t.noteId.load (std::memory_order_relaxed), note);
    }

    if (now != sounding)
    {
        sounding = now;
        repaint();
    }
}

void ExpressionKeyboard::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const int highest = lowestNote + numWhiteKeys / 7 * 12 + 12;

    auto drawKey = [&] (int n)
    {
        const auto r = keyBounds (n).reduced (0.5f, 0.0f);
        const bool black = isBlack (n);
        bool pressed = false;
        for (const auto& t : touches)
            pressed = pressed || (t.active && t.note == n);

        if (black)
            g.setGradientFill (juce::ColourGradient (colours::keyBlack.brighter (0.15f), r.getX(), r.getY(), colours::keyBlack.darker (0.08f), r.getX(), r.getBottom(), false));
        else
            g.setGradientFill (juce::ColourGradient (colours::keyWhite, r.getX(), r.getY(), colours::control, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 4.0f);

        const float level = sounding[(size_t) n];
        if (level > 0.001f || pressed)
        {
            const auto c = pressed ? colours::accent : soundingColour[(size_t) n];
            g.setColour (c.withAlpha (black ? 0.55f + 0.4f * level : 0.35f + 0.5f * level));
            g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * 0.35f), 4.0f);
        }

        g.setColour (black ? colours::keyBlack.darker (0.15f) : colours::outlineStrong);
        g.drawRoundedRectangle (r.withTrimmedTop (-4.0f), 4.0f, 1.0f);

        if (! black && n % 12 == 0)
        {
            g.setColour (colours::textDim);
            g.setFont (font (9.5f));
            g.drawText (noteName (n), r.withTop (r.getBottom() - 13.0f), juce::Justification::centred);
        }
    };

    for (int n = lowestNote; n < highest && n < 128; ++n)
        if (! isBlack (n)) drawKey (n);
    for (int n = lowestNote; n < highest && n < 128; ++n)
        if (isBlack (n)) drawKey (n);
}

int ExpressionKeyboard::allocateChannel()
{
    // Least recently used member channel that no held touch occupies.
    int best = 2;
    uint32_t oldest = std::numeric_limits<uint32_t>::max();
    for (int ch = 2; ch <= 16; ++ch)
    {
        bool used = false;
        for (const auto& t : touches)
            used = used || (t.active && t.channel == ch);
        if (! used && channelAge[(size_t) ch] < oldest)
        {
            oldest = channelAge[(size_t) ch];
            best = ch;
        }
    }
    channelAge[(size_t) best] = ++ageCounter;
    return best;
}

void ExpressionKeyboard::send (const juce::MidiMessage& m)
{
    UiMidiMessage msg;
    msg.size = std::min (3, m.getRawDataSize());
    std::memcpy (msg.bytes, m.getRawData(), (size_t) msg.size);
    ctx.processor.getShared().uiMidi.push (msg);
}

ExpressionKeyboard::Touch* ExpressionKeyboard::touchFor (int source)
{
    for (auto& t : touches)
        if (t.active && t.source == source)
            return &t;
    return nullptr;
}

float ExpressionKeyboard::slideFor (float y) const
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - y / (float) getHeight());
}

void ExpressionKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int note = noteAt (e.position);
    if (note < 0)
        return;

    Touch* slot = nullptr;
    for (auto& t : touches)
        if (! t.active) { slot = &t; break; }
    if (slot == nullptr)
        return;

    const auto key = keyBounds (note);
    const float velocity = juce::jlimit (0.2f, 1.0f, 0.3f + 0.75f * (e.position.y - key.getY()) / key.getHeight());

    slot->active = true;
    slot->source = e.source.getIndex();
    slot->note = note;
    slot->channel = allocateChannel();
    slot->startX = e.position.x;
    slot->pressure = velocity * 0.6f;

    // MPE order: expression first, then the note.
    send (juce::MidiMessage::pitchWheel (slot->channel, 8192));
    send (juce::MidiMessage::controllerEvent (slot->channel, 74, juce::roundToInt (slideFor (e.position.y) * 127.0f)));
    send (juce::MidiMessage::channelPressureChange (slot->channel, juce::roundToInt (slot->pressure * 127.0f)));
    send (juce::MidiMessage::noteOn (slot->channel, note, velocity));
    repaint();
}

void ExpressionKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    auto* t = touchFor (e.source.getIndex());
    if (t == nullptr)
        return;

    const float semitoneWidth = whiteWidth * 7.0f / 12.0f;
    const float bendSemis = (e.position.x - t->startX) / semitoneWidth;
    const float range = std::max (1.0f, ctx.param (pid::global (GlobalField::MpeBendRange)));
    const int bend = juce::jlimit (0, 16383, juce::roundToInt (8192.0f + bendSemis / range * 8191.0f));

    send (juce::MidiMessage::pitchWheel (t->channel, bend));
    send (juce::MidiMessage::controllerEvent (t->channel, 74, juce::roundToInt (slideFor (e.position.y) * 127.0f)));
}

void ExpressionKeyboard::mouseUp (const juce::MouseEvent& e)
{
    if (auto* t = touchFor (e.source.getIndex()))
    {
        send (juce::MidiMessage::noteOff (t->channel, t->note, 0.5f));
        send (juce::MidiMessage::pitchWheel (t->channel, 8192));
        t->active = false;
        repaint();
    }
}

void ExpressionKeyboard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto* t = touchFor (e.source.getIndex());
    if (t == nullptr)
    {
        // Not holding a note: scroll the keyboard by octaves.
        const int direction = wheel.deltaY > 0 ? 12 : -12;
        setLowestNote (juce::jlimit (12, 84, lowestNote + direction));
        return;
    }
    t->pressure = juce::jlimit (0.0f, 1.0f, t->pressure + wheel.deltaY * 0.5f);
    send (juce::MidiMessage::channelPressureChange (t->channel, juce::roundToInt (t->pressure * 127.0f)));
}

} // namespace nedd::ui
