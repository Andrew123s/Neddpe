#include "Voice.h"

namespace nedd
{
namespace
{
    constexpr float kCrossFmDepth = 1.5f;       // cycles of phase modulation at 100% cross-FM
    constexpr float kFmIndexScale = 2.0f;       // cycles of phase modulation at 100% operator amount
    constexpr float kFilterEnvRange = 96.0f;    // semitones at 100% filter envelope amount
    constexpr float kStealFadeSeconds = 0.003f;
    constexpr float kMinAmpAttack = 0.0005f;
    constexpr float kMinAmpRelease = 0.002f;

    inline float destValue (const std::array<float, (size_t) kNumModDests>& d, ModDest which) noexcept
    {
        return d[(size_t) which];
    }

    inline float smoothingCoefficient (float seconds, float samples, float sampleRate) noexcept
    {
        if (seconds <= 0.0f)
            return 1.0f;
        return 1.0f - std::exp (-samples / (seconds * sampleRate));
    }

    std::vector<int> collectMorphParams (bool perVoice)
    {
        std::vector<int> result;
        for (const auto& d : getParamDefs())
            if (d.morphable && d.perVoice == perVoice)
                result.push_back (d.index);
        return result;
    }
} // namespace

const std::vector<int>& getVoiceMorphParams()
{
    static const std::vector<int> params = collectMorphParams (true);
    return params;
}

const std::vector<int>& getGlobalMorphParams()
{
    static const std::vector<int> params = collectMorphParams (false);
    return params;
}

void Voice::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& o : oscillators)
        o.prepare (sampleRate);
    filter.prepare (sampleRate);
    ampEnv.setSampleRate (sampleRate);
    filterEnv.setSampleRate (sampleRate);
    modEnv.setSampleRate (sampleRate);
    morphParams.setToDefaults();
    (void) getVoiceMorphParams();   // build the static list off the audio thread
    kill();
}

float Voice::tunedPitch (const VoiceContext& ctx, int note) const noexcept
{
    note = juce::jlimit (0, 127, note);
    if (ctx.customTuning && ctx.tuning != nullptr)
        return ctx.tuning->pitch[(size_t) note];
    return (float) note;
}

void Voice::start (const NoteEvent& e, int soundingNote, const VoiceContext& ctx, bool glide, float glideFromPitch,
                   int64_t startSample, uint32_t seed)
{
    const auto& p = *ctx.params;

    state = {};
    state.noteId = e.noteId;
    state.exprId = e.exprId;
    state.origin = e.origin;
    state.noteNumber = soundingNote;
    state.midiChannel = e.channel;
    state.velocity = dsp::clamp01 (e.value);
    state.pitchBend = state.smoothedPitch = e.pitch;
    state.pressure = state.smoothedPressure = dsp::clamp01 (e.pressure);
    state.timbre = state.smoothedTimbre = dsp::clamp01 (e.slide);
    state.noteStartTime = startSample;
    state.gate = true;

    rng.seed (seed);
    state.noteRandom = rng.nextBipolar();

    for (int o = 0; o < kNumOscillators; ++o)
        oscillators[(size_t) o].noteOn (p[pid::osc (o, OscField::Phase)], p[pid::osc (o, OscField::PhaseRandom)], rng);

    filter.reset();
    ampEnv.reset();
    filterEnv.reset();
    modEnv.reset();
    ampEnv.noteOn (true);
    filterEnv.noteOn (true);
    modEnv.noteOn (true);

    for (int l = 0; l < kNumLfos; ++l)
    {
        const bool retrig = p.getBool (pid::lfo (l, LfoField::Retrigger));
        lfos[(size_t) l].reset (retrig ? 0.0f : ctx.globalLfoPhase[(size_t) l], rng.next());
    }

    sampleHoldPhase = 0.0f;
    sampleHoldValue = rng.nextBipolar();

    const float target = tunedPitch (ctx, soundingNote);
    glideOffset = glide ? glideFromPitch - target : 0.0f;
    basePitch = target + glideOffset;

    dest.fill (0.0f);
    slotContribution.fill (0.0f);
    lastMono.fill (0.0f);
    lastWrapped.fill (false);
    lastWrapFraction.fill (0.0f);

    active = true;
    stealing = false;
    stealGain = 1.0f;
    stealStep = 0.0f;
    firstBlock = true;
    samplesUntilControl = 0;
}

