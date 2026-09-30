#pragma once

#include "Voice.h"

namespace nedd
{
/**
    Owns the voices and decides which voice plays which note.

    Poly: one voice per note. When the polyphony limit is reached the quietest released voice
    is stolen, otherwise the oldest held one. A stolen voice fades out over 3 ms on one of the
    spare physical voices while the new note starts on a clean voice, and it stops following
    its note id immediately — so later expression for the stolen note can never leak into
    whichever note replaced it.

    Mono / Legato: a note stack with last-note priority. Legato keeps envelopes running and
    glides; Mono retriggers. The voice follows the expression of the note currently on top.
*/
class VoiceManager
{
public:
    void prepare (float sampleRate);
    void reset();

    void setConfig (VoiceMode mode, int polyphony, GlideMode glideMode) noexcept;

    void noteOn (const NoteEvent& e, int soundingNote, const VoiceContext& ctx) noexcept;
    void noteOff (const NoteEvent& e) noexcept;
    void expression (const NoteEvent& e) noexcept;
    void allNotesOff (bool immediate) noexcept;

    void render (const VoiceContext& ctx, const VoiceBuses& buses, int startSample, int numSamples) noexcept;

    /** Every voice re-reads its settings before rendering again. */
    void forceControlUpdate() noexcept
    {
        for (auto& v : voices)
            v.forceControlUpdate();
    }

    int getNumActiveVoices() const noexcept;
    int getNumSoundingNotes() const noexcept;

    /** The most recently started voice that is still active, or -1. Used for global modulation and display. */
    int getFocusVoiceIndex() const noexcept;

    const Voice& getVoice (int index) const noexcept { return voices[(size_t) index]; }
    int64_t getSampleCounter() const noexcept { return sampleCounter; }
    void advanceSampleCounter (int numSamples) noexcept { sampleCounter += numSamples; }

private:
    struct HeldNote
    {
        uint32_t noteId = 0, exprId = 0;
        int noteNumber = 60, soundingNote = 60;
        float velocity = 0.8f, pitch = 0.0f, pressure = 0.0f, slide = 0.0f;
    };

    int findFreeVoice() noexcept;
    int chooseVictim() const noexcept;
    void monoNoteOn (const NoteEvent& e, int soundingNote, const VoiceContext& ctx) noexcept;
    void monoNoteOff (const NoteEvent& e) noexcept;

    std::array<Voice, (size_t) kPhysicalVoices> voices;
    std::array<HeldNote, 32> monoStack {};
    int monoStackSize = 0;
    int monoVoice = -1;

    VoiceMode mode = VoiceMode::Poly;
    GlideMode glideMode = GlideMode::Off;
    int polyphony = 16;
    float lastNotePitch = -1.0f;
    int64_t sampleCounter = 0;
    uint32_t seedCounter = 0x1234567u;
    const VoiceContext* lastContext = nullptr;
};

} // namespace nedd
