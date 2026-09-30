#include "Wavetable.h"
#include "DspMath.h"
#include "Parameters/ParameterDefs.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace nedd
{
namespace
{
    constexpr int kFramesPerTable = 32;
    constexpr int kOversampledOrder = 14;                    // 16384-sample single cycles for time-domain sources
    constexpr int kOversampledSize = 1 << kOversampledOrder;
    constexpr int kFrameOrder = 11;                           // 2048
    static_assert ((1 << kFrameOrder) == Wavetable::kFrameSize);

    using Harmonics = std::vector<std::complex<float>>;       // index = harmonic number, [0] unused

    /** A time-domain generator: frame position t in [0,1], phase p in [0,1) -> sample. */
    using TimeFn = std::function<float (float t, float p)>;
    /** A spectral generator: frame position t, harmonic h (1-based) -> amplitude (sine phase). */
    using SpectralFn = std::function<float (float t, int h)>;

    Harmonics harmonicsFromTimeDomain (const TimeFn& fn, float t, juce::dsp::FFT& fft)
    {
        std::vector<float> buffer ((size_t) kOversampledSize * 2, 0.0f);
        for (int i = 0; i < kOversampledSize; ++i)
            buffer[(size_t) i] = fn (t, (float) i / (float) kOversampledSize);

        fft.performRealOnlyForwardTransform (buffer.data(), true);

        Harmonics h ((size_t) Wavetable::kMaxHarmonics + 1);
        for (int k = 1; k <= Wavetable::kMaxHarmonics; ++k)
            h[(size_t) k] = { buffer[(size_t) k * 2], buffer[(size_t) k * 2 + 1] };
        return h;
    }

    Harmonics harmonicsFromSpectrum (const SpectralFn& fn, float t)
    {
        Harmonics h ((size_t) Wavetable::kMaxHarmonics + 1);
        for (int k = 1; k <= Wavetable::kMaxHarmonics; ++k)
        {
            // A sine partial of amplitude a corresponds to the bin value -j * a.
            const float a = fn (t, k);
            h[(size_t) k] = { 0.0f, -a };
        }
        return h;
    }

    void renderFrame (Wavetable& table, int frame, const Harmonics& harmonics, juce::dsp::FFT& fft)
    {
        std::vector<float> buffer ((size_t) Wavetable::kFrameSize * 2);
        float normalisation = 1.0f;

        for (int mip = 0; mip < Wavetable::kNumMips; ++mip)
        {
            const int limit = Wavetable::kMaxHarmonics >> mip;
            std::fill (buffer.begin(), buffer.end(), 0.0f);

            for (int k = 1; k <= limit && k < Wavetable::kMaxHarmonics; ++k)
            {
                buffer[(size_t) k * 2] = harmonics[(size_t) k].real();
                buffer[(size_t) k * 2 + 1] = harmonics[(size_t) k].imag();
            }

            fft.performRealOnlyInverseTransform (buffer.data());

            if (mip == 0)
            {
                float peak = 0.0f;
                for (int i = 0; i < Wavetable::kFrameSize; ++i)
                    peak = std::max (peak, std::abs (buffer[(size_t) i]));
                normalisation = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
            }

            auto* dest = table.getFrameForWriting (frame, mip);
            for (int i = 0; i < Wavetable::kFrameSize; ++i)
                dest[i] = buffer[(size_t) i] * normalisation;
            dest[Wavetable::kFrameSize] = dest[0];
        }
    }

    Wavetable makeTable (const juce::String& name, const TimeFn& timeFn, const SpectralFn& spectralFn,
                         juce::dsp::FFT& bigFft, juce::dsp::FFT& frameFft)
    {
        Wavetable table;
        table.name = name;
        table.numFrames = kFramesPerTable;
        table.samples.assign ((size_t) kFramesPerTable * Wavetable::kNumMips * Wavetable::kFrameStride, 0.0f);

        for (int f = 0; f < kFramesPerTable; ++f)
        {
            const float t = (float) f / (float) (kFramesPerTable - 1);
            const auto harmonics = timeFn ? harmonicsFromTimeDomain (timeFn, t, bigFft)
                                          : harmonicsFromSpectrum (spectralFn, t);
            renderFrame (table, f, harmonics, frameFft);
        }

        return table;
    }

    // ----- naive shapes used by the time-domain generators (band-limited afterwards) -----
    float naiveSaw (float p) { return 2.0f * p - 1.0f; }
    float naiveSquare (float p) { return p < 0.5f ? 1.0f : -1.0f; }
    float naiveTriangle (float p) { return 1.0f - 4.0f * std::abs (p - 0.5f); }
    float sine (float p) { return std::sin (dsp::kTwoPi * p); }

    float gaussian (float x, float centre, float width)
    {
        const float d = (x - centre) / width;
        return std::exp (-0.5f * d * d);
    }
} // namespace

Wavetable buildWavetableFromFrames (const juce::String& name, const float* frames, int numFrames)
{
    juce::dsp::FFT frameFft (kFrameOrder);
    Wavetable table;
    table.name = name;
    table.numFrames = std::max (1, numFrames);
    table.samples.assign ((size_t) table.numFrames * Wavetable::kNumMips * Wavetable::kFrameStride, 0.0f);

    std::vector<float> buffer ((size_t) Wavetable::kFrameSize * 2);
    for (int f = 0; f < numFrames; ++f)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::copy (frames + (size_t) f * Wavetable::kFrameSize, frames + (size_t) (f + 1) * Wavetable::kFrameSize, buffer.begin());
        frameFft.performRealOnlyForwardTransform (buffer.data(), true);

        Harmonics h ((size_t) Wavetable::kMaxHarmonics + 1);
        for (int k = 1; k <= Wavetable::kMaxHarmonics; ++k)
            h[(size_t) k] = { buffer[(size_t) k * 2], buffer[(size_t) k * 2 + 1] };
        renderFrame (table, f, h, frameFft);
    }

    return table;
}

