#include "OscillatorAssets.h"
#include "DSP/DspMath.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace nedd
{
namespace
{
    namespace ids
    {
        const juce::Identifier assets { "Assets" };
        const juce::Identifier slot { "Slot" };
        const juce::Identifier index { "index" };
        const juce::Identifier wavetable { "Wavetable" };
        const juce::Identifier sample { "Sample" };
        const juce::Identifier name { "name" };
        const juce::Identifier frames { "frames" };
        const juce::Identifier rate { "rate" };
        const juce::Identifier channels { "channels" };
        const juce::Identifier length { "length" };
        const juce::Identifier encoding { "encoding" };
        const juce::Identifier data { "data" };
    }

    constexpr double kWavetableStorageRate = 48000.0;

    // ---- audio <-> base64 text (FLAC when possible, raw float32 otherwise) --------------------
    struct EncodedAudio
    {
        juce::String encoding;
        juce::String data;
    };

    EncodedAudio encodeAudio (const float* const* channels, int numChannels, int numSamples, double sampleRate)
    {
        juce::MemoryBlock block;

        // 24-bit FLAC clips at full scale; keep float data that goes beyond it exact.
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                peak = std::max (peak, std::abs (channels[ch][i]));

        if (peak <= 1.0f)
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                           .withNumChannels (numChannels)
                                                           .withBitsPerSample (24);
            if (auto writer = flac.createWriterFor (stream, options))
            {
                const bool ok = writer->writeFromFloatArrays (channels, numChannels, numSamples);
                writer.reset();   // flushes into the block
                if (ok && block.getSize() > 0)
                    return { "flac", block.toBase64Encoding() };
            }
        }

        // Fallback: interleaved-by-channel raw float32.
        block.reset();
        for (int ch = 0; ch < numChannels; ++ch)
            block.append (channels[ch], (size_t) numSamples * sizeof (float));
        return { "f32", block.toBase64Encoding() };
    }

    /** Decodes into numChannels vectors of numSamples; returns false on malformed data. */
    bool decodeAudio (const juce::String& encoding, const juce::String& text, int numChannels, int numSamples,
                      std::vector<std::vector<float>>& out)
    {
        juce::MemoryBlock block;
        if (! block.fromBase64Encoding (text))
            return false;

        out.assign ((size_t) numChannels, std::vector<float> ((size_t) numSamples, 0.0f));

        if (encoding == "f32")
        {
            if (block.getSize() != (size_t) numChannels * (size_t) numSamples * sizeof (float))
                return false;
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy (out[(size_t) ch].data(), static_cast<const char*> (block.getData()) + (size_t) ch * (size_t) numSamples * sizeof (float),
                             (size_t) numSamples * sizeof (float));
            return true;
        }

        if (encoding == "flac")
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, false), true));
            if (reader == nullptr || (int) reader->numChannels < numChannels)
                return false;

            juce::AudioBuffer<float> buffer ((int) reader->numChannels, numSamples);
            buffer.clear();
            reader->read (&buffer, 0, (int) std::min<juce::int64> (reader->lengthInSamples, numSamples), 0, true, true);
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + numSamples, out[(size_t) ch].begin());
            return true;
        }

        return false;
    }

    std::shared_ptr<const SampleData> buildSample (const juce::String& name, double sampleRate, const float* left,
                                                   const float* right, int length, EncodedAudio* encoded)
    {
        auto s = std::make_unique<SampleData>();
        s->name = name;
        s->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        s->length = std::max (0, length);
        s->stereo = right != nullptr;
        s->left.assign ((size_t) s->length + 2 * SampleData::kGuard, 0.0f);
        std::copy (left, left + s->length, s->left.begin() + SampleData::kGuard);
        if (s->stereo)
        {
            s->right.assign ((size_t) s->length + 2 * SampleData::kGuard, 0.0f);
            std::copy (right, right + s->length, s->right.begin() + SampleData::kGuard);
        }

        if (encoded == nullptr)
        {
            const float* chans[2] = { left, right };
            auto e = encodeAudio (chans, s->stereo ? 2 : 1, s->length, s->sampleRate);
            s->storedEncoding = e.encoding;
            s->storedData = e.data;
        }
        else
        {
            s->storedEncoding = encoded->encoding;
            s->storedData = encoded->data;
        }
        return std::shared_ptr<const SampleData> (std::move (s));
    }

    std::shared_ptr<const UserWavetable> buildWavetable (const juce::String& name, std::vector<float> frames, EncodedAudio* encoded)
    {
        auto w = std::make_unique<UserWavetable>();
        w->name = name;
        w->numFrames = (int) (frames.size() / (size_t) Wavetable::kFrameSize);
        frames.resize ((size_t) w->numFrames * Wavetable::kFrameSize);
        w->frames = std::move (frames);
        w->table = buildWavetableFromFrames (name, w->frames.data(), w->numFrames);

        if (encoded == nullptr)
        {
            const float* chans[1] = { w->frames.data() };
            auto e = encodeAudio (chans, 1, (int) w->frames.size(), kWavetableStorageRate);
            w->storedEncoding = e.encoding;
            w->storedData = e.data;
        }
        else
        {
            w->storedEncoding = encoded->encoding;
            w->storedData = encoded->data;
        }
        return std::shared_ptr<const UserWavetable> (std::move (w));
    }

    // ---- file reading --------------------------------------------------------------------------
    std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            error = "\"" + file.getFileName() + "\" is not an audio file NeddPE can read (use WAV, AIFF, FLAC or OGG).";
        else if (reader->lengthInSamples <= 0 || reader->numChannels == 0)
        {
            error = "\"" + file.getFileName() + "\" contains no audio.";
            reader.reset();
        }
        return reader;
    }

    /** Serum and similar editors store the frame size in a "clm " RIFF chunk: "<!>2048 ...". */
    int readClmFrameSize (const juce::File& file)
    {
        if (! file.hasFileExtension ("wav"))
            return 0;

        juce::FileInputStream in (file);
        if (! in.openedOk() || in.readInt() != (int) juce::ByteOrder::littleEndianInt ("RIFF"))
            return 0;
        in.readInt();
        if (in.readInt() != (int) juce::ByteOrder::littleEndianInt ("WAVE"))
            return 0;

        while (! in.isExhausted())
        {
            const int chunkId = in.readInt();
            const auto chunkSize = (juce::int64) (juce::uint32) in.readInt();
            const auto next = in.getPosition() + chunkSize + (chunkSize & 1);

            if (chunkId == (int) juce::ByteOrder::littleEndianInt ("clm ") && chunkSize < 1024)
            {
                juce::MemoryBlock text;
                in.readIntoMemoryBlock (text, (juce::pointer_sized_int) chunkSize);
                const auto s = text.toString();
                if (s.startsWith ("<!>"))
                    return s.substring (3).getIntValue();
                return 0;
            }

            if (! in.setPosition (next))
                break;
        }
        return 0;
    }

    /** Linear resampling of one cycle to kFrameSize, treating the cycle as periodic. */
    void resampleCycle (const float* src, int srcSize, float* dest)
    {
        for (int i = 0; i < Wavetable::kFrameSize; ++i)
        {
            const double pos = (double) i * (double) srcSize / (double) Wavetable::kFrameSize;
            const int a = (int) pos;
            const float frac = (float) (pos - (double) a);
            const float x0 = src[a % srcSize], x1 = src[(a + 1) % srcSize];
            dest[i] = x0 + frac * (x1 - x0);
        }
    }

    // ---- built-in sources ----------------------------------------------------------------------
    // All are generated at start-up from the factory wavetables and simple DSP (no recorded
    // material), stereo, 48 kHz, pitched at middle C (note 60) so the root key default is right.
    constexpr double kBuiltInRate = 48000.0;
    constexpr float kMiddleC = 261.6256f;

    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float bandPass (float x, float cutoff, float q) noexcept
        {
            const float g = std::tan (dsp::kPi * std::clamp (cutoff, 20.0f, 20000.0f) / (float) kBuiltInRate);
            const float k = 1.0f / q;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v1 = a1 * (ic1 + g * (x - ic2));
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v1;
        }
        float lowPass (float x, float cutoff) noexcept
        {
            const float g = std::tan (dsp::kPi * std::clamp (cutoff, 20.0f, 20000.0f) / (float) kBuiltInRate);
            const float k = 1.4142f;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v1 = a1 * (ic1 + g * (x - ic2));
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    SampleData newBuiltIn (const juce::String& name, double seconds)
    {
        SampleData s;
        s.name = "Built-in: " + name;
        s.sampleRate = kBuiltInRate;
        s.length = (int) (kBuiltInRate * seconds);
        s.stereo = true;
        s.left.assign ((size_t) s.length + 2 * SampleData::kGuard, 0.0f);
        s.right.assign ((size_t) s.length + 2 * SampleData::kGuard, 0.0f);
        return s;
    }

    /** Peak-normalises to `peak` and fades the ends so loops and grains never click. */
    void finishBuiltIn (SampleData& s, float peak)
    {
        float maxAbs = 1.0e-9f;
        for (int i = 0; i < s.length; ++i)
            maxAbs = std::max ({ maxAbs, std::abs (s.left[(size_t) (i + SampleData::kGuard)]), std::abs (s.right[(size_t) (i + SampleData::kGuard)]) });
        const float gain = peak / maxAbs;
        const int fade = (int) (kBuiltInRate * 0.02);
        for (int i = 0; i < s.length; ++i)
        {
            const float f = std::min (1.0f, (float) std::min (i, s.length - 1 - i) / (float) fade);
            s.left[(size_t) (i + SampleData::kGuard)] *= gain * f;
            s.right[(size_t) (i + SampleData::kGuard)] *= gain * f;
        }
    }

    void put (SampleData& s, int i, float l, float r)
    {
        s.left[(size_t) (i + SampleData::kGuard)] = l;
        s.right[(size_t) (i + SampleData::kGuard)] = r;
    }

    SampleData makeVowelDrift()
    {
        const auto& vowels = WavetableBank::getInstance().get (3);
        const auto& glass = WavetableBank::getInstance().get (7);
        const int mip = Wavetable::selectMip (kMiddleC / (float) kBuiltInRate, (float) kBuiltInRate);
        auto s = newBuiltIn ("Vowel Drift", 4.0);

        float phaseL = 0.0f, phaseR = 0.37f;
        for (int i = 0; i < s.length; ++i)
        {
            const float t = (float) i / (float) s.length;
            const float seconds = (float) i / (float) kBuiltInRate;
            const float scan = 0.5f - 0.5f * std::cos (dsp::kTwoPi * t);
            const float vibrato = std::exp2 (0.12f / 12.0f * std::sin (dsp::kTwoPi * 5.1f * seconds));
            const float swell = 0.75f + 0.25f * std::sin (dsp::kTwoPi * 0.5f * seconds);
            const float glassMix = 0.25f * t;
            const float l = dsp::lerp (vowels.sample (phaseL, scan, mip), glass.sample (phaseL, t, mip), glassMix);
            const float r = dsp::lerp (vowels.sample (phaseR, 1.0f - scan * 0.8f, mip), glass.sample (phaseR, t, mip), glassMix);
            put (s, i, l * swell, r * swell);
            phaseL = dsp::wrapPhase (phaseL + kMiddleC * vibrato / (float) kBuiltInRate);
            phaseR = dsp::wrapPhase (phaseR + kMiddleC * vibrato * 1.0015f / (float) kBuiltInRate);
        }
        finishBuiltIn (s, 0.6f);
        return s;
    }

    SampleData makeGlassBloom()
    {
        const auto& glass = WavetableBank::getInstance().get (7);
        const int mip = Wavetable::selectMip (2.0f * kMiddleC / (float) kBuiltInRate, (float) kBuiltInRate);
        auto s = newBuiltIn ("Glass Bloom", 5.0);

        float p1 = 0.0f, p2 = 0.25f, p3 = 0.5f;
        for (int i = 0; i < s.length; ++i)
        {
            const float t = (float) i / (float) s.length;
            const float bloom = 1.0f - std::exp (-t * 6.0f);
            const float scan = 0.1f + 0.8f * t;
            const float a = glass.sample (p1, scan, mip);
            const float b = glass.sample (p2, std::min (1.0f, scan + 0.15f), mip);
            const float octave = std::sin (dsp::kTwoPi * p3) * 0.35f * t;
            put (s, i, (a * 0.8f + octave) * bloom, (b * 0.8f + octave) * bloom);
            p1 = dsp::wrapPhase (p1 + kMiddleC / (float) kBuiltInRate);
            p2 = dsp::wrapPhase (p2 + kMiddleC * 1.0021f / (float) kBuiltInRate);
            p3 = dsp::wrapPhase (p3 + 2.0f * kMiddleC * 0.9993f / (float) kBuiltInRate);
        }
        finishBuiltIn (s, 0.6f);
        return s;
    }

    SampleData makeNightChoir()
    {
        const auto& vowels = WavetableBank::getInstance().get (3);
        const int mip = Wavetable::selectMip (kMiddleC / (float) kBuiltInRate, (float) kBuiltInRate);
        auto s = newBuiltIn ("Night Choir", 5.0);

        const float detune[3] = { -9.0f, 0.0f, 7.0f };
        const float panL[3] = { 0.9f, 0.6f, 0.25f }, panR[3] = { 0.25f, 0.6f, 0.9f };
        const float vibRate[3] = { 4.6f, 5.1f, 4.9f };
        float phase[3] = { 0.0f, 0.31f, 0.67f };
        dsp::Random32 rng;
        rng.seed (777u);
        Svf breathL, breathR;

        for (int i = 0; i < s.length; ++i)
        {
            const float seconds = (float) i / (float) kBuiltInRate;
            const float t = (float) i / (float) s.length;
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                // Slowly between "oo" and "ah", each voice on its own breath.
                const float scan = 0.55f + 0.2f * std::sin (dsp::kTwoPi * (0.11f + 0.03f * (float) v) * seconds + (float) v);
                const float x = vowels.sample (phase[v], scan, mip);
                l += x * panL[v];
                r += x * panR[v];
                const float vib = std::exp2 ((detune[v] + 9.0f * std::sin (dsp::kTwoPi * vibRate[v] * seconds)) / 1200.0f);
                phase[v] = dsp::wrapPhase (phase[v] + kMiddleC * vib / (float) kBuiltInRate);
            }
            const float swell = 0.7f + 0.3f * std::sin (dsp::kTwoPi * 0.2f * seconds - 1.5f);
            l += breathL.bandPass (rng.nextBipolar(), 2600.0f, 1.2f) * 0.35f;
            r += breathR.bandPass (rng.nextBipolar(), 2900.0f, 1.2f) * 0.35f;
            put (s, i, l * swell * (0.85f + 0.15f * t), r * swell * (0.85f + 0.15f * t));
        }
        finishBuiltIn (s, 0.6f);
        return s;
    }

    SampleData makeBreathAir()
    {
        auto s = newBuiltIn ("Breath Air", 4.0);
        dsp::Random32 rng;
        rng.seed (4242u);
        Svf f1L, f2L, f1R, f2R;
        float phase = 0.0f;

        for (int i = 0; i < s.length; ++i)
        {
            const float t = (float) i / (float) s.length;
            // Formants glide from "ah" towards "oo" and back.
            const float morph = 0.5f - 0.5f * std::cos (dsp::kTwoPi * t);
            const float f1 = dsp::lerp (750.0f, 320.0f, morph);
            const float f2 = dsp::lerp (1250.0f, 780.0f, morph);
            const float nl = rng.nextBipolar(), nr = rng.nextBipolar();
            const float tone = std::sin (dsp::kTwoPi * phase) * 0.06f;
            const float l = f1L.bandPass (nl, f1, 5.0f) * 0.9f + f2L.bandPass (nl, f2, 6.0f) * 0.6f + tone;
            const float r = f1R.bandPass (nr, f1 * 1.03f, 5.0f) * 0.9f + f2R.bandPass (nr, f2 * 0.97f, 6.0f) * 0.6f + tone;
            put (s, i, l, r);
            phase = dsp::wrapPhase (phase + kMiddleC / (float) kBuiltInRate);
        }
        finishBuiltIn (s, 0.55f);
        return s;
    }

    SampleData makeBellCloud()
    {
        auto s = newBuiltIn ("Bell Cloud", 6.0);
        const float ratios[] = { 1.0f, 2.0f, 2.76f, 4.07f, 5.40f, 8.93f };
        const float amps[] = { 1.0f, 0.45f, 0.6f, 0.3f, 0.22f, 0.1f };
        dsp::Random32 rng;
        rng.seed (90210u);

        double strikeAt = 0.0;
        while (strikeAt < 5.4)
        {
            const int start = (int) (strikeAt * kBuiltInRate);
            const float octave = rng.nextFloat() < 0.3f ? 2.0f : 1.0f;
            const float level = 0.5f + 0.5f * rng.nextFloat();
            const float pan = rng.nextBipolar() * 0.8f;
            const float gl = std::sqrt (0.5f * (1.0f - pan)), gr = std::sqrt (0.5f * (1.0f + pan));
            for (int k = 0; k < 6; ++k)
            {
                const float freq = kMiddleC * octave * ratios[k];
                if (freq > 18000.0f) continue;
                const float tau = 2.4f / (1.0f + ratios[k] * 0.45f);
                const float inc = freq / (float) kBuiltInRate;
                float phase = rng.nextFloat();
                for (int i = start; i < s.length; ++i)
                {
                    const float age = (float) (i - start) / (float) kBuiltInRate;
                    const float env = std::exp (-age / tau) * std::min (1.0f, age * 400.0f);
                    if (env < 1.0e-4f) break;
                    const float v = std::sin (dsp::kTwoPi * phase) * amps[k] * env * level;
                    s.left[(size_t) (i + SampleData::kGuard)] += v * gl;
                    s.right[(size_t) (i + SampleData::kGuard)] += v * gr;
                    phase = dsp::wrapPhase (phase + inc);
                }
            }
            strikeAt += 0.38 + 0.5 * rng.nextFloat();
        }
        finishBuiltIn (s, 0.6f);
        return s;
    }

    SampleData makeDeepDrone()
    {
        const auto& sweep = WavetableBank::getInstance().get (2);
        const int mip = Wavetable::selectMip (kMiddleC / (float) kBuiltInRate, (float) kBuiltInRate);
        auto s = newBuiltIn ("Deep Drone", 5.0);
        float p[3] = { 0.0f, 0.4f, 0.8f }, sub = 0.0f;
        const float cents[3] = { -7.0f, 0.0f, 6.0f };
        Svf lpL, lpR;

        for (int i = 0; i < s.length; ++i)
        {
            const float seconds = (float) i / (float) kBuiltInRate;
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                const float x = sweep.sample (p[v], 0.75f, mip);
                l += x * (v == 2 ? 0.4f : 0.8f);
                r += x * (v == 0 ? 0.4f : 0.8f);
                p[v] = dsp::wrapPhase (p[v] + kMiddleC * std::exp2 (cents[v] / 1200.0f) / (float) kBuiltInRate);
            }
            const float subWave = std::sin (dsp::kTwoPi * sub) * 0.9f;
            sub = dsp::wrapPhase (sub + 0.5f * kMiddleC / (float) kBuiltInRate);
            const float cutoff = 500.0f * std::exp2 (2.2f * (0.5f - 0.5f * std::cos (dsp::kTwoPi * seconds / 5.0f)));
            put (s, i, lpL.lowPass (l, cutoff) + subWave, lpR.lowPass (r, cutoff * 1.08f) + subWave);
        }
        finishBuiltIn (s, 0.65f);
        return s;
    }

    const std::vector<SampleData>& builtIns()
    {
        static const std::vector<SampleData> sources = []
        {
            std::vector<SampleData> v;
            v.push_back (makeVowelDrift());
            v.push_back (makeGlassBloom());
            v.push_back (makeNightChoir());
            v.push_back (makeBreathAir());
            v.push_back (makeBellCloud());
            v.push_back (makeDeepDrone());
            return v;
        }();
        return sources;
    }
} // namespace

