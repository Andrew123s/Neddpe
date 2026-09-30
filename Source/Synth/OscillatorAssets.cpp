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

    // ---- built-in source -----------------------------------------------------------------------
    SampleData makeBuiltIn()
    {
        constexpr double rate = 48000.0;
        constexpr int length = (int) (rate * 4.0);
        constexpr float pitchHz = 261.6256f;   // middle C

        const auto& vowels = WavetableBank::getInstance().get (3);
        const auto& glass = WavetableBank::getInstance().get (7);
        const int mip = Wavetable::selectMip (pitchHz / (float) rate, (float) rate);

        SampleData s;
        s.name = "Built-in: Vowel Drift";
        s.sampleRate = rate;
        s.length = length;
        s.stereo = true;
        s.left.assign ((size_t) length + 2 * SampleData::kGuard, 0.0f);
        s.right.assign ((size_t) length + 2 * SampleData::kGuard, 0.0f);

        float phaseL = 0.0f, phaseR = 0.37f;
        for (int i = 0; i < length; ++i)
        {
            const float t = (float) i / (float) length;
            const float seconds = (float) i / (float) rate;
            const float scan = 0.5f - 0.5f * std::cos (dsp::kTwoPi * t);                     // 0 -> 1 -> 0
            const float vibrato = std::exp2 (0.12f / 12.0f * std::sin (dsp::kTwoPi * 5.1f * seconds));
            const float swell = 0.75f + 0.25f * std::sin (dsp::kTwoPi * 0.5f * seconds);
            const float fade = std::min (1.0f, std::min (t, 1.0f - t) * 40.0f);             // no clicks at the ends

            const float glassMix = 0.25f * t;
            const float l = dsp::lerp (vowels.sample (phaseL, scan, mip), glass.sample (phaseL, t, mip), glassMix);
            const float r = dsp::lerp (vowels.sample (phaseR, 1.0f - scan * 0.8f, mip), glass.sample (phaseR, t, mip), glassMix);
            s.left[(size_t) (i + SampleData::kGuard)] = 0.6f * l * swell * fade;
            s.right[(size_t) (i + SampleData::kGuard)] = 0.6f * r * swell * fade;

            phaseL = dsp::wrapPhase (phaseL + pitchHz * vibrato / (float) rate);
            phaseR = dsp::wrapPhase (phaseR + pitchHz * vibrato * 1.0015f / (float) rate);
        }
        return s;
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

    const SampleData& getBuiltInSample()
    {
        static const SampleData builtIn = makeBuiltIn();
        return builtIn;
    }
} // namespace assets

} // namespace nedd
