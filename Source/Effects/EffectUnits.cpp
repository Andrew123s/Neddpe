#include "EffectUnits.h"

namespace nedd::fx
{
namespace
{
    inline float onePoleCoefficient (float cutoffHz, float sampleRate) noexcept
    {
        return 1.0f - std::exp (-2.0f * dsp::kPi * std::min (cutoffHz, sampleRate * 0.45f) / sampleRate);
    }
} // namespace

// =============================================================================================
// Distortion
// =============================================================================================
void Distortion::prepare (float newSampleRate, int maxBlock)
{
    sampleRate = newSampleRate;
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversamplers[i]->initProcessing ((size_t) maxBlock);
    }
    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    dc.prepare (sampleRate);
    reset();
}

void Distortion::reset()
{
    for (auto& os : oversamplers)
        if (os != nullptr)
            os->reset();
    toneState[0] = toneState[1] = 0.0f;
    dc.reset();
}

float Distortion::shape (float x, DistortionType type) noexcept
{
    switch (type)
    {
        case DistortionType::Soft:       return dsp::fastTanh (x);
        case DistortionType::Hard:       return std::clamp (x, -1.0f, 1.0f);
        case DistortionType::Fold:       return std::sin (x * dsp::kHalfPi);
        case DistortionType::Asymmetric: return dsp::fastTanh (x + 0.35f) - 0.3364f;   // tanh(0.35)
        case DistortionType::Tube:
            return x >= 0.0f ? 1.0f - std::exp (-x) : -0.8f * (1.0f - std::exp (x * 1.25f));
    }
    return x;
}

void Distortion::process (float* l, float* r, int n, DistortionType type, float drive, float tone, float mix, int osLog2) noexcept
{
    n = std::min (n, (int) dryL.size());
    std::copy (l, l + n, dryL.begin());
    std::copy (r, r + n, dryR.begin());

    const float gain = dsp::dbToGain (dsp::clamp01 (drive) * 36.0f);
    // Level compensation: keep a -10 dBFS signal at the same level whatever the drive. Never boost:
    // the wavefolder passes through zero at some drives, where dividing by its output would add
    // up to +16 dB and clip.
    const float reference = 0.3f;
    const float compensation = std::min (1.0f, reference / std::max (0.05f, std::abs (shape (reference * gain, type))));

    auto run = [&] (float* const* channels, int numSamples)
    {
        driveRamp.setTarget (gain, numSamples);
        const float d0 = driveRamp.current;
        const float step = driveRamp.step;
        for (int ch = 0; ch < 2; ++ch)
        {
            float g = d0;
            float* x = channels[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                g += step;
                x[i] = shape (x[i] * g, type) * compensation;
            }
        }
        driveRamp.current = d0 + step * (float) numSamples;
    };

    const int factor = juce::jlimit (0, 3, osLog2);
    if (factor == 0)
    {
        float* channels[] = { l, r };
        run (channels, n);
    }
    else
    {
        float* channels[] = { l, r };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) n);
        auto up = oversamplers[(size_t) factor - 1]->processSamplesUp (block);
        float* upChannels[] = { up.getChannelPointer (0), up.getChannelPointer (1) };
        run (upChannels, (int) up.getNumSamples());
        oversamplers[(size_t) factor - 1]->processSamplesDown (block);
    }

    const float toneCoeff = onePoleCoefficient (800.0f * std::exp2 (dsp::clamp01 (tone) * 4.7f), sampleRate);
    mixRamp.setTarget (dsp::clamp01 (mix), n);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixRamp.next();
        toneState[0] += toneCoeff * (l[i] - toneState[0]);
        toneState[1] += toneCoeff * (r[i] - toneState[1]);
        const float wetL = dc.process (toneState[0], 0);
        const float wetR = dc.process (toneState[1], 1);
        l[i] = dryL[(size_t) i] + (wetL - dryL[(size_t) i]) * m;
        r[i] = dryR[(size_t) i] + (wetR - dryR[(size_t) i]) * m;
    }
}