void Voice::legatoTo (const LegatoTarget& t, const VoiceContext& ctx, bool retrigger, bool glide)
{
    const float previousPitch = basePitch;

    state.noteId = t.noteId;
    state.exprId = t.exprId;
    state.noteNumber = t.noteNumber;
    state.pitchBend = t.pitch;
    state.pressure = dsp::clamp01 (t.pressure);
    state.timbre = dsp::clamp01 (t.slide);
    state.gate = true;
    state.releaseVelocity = 0.0f;

    if (retrigger)
    {
        state.velocity = dsp::clamp01 (t.velocity);
        ampEnv.noteOn (false);
        filterEnv.noteOn (false);
        modEnv.noteOn (false);

        const auto& p = *ctx.params;
        for (int l = 0; l < kNumLfos; ++l)
            if (p.getBool (pid::lfo (l, LfoField::Retrigger)))
                lfos[(size_t) l].reset (0.0f, rng.next());
    }

    const float target = tunedPitch (ctx, t.noteNumber);
    glideOffset = glide ? previousPitch - target : 0.0f;
    basePitch = target + glideOffset;
}

void Voice::release (float releaseVelocity)
{
    if (! active || stealing)
        return;
    state.gate = false;
    state.releaseVelocity = dsp::clamp01 (releaseVelocity);
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();
}

void Voice::beginSteal()
{
    if (! active)
        return;
    stealing = true;
    state.gate = false;
    state.noteId = 0;
    state.exprId = 0;
    stealStep = 1.0f / std::max (1.0f, kStealFadeSeconds * sampleRate);
}

void Voice::kill()
{
    active = false;
    stealing = false;
    state = {};
    ampEnv.reset();
    filterEnv.reset();
    modEnv.reset();
    filter.reset();
}

void Voice::setExpression (ExprDim dim, float value) noexcept
{
    switch (dim)
    {
        case ExprDim::Pitch:    state.pitchBend = value; break;
        case ExprDim::Pressure: state.pressure = dsp::clamp01 (value); break;
        case ExprDim::Slide:    state.timbre = dsp::clamp01 (value); break;
    }
}

const ParamSnapshot& Voice::computeMorph (const VoiceContext& ctx) noexcept
{
    const auto* morph = ctx.morph;
    if (morph == nullptr || ! morph->enabled)
    {
        morphPosition = 0.0f;
        return *ctx.params;
    }

    morphPosition = dsp::clamp01 (morph->position + destValue (dest, ModDest::Morph));
    morphParams = *ctx.params;

    // A is the live patch, B the stored target. Continuous parameters interpolate in the
    // normalised (perceptual) domain; discrete ones switch at the midpoint.
    for (int index : getVoiceMorphParams())
    {
        const auto& def = getParamDef (index);
        if (def.type == ParamType::Float)
        {
            const float n = dsp::lerp (morph->liveNormalised[(size_t) index], morph->targetNormalised[(size_t) index], morphPosition);
            morphParams[index] = def.range.convertFrom0to1 (n);
        }
        else if (morphPosition >= 0.5f)
        {
            morphParams[index] = morph->targetPlain[index];
        }
    }

    return morphParams;
}

