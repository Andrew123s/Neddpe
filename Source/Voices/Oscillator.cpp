#include "Oscillator.h"

namespace nedd
{
namespace
{
    using simd::f4;

    constexpr float kGoldenRatioFraction = 0.61803398875f;
    constexpr float kMaxUnisonCents = 50.0f;
    constexpr float kMinBlepIncrement = 1.0e-6f;
    constexpr float kGrainGain = 1.2f;

    inline float wrap01 (float x) noexcept { return dsp::wrapPhase (x); }

    inline float pulseWidthFor (const OscillatorBlockParams& p) noexcept
    {
        return p.wave == AnalogWave::Square ? 0.5f : std::clamp (p.pulseWidth, 0.02f, 0.98f);
    }

    /** The analog shapes without band-limiting (used to measure the jump at a sync reset). */
    inline float naiveAnalog (AnalogWave wave, float t, float pw) noexcept
    {
        switch (wave)
        {
            case AnalogWave::Sine:     return simd::sinCycles (t);
            case AnalogWave::Triangle: return 4.0f * std::abs (wrap01 (t + 0.75f) - 0.5f) - 1.0f;
            case AnalogWave::Saw:      return 2.0f * t - 1.0f;
            case AnalogWave::Square:
            case AnalogWave::Pulse:    return (t < pw ? 1.0f : -1.0f) - (2.0f * pw - 1.0f);
        }
        return 0.0f;
    }