// =============================================================================================
// Saturation
// =============================================================================================
void Saturation::process (float* l, float* r, int n, float drive, float warmth, float mix) noexcept
{
    const float g = 1.0f + dsp::clamp01 (drive) * 7.0f;
    const float compensation = 0.3f / dsp::fastTanh (0.3f * g);
    const float lpCoeff = onePoleCoefficient (20000.0f * std::exp2 (-dsp::clamp01 (warmth) * 2.8f), sampleRate);
    driveRamp.setTarget (g, n);
    mixRamp.setTarget (dsp::clamp01 (mix), n);

    for (int i = 0; i < n; ++i)
    {
        const float gg = driveRamp.next();
        const float m = mixRamp.next();
        float* ch[] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const float dry = ch[c][i];
            // Slight asymmetry adds even harmonics, like tape and transformers.
            float wet = dsp::fastTanh (dry * gg + 0.08f * dry * dry * gg) * compensation;
            lp[c] += lpCoeff * (wet - lp[c]);
            wet = dc.process (lp[c], c);
            ch[c][i] = dry + (wet - dry) * m;
        }
    }
}

// =============================================================================================
// Bitcrusher
// =============================================================================================
void Bitcrusher::process (float* l, float* r, int n, float bits, float downsample, float mix) noexcept
{
    const float levels = std::exp2 (std::clamp (bits, 1.0f, 16.0f) - 1.0f);
    const float hold = std::max (1.0f, downsample);
    mixRamp.setTarget (dsp::clamp01 (mix), n);

    for (int i = 0; i < n; ++i)
    {
        counter += 1.0f;
        if (counter >= hold)
        {
            counter -= hold;
            held[0] = std::round (l[i] * levels) / levels;
            held[1] = std::round (r[i] * levels) / levels;
        }
        const float m = mixRamp.next();
        l[i] += (held[0] - l[i]) * m;
        r[i] += (held[1] - r[i]) * m;
    }
}

// =============================================================================================
// Chorus / flanger
// =============================================================================================
void ModulatedDelay::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& line : lines)
        line.prepare ((int) (sampleRate * 0.06f) + 8);
    reset();
}

void ModulatedDelay::reset()
{
    for (auto& line : lines)
        line.clear();
    feedbackState[0] = feedbackState[1] = 0.0f;
}

void ModulatedDelay::process (float* l, float* r, int n, float rateHz, float depth, float feedback, float mix) noexcept
{
    const bool chorus = mode == Mode::Chorus;
    const float centreMs = chorus ? 14.0f : 2.2f;
    const float depthMs = chorus ? dsp::clamp01 (depth) * 7.0f : dsp::clamp01 (depth) * 1.95f;
    const float msToSamples = sampleRate * 0.001f;
    const float increment = rateHz / sampleRate;
    const float fb = chorus ? 0.0f : std::clamp (feedback, -0.95f, 0.95f);
    mixRamp.setTarget (dsp::clamp01 (mix), n);

    float* ch[] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        phase += increment;
        if (phase >= 1.0f) phase -= 1.0f;
        const float m = mixRamp.next();

        for (int c = 0; c < 2; ++c)
        {
            const float x = ch[c][i];
            const float offset = c == 0 ? 0.0f : 0.25f;
            float wet;

            if (chorus)
            {
                const float d1 = (centreMs + depthMs * dsp::fastSin (phase + offset)) * msToSamples;
                const float d2 = (centreMs * 1.37f + depthMs * dsp::fastSin (phase + offset + 0.33f)) * msToSamples;
                wet = 0.5f * (lines[c].read (d1) + lines[c].read (d2));
                lines[c].push (x);
                ch[c][i] = x + (wet - x) * m;
            }
            else
            {
                const float d = std::max (0.1f, centreMs + depthMs * dsp::fastSin (phase + offset)) * msToSamples;
                wet = lines[c].read (d);
                lines[c].push (dsp::fastTanh (x + fb * wet));
                ch[c][i] = x * (1.0f - 0.5f * m) + wet * 0.5f * m;
            }
        }
    }
}

// =============================================================================================
// Phaser
// =============================================================================================
void Phaser::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        last[c] = 0.0f;
        for (int s = 0; s < kStages; ++s)
            x1[c][s] = y1[c][s] = 0.0f;
    }
    counter = 0;
}