void Voice::updateControl (const VoiceContext& ctx) noexcept
{
    const auto& live = *ctx.params;
    const float blockSamples = (float) ctx.controlBlockSize;
    const float blockSeconds = blockSamples / sampleRate;

    // ---- expression smoothing ----
    const float smoothSeconds = live[pid::global (GlobalField::ExpressionSmoothing)] * 0.001f;
    const float exprCoeff = smoothingCoefficient (smoothSeconds, blockSamples, sampleRate);
    const float pitchCoeff = smoothingCoefficient (smoothSeconds * 0.5f, blockSamples, sampleRate);

    float bendTarget = state.pitchBend;
    const float bendQuantize = live[pid::global (GlobalField::BendQuantize)];
    if (bendQuantize > 0.0f && ctx.scaleActive)
    {
        const float sounding = (float) state.noteNumber + bendTarget;
        const float nearest = Scales::nearestScalePitch (sounding, ctx.scaleMask, ctx.scaleRoot);
        bendTarget = dsp::lerp (bendTarget, nearest - (float) state.noteNumber, bendQuantize);
    }

    if (firstBlock)
    {
        state.smoothedPitch = bendTarget;
        state.smoothedPressure = state.pressure;
        state.smoothedTimbre = state.timbre;
    }
    else
    {
        state.smoothedPitch += pitchCoeff * (bendTarget - state.smoothedPitch);
        state.smoothedPressure += exprCoeff * (state.pressure - state.smoothedPressure);
        state.smoothedTimbre += exprCoeff * (state.timbre - state.smoothedTimbre);
    }

    // ---- glide ----
    if (std::abs (glideOffset) > 1.0e-4f)
    {
        const float glideTime = std::max (1.0e-4f, live[pid::global (GlobalField::Glide)]);
        glideOffset *= std::exp (-blockSamples / (glideTime / 3.0f * sampleRate));
    }
    else
    {
        glideOffset = 0.0f;
    }
    basePitch = tunedPitch (ctx, state.noteNumber) + glideOffset;

    // ---- modulation sources ----
    sources = ctx.globalSources;
    const float pitchSensitivity = live[pid::global (GlobalField::PitchSensitivity)];
    sources[(size_t) ModSource::MpePitch] = std::clamp (state.smoothedPitch * pitchSensitivity / 48.0f, -1.0f, 1.0f);
    sources[(size_t) ModSource::MpePressure] = dsp::responseCurve (state.smoothedPressure, live[pid::global (GlobalField::PressureCurve)]);
    sources[(size_t) ModSource::MpeSlide] = dsp::responseCurve (state.smoothedTimbre, live[pid::global (GlobalField::SlideCurve)]);
    sources[(size_t) ModSource::Velocity] = dsp::responseCurve (state.velocity, live[pid::global (GlobalField::VelocityCurve)]);
    sources[(size_t) ModSource::ReleaseVelocity] = state.releaseVelocity;

    for (int l = 0; l < kNumLfos; ++l)
    {
        dsp::Lfo::Settings s;
        s.shape = live.getChoice<LfoShape> (pid::lfo (l, LfoField::Shape));
        s.phaseOffset = live[pid::lfo (l, LfoField::Phase)];
        s.fadeSeconds = live[pid::lfo (l, LfoField::Fade)];
        s.customTable = ctx.lfoShapes != nullptr ? ctx.lfoShapes->tables[(size_t) l].data() : nullptr;

        auto& lfo = lfos[(size_t) l];
        const bool retrig = live.getBool (pid::lfo (l, LfoField::Retrigger));
        const bool sync = live.getBool (pid::lfo (l, LfoField::Sync));
        const auto rateDest = (ModDest) ((int) ModDest::Lfo1Rate + l);
        const auto depthDest = (ModDest) ((int) ModDest::Lfo1Depth + l);

        float value;
        if (sync && ! retrig)
        {
            lfo.setPhase (ctx.globalLfoPhase[(size_t) l]);    // locked to the host position
            value = lfo.advance (s, 0.0f, ctx.controlBlockSize, sampleRate);
        }
        else
        {
            const float rate = ctx.lfoRateHz[(size_t) l] * std::exp2 (destValue (dest, rateDest) * 4.0f);
            value = lfo.advance (s, rate, ctx.controlBlockSize, sampleRate);
        }

        const float depth = dsp::clamp01 (live[pid::lfo (l, LfoField::Amount)] + destValue (dest, depthDest));
        sources[(size_t) ModSource::Lfo1 + (size_t) l] = value * depth;
    }

    sources[(size_t) ModSource::AmpEnv] = ampEnv.getValue();
    sources[(size_t) ModSource::FilterEnv] = filterEnv.processBlock (ctx.controlBlockSize);
    sources[(size_t) ModSource::ModEnv] = modEnv.processBlock (ctx.controlBlockSize);
    sources[(size_t) ModSource::Random] = state.noteRandom;

    sampleHoldPhase += ctx.sampleHoldRateHz * blockSeconds;
    if (sampleHoldPhase >= 1.0f)
    {
        sampleHoldPhase -= std::floor (sampleHoldPhase);
        sampleHoldValue = rng.nextBipolar();
    }
    sources[(size_t) ModSource::SampleHold] = sampleHoldValue;
    sources[(size_t) ModSource::KeyPosition] = std::clamp (((float) state.noteNumber - 60.0f) / 60.0f, -1.0f, 1.0f);
    sources[(size_t) ModSource::NoteNumber] = (float) state.noteNumber / 127.0f;
    sources[(size_t) ModSource::Gate] = state.gate ? 1.0f : 0.0f;

    // ---- matrix, then per-note morph ----
    slotContribution.fill (0.0f);
    evaluateModMatrix (*ctx.routing, sources.data(), dest.data(), ModScope::Voice, slotContribution.data());
    const auto& p = computeMorph (ctx);

    const float pitchMod = destValue (dest, ModDest::Pitch) * getModDestInfo (ModDest::Pitch).range;
    const float modEnvValue = sources[(size_t) ModSource::ModEnv];
    const float voicePitch = basePitch + pitchMod;

    // ---- oscillators ----
    for (int o = 0; o < kNumOscillators; ++o)
    {
        auto f = [o] (OscField field) { return pid::osc (o, field); };
        oscOn[(size_t) o] = p.getBool (f (OscField::On));

        const float levelTarget = oscOn[(size_t) o]
                                      ? std::clamp (p[f (OscField::Level)] + destValue (dest, oscDest (ModDest::Osc1Level, o)), 0.0f, 1.0f)
                                      : 0.0f;
        if (firstBlock)
            levelCur[(size_t) o] = levelTarget;
        levelStep[(size_t) o] = (levelTarget - levelCur[(size_t) o]) / blockSamples;

        if (! oscOn[(size_t) o])
            continue;

        OscillatorBlockParams bp;
        bp.engine = p.getChoice<OscEngine> (f (OscField::Engine));
        bp.wave = p.getChoice<AnalogWave> (f (OscField::Wave));
        bp.noise = p.getChoice<NoiseType> (f (OscField::NoiseType));
        bp.algorithm = p.getChoice<FmAlgorithm> (f (OscField::FmAlgorithm));
        bp.table = ctx.wavetables != nullptr ? &ctx.wavetables->get (p.getInt (f (OscField::Table))) : nullptr;

        const float oscPitch = voicePitch
                             + destValue (dest, oscDest (ModDest::Osc1Pitch, o)) * getModDestInfo (ModDest::Osc1Pitch).range
                             + 12.0f * (float) p.getInt (f (OscField::Octave))
                             + (float) p.getInt (f (OscField::Semi))
                             + p[f (OscField::Fine)] * 0.01f;
        bp.frequency = std::clamp (dsp::midiNoteToHz (oscPitch), 0.5f, sampleRate * 0.45f);
        bp.pulseWidth = std::clamp (p[f (OscField::PulseWidth)] + destValue (dest, oscDest (ModDest::Osc1Pw, o)) * 0.5f, 0.02f, 0.98f);
        bp.wtPosition = dsp::clamp01 (p[f (OscField::WtPos)] + destValue (dest, oscDest (ModDest::Osc1WtPos, o)));
        bp.unison = p.getInt (f (OscField::Unison));
        bp.detune = dsp::clamp01 (p[f (OscField::Detune)] + destValue (dest, ModDest::UnisonDetune));
        bp.spread = dsp::clamp01 (p[f (OscField::Spread)] + destValue (dest, ModDest::StereoWidth));
        bp.pan = p[f (OscField::Pan)];

        const float fmMod = destValue (dest, oscDest (ModDest::Osc1Fm, o));

        if (bp.engine == OscEngine::FM)
        {
            const float fine = p[f (OscField::OpFine)];
            bp.op1Ratio = std::max (0.0f, fmRatioValue (p.getInt (f (OscField::Op1Ratio))) + fine);
            bp.op2Ratio = std::max (0.0f, fmRatioValue (p.getInt (f (OscField::Op2Ratio))) + fine);

            const float envAmount = p[f (OscField::FmEnvAmount)];
            const float envScale = dsp::lerp (1.0f, modEnvValue, envAmount);
            const float keyScale = std::exp2 (-p[f (OscField::FmKeyTrack)] * ((float) state.noteNumber - 60.0f) / 24.0f);
            const float modScale = std::max (0.0f, 1.0f + 2.0f * fmMod);
            const float scale = kFmIndexScale * envScale * keyScale * modScale;

            const float a1 = p[f (OscField::Op1Amount)];
            const float a2 = p[f (OscField::Op2Amount)];
            bp.op1Index = a1 * a1 * scale;
            bp.op2Index = a2 * a2 * scale;
            bp.feedback = p[f (OscField::FmFeedback)];
        }

        oscillators[(size_t) o].setBlock (bp);

        const float crossFm = dsp::clamp01 (p[f (OscField::FmAmount)] + fmMod);
        fmDepth[(size_t) o] = crossFm * crossFm * kCrossFmDepth;
        fmSource[(size_t) o] = p.getInt (f (OscField::FmSource));
        ringAmount[(size_t) o] = p[f (OscField::Ring)];
        oscSync[(size_t) o] = p.getBool (f (OscField::Sync));
        oscDirect[(size_t) o] = p.getInt (f (OscField::Route)) == 1;
    }

    // ---- filter ----
    filterOn = p.getBool (pid::filter (FilterField::On));
    filter.setType (p.getChoice<FilterType> (pid::filter (FilterField::Type)));

    const float velocity = sources[(size_t) ModSource::Velocity];
    const float envVelScale = dsp::lerp (1.0f, velocity, p[pid::filter (FilterField::Velocity)]);
    const float envAmount = p[pid::filter (FilterField::EnvAmount)] + destValue (dest, ModDest::FilterEnvAmount);

    const float cutoffSemis = dsp::hzToMidiNote (p[pid::filter (FilterField::Cutoff)])
                            + p[pid::filter (FilterField::KeyTrack)] * (voicePitch - 60.0f)
                            + sources[(size_t) ModSource::FilterEnv] * envAmount * envVelScale * kFilterEnvRange
                            + destValue (dest, ModDest::FilterCutoff) * getModDestInfo (ModDest::FilterCutoff).range;
    cutoffHz = std::clamp (dsp::midiNoteToHz (cutoffSemis), 16.0f, 22000.0f);

    filter.setBlockTargets (cutoffHz,
                            dsp::clamp01 (p[pid::filter (FilterField::Resonance)] + destValue (dest, ModDest::FilterResonance)),
                            dsp::clamp01 (p[pid::filter (FilterField::Drive)] + destValue (dest, ModDest::FilterDrive)),
                            ctx.controlBlockSize);

    const float mixTarget = dsp::clamp01 (p[pid::filter (FilterField::Mix)] + destValue (dest, ModDest::FilterMix));
    if (firstBlock) filterMixCur = mixTarget;
    filterMixStep = (mixTarget - filterMixCur) / blockSamples;

    // ---- envelopes ----
    auto envSettings = [&p] (int e)
    {
        dsp::Envelope::Settings s;
        s.delay = p[pid::env (e, EnvField::Delay)];
        s.attack = p[pid::env (e, EnvField::Attack)];
        s.hold = p[pid::env (e, EnvField::Hold)];
        s.decay = p[pid::env (e, EnvField::Decay)];
        s.sustain = p[pid::env (e, EnvField::Sustain)];
        s.release = p[pid::env (e, EnvField::Release)];
        s.attackCurve = p[pid::env (e, EnvField::AttackCurve)];
        s.decayCurve = p[pid::env (e, EnvField::DecayCurve)];
        s.releaseCurve = p[pid::env (e, EnvField::ReleaseCurve)];
        return s;
    };

    auto amp = envSettings (pid::ampEnv);
    amp.attack = std::max (kMinAmpAttack, amp.attack * std::exp2 (destValue (dest, ModDest::AmpAttack) * 4.0f));
    amp.decay = std::max (0.001f, amp.decay * std::exp2 (destValue (dest, ModDest::AmpDecay) * 4.0f));
    amp.release = std::max (kMinAmpRelease, amp.release * std::exp2 (destValue (dest, ModDest::AmpRelease) * 4.0f));
    ampEnv.setSettings (amp);
    filterEnv.setSettings (envSettings (pid::filterEnv));
    modEnv.setSettings (envSettings (pid::modEnv));

    // ---- amp, pan, sends ----
    const float velGain = dsp::lerp (1.0f, velocity, p[pid::amp (AmpField::Velocity)]);
    const float pressureGain = dsp::lerp (1.0f, sources[(size_t) ModSource::MpePressure], p[pid::amp (AmpField::Pressure)]);
    const float level = std::clamp (p[pid::amp (AmpField::Level)] + destValue (dest, ModDest::AmpLevel), 0.0f, 1.5f);
    const float gainTarget = level * velGain * pressureGain;

    float panL = 0.0f, panR = 0.0f;
    dsp::panGains (p[pid::amp (AmpField::Pan)] + destValue (dest, ModDest::Pan), panL, panR);
    // Normalise so a centred voice keeps unity gain per channel.
    panL *= 1.4142136f;
    panR *= 1.4142136f;

    const float delaySend = dsp::clamp01 (p[pid::fx (FxField::DelaySend)] + destValue (dest, ModDest::DelaySend));
    const float reverbSend = dsp::clamp01 (p[pid::fx (FxField::ReverbSend)] + destValue (dest, ModDest::ReverbSend));

    if (firstBlock)
    {
        gainCur = gainTarget;
        panLCur = panL;
        panRCur = panR;
        delaySendCur = delaySend;
        reverbSendCur = reverbSend;
    }

    gainStep = (gainTarget - gainCur) / blockSamples;
    panLStep = (panL - panLCur) / blockSamples;
    panRStep = (panR - panRCur) / blockSamples;
    delaySendStep = (delaySend - delaySendCur) / blockSamples;
    reverbSendStep = (reverbSend - reverbSendCur) / blockSamples;

    firstBlock = false;
}

