#pragma once

#include "DSP/Decimator.h"
#include "DSP/Envelope.h"
#include "DSP/VoiceFilter.h"
#include "MPE/NoteEvents.h"
#include "Modulation/Monitors.h"
#include "Oscillator.h"
#include "VoiceContext.h"

namespace nedd
{
/**
    Identity and expression of the note a voice is playing.

    Expression targets (pitchBend, pressure, timbre) are written by incoming events; the
    smoothed values are what the synthesis actually uses, so 7-bit controller steps never
    zipper. Nothing in here is shared between voices.
*/
struct MPEVoiceState
{
    uint32_t noteId = 0;          // the note that started this voice
    uint32_t exprId = 0;          // the note whose expression this voice follows
    NoteOrigin origin = NoteOrigin::Live;
    int noteNumber = 60;          // sounding key (after scale snapping)
    int midiChannel = 1;
    float velocity = 0.0f;
    float releaseVelocity = 0.0f; // 0 until the note is released

    float pitchBend = 0.0f;       // semitones, target
    float pressure = 0.0f;        // 0..1, target
    float timbre = 0.0f;          // 0..1 (MPE slide / CC74), target

    float smoothedPitch = 0.0f;
    float smoothedPressure = 0.0f;
    float smoothedTimbre = 0.0f;

    int64_t noteStartTime = 0;    // engine sample counter at note on
    bool gate = false;
    float noteRandom = 0.0f;      // bipolar random value fixed for the note's lifetime
};

class Voice
{
public:
    struct LegatoTarget
    {
        uint32_t noteId = 0, exprId = 0;
        int noteNumber = 60;
        float velocity = 0.8f;
        float pitch = 0.0f, pressure = 0.0f, slide = 0.0f;
    };

    void prepare (float sampleRate);

    void start (const NoteEvent& e, int soundingNote, const VoiceContext& ctx, bool glide, float glideFromPitch,
                int64_t startSample, uint32_t seed);

    /** Mono/legato note change without restarting the voice. */
    void legatoTo (const LegatoTarget& target, const VoiceContext& ctx, bool retrigger, bool glide);

    void release (float releaseVelocity);

    /** Very short fade-out used when the voice is stolen; the voice stops following its note at once. */
    void beginSteal();

    void kill();

    void setExpression (ExprDim dim, float value) noexcept;

    void render (const VoiceContext& ctx, const VoiceBuses& buses, int startSample, int numSamples) noexcept;

    /** Makes the next render start with a control update (used when oscillator content is replaced). */
    void forceControlUpdate() noexcept { samplesUntilControl = 0; }

    /** Oscillator oversampling factor chosen for the current note (1, 2 or 4). */
    int getOversampling() const noexcept { return oversampling; }

    bool isActive() const noexcept { return active; }
    bool isGated() const noexcept { return active && state.gate && ! stealing; }
    bool isStealing() const noexcept { return stealing; }
    bool isReleasing() const noexcept { return active && ! state.gate && ! stealing; }

    const MPEVoiceState& getState() const noexcept { return state; }
    float getAmpEnvelope() const noexcept { return ampEnv.getValue(); }
    float getBasePitch() const noexcept { return basePitch; }
    float getSmoothedMorph() const noexcept { return morphPosition; }
    float getCutoffHz() const noexcept { return cutoffHz; }
    float getRemainingStealGain() const noexcept { return stealGain; }

    const std::array<float, (size_t) kNumModSources>& getSources() const noexcept { return sources; }
    const std::array<float, (size_t) kNumModDests>& getDestMods() const noexcept { return dest; }
    const std::array<float, (size_t) kNumModSlots>& getSlotContributions() const noexcept { return slotContribution; }

    void writeTelemetry (VoiceTelemetry& t) const noexcept;

private:
    void updateControl (const VoiceContext& ctx) noexcept;
    void renderSamples (const VoiceBuses& buses, int start, int num) noexcept;
    const ParamSnapshot& computeMorph (const VoiceContext& ctx) noexcept;
    float tunedPitch (const VoiceContext& ctx, int note) const noexcept;
    static int chooseOversampling (const VoiceContext& ctx) noexcept;
    void renderOscillators (float& filterL, float& filterR, float& directL, float& directR) noexcept;

    MPEVoiceState state;
    float sampleRate = 44100.0f;
    bool active = false;
    bool stealing = false;
    bool firstBlock = true;
    int samplesUntilControl = 0;

    std::array<Oscillator, (size_t) kNumOscillators> oscillators;
    int oversampling = 1;
    bool anyDirect = false;
    std::array<dsp::OversamplingDecimator, 4> decimators;   // filter L/R, direct L/R
    dsp::VoiceFilter filter;
    dsp::Envelope ampEnv, filterEnv, modEnv;
    std::array<dsp::Lfo, (size_t) kNumLfos> lfos;
    dsp::Random32 rng;

    std::array<float, (size_t) kNumModSources> sources {};
    std::array<float, (size_t) kNumModDests> dest {};
    std::array<float, (size_t) kNumModSlots> slotContribution {};
    ParamSnapshot morphParams;
    float morphPosition = 0.0f;

    // Per-block derived values
    float basePitch = 60.0f;
    float glideOffset = 0.0f;
    float cutoffHz = 1000.0f;
    float sampleHoldPhase = 0.0f, sampleHoldValue = 0.0f;

    std::array<bool, (size_t) kNumOscillators> oscOn {};
    std::array<bool, (size_t) kNumOscillators> oscDirect {};
    std::array<bool, (size_t) kNumOscillators> oscSync {};
    std::array<int, (size_t) kNumOscillators> fmSource {};
    std::array<float, (size_t) kNumOscillators> fmDepth {};
    std::array<float, (size_t) kNumOscillators> ringAmount {};
    std::array<float, (size_t) kNumOscillators> levelCur {}, levelStep {};
    std::array<float, (size_t) kNumOscillators> lastMono {};
    std::array<bool, (size_t) kNumOscillators> lastWrapped {};
    std::array<float, (size_t) kNumOscillators> lastWrapFraction {};

    bool filterOn = true;
    float filterMixCur = 1.0f, filterMixStep = 0.0f;
    float gainCur = 0.0f, gainStep = 0.0f;
    float panLCur = 0.7071f, panLStep = 0.0f, panRCur = 0.7071f, panRStep = 0.0f;
    float delaySendCur = 0.0f, delaySendStep = 0.0f;
    float reverbSendCur = 0.0f, reverbSendStep = 0.0f;
    float stealGain = 1.0f, stealStep = 0.0f;
};

/** Indices of parameters that are morphed per voice (continuous and discrete sound parameters). */
const std::vector<int>& getVoiceMorphParams();
/** Indices of global (effects) parameters that are morphed once per block. */
const std::vector<int>& getGlobalMorphParams();

} // namespace nedd