// =============================================================================================
std::shared_ptr<const SampleData> SampleData::create (const juce::String& name, double sampleRate,
                                                      const float* left, const float* right, int length)
{
    return buildSample (name, sampleRate, left, right, length, nullptr);
}

std::shared_ptr<const UserWavetable> UserWavetable::create (const juce::String& name, std::vector<float> frames)
{
    return buildWavetable (name, std::move (frames), nullptr);
}

juce::ValueTree OscillatorAssets::toValueTree() const
{
    juce::ValueTree root (ids::assets);

    for (int o = 0; o < kNumOscillators; ++o)
    {
        const auto& s = slots[(size_t) o];
        if (s.wavetable == nullptr && s.sample == nullptr)
            continue;

        juce::ValueTree slot (ids::slot);
        slot.setProperty (ids::index, o, nullptr);

        if (s.wavetable != nullptr)
        {
            juce::ValueTree w (ids::wavetable);
            w.setProperty (ids::name, s.wavetable->name, nullptr);
            w.setProperty (ids::frames, s.wavetable->numFrames, nullptr);
            w.setProperty (ids::encoding, s.wavetable->storedEncoding, nullptr);
            w.setProperty (ids::data, s.wavetable->storedData, nullptr);
            slot.appendChild (w, nullptr);
        }

        if (s.sample != nullptr)
        {
            juce::ValueTree node (ids::sample);
            node.setProperty (ids::name, s.sample->name, nullptr);
            node.setProperty (ids::rate, s.sample->sampleRate, nullptr);
            node.setProperty (ids::channels, s.sample->stereo ? 2 : 1, nullptr);
            node.setProperty (ids::length, s.sample->length, nullptr);
            node.setProperty (ids::encoding, s.sample->storedEncoding, nullptr);
            node.setProperty (ids::data, s.sample->storedData, nullptr);
            slot.appendChild (node, nullptr);
        }

        root.appendChild (slot, nullptr);
    }

    return root;
}