void Voice::renderSamples (const VoiceBuses& buses, int start, int num) noexcept
{
    for (int i = 0; i < num; ++i)
    {
        float filterL = 0.0f, filterR = 0.0f, directL = 0.0f, directR = 0.0f;
        std::array<float, (size_t) kNumOscillators> mono {};
        std::array<bool, (size_t) kNumOscillators> wrapped {};
        std::array<float, (size_t) kNumOscillators> wrapFraction {};

        for (int o = 0; o < kNumOscillators; ++o)
        {
            if (! oscOn[(size_t) o])
            {
                levelCur[(size_t) o] += levelStep[(size_t) o];
                continue;
            }

            // Sources earlier in the chain contribute this sample, later ones their previous sample.
            const int src = fmSource[(size_t) o];
            const float fmIn = src < o ? mono[(size_t) src] : lastMono[(size_t) src];
            const int prev = (o + kNumOscillators - 1) % kNumOscillators;

            float syncFraction = -1.0f;
            if (oscSync[(size_t) o])
            {
                if (prev < o && wrapped[(size_t) prev]) syncFraction = wrapFraction[(size_t) prev];
                else if (prev > o && lastWrapped[(size_t) prev]) syncFraction = lastWrapFraction[(size_t) prev];
            }

            auto out = oscillators[(size_t) o].tick (fmDepth[(size_t) o] * fmIn, syncFraction);

            if (ringAmount[(size_t) o] > 0.0f)
            {
                const float carrier = prev < o ? mono[(size_t) prev] : lastMono[(size_t) prev];
                const float ring = dsp::lerp (1.0f, carrier, ringAmount[(size_t) o]);
                out.left *= ring;
                out.right *= ring;
            }

            mono[(size_t) o] = out.mono;
            wrapped[(size_t) o] = out.wrapped;
            wrapFraction[(size_t) o] = out.wrapFraction;

            levelCur[(size_t) o] += levelStep[(size_t) o];
            const float level = levelCur[(size_t) o];

            if (oscDirect[(size_t) o])
            {
                directL += out.left * level;
                directR += out.right * level;
            }
            else
            {
                filterL += out.left * level;
                filterR += out.right * level;
            }
        }

        lastMono = mono;
        lastWrapped = wrapped;
        lastWrapFraction = wrapFraction;

        filterMixCur += filterMixStep;
        if (filterOn)
        {
            float wetL = filterL, wetR = filterR;
            filter.process (wetL, wetR);
            filterL += (wetL - filterL) * filterMixCur;
            filterR += (wetR - filterR) * filterMixCur;
        }

        gainCur += gainStep;
        panLCur += panLStep;
        panRCur += panRStep;
        delaySendCur += delaySendStep;
        reverbSendCur += reverbSendStep;

        float amp = ampEnv.process() * gainCur;
        if (stealing)
        {
            amp *= stealGain;
            stealGain = std::max (0.0f, stealGain - stealStep);
        }

        const float left = (filterL + directL) * amp * panLCur;
        const float right = (filterR + directR) * amp * panRCur;
        const int n = start + i;

        buses.mainL[n] += left;
        buses.mainR[n] += right;
        if (buses.delayL != nullptr)
        {
            buses.delayL[n] += left * delaySendCur;
            buses.delayR[n] += right * delaySendCur;
        }
        if (buses.reverbL != nullptr)
        {
            buses.reverbL[n] += left * reverbSendCur;
            buses.reverbR[n] += right * reverbSendCur;
        }
    }
}