    /** Hann window, x in [0, 1]. */
    inline float hann (float x) noexcept
    {
        const float s = simd::sinCycles (0.5f * x);
        return s * s;
    }
} // namespace

void Oscillator::noteOn (float newStartPhase, float phaseRandom, dsp::Random32& rng) noexcept
{
    startPhase = newStartPhase;

    for (int i = 0; i < kMaxUnison; ++i)
    {
        // Unison voices start at spread-out phases so the stack does not phase-cancel on attack.
        const float spreadPhase = i > 0 ? kGoldenRatioFraction * (float) i : 0.0f;
        phase[(size_t) i] = wrap01 (startPhase + spreadPhase + rng.nextFloat() * phaseRandom);
        op1Phase[(size_t) i] = 0.0f;
        op2Phase[(size_t) i] = 0.0f;
        fbHistory[(size_t) i] = 0.0f;
        fbLast[(size_t) i] = 0.0f;
        samplePos[(size_t) i] = 0.0;
    }

    noiseL.rng.seed (rng.next());
    noiseR.rng.seed (rng.next());
    digitalPhaseL = digitalPhaseR = 0.0f;
    noiseHeld = {};
    noiseCounter = 0;

    grainRng.seed (rng.next());
    for (auto& g : grains)
        g.active = false;
    grainClock = 1.0f;   // the first grain starts with the note

    sampleNeedsStart = true;
    pending = {};
    lastPhaseMod = 0.0f;
}

float Oscillator::sampleStartPosition() const noexcept
{
    const auto* s = params.sample;
    if (s == nullptr || s->length < 2)
        return 0.0f;
    const float maxStart = (float) std::max (0, s->length - 64);
    return std::clamp (dsp::clamp01 (params.wtPosition) * (float) (s->length - 1), 0.0f, maxStart);
}

void Oscillator::setBlock (const OscillatorBlockParams& p) noexcept
{
    params = p;
    params.unison = juce::jlimit (1, kMaxUnison, p.unison);

    const bool singleLane = params.engine == OscEngine::Noise || params.engine == OscEngine::Granular;
    numLanes = singleLane ? 1 : params.unison;
    const int n = numLanes;
    const float norm = 1.0f / std::sqrt ((float) n);
    const bool strictMips = oversampling > 1;

    // Source samples per output sample, per Hz of oscillator frequency.
    const float sampleRatio = params.sample != nullptr
                                  ? (float) (params.sample->sampleRate / (double) rate) / std::max (1.0f, params.rootHz)
                                  : 0.0f;

    for (int i = 0; i < kMaxUnison; ++i)
    {
        const auto lane = (size_t) i;
        if (i >= n)
        {
            // Padding lanes of the last SIMD group: silent, but with a safe increment.
            increment[lane] = 0.01f;
            gainL[lane] = gainR[lane] = laneWeight[lane] = 0.0f;
            sampleIncrement[lane] = 0.0f;
            continue;
        }

        const float offset = n > 1 ? ((float) i / (float) (n - 1)) * 2.0f - 1.0f : 0.0f;
        const float cents = offset * dsp::clamp01 (params.detune) * kMaxUnisonCents;
        const float freq = params.frequency * std::exp2 (cents * (1.0f / 1200.0f));

        increment[lane] = std::max (1.0e-7f, std::min (freq / rate, 0.49f));
        float l = 0.0f, r = 0.0f;
        dsp::panGains (params.pan + offset * params.spread, l, r);
        gainL[lane] = l * norm;
        gainR[lane] = r * norm;
        laneWeight[lane] = 1.0f / (float) n;
        sampleIncrement[lane] = freq * sampleRatio;

        if (params.engine == OscEngine::Wavetable)
            mip[lane] = Wavetable::selectMip (increment[lane], rate, strictMips);
    }

    if (params.engine == OscEngine::Sample && sampleNeedsStart && params.sample != nullptr)
    {
        const double start = sampleStartPosition();
        for (auto& pos : samplePos)
            pos = start;
        sampleNeedsStart = false;
    }

    if (params.engine == OscEngine::Granular)
    {
        const float overlap = std::clamp (params.grainDensity * params.grainSeconds, 1.0f, (float) kMaxGrains);
        grainNorm = kGrainGain / std::sqrt (overlap);
        grainBaseIncrement = params.frequency * sampleRatio;
    }
}

// ---------------------------------------------------------------------------------------------
// Per-sample rendering
// ---------------------------------------------------------------------------------------------
Oscillator::Output Oscillator::tick (float phaseMod, float syncFraction) noexcept
{
    Output out;
    Frame current;

    switch (params.engine)
    {
        case OscEngine::Noise:    renderNoiseFrame (current); break;
        case OscEngine::Granular: renderGranular (current); break;
        case OscEngine::Sample:   renderSample (syncFraction, current, out); break;
        case OscEngine::Analog:
        case OscEngine::Wavetable:
        case OscEngine::FM:
            if (syncFraction >= 0.0f)
                renderSynced (phaseMod, syncFraction, current);
            else if (params.engine == OscEngine::Analog)
                renderAnalog (phaseMod, current);
            else if (params.engine == OscEngine::FM)
                renderFm (current, phaseMod);
            else
                renderWavetable (phaseMod, current);
            advanceLanePhases (out);
            break;
    }

    lastPhaseMod = phaseMod;

    // Emit the previous sample (a sync reset in this sample may just have corrected it).
    out.left = pending.left;
    out.right = pending.right;
    out.mono = pending.mono;
    pending = current;
    return out;
}

void Oscillator::advanceLanePhases (Output& out) noexcept
{
    const float next0 = phase[0] + increment[0];
    if (next0 >= 1.0f)
    {
        out.wrapped = true;
        out.wrapFraction = (next0 - 1.0f) / increment[0];
    }

    for (int g = 0; g < numLanes; g += 4)
    {
        f4 ph = f4::load (phase.data() + g) + f4::load (increment.data() + g);
        ph = ph - (f4 (1.0f) & (ph >= f4 (1.0f)));
        ph.store (phase.data() + g);
    }
}

void Oscillator::renderAnalog (float phaseMod, Frame& f) noexcept
{
    const f4 pm (phaseMod);
    // Under phase modulation the instantaneous increment differs from the nominal one; the
    // BLEP width follows it so edges stay band-limited while being pushed around.
    const f4 dPm (phaseMod - lastPhaseMod);
    const float pw = pulseWidthFor (params);
    f4 accL, accR, accM;

    for (int g = 0; g < numLanes; g += 4)
    {
        const f4 dt = f4::load (increment.data() + g);
        const f4 t = simd::wrap01 (f4::load (phase.data() + g) + pm);
        const f4 dtBlep = f4::min (f4 (0.5f), f4::max (f4::abs (dt + dPm), f4 (kMinBlepIncrement)));
        f4 v;

        switch (params.wave)
        {
            case AnalogWave::Sine:
                v = simd::sinCycles (t);
                break;
            case AnalogWave::Triangle:
                v = f4 (4.0f) * f4::abs (simd::wrap01 (t + f4 (0.75f)) - f4 (0.5f)) - f4 (1.0f);
                break;
            case AnalogWave::Saw:
                v = t + t - f4 (1.0f) - simd::polyBlep (t, dtBlep);
                break;
            case AnalogWave::Square:
            case AnalogWave::Pulse:
                v = f4::select (t < f4 (pw), f4 (1.0f), f4 (-1.0f));
                v = v + simd::polyBlep (t, dtBlep) - simd::polyBlep (simd::wrap01 (t + f4 (1.0f - pw)), dtBlep);
                v = v - f4 (2.0f * pw - 1.0f);   // remove DC of asymmetric pulses
                break;
        }

        accL = accL + v * f4::load (gainL.data() + g);
        accR = accR + v * f4::load (gainR.data() + g);
        accM = accM + v * f4::load (laneWeight.data() + g);
    }

    f.left = accL.sum();
    f.right = accR.sum();
    f.mono = accM.sum();
}

void Oscillator::renderFm (Frame& f, float phaseMod) noexcept
{
    const f4 pm (phaseMod);
    const f4 index1 (params.op1Index), index2 (params.op2Index), halfIndex2 (params.op2Index * 0.5f);
    const f4 feedback (params.feedback * 0.3f);
    const f4 ratio1 (params.op1Ratio), ratio2 (params.op2Ratio);
    f4 accL, accR, accM;

    for (int g = 0; g < numLanes; g += 4)
    {
        const f4 dt = f4::load (increment.data() + g);
        const f4 t = simd::wrap01 (f4::load (phase.data() + g) + pm);
        const f4 p1 = f4::load (op1Phase.data() + g);
        const f4 p2 = f4::load (op2Phase.data() + g);
        const f4 fb = feedback * f4::load (fbHistory.data() + g);
        f4 v, fbSource;

        switch (params.algorithm)
        {
            case FmAlgorithm::Stack:
            {
                const f4 m2 = simd::sinCycles (p2 + fb);
                const f4 m1 = simd::sinCycles (p1 + index2 * m2);
                v = simd::sinCycles (t + index1 * m1);
                fbSource = m2;
                break;
            }
            case FmAlgorithm::Parallel:
            {
                const f4 m1 = simd::sinCycles (p1 + fb);
                const f4 m2 = simd::sinCycles (p2);
                v = simd::sinCycles (t + index1 * m1 + index2 * m2);
                fbSource = m1;
                break;
            }
            case FmAlgorithm::Branch:
            {
                const f4 m2 = simd::sinCycles (p2 + fb);
                const f4 m1 = simd::sinCycles (p1 + index2 * m2);
                v = simd::sinCycles (t + index1 * m1 + halfIndex2 * m2);
                fbSource = m2;
                break;
            }
        }

        // Averaging the last two feedback samples tames the classic FM feedback buzz.
        (f4 (0.5f) * (fbSource + f4::load (fbLast.data() + g))).store (fbHistory.data() + g);
        fbSource.store (fbLast.data() + g);
        simd::wrap01 (p1 + dt * ratio1).store (op1Phase.data() + g);
        simd::wrap01 (p2 + dt * ratio2).store (op2Phase.data() + g);

        accL = accL + v * f4::load (gainL.data() + g);
        accR = accR + v * f4::load (gainR.data() + g);
        accM = accM + v * f4::load (laneWeight.data() + g);
    }

    f.left = accL.sum();
    f.right = accR.sum();
    f.mono = accM.sum();
}

void Oscillator::renderWavetable (float phaseMod, Frame& f) noexcept
{
    if (params.table == nullptr)
        return;

    const auto& table = *params.table;
    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = (size_t) i;
        const float v = table.sample (wrap01 (phase[lane] + phaseMod), params.wtPosition, mip[lane]);
        f.left += v * gainL[lane];
        f.right += v * gainR[lane];
        f.mono += v * laneWeight[lane];
    }
}