void Phaser::process (float* l, float* r, int n, float rateHz, float depth, float feedback, float mix) noexcept
{
    const float increment = rateHz / sampleRate;
    mixRamp.setTarget (dsp::clamp01 (mix), n);
    float* ch[] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        phase += increment;
        if (phase >= 1.0f) phase -= 1.0f;

        if (counter-- <= 0)
        {
            counter = 16;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * dsp::fastSin (phase + (c == 0 ? 0.0f : 0.25f));
                const float f = 180.0f * std::exp2 (lfo * dsp::clamp01 (depth) * 4.5f);
                const float t = std::tan (dsp::kPi * std::min (f, sampleRate * 0.45f) / sampleRate);
                coefficient[c] = (t - 1.0f) / (t + 1.0f);
            }
        }

        const float m = mixRamp.next();
        for (int c = 0; c < 2; ++c)
        {
            const float dry = ch[c][i];
            float x = dry + std::clamp (feedback, 0.0f, 0.95f) * last[c];
            const float a = coefficient[c];
            for (int s = 0; s < kStages; ++s)
            {
                const float y = a * x + x1[c][s] - a * y1[c][s];
                x1[c][s] = x;
                y1[c][s] = y;
                x = y;
            }
            last[c] = dsp::fastTanh (x);
            ch[c][i] = dry * (1.0f - 0.5f * m) + x * 0.5f * m;
        }
    }
}

// =============================================================================================
// Delay
// =============================================================================================
void StereoDelay::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& line : lines)
        line.prepare ((int) (sampleRate * 2.6f) + 8);
    reset();
}

void StereoDelay::reset()
{
    for (auto& line : lines)
        line.clear();
    damp[0] = damp[1] = 0.0f;
    smoothedDelay = -1.0f;
}

void StereoDelay::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                           float delaySamples, float feedback, float damping, bool pingPong, float returnLevel) noexcept
{
    delaySamples = std::clamp (delaySamples, 1.0f, sampleRate * 2.5f);
    if (smoothedDelay < 0.0f)
        smoothedDelay = delaySamples;

    const float dampCoeff = onePoleCoefficient (18000.0f * std::exp2 (-dsp::clamp01 (damping) * 4.0f), sampleRate);
    returnRamp.setTarget (dsp::clamp01 (returnLevel), n);
    feedbackRamp.setTarget (std::clamp (feedback, 0.0f, 0.98f), n);
    const float timeSmoothing = 1.0f - std::exp (-1.0f / (0.05f * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        // Time changes glide like a tape delay instead of clicking.
        smoothedDelay += (delaySamples - smoothedDelay) * timeSmoothing;
        const float fb = feedbackRamp.next();
        const float ret = returnRamp.next();

        const float readL = lines[0].read (smoothedDelay);
        const float readR = lines[1].read (smoothedDelay);
        damp[0] += dampCoeff * (readL - damp[0]);
        damp[1] += dampCoeff * (readR - damp[1]);

        if (pingPong)
        {
            lines[0].push (dsp::fastTanh (0.5f * (inL[i] + inR[i]) + fb * damp[1]));
            lines[1].push (dsp::fastTanh (fb * damp[0]));
        }
        else
        {
            lines[0].push (dsp::fastTanh (inL[i] + fb * damp[0]));
            lines[1].push (dsp::fastTanh (inR[i] + fb * damp[1]));
        }

        outL[i] += readL * ret;
        outR[i] += readR * ret;
    }
}

// =============================================================================================
// Reverb
// =============================================================================================
void FdnReverb::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    const float lengthsMs[kLines] = { 31.3f, 37.9f, 41.3f, 47.9f, 53.1f, 59.3f, 67.7f, 73.1f };
    for (int i = 0; i < kLines; ++i)
    {
        baseLength[i] = lengthsMs[i] * 0.001f * sampleRate;
        lines[i].prepare ((int) (baseLength[i] * 1.7f) + 64);
    }
    for (auto& p : predelay)
        p.prepare ((int) (sampleRate * 0.21f) + 8);
    reset();
}

void FdnReverb::reset()
{
    for (auto& line : lines) line.clear();
    for (auto& p : predelay) p.clear();
    std::fill (std::begin (lowpass), std::end (lowpass), 0.0f);
}