void Voice::render (const VoiceContext& ctx, const VoiceBuses& buses, int startSample, int numSamples) noexcept
{
    if (! active)
        return;

    int pos = startSample;
    const int end = startSample + numSamples;

    while (pos < end)
    {
        if (samplesUntilControl <= 0)
        {
            updateControl (ctx);
            samplesUntilControl = ctx.controlBlockSize;
        }

        const int n = std::min (end - pos, samplesUntilControl);
        renderSamples (buses, pos, n);
        pos += n;
        samplesUntilControl -= n;

        if (! ampEnv.isActive() || (stealing && stealGain <= 0.0f))
        {
            kill();
            return;
        }
    }
}

void Voice::writeTelemetry (VoiceTelemetry& t) const noexcept
{
    t.active.store (active, std::memory_order_relaxed);
    if (! active)
        return;

    t.gate.store (state.gate, std::memory_order_relaxed);
    t.noteId.store (state.noteId, std::memory_order_relaxed);
    t.noteNumber.store (state.noteNumber, std::memory_order_relaxed);
    t.channel.store (state.midiChannel, std::memory_order_relaxed);
    t.origin.store ((int) state.origin, std::memory_order_relaxed);
    t.pitch.store (state.smoothedPitch, std::memory_order_relaxed);
    t.pressure.store (sources[(size_t) ModSource::MpePressure], std::memory_order_relaxed);
    t.slide.store (sources[(size_t) ModSource::MpeSlide], std::memory_order_relaxed);
    t.velocity.store (state.velocity, std::memory_order_relaxed);
    t.releaseVelocity.store (state.releaseVelocity, std::memory_order_relaxed);
    t.rawPressure.store (state.smoothedPressure, std::memory_order_relaxed);
    t.rawSlide.store (state.smoothedTimbre, std::memory_order_relaxed);
    t.ampEnv.store (ampEnv.getValue(), std::memory_order_relaxed);
    t.cutoffHz.store (cutoffHz, std::memory_order_relaxed);
    t.startSample.store (state.noteStartTime, std::memory_order_relaxed);

    float activity = 0.0f;
    for (auto c : slotContribution)
        activity += std::abs (c);
    t.modActivity.store (activity, std::memory_order_relaxed);
}

} // namespace nedd