float Oscillator::naiveValue (int lane, float t, float p1, float p2) const noexcept
{
    const auto i = (size_t) lane;
    switch (params.engine)
    {
        case OscEngine::Analog:
            return naiveAnalog (params.wave, t, pulseWidthFor (params));

        case OscEngine::Wavetable:
            return params.table != nullptr ? params.table->sample (t, params.wtPosition, mip[i]) : 0.0f;

        case OscEngine::FM:
        {
            const float fb = params.feedback * 0.3f * fbHistory[i];
            switch (params.algorithm)
            {
                case FmAlgorithm::Stack:
                {
                    const float m2 = simd::sinCycles (p2 + fb);
                    return simd::sinCycles (t + params.op1Index * simd::sinCycles (p1 + params.op2Index * m2));
                }
                case FmAlgorithm::Parallel:
                    return simd::sinCycles (t + params.op1Index * simd::sinCycles (p1 + fb) + params.op2Index * simd::sinCycles (p2));
                case FmAlgorithm::Branch:
                {
                    const float m2 = simd::sinCycles (p2 + fb);
                    return simd::sinCycles (t + params.op1Index * simd::sinCycles (p1 + params.op2Index * m2) + params.op2Index * 0.5f * m2);
                }
            }
            return 0.0f;
        }

        case OscEngine::Noise:
        case OscEngine::Granular:
        case OscEngine::Sample:
            break;
    }
    return 0.0f;
}

