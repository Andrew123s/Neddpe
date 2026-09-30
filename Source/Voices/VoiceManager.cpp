#include "VoiceManager.h"

namespace nedd
{
void VoiceManager::prepare (float sampleRate)
{
    for (auto& v : voices)
        v.prepare (sampleRate);
    reset();
}

void VoiceManager::reset()
{
    for (auto& v : voices)
        v.kill();
    monoStackSize = 0;
    monoVoice = -1;
    lastNotePitch = -1.0f;
}

void VoiceManager::setConfig (VoiceMode newMode, int newPolyphony, GlideMode newGlideMode) noexcept
{
    if (newMode != mode)
    {
        allNotesOff (false);
        monoStackSize = 0;
        monoVoice = -1;
        mode = newMode;
    }
    polyphony = juce::jlimit (1, kMaxVoices, newPolyphony);
    glideMode = newGlideMode;
}

int VoiceManager::findFreeVoice() noexcept
{
    for (int i = 0; i < kPhysicalVoices; ++i)
        if (! voices[(size_t) i].isActive())
            return i;

    // Every physical voice is busy (many stolen voices still fading): cut the quietest fade.
    int best = -1;
    float quietest = 2.0f;
    for (int i = 0; i < kPhysicalVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (v.isStealing() && v.getRemainingStealGain() < quietest)
        {
            quietest = v.getRemainingStealGain();
            best = i;
        }
    }

    if (best < 0)
        best = chooseVictim();
    if (best >= 0)
        voices[(size_t) best].kill();
    return best;
}

int VoiceManager::chooseVictim() const noexcept
{
    int releasedVictim = -1, heldVictim = -1;
    float quietestRelease = 2.0f;
    int64_t oldestStart = std::numeric_limits<int64_t>::max();

    for (int i = 0; i < kPhysicalVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (! v.isActive() || v.isStealing())
            continue;

        if (v.isReleasing())
        {
            if (v.getAmpEnvelope() < quietestRelease)
            {
                quietestRelease = v.getAmpEnvelope();
                releasedVictim = i;
            }
        }
        else if (v.getState().noteStartTime < oldestStart)
        {
            oldestStart = v.getState().noteStartTime;
            heldVictim = i;
        }
    }

    return releasedVictim >= 0 ? releasedVictim : heldVictim;
}

int VoiceManager::getNumSoundingNotes() const noexcept
{
    int count = 0;
    for (const auto& v : voices)
        if (v.isActive() && ! v.isStealing())
            ++count;
    return count;
}

int VoiceManager::getNumActiveVoices() const noexcept
{
    int count = 0;
    for (const auto& v : voices)
        if (v.isActive())
            ++count;
    return count;
}

int VoiceManager::getFocusVoiceIndex() const noexcept
{
    int best = -1;
    int64_t newest = -1;
    for (int i = 0; i < kPhysicalVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (v.isActive() && ! v.isStealing() && v.getState().noteStartTime >= newest)
        {
            newest = v.getState().noteStartTime;
            best = i;
        }
    }
    return best;
}

void VoiceManager::noteOn (const NoteEvent& e, int soundingNote, const VoiceContext& ctx) noexcept
{
    lastContext = &ctx;

    if (mode != VoiceMode::Poly)
    {
        monoNoteOn (e, soundingNote, ctx);
        return;
    }

    if (getNumSoundingNotes() >= polyphony)
    {
        const int victim = chooseVictim();
        if (victim >= 0)
            voices[(size_t) victim].beginSteal();
    }

    const int index = findFreeVoice();
    if (index < 0)
        return;

    bool anyHeld = false;
    for (const auto& v : voices)
        anyHeld = anyHeld || v.isGated();

    const bool glide = lastNotePitch >= 0.0f
                    && (glideMode == GlideMode::Always || (glideMode == GlideMode::Legato && anyHeld));

    auto& voice = voices[(size_t) index];
    voice.start (e, soundingNote, ctx, glide, lastNotePitch, sampleCounter + e.sampleOffset, ++seedCounter * 2654435761u);
    lastNotePitch = (float) soundingNote;
}

void VoiceManager::monoNoteOn (const NoteEvent& e, int soundingNote, const VoiceContext& ctx) noexcept
{
    HeldNote held;
    held.noteId = e.noteId;
    held.exprId = e.exprId;
    held.noteNumber = e.noteNumber;
    held.soundingNote = soundingNote;
    held.velocity = e.value;
    held.pitch = e.pitch;
    held.pressure = e.pressure;
    held.slide = e.slide;

    if (monoStackSize == (int) monoStack.size())
    {
        std::move (monoStack.begin() + 1, monoStack.end(), monoStack.begin());
        --monoStackSize;
    }
    monoStack[(size_t) monoStackSize++] = held;

    const bool voiceHeld = monoVoice >= 0 && voices[(size_t) monoVoice].isGated();
    const bool glide = glideMode == GlideMode::Always || (glideMode == GlideMode::Legato && voiceHeld);

    if (voiceHeld)
    {
        Voice::LegatoTarget t { held.noteId, held.exprId, soundingNote, held.velocity, held.pitch, held.pressure, held.slide };
        voices[(size_t) monoVoice].legatoTo (t, ctx, mode == VoiceMode::Mono, glide);
        lastNotePitch = (float) soundingNote;
        return;
    }

    const float glideFrom = monoVoice >= 0 && voices[(size_t) monoVoice].isActive()
                                ? voices[(size_t) monoVoice].getBasePitch()
                                : lastNotePitch;

    if (monoVoice >= 0 && voices[(size_t) monoVoice].isActive())
        voices[(size_t) monoVoice].beginSteal();

    monoVoice = findFreeVoice();
    if (monoVoice < 0)
        return;

    voices[(size_t) monoVoice].start (e, soundingNote, ctx, glideMode == GlideMode::Always && glideFrom >= 0.0f, glideFrom,
                                      sampleCounter + e.sampleOffset, ++seedCounter * 2654435761u);
    lastNotePitch = (float) soundingNote;
}

void VoiceManager::monoNoteOff (const NoteEvent& e) noexcept
{
    int found = -1;
    for (int i = 0; i < monoStackSize; ++i)
        if (monoStack[(size_t) i].noteId == e.noteId)
            found = i;

    if (found < 0)
        return;

    const bool wasTop = found == monoStackSize - 1;
    std::move (monoStack.begin() + found + 1, monoStack.begin() + monoStackSize, monoStack.begin() + found);
    --monoStackSize;

    if (! wasTop || monoVoice < 0)
        return;

    auto& voice = voices[(size_t) monoVoice];

    if (monoStackSize > 0 && lastContext != nullptr)
    {
        // Fall back to the previous held note, legato.
        const auto& h = monoStack[(size_t) monoStackSize - 1];
        Voice::LegatoTarget t { h.noteId, h.exprId, h.soundingNote, h.velocity, h.pitch, h.pressure, h.slide };
        voice.legatoTo (t, *lastContext, false, glideMode != GlideMode::Off);
        lastNotePitch = (float) h.soundingNote;
    }
    else
    {
        voice.release (e.value);
    }
}

void VoiceManager::noteOff (const NoteEvent& e) noexcept
{
    if (mode != VoiceMode::Poly)
    {
        monoNoteOff (e);
        return;
    }

    for (auto& v : voices)
        if (v.isActive() && v.getState().noteId == e.noteId && v.getState().gate)
            v.release (e.value);
}

void VoiceManager::expression (const NoteEvent& e) noexcept
{
    // Only voices following this note id respond: this is the heart of per-note MPE.
    for (auto& v : voices)
        if (v.isActive() && ! v.isStealing() && v.getState().exprId == e.noteId)
            v.setExpression (e.dim, e.value);

    for (int i = 0; i < monoStackSize; ++i)
    {
        auto& h = monoStack[(size_t) i];
        if (h.exprId != e.noteId)
            continue;
        switch (e.dim)
        {
            case ExprDim::Pitch:    h.pitch = e.value; break;
            case ExprDim::Pressure: h.pressure = e.value; break;
            case ExprDim::Slide:    h.slide = e.value; break;
        }
    }
}

void VoiceManager::allNotesOff (bool immediate) noexcept
{
    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        if (immediate) v.beginSteal();
        else           v.release (0.5f);
    }
    monoStackSize = 0;
}

void VoiceManager::render (const VoiceContext& ctx, const VoiceBuses& buses, int startSample, int numSamples) noexcept
{
    lastContext = &ctx;
    for (auto& v : voices)
        v.render (ctx, buses, startSample, numSamples);
}

} // namespace nedd
