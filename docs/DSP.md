# NeddPE DSP

All processing is 32-bit float. Denormals are flushed (`ScopedNoDenormals`) for every block.

## Oscillators (`Voices/Oscillator.*`)

Each voice has three oscillators, each with up to 8 unison sub-voices spread symmetrically over up to +-50 cents
and across the stereo field (equal-power pans, `1/sqrt(n)` level normalisation). Unison sub-voices start at
golden-ratio-spaced phases so a stack never phase-cancels on the attack; *Phase Random* adds per-note randomness.

| Engine | Implementation | Aliasing strategy |
|---|---|---|
| Analog | Sine (degree-11 polynomial), triangle (naive; harmonics fall at 12 dB/oct), saw and square/pulse with PolyBLEP; pulse DC is removed | PolyBLEP residuals at every discontinuity; under phase modulation the BLEP width follows the instantaneous phase increment |
| Wavetable | 32-frame factory tables or an imported table (up to 256 frames), 2048-sample frames, bilinear interpolation across phase and frame | 10 mip levels per frame (harmonics 1024 >> m); the mip is chosen per unison voice so aliases fold back above ~18 kHz (when oversampled, everything stays below the oversampled Nyquist frequency) |
| FM | 3 operators per unison voice (carrier + 2 modulators): Stack (2>1>C), Parallel (1+2>C), Branch (2>1>C and 2>C). Ratios x0.25-x16 plus fine offset, operator feedback with 2-sample averaging, envelope and key-tracking of the index | Index scaling and key tracking; set *Oscillator Oversampling* to 2x/4x for extreme indices |
| Noise | White, pink (Kellet), brown, crackle (pitch-dependent density), digital (sample & hold at the note frequency) | n/a (rendered at the base rate and held when oversampled, so its spectrum does not change) |
| Granular | Up to 32 overlapping Hann-windowed grains per oscillator per note, read from the oscillator's sample (or the built-in source) with 4-point Hermite interpolation. Position (the WT Position control, so MPE Slide can scan it per note), size 5 ms-1 s, density 1-200 grains/s, position spray, pitch spray (+-12 st), stereo scatter from *Spread*. Grain pitch follows the note relative to *Sample Root Key*. Level is normalised by `1/sqrt(overlap)` | Hermite interpolation |
| Sample | Pitched playback of the oscillator's sample from a start point (WT Position control), one-shot or looped to the end with a cross-fade (up to 10 ms) at the loop point. Unison, detune, spread, cross-FM (none: phase mod is ignored), sync (restarts from the start point) all apply | Hermite interpolation; transposing far up aliases unless oversampled |

Interactions, per sample: cross-FM (phase modulation from any oscillator; later oscillators use the previous
sample), hard sync to the previous oscillator with sub-sample reset position, ring modulation with the previous
oscillator. Each oscillator can bypass the filter (*Route: Direct*).

### SIMD unison

Unison sub-voices of the Analog and FM engines are rendered four at a time (`DSP/Simd.h`: SSE2 on x86-64, a portable
fallback elsewhere). Phases, increments, pan gains and FM operator state are stored per lane; the waveform switch is
taken once per group of four instead of once per sub-voice, and the sine used by FM operators and the analog sine is
a polynomial rather than a table lookup, so it vectorises. An 8-voice stack is two passes instead of eight. Wavetable,
Sample and Granular sub-voices are still rendered one at a time (their table/sample reads are gathers).

### Anti-aliasing of sync and cross-modulation

- **Band-limited hard sync.** When the master wraps, the slave resets at the exact sub-sample instant. The jump h
  between the slave's value just before the reset and its value at the reset point is band-limited with a two-sided
  PolyBLEP step: the previous sample gets `+h/2 * d^2` and the current one `-h/2 * (1-d)^2`, where d is the time since
  the reset in samples. To correct the previous sample, every oscillator outputs with a fixed one-sample delay.
  This works for every engine that can be synced (Analog, Wavetable, FM, Sample).