void Oscillator::renderSynced (float phaseMod, float d, Frame& f) noexcept
{
    // The master wrapped d samples ago (0 <= d < 1), so the reset happened between the
    // previous sample and this one. The waveform jumps by h at that instant; a PolyBLEP
    // step of height h spread over the two neighbouring samples band-limits the jump:
    //   previous sample += h/2 * d^2,  this sample -= h/2 * (1 - d)^2
    // (the previous sample is still in `pending`, so it can be corrected before it is emitted).
    const float before = 0.5f * d * d;
    const float after = 0.5f * (1.0f - d) * (1.0f - d);

    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = (size_t) i;
        const float dt = increment[lane];

        const float valueBefore = naiveValue (i, wrap01 (phase[lane] - d * dt + phaseMod),
                                              op1Phase[lane] - d * dt * params.op1Ratio,
                                              op2Phase[lane] - d * dt * params.op2Ratio);
        const float valueAtReset = naiveValue (i, wrap01 (startPhase + phaseMod), 0.0f, 0.0f);
        const float jump = valueAtReset - valueBefore;

        phase[lane] = wrap01 (startPhase + d * dt);
        fbHistory[lane] = fbLast[lane] = 0.0f;
        const float p1 = wrap01 (d * dt * params.op1Ratio);
        const float p2 = wrap01 (d * dt * params.op2Ratio);
        const float v = naiveValue (i, wrap01 (phase[lane] + phaseMod), p1, p2) - jump * after;
        op1Phase[lane] = wrap01 (p1 + dt * params.op1Ratio);
        op2Phase[lane] = wrap01 (p2 + dt * params.op2Ratio);

        const float correction = jump * before;
        pending.left += correction * gainL[lane];
        pending.right += correction * gainR[lane];
        pending.mono += correction * laneWeight[lane];

        f.left += v * gainL[lane];
        f.right += v * gainR[lane];
        f.mono += v * laneWeight[lane];
    }
}

