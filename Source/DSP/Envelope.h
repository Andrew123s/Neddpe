#pragma once

#include "DspMath.h"

namespace nedd::dsp
{
/**
    Delay / Attack / Hold / Decay / Sustain / Release envelope with per-segment curvature.

    Each segment interpolates from the level it started at towards its target using
    tensionCurve(), so retriggering from a non-zero level (legato, voice reuse) never clicks.
    Segment times are re-read every control block, which lets modulation change a segment
    while it is running.
*/
class Envelope
{
public:
    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    struct Settings
    {
        float delay = 0.0f, attack = 0.005f, hold = 0.0f, decay = 0.3f, sustain = 0.8f, release = 0.2f;
        float attackCurve = 0.0f, decayCurve = 0.5f, releaseCurve = 0.5f;
    };

    void setSampleRate (float newRate) noexcept { sampleRate = newRate; }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        updateIncrement();
    }

    /** Starts from the current level (no click) unless hardReset is set. */
    void noteOn (bool hardReset = false) noexcept
    {
        if (hardReset)
            value = 0.0f;
        enterStage (settings.delay > 0.0f ? Stage::Delay : Stage::Attack);
    }

    void noteOff() noexcept
    {
        if (stage != Stage::Idle && stage != Stage::Release)
            enterStage (Stage::Release);
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        value = 0.0f;
        progress = 0.0f;
    }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle:
                return 0.0f;

            case Stage::Sustain:
                // Follow sustain changes smoothly.
                value += (settings.sustain - value) * 0.002f;
                return value;

            case Stage::Delay:
            case Stage::Hold:
                progress += increment;
                if (progress >= 1.0f)
                    enterStage (stage == Stage::Delay ? Stage::Attack : Stage::Decay);
                return value;

            case Stage::Attack:
            case Stage::Decay:
            case Stage::Release:
                break;
        }

        progress += increment;

        if (progress >= 1.0f)
        {
            value = target;
            if (stage == Stage::Attack)       enterStage (settings.hold > 0.0f ? Stage::Hold : Stage::Decay);
            else if (stage == Stage::Decay)   enterStage (Stage::Sustain);
            else                              { stage = Stage::Idle; value = 0.0f; }
            return value;
        }

        value = start + (target - start) * tensionCurve (progress, curve);
        return value;
    }

    /** Advances by numSamples and returns the final value (for control-rate envelopes). */
    float processBlock (int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            process();
        return value;
    }

    float getValue() const noexcept { return value; }
    Stage getStage() const noexcept { return stage; }
    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }

private:
    void enterStage (Stage newStage) noexcept
    {
        stage = newStage;
        progress = 0.0f;
        start = value;

        switch (stage)
        {
            case Stage::Attack:  target = 1.0f; curve = settings.attackCurve; break;
            case Stage::Decay:   target = settings.sustain; curve = settings.decayCurve; break;
            case Stage::Release: target = 0.0f; curve = settings.releaseCurve; break;
            case Stage::Sustain: value = target = settings.sustain; break;
            case Stage::Idle:
            case Stage::Delay:
            case Stage::Hold:    target = value; break;
        }

        updateIncrement();
    }

    void updateIncrement() noexcept
    {
        float seconds = 0.0f;
        switch (stage)
        {
            case Stage::Delay:   seconds = settings.delay; break;
            case Stage::Attack:  seconds = settings.attack; break;
            case Stage::Hold:    seconds = settings.hold; break;
            case Stage::Decay:   seconds = settings.decay; target = settings.sustain; break;
            case Stage::Release: seconds = settings.release; break;
            case Stage::Idle:
            case Stage::Sustain: return;
        }

        const float samples = seconds * sampleRate;
        increment = samples < 1.0f ? 1.0f : 1.0f / samples;
    }

    Settings settings;
    Stage stage = Stage::Idle;
    float sampleRate = 44100.0f;
    float value = 0.0f, start = 0.0f, target = 0.0f, curve = 0.0f;
    float progress = 0.0f, increment = 1.0f;
};

} // namespace nedd::dsp