OscillatorAssets OscillatorAssets::fromValueTree (const juce::ValueTree& root)
{
    OscillatorAssets result;
    if (! root.hasType (ids::assets))
        return result;

    for (const auto& slot : root)
    {
        const int o = slot.getProperty (ids::index, -1);
        if (o < 0 || o >= kNumOscillators)
            continue;

        const auto w = slot.getChildWithName (ids::wavetable);
        if (w.isValid())
        {
            const int frames = juce::jlimit (0, assets::kMaxWavetableFrames, (int) w.getProperty (ids::frames));
            std::vector<std::vector<float>> decoded;
            const auto encoding = w.getProperty (ids::encoding).toString();
            const auto data = w.getProperty (ids::data).toString();
            if (frames > 0 && decodeAudio (encoding, data, 1, frames * Wavetable::kFrameSize, decoded))
            {
                EncodedAudio encoded { encoding, data };
                result.slots[(size_t) o].wavetable = buildWavetable (w.getProperty (ids::name).toString(), std::move (decoded[0]), &encoded);
            }
        }

        const auto s = slot.getChildWithName (ids::sample);
        if (s.isValid())
        {
            const int channels = juce::jlimit (1, 2, (int) s.getProperty (ids::channels, 1));
            const double rate = s.getProperty (ids::rate, 48000.0);
            const int length = juce::jlimit (0, (int) (assets::kMaxSampleSeconds * 192000.0), (int) s.getProperty (ids::length));
            std::vector<std::vector<float>> decoded;
            const auto encoding = s.getProperty (ids::encoding).toString();
            const auto data = s.getProperty (ids::data).toString();
            if (length > 0 && rate > 0.0 && decodeAudio (encoding, data, channels, length, decoded))
            {
                EncodedAudio encoded { encoding, data };
                result.slots[(size_t) o].sample = buildSample (s.getProperty (ids::name).toString(), rate, decoded[0].data(),
                                                               channels == 2 ? decoded[1].data() : nullptr, length, &encoded);
            }
        }
    }

    return result;
}