void Oscillator::renderSample (float syncFraction, Frame& f, Output& out) noexcept
{
    const auto* s = params.sample;
    if (s == nullptr || s->length < 64)
        return;

    const float* left = s->channel (0);
    const float* right = s->channel (1);
    const double start = sampleStartPosition();
    const double end = (double) s->length - 1.0;
    const double fade = params.loop ? std::min ((end - start) * 0.25, 0.01 * s->sampleRate) : 0.0;
    const double loopStart = start + fade;

    // Reads one channel; the last `fade` samples of a loop cross-fade into the samples after
    // the start, so the wrap is seamless whatever the audio.
    auto read = [&] (const float* data, double pos) noexcept
    {
        const float v = SampleData::readCubic (data, pos);
        if (fade > 1.0 && pos > end - fade)
        {
            const double into = pos - (end - fade);
            const float x = (float) (into / fade);
            return v + x * (SampleData::readCubic (data, start + into) - v);
        }
        return v;
    };

    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = (size_t) i;
        double& pos = samplePos[lane];
        const double inc = sampleIncrement[lane];
        float jumpL = 0.0f, jumpR = 0.0f;

        if (syncFraction >= 0.0f)
        {
            const double beforePos = pos - (double) syncFraction * inc;
            const bool wasPlaying = pos >= 0.0 && beforePos >= 0.0;
            const float beforeL = wasPlaying ? read (left, beforePos) : 0.0f;
            const float beforeR = wasPlaying ? (s->stereo ? read (right, beforePos) : beforeL) : 0.0f;
            jumpL = read (left, start) - beforeL;
            jumpR = (s->stereo ? read (right, start) : read (left, start)) - beforeR;
            pos = std::min (start + (double) syncFraction * inc, end);

            const float before = 0.5f * syncFraction * syncFraction;
            pending.left += jumpL * before * gainL[lane];
            pending.right += jumpR * before * gainR[lane];
            pending.mono += 0.5f * (jumpL + jumpR) * before * laneWeight[lane];
        }

        if (pos < 0.0)
            continue;   // a one-shot that has finished

        const float after = syncFraction >= 0.0f ? 0.5f * (1.0f - syncFraction) * (1.0f - syncFraction) : 0.0f;
        const float l = read (left, pos) - jumpL * after;
        const float r = (s->stereo ? read (right, pos) : read (left, pos)) - jumpR * after;
        f.left += l * gainL[lane];
        f.right += r * gainR[lane];
        f.mono += 0.5f * (l + r) * laneWeight[lane];

        pos += inc;
        if (params.loop)
        {
            if (pos >= end)
            {
                pos = std::min (loopStart + (pos - end), end - 1.0e-3);
                if (i == 0)
                {
                    out.wrapped = true;
                    out.wrapFraction = inc > 0.0 ? (float) std::clamp ((pos - loopStart) / inc, 0.0, 1.0) : 0.0f;
                }
            }
        }
        else if (pos >= end)
        {
            pos = -1.0;
        }
    }
}

void Oscillator::spawnGrain() noexcept
{
    const auto* s = params.sample;
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active)
        {
            slot = &g;
            break;
        }
    if (slot == nullptr)
        return;   // all grains busy: density x size exceeds the grain pool

    const float semitones = params.grainPitchSpray * grainRng.nextBipolar();
    const float inc = grainBaseIncrement * std::exp2 (semitones * (1.0f / 12.0f));
    const float lengthSamples = std::max (8.0f, params.grainSeconds * rate);
    const double span = (double) inc * (double) lengthSamples;
    const double length = (double) s->length;

    const double centre = (double) dsp::clamp01 (params.wtPosition) * (length - 1.0)
                        + (double) (params.grainSpray * grainRng.nextBipolar()) * 0.15 * length;
    const double first = std::clamp (centre - 0.5 * span, 0.0, std::max (0.0, length - 2.0 - span));

    slot->position = first;
    slot->increment = inc;
    slot->window = 0.0f;
    slot->windowIncrement = 1.0f / lengthSamples;
    float l = 0.0f, r = 0.0f;
    dsp::panGains (params.pan + params.spread * grainRng.nextBipolar(), l, r);
    slot->gainL = l * grainNorm;
    slot->gainR = r * grainNorm;
    slot->active = true;
}