- **Oscillator oversampling** (`osc_oversampling`: Off / Auto / 2x / 4x, Settings page). The oscillators of a note
  run at 2x or 4x the sample rate, including cross-FM, ring modulation and sync, and the result is decimated by a
  47-tap Kaiser half-band FIR per stage (`DSP/Decimator.h`: passband to 0.39 fs, stopband below -80 dB from 0.61 fs,
  11.5 samples latency per 2x stage). Auto oversamples 2x only the notes that use cross-FM (amount > 0 or routed in
  the matrix), ring modulation or hard sync; the choice is made when the note starts so the latency never changes
  under a sounding note. Envelopes, filter and everything after the oscillators stay at the base rate.
- Cross-FM through oversampling reduces aliasing a lot but does not remove it completely: phase modulation creates
  sidebands without limit, so very deep cross-FM on high notes can still produce some above 2x/4x Nyquist.

### Wavetables (`DSP/Wavetable.*`)

Ten tables are generated procedurally at start-up (no sample assets, no third-party content): Basic Shapes,
PWM, Harmonic Sweep, Formant Vowels, Growl, Sync Sweep, Digital Steps, Glass, Drawbars, Sine Fold. Time-domain
designs are rendered at 16384 samples per cycle and converted to a spectrum with an FFT; spectral designs define
harmonic amplitudes directly. Each mip level is an inverse FFT of the truncated spectrum, normalised with the
mip-0 gain so levels match across mips. The bank (~26 MB) is shared by all plugin instances in a process.

### Imported wavetables and samples (`Synth/OscillatorAssets.*`)

Each oscillator can hold one imported wavetable and one imported sample (*Import* on the oscillator panel).

- **Wavetables**: WAV/AIFF/FLAC/OGG. The frame size comes from a Serum-style `clm ` chunk if present, otherwise the
  first of 2048, 1024, 512, 256 or 4096 samples that divides the file length; a short file that fits none is one
  single cycle, a long one is cut into 2048-sample frames. Frames are resampled to 2048 samples and go through the
  same FFT mip-mapping as the factory tables (DC removed, each frame normalised). Up to 256 frames. Select
  *Imported* in the wavetable list to play it.
- **Samples**: WAV/AIFF/FLAC/OGG, mono or stereo (first two channels), up to 60 s, kept at their own sample rate.
  Used by the Granular and Sample engines; without an import they play a built-in 4-second vowel texture generated
  at start-up from the factory tables (root C4).
- Imported content is part of the sound: saved inside presets and projects (24-bit FLAC, base64; raw float if the
  audio exceeds full scale), restored by undo, and handed to the audio thread through a `RealtimeExchange` so the
  audio thread never allocates or frees it.

## Filter (`DSP/VoiceFilter.h`)

Stereo, per voice:

- **SVF modes** (LP12, HP12, BP, Notch): topology-preserving-transform state variable filter (Zavalishin/Simper),
  stable under fast modulation. Band-pass is peak-normalised.
- **LP24 / HP24**: two cascaded SVF stages tuned as a Butterworth pair; resonance sharpens the second stage.
- **Ladder**: zero-delay-feedback 4-pole ladder. The feedback equation is solved linearly and the loop input is
  saturated with tanh; passband loss at high resonance is partly compensated. Self-oscillates near 100%.

Cutoff = base + key tracking (relative to C4, including bend and glide) + filter envelope x amount x velocity
scaling (+-96 st at 100%) + matrix. Coefficients (`g = tan(pi fc/fs)` and damping) are computed per control block
and interpolated per sample; cutoff is clamped to 0.45 fs. Pre-filter drive is a tanh with gain makeup.

## Envelopes (`DSP/Envelope.h`)

Delay / Attack / Hold / Decay / Sustain / Release with a curvature per segment:
`f(t) = t(1+a)/(1+at)` for fast-start curves and its mirror for slow-start curves (one division per sample).
Every segment interpolates from the level it started at, so retriggers and legato never jump. Segment times are
re-read each control block, so modulating attack/decay/release acts on a running segment. The amp envelope has a
0.5 ms minimum attack and 2 ms minimum release to prevent clicks.

