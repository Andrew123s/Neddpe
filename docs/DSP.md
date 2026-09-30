# NeddPE DSP

All processing is 32-bit float. Denormals are flushed (`ScopedNoDenormals`) for every block.

## Oscillators (`Voices/Oscillator.*`)

Each voice has three oscillators, each with up to 8 unison sub-voices spread symmetrically over up to +-50 cents
and across the stereo field (equal-power pans, `1/sqrt(n)` level normalisation). Unison sub-voices start at
golden-ratio-spaced phases so a stack never phase-cancels on the attack; *Phase Random* adds per-note randomness.

| Engine | Implementation | Aliasing strategy |
|---|---|---|
| Analog | Sine (table), triangle (naive; harmonics fall at 12 dB/oct), saw and square/pulse with PolyBLEP; pulse DC is removed | PolyBLEP residuals at every discontinuity |
| Wavetable | 32-frame tables, 2048-sample frames, bilinear interpolation across phase and frame | 10 mip levels per frame (harmonics 1024 >> m); the mip is chosen per unison voice so aliases fold back above ~18 kHz |
| FM | 3 operators per unison voice (carrier + 2 modulators): Stack (2>1>C), Parallel (1+2>C), Branch (2>1>C and 2>C). Ratios x0.25-x16 plus fine offset, operator feedback with 2-sample averaging, envelope and key-tracking of the index | Inherent to FM; kept moderate by index scaling and key tracking |
| Noise | White, pink (Kellet), brown, crackle (pitch-dependent density), digital (sample & hold at the note frequency) | n/a |

Interactions, per sample: cross-FM (phase modulation from any oscillator; later oscillators use the previous
sample), hard sync to the previous oscillator with sub-sample reset position, ring modulation with the previous
oscillator. Each oscillator can bypass the filter (*Route: Direct*).

Known limitation: sync resets and cross-FM are not band-limited, so heavy sync/FM at high pitches aliases.

### Wavetables (`DSP/Wavetable.*`)

Ten tables are generated procedurally at start-up (no sample assets, no third-party content): Basic Shapes,
PWM, Harmonic Sweep, Formant Vowels, Growl, Sync Sweep, Digital Steps, Glass, Drawbars, Sine Fold. Time-domain
designs are rendered at 16384 samples per cycle and converted to a spectrum with an FFT; spectral designs define
harmonic amplitudes directly. Each mip level is an inverse FFT of the truncated spectrum, normalised with the
mip-0 gain so levels match across mips. The bank (~26 MB) is shared by all plugin instances in a process.

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

Measured costs are in [TESTING.md](TESTING.md#performance).