void Oscillator::renderGranular (Frame& f) noexcept
{
    const auto* s = params.sample;
    if (s == nullptr || s->length < 64)
        return;

    grainClock += params.grainDensity / rate;
    if (grainClock >= 1.0f)
    {
        grainClock -= (float) (int) grainClock;
        spawnGrain();
    }

    const float* left = s->channel (0);
    const float* right = s->channel (1);
    const double last = (double) s->length - 1.0;
    float mono = 0.0f;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        // Content may have been replaced since the grain started: never read outside it.
        if (g.position < 0.0 || g.position >= last)
        {
            g.active = false;
            continue;
        }

        const float w = hann (g.window);
        const float l = SampleData::readCubic (left, g.position) * w;
        const float r = s->stereo ? SampleData::readCubic (right, g.position) * w : l;
        f.left += l * g.gainL;
        f.right += r * g.gainR;
        mono += 0.5f * (l + r);

        g.position += g.increment;
        g.window += g.windowIncrement;
        if (g.window >= 1.0f)
            g.active = false;
    }

    f.mono = mono * grainNorm;
}

void Oscillator::renderNoiseFrame (Frame& f) noexcept
{
    if (noiseCounter == 0)
    {
        const float dt = increment[0] * (float) oversampling;
        const float l = renderNoise (noiseL, dt, digitalPhaseL);
        const float r = dsp::lerp (l, renderNoise (noiseR, dt, digitalPhaseR), dsp::clamp01 (params.spread));
        noiseHeld.left = l * gainL[0] * 1.4142f;
        noiseHeld.right = r * gainR[0] * 1.4142f;
        noiseHeld.mono = l;
    }

    noiseCounter = noiseCounter + 1 >= oversampling ? 0 : noiseCounter + 1;
    f = noiseHeld;
}

float Oscillator::renderNoise (NoiseState& n, float dt, float& digitalPhase) noexcept
{
    const float white = n.rng.nextBipolar();

    switch (params.noise)
    {
        case NoiseType::White:
            return white * 0.7f;

        case NoiseType::Pink:
        {
            // Paul Kellet's refined pink-noise filter.
            n.b0 = 0.99886f * n.b0 + white * 0.0555179f;
            n.b1 = 0.99332f * n.b1 + white * 0.0750759f;
            n.b2 = 0.96900f * n.b2 + white * 0.1538520f;
            n.b3 = 0.86650f * n.b3 + white * 0.3104856f;
            n.b4 = 0.55000f * n.b4 + white * 0.5329522f;
            n.b5 = -0.7616f * n.b5 - white * 0.0168980f;
            const float pink = n.b0 + n.b1 + n.b2 + n.b3 + n.b4 + n.b5 + n.b6 + white * 0.5362f;
            n.b6 = white * 0.115926f;
            return pink * 0.11f;
        }

        case NoiseType::Brown:
            n.brown = (n.brown + 0.02f * white) * (1.0f / 1.02f);
            return n.brown * 3.5f;

        case NoiseType::Crackle:
        {
            // Sparse random impulses; density follows the note pitch.
            const float density = std::min (0.25f, dt * 8.0f);
            if (n.rng.nextFloat() < density)
                n.crackle = n.rng.nextBipolar();
            n.crackle *= 0.9f;
            return n.crackle;
        }

        case NoiseType::Digital:
        {
            // Sample-and-hold noise clocked at the note frequency: pitched, gritty.
            digitalPhase += dt;
            if (digitalPhase >= 1.0f)
            {
                digitalPhase -= std::floor (digitalPhase);
                n.held = white;
            }
            return n.held * 0.7f;
        }
    }

    return 0.0f;
}

} // namespace nedd