juce::StringArray getWavetableNames()
{
    return { "Basic Shapes", "PWM", "Harmonic Sweep", "Formant Vowels", "Growl",
             "Sync Sweep", "Digital Steps", "Glass", "Drawbars", "Sine Fold" };
}

const WavetableBank& WavetableBank::getInstance()
{
    static const WavetableBank bank;
    return bank;
}

WavetableBank::WavetableBank()
{
    juce::dsp::FFT bigFft (kOversampledOrder);
    juce::dsp::FFT frameFft (kFrameOrder);
    const auto names = getWavetableNames();

    auto addTime = [&] (int index, TimeFn fn) { tables.push_back (makeTable (names[index], fn, {}, bigFft, frameFft)); };
    auto addSpectral = [&] (int index, SpectralFn fn) { tables.push_back (makeTable (names[index], {}, fn, bigFft, frameFft)); };

    // 0: sine -> triangle -> saw -> square
    addTime (0, [] (float t, float p)
    {
        const float seg = t * 3.0f;
        if (seg < 1.0f) return dsp::lerp (sine (p), naiveTriangle (std::fmod (p + 0.25f, 1.0f)), seg);
        if (seg < 2.0f) return dsp::lerp (naiveTriangle (std::fmod (p + 0.25f, 1.0f)), naiveSaw (std::fmod (p + 0.5f, 1.0f)), seg - 1.0f);
        return dsp::lerp (naiveSaw (std::fmod (p + 0.5f, 1.0f)), naiveSquare (p), seg - 2.0f);
    });

    // 1: pulse width 50% -> 4%
    addTime (1, [] (float t, float p)
    {
        const float width = 0.5f - 0.46f * t;
        return (p < width ? 1.0f : -1.0f) - (2.0f * width - 1.0f);
    });

    // 2: harmonic count grows 1 -> 64 with a soft edge
    addSpectral (2, [] (float t, int h)
    {
        const float count = 1.0f + t * t * 63.0f;
        const float edge = juce::jlimit (0.0f, 1.0f, count - (float) h + 1.0f);
        return edge / (float) h;
    });

    // 3: formant vowels A-E-I-O-U over a bright source, formants fixed in harmonic space
    addSpectral (3, [] (float t, int h)
    {
        static const float vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                                            { 450, 800, 2830 }, { 325, 700, 2530 } };
        const float pos = t * 4.0f;
        const int a = std::min ((int) pos, 3);
        const float frac = pos - (float) a;
        constexpr float fundamental = 110.0f;
        float amp = 0.0f;
        const float gains[3] = { 1.0f, 0.6f, 0.3f };

        for (int f = 0; f < 3; ++f)
        {
            const float centre = dsp::lerp (vowels[a][f], vowels[a + 1][f], frac) / fundamental;
            amp += gains[f] * gaussian ((float) h, centre, 0.9f + centre * 0.08f);
        }
        return (0.08f + amp) / std::pow ((float) h, 0.6f);
    });

    // 4: phase-distorted, saturated sine that grows teeth
    addTime (4, [] (float t, float p)
    {
        const float warped = p + t * 0.22f * std::sin (dsp::kTwoPi * 3.0f * p);
        return dsp::fastTanh ((1.0f + 5.0f * t) * std::sin (dsp::kTwoPi * warped));
    });

    // 5: hard-sync saw, slave ratio 1 -> 8
    addTime (5, [] (float t, float p)
    {
        const float ratio = 1.0f + 7.0f * t;
        const float slave = p * ratio;
        return naiveSaw (slave - std::floor (slave)) * (1.0f - 0.3f * p);
    });

    // 6: quantised sine, 32 -> 2 steps
    addTime (6, [] (float t, float p)
    {
        const float levels = 2.0f + (1.0f - t) * 30.0f;
        return std::round (sine (p) * levels) / levels;
    });

    // 7: glassy partials on a Fibonacci series, energy moving upward
    addSpectral (7, [] (float t, int h)
    {
        static const int partials[] = { 1, 2, 3, 5, 8, 13, 21, 34, 55 };
        for (int i = 0; i < 9; ++i)
            if (partials[i] == h)
                return gaussian ((float) i, t * 8.0f, 1.6f) * (1.0f / (1.0f + (float) i * 0.15f));
        return 0.0f;
    });

    // 8: organ drawbar registrations (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1')
    addSpectral (8, [] (float t, int h)
    {
        static const int harmonicOf[9] = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };
        static const float registrations[4][9] = { { 8, 8, 8, 0, 0, 0, 0, 0, 0 },
                                                   { 8, 3, 8, 6, 0, 4, 0, 0, 2 },
                                                   { 8, 8, 8, 8, 8, 8, 8, 8, 8 },
                                                   { 0, 0, 8, 8, 6, 8, 4, 6, 8 } };
        const float pos = t * 3.0f;
        const int a = std::min ((int) pos, 2);
        const float frac = pos - (float) a;
        for (int i = 0; i < 9; ++i)
            if (harmonicOf[i] == h)
                return dsp::lerp (registrations[a][i], registrations[a + 1][i], frac) / 8.0f;
        return 0.0f;
    });

    // 9: sine wavefolder, gain 1 -> 10
    addTime (9, [] (float t, float p)
    {
        const float gain = 1.0f + 9.0f * t * t;
        return std::sin (gain * dsp::kHalfPi * sine (p));
    });

    jassert (tables.size() == (size_t) names.size());
    jassert (names.size() == kImportedWavetableChoice);
}

} // namespace nedd