## LFOs (`DSP/Lfo.h`)

Sine, triangle, saw, reverse saw, square, sample & hold, smooth random and a user-drawn custom curve (up to 32
points, rendered to a 256-point table). Rate 0.01-100 Hz or tempo-synced (8/1 ... 1/64 including dotted and
triplet). Retriggered LFOs start at note-on; free LFOs start in phase with the global instance and synced free LFOs
lock to the host position. Fade-in and depth are per voice. LFOs run at control rate (up to 6 kHz at Ultra).

## Modulation matrix (`Modulation/*`)

16 slots: source, amount (-100..+100%), destination, curve (linear, exponential, logarithmic, S, 8-step),
polarity (unipolar/bipolar). Voice-scope destinations are evaluated for every voice at control rate; global ones
once per block. A destination's unit is fixed (e.g. cutoff: 96 st, envelope times: 4 octaves, levels: 1.0) so
100% means the same thing everywhere.

## A/B morph

A is always the live patch, B a stored snapshot. Continuous parameters interpolate in their normalised (perceptual)
range, discrete ones switch at 50%. The morph position is `morph_pos` plus the per-voice *Morph* destination, so
routing MPE Slide to Morph morphs each note independently; effects morph with the base position plus the most
recent note.

## Effects (`Effects/*`)

| Effect | Implementation |
|---|---|
| Distortion | Soft (tanh), hard clip, sine wavefolder, asymmetric, tube (exponential). 1x/2x/4x/8x IIR polyphase oversampling by CPU quality (Eco/Normal/High/Ultra); tone low-pass, DC blocker, level compensation keeping a -10 dBFS signal constant |
| Saturation | Asymmetric tanh (even harmonics), warmth low-pass, DC blocker |
| Bitcrush | Bit depth 2-16 (fractional) and sample-and-hold rate reduction |
| Chorus | Two modulated taps per channel (14 ms centre), quadrature LFOs |
| Phaser | 6 first-order all-pass stages per channel, 180 Hz-4 kHz sweep, feedback |
| Flanger | 0.1-4 ms modulated delay with +-95% feedback (tanh in the loop) |
| Delay | Stereo or ping-pong, synced or free (to 2.5 s), damping in the loop, delay-time changes glide like tape |
| Reverb | 8-line feedback delay network, Householder feedback matrix, per-line damping, RT60-based gains (0.35-11 s), pre-delay, two slowly modulated lines, width |
| EQ | Low shelf 120 Hz, peak (150 Hz-10 kHz, Q 0.9), high shelf 8 kHz; allocation-free coefficient design |
| Compressor | Stereo-linked, feed-forward, 6 dB soft knee, attack/release, makeup |
| Limiter | No look-ahead (no latency): fast gain follower to -0.3 dBFS plus a soft clip for the few samples that pass the attack |

Delay and reverb inputs are the **per-voice send buses**: each voice adds its output x its own send level, so
modulating *Delay Send* / *Reverb Send* per note decides which notes echo or bloom.

## Smoothing and click prevention

- Expression: one-pole smoothing per note (`mpe_smooth`), pitch at half the time.
- All voice targets (oscillator levels, filter coefficients, filter mix, gain, pan, sends) ramp linearly per sample.
- Effect mixes/drives ramp per block; delay time glides.
- Stolen voices fade over 3 ms on a spare voice; master volume is smoothed (30 ms).

## CPU quality

| Quality | Control block | Distortion oversampling |
|---|---|---|
| Eco | 64 samples | none |
| Normal | 32 | 2x |
| High | 16 | 4x |
| Ultra | 8 | 8x |

Oscillator oversampling is a separate setting (see *Anti-aliasing of sync and cross-modulation* above).

Measured costs are in [TESTING.md](TESTING.md#performance).