void FdnReverb::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                         float size, float damping, float predelayMs, float width, float returnLevel) noexcept
{
    size = dsp::clamp01 (size);
    const float scale = 0.5f + size * 1.1f;
    const float rt60 = 0.35f + size * size * 11.0f;
    const float dampCoeff = onePoleCoefficient (16000.0f * std::exp2 (-dsp::clamp01 (damping) * 3.8f), sampleRate);
    const float predelaySamples = std::max (1.0f, predelayMs * 0.001f * sampleRate);
    returnRamp.setTarget (dsp::clamp01 (returnLevel), n);

    float length[kLines], gain[kLines];
    for (int k = 0; k < kLines; ++k)
    {
        length[k] = baseLength[k] * scale;
        gain[k] = std::pow (10.0f, -3.0f * length[k] / (rt60 * sampleRate));
    }

    const float modIncrement = 0.3f / sampleRate;
    const float modDepth = 0.0012f * sampleRate * 0.25f;

    for (int i = 0; i < n; ++i)
    {
        predelay[0].push (inL[i]);
        predelay[1].push (inR[i]);
        const float pl = predelay[0].read (predelaySamples);
        const float pr = predelay[1].read (predelaySamples);

        modPhase += modIncrement;
        if (modPhase >= 1.0f) modPhase -= 1.0f;

        float out[kLines], v[kLines];
        float sum = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            // Slow modulation of two lines breaks up metallic resonances.
            const float mod = k == 1 ? modDepth * dsp::fastSin (modPhase) : (k == 6 ? modDepth * dsp::fastSin (modPhase + 0.37f) : 0.0f);
            out[k] = lines[k].read (length[k] + mod);
            lowpass[k] += dampCoeff * (out[k] - lowpass[k]);
            v[k] = lowpass[k] * gain[k];
            sum += v[k];
        }

        // Householder feedback matrix: energy preserving, fully mixing.
        const float h = sum * (2.0f / (float) kLines);
        for (int k = 0; k < kLines; ++k)
            lines[k].push (v[k] - h + ((k & 1) == 0 ? pl : pr) * 0.3f);

        const float l = (out[0] - out[2] + out[4] - out[6]) * 0.35f;
        const float r = (out[1] - out[3] + out[5] - out[7]) * 0.35f;
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * dsp::clamp01 (width);
        const float ret = returnRamp.next();
        outL[i] += (mid + side) * ret;
        outR[i] += (mid - side) * ret;
    }
}

// =============================================================================================
// EQ
// =============================================================================================
void Equalizer::process (float* l, float* r, int n, float lowGainDb, float midFreq, float midGainDb, float highGainDb) noexcept
{
    const float signature = lowGainDb * 1.3f + midFreq * 0.01f + midGainDb * 7.1f + highGainDb * 11.9f;
    if (signature != lastSignature)
    {
        lastSignature = signature;
        using AC = juce::dsp::IIR::ArrayCoefficients<float>;
        low.set (AC::makeLowShelf (sampleRate, 120.0f, 0.7f, juce::Decibels::decibelsToGain (lowGainDb)));
        mid.set (AC::makePeakFilter (sampleRate, std::min (midFreq, sampleRate * 0.4f), 0.9f, juce::Decibels::decibelsToGain (midGainDb)));
        high.set (AC::makeHighShelf (sampleRate, std::min (8000.0f, sampleRate * 0.4f), 0.7f, juce::Decibels::decibelsToGain (highGainDb)));
    }

    for (int i = 0; i < n; ++i)
    {
        l[i] = high.process (mid.process (low.process (l[i], 0), 0), 0);
        r[i] = high.process (mid.process (low.process (r[i], 1), 1), 1);
    }
}

// =============================================================================================
// Compressor
// =============================================================================================
void Compressor::process (float* l, float* r, int n, float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupDb) noexcept
{
    const float attack = 1.0f - std::exp (-1.0f / (std::max (0.05f, attackMs) * 0.001f * sampleRate));
    const float release = 1.0f - std::exp (-1.0f / (std::max (1.0f, releaseMs) * 0.001f * sampleRate));
    const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
    constexpr float knee = 6.0f;
    float maxReduction = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float level = dsp::gainToDb (std::max (std::abs (l[i]), std::abs (r[i])));
        envelopeDb += (level > envelopeDb ? attack : release) * (level - envelopeDb);

        const float over = envelopeDb - thresholdDb;
        float reduction = 0.0f;
        if (over > knee * 0.5f)       reduction = over * slope;
        else if (over > -knee * 0.5f) reduction = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);

        maxReduction = std::max (maxReduction, reduction);
        const float g = dsp::dbToGain (makeupDb - reduction);
        l[i] *= g;
        r[i] *= g;
    }
    lastReduction = maxReduction;
}

} // namespace nedd::fx