// =============================================================================================
namespace assets
{
    juce::String getAudioFileWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg"; }

    std::shared_ptr<const SampleData> loadSample (const juce::File& file, juce::String& error)
    {
        auto reader = openReader (file, error);
        if (reader == nullptr)
            return nullptr;

        const auto maxLength = (juce::int64) (kMaxSampleSeconds * reader->sampleRate);
        const int length = (int) std::min (reader->lengthInSamples, maxLength);
        const int channels = std::min (2, (int) reader->numChannels);

        juce::AudioBuffer<float> buffer (channels, length);
        if (! reader->read (&buffer, 0, length, 0, true, channels > 1))
        {
            error = "Could not read the audio in \"" + file.getFileName() + "\".";
            return nullptr;
        }

        return SampleData::create (file.getFileNameWithoutExtension(), reader->sampleRate, buffer.getReadPointer (0),
                                   channels > 1 ? buffer.getReadPointer (1) : nullptr, length);
    }

    std::shared_ptr<const UserWavetable> loadWavetable (const juce::File& file, juce::String& error)
    {
        auto reader = openReader (file, error);
        if (reader == nullptr)
            return nullptr;

        const int totalLength = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) kMaxWavetableFrames * 4096);
        juce::AudioBuffer<float> buffer (1, totalLength);
        if (! reader->read (&buffer, 0, totalLength, 0, true, false))
        {
            error = "Could not read the audio in \"" + file.getFileName() + "\".";
            return nullptr;
        }

        int frameSize = readClmFrameSize (file);
        if (frameSize < 16 || frameSize > 16384)
        {
            frameSize = 0;
            for (int candidate : { 2048, 1024, 512, 256, 4096 })
                if (totalLength >= candidate && totalLength % candidate == 0)
                {
                    frameSize = candidate;
                    break;
                }
        }

        if (frameSize == 0)
        {
            if (totalLength < 16)
            {
                error = "\"" + file.getFileName() + "\" is too short to be a wavetable.";
                return nullptr;
            }
            // Not a multiple of any usual frame size: a short file is one cycle; a long one is cut into 2048-sample frames.
            frameSize = totalLength <= 4096 ? totalLength : 2048;
        }

        const int numFrames = juce::jlimit (1, kMaxWavetableFrames, totalLength / frameSize);
        std::vector<float> frames ((size_t) numFrames * Wavetable::kFrameSize);
        const float* src = buffer.getReadPointer (0);
        for (int f = 0; f < numFrames; ++f)
        {
            float* dest = frames.data() + (size_t) f * Wavetable::kFrameSize;
            if (frameSize == Wavetable::kFrameSize)
                std::copy (src + (size_t) f * (size_t) frameSize, src + (size_t) (f + 1) * (size_t) frameSize, dest);
            else
                resampleCycle (src + (size_t) f * (size_t) frameSize, frameSize, dest);
        }

        return UserWavetable::create (file.getFileNameWithoutExtension(), std::move (frames));
    }

    juce::StringArray getBuiltInSampleNames()
    {
        return { "Vowel Drift", "Glass Bloom", "Night Choir", "Breath Air", "Bell Cloud", "Deep Drone" };
    }

    const SampleData& getBuiltInSample (int index)
    {
        const auto& sources = builtIns();
        return sources[(size_t) juce::jlimit (0, (int) sources.size() - 1, index)];
    }
} // namespace assets

} // namespace nedd
