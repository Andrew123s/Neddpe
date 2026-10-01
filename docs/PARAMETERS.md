# NeddPE parameter reference

Generated from `Source/Parameters/ParameterDefs.cpp` by `NeddPETests --dump-params`. Do not edit by hand.

- **ID** is the stable identifier stored in DAW projects and presets. IDs are never renamed.
- **Auto**: automatable by the host. Matrix routing choices are deliberately not automatable.
- **Morph**: included in A/B morphing and mutation. **Scope**: `voice` parameters are evaluated per note.

Total: 360 parameters.

## Oscillators

Shown for OSC 1 (`osc1_`); OSC 2 and 3 use `osc2_` / `osc3_`.

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `osc1_on` | OSC 1 On | bool | off / on | On | yes | yes | voice |
| `osc1_engine` | OSC 1 Engine | choice | Analog / Wavetable / FM / Noise / Granular / Sample | Analog | yes | yes | voice |
| `osc1_wave` | OSC 1 Waveform | choice | Sine / Triangle / Saw / Square / Pulse | Saw | yes | yes | voice |
| `osc1_pw` | OSC 1 Pulse Width | float | 2% .. 98% | 50% | yes | yes | voice |
| `osc1_table` | OSC 1 Wavetable | choice | Basic Shapes / PWM / Harmonic Sweep / Formant Vowels / Growl / Sync Sweep / Digital Steps / Glass / Drawbars / Sine Fold / Imported | Basic Shapes | yes | yes | voice |
| `osc1_wtpos` | OSC 1 WT Position | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_octave` | OSC 1 Octave | int | -4 oct .. +4 oct | 0 oct | yes | yes | voice |
| `osc1_semi` | OSC 1 Semitone | int | -12 st .. +12 st | 0 st | yes | yes | voice |
| `osc1_fine` | OSC 1 Fine | float | -100 ct .. +100 ct | 0 ct | yes | yes | voice |
| `osc1_phase` | OSC 1 Phase | float | 0° .. 360° | 0° | yes | yes | voice |
| `osc1_phrand` | OSC 1 Phase Random | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_level` | OSC 1 Level | float | 0% .. 100% | 75% | yes | yes | voice |
| `osc1_pan` | OSC 1 Pan | float | L100 .. R100 | C | yes | yes | voice |
| `osc1_unison` | OSC 1 Unison | int | 1 .. 8 | 1 | yes | yes | voice |
| `osc1_detune` | OSC 1 Detune | float | 0% .. 100% | 20% | yes | yes | voice |
| `osc1_spread` | OSC 1 Stereo Spread | float | 0% .. 100% | 60% | yes | yes | voice |
| `osc1_fmsrc` | OSC 1 FM Source | choice | OSC 1 / OSC 2 / OSC 3 | OSC 3 | yes | yes | voice |
| `osc1_fm` | OSC 1 FM Amount | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_sync` | OSC 1 Hard Sync | bool | off / on | Off | yes | yes | voice |
| `osc1_ring` | OSC 1 Ring Mod | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_route` | OSC 1 Route | choice | Filter / Direct | Filter | yes | yes | voice |
| `osc1_noise` | OSC 1 Noise Type | choice | White / Pink / Brown / Crackle / Digital | White | yes | yes | voice |
| `osc1_fmalgo` | OSC 1 FM Algorithm | choice | Stack 2>1>C / Parallel 1+2>C / Branch 2>1+C | Stack 2>1>C | yes | yes | voice |
| `osc1_op1ratio` | OSC 1 Mod 1 Ratio | choice | x0.25 / x0.50 / x1 / x2 / x3 / x4 / x5 / x6 / x7 / x8 / x9 / x10 / x11 / x12 / x13 / x14 / x15 / x16 | x2 | yes | yes | voice |
| `osc1_op2ratio` | OSC 1 Mod 2 Ratio | choice | x0.25 / x0.50 / x1 / x2 / x3 / x4 / x5 / x6 / x7 / x8 / x9 / x10 / x11 / x12 / x13 / x14 / x15 / x16 | x1 | yes | yes | voice |
| `osc1_opfine` | OSC 1 Mod Fine Ratio | float | -0.500 .. +0.500 | +0.000 | yes | yes | voice |
| `osc1_op1amt` | OSC 1 Mod 1 Amount | float | 0% .. 100% | 35% | yes | yes | voice |
| `osc1_op2amt` | OSC 1 Mod 2 Amount | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_fmfb` | OSC 1 FM Feedback | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_fmenv` | OSC 1 FM Env Amount | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_fmkt` | OSC 1 FM Key Track | float | 0% .. 100% | 0% | yes | yes | voice |
| `osc1_root` | OSC 1 Sample Root Key | int | C-1 .. G9 | C4 | yes | yes | voice |
| `osc1_loop` | OSC 1 Sample Loop | bool | off / on | On | yes | yes | voice |
| `osc1_gsize` | OSC 1 Grain Size | float | 5.0 ms .. 1.00 s | 80 ms | yes | yes | voice |
| `osc1_gdensity` | OSC 1 Grain Density | float | 1.0 /s .. 200 /s | 30 /s | yes | yes | voice |
| `osc1_gspray` | OSC 1 Grain Position Spray | float | 0% .. 100% | 10% | yes | yes | voice |
| `osc1_gpitch` | OSC 1 Grain Pitch Spray | float | 0.0 st .. 12.0 st | 0.0 st | yes | yes | voice |
| `osc1_source` | OSC 1 Built-in Source | choice | Vowel Drift / Glass Bloom / Night Choir / Breath Air / Bell Cloud / Deep Drone | Vowel Drift | yes | yes | voice |

## Filter

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `flt_on` | Filter On | bool | off / on | On | yes | yes | voice |
| `flt_type` | Filter Type | choice | LP 12 / LP 24 / HP 12 / HP 24 / Band Pass / Notch / Ladder LP | LP 24 | yes | yes | voice |
| `flt_cutoff` | Filter Cutoff | float | 20.0 Hz .. 20.0 kHz | 6.00 kHz | yes | yes | voice |
| `flt_reso` | Filter Resonance | float | 0% .. 100% | 15% | yes | yes | voice |
| `flt_drive` | Filter Drive | float | 0% .. 100% | 0% | yes | yes | voice |
| `flt_keytrack` | Filter Key Track | float | 0% .. 100% | 30% | yes | yes | voice |
| `flt_env` | Filter Env Amount | float | -100% .. +100% | +20% | yes | yes | voice |
| `flt_vel` | Filter Env Velocity | float | 0% .. 100% | 30% | yes | yes | voice |
| `flt_mix` | Filter Mix | float | 0% .. 100% | 100% | yes | yes | voice |

## Amp

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `amp_level` | Amp Level | float | -inf dB .. 0.0 dB | -3.1 dB | yes | yes | voice |
| `amp_vel` | Amp Velocity Sens | float | 0% .. 100% | 60% | yes | yes | voice |
| `amp_pressure` | Amp Pressure Sens | float | 0% .. 100% | 0% | yes | yes | voice |
| `amp_pan` | Amp Pan | float | L100 .. R100 | C | yes | yes | voice |

## Envelopes

Shown for the amp envelope (`aenv_`); filter and mod envelopes use `fenv_` / `menv_`.

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `aenv_delay` | Amp Env Delay | float | 0.0 ms .. 5.00 s | 0.0 ms | yes | yes | voice |
| `aenv_attack` | Amp Env Attack | float | 0.0 ms .. 20.00 s | 4.0 ms | yes | yes | voice |
| `aenv_hold` | Amp Env Hold | float | 0.0 ms .. 5.00 s | 0.0 ms | yes | yes | voice |
| `aenv_decay` | Amp Env Decay | float | 1.0 ms .. 20.00 s | 350 ms | yes | yes | voice |
| `aenv_sustain` | Amp Env Sustain | float | 0% .. 100% | 80% | yes | yes | voice |
| `aenv_release` | Amp Env Release | float | 1.0 ms .. 20.00 s | 250 ms | yes | yes | voice |
| `aenv_acurve` | Amp Env Attack Curve | float | -100% .. +100% | +20% | yes | yes | voice |
| `aenv_dcurve` | Amp Env Decay Curve | float | -100% .. +100% | +50% | yes | yes | voice |
| `aenv_rcurve` | Amp Env Release Curve | float | -100% .. +100% | +50% | yes | yes | voice |

## LFOs

Shown for LFO 1 (`lfo1_`); LFO 2 and 3 use `lfo2_` / `lfo3_`.

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `lfo1_shape` | LFO 1 Shape | choice | Sine / Triangle / Saw / Reverse Saw / Square / Sample & Hold / Smooth Random / Custom | Sine | yes | yes | voice |
| `lfo1_rate` | LFO 1 Rate | float | 0.01 Hz .. 100 Hz | 2.00 Hz | yes | yes | voice |
| `lfo1_sync` | LFO 1 Tempo Sync | bool | off / on | Off | yes | yes | voice |
| `lfo1_div` | LFO 1 Division | choice | 8/1 / 4/1 / 2/1 / 1/1 / 1/2 / 1/2T / 1/4. / 1/4 / 1/4T / 1/8. / 1/8 / 1/8T / 1/16. / 1/16 / 1/16T / 1/32 / 1/32T / 1/64 | 1/4 | yes | yes | voice |
| `lfo1_phase` | LFO 1 Phase | float | 0% .. 100% | 0% | yes | yes | voice |
| `lfo1_fade` | LFO 1 Fade In | float | 0.0 ms .. 10.00 s | 0.0 ms | yes | yes | voice |
| `lfo1_retrig` | LFO 1 Retrigger | bool | off / on | On | yes | yes | voice |
| `lfo1_amount` | LFO 1 Amount | float | 0% .. 100% | 100% | yes | yes | voice |

## Modulation matrix

Shown for slot 1 (`mod1_`); slots 2-16 use `mod2_` ... `mod16_`.

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `mod1_src` | Mod 1 Source | choice | None / MPE Pitch / MPE Pressure / MPE Slide / Velocity / Release Velocity / Mod Wheel / Pitch Bend / Aftertouch / LFO 1 / LFO 2 / LFO 3 / Amp Env / Filter Env / Mod Env / Random / Sample & Hold / Key Position / Note Number / Gate / Macro 1 / Macro 2 / Macro 3 / Macro 4 | MPE Pitch | no |  | global |
| `mod1_dst` | Mod 1 Destination | choice | None / Pitch / OSC 1 Pitch / OSC 2 Pitch / OSC 3 Pitch / OSC 1 WT Position / OSC 2 WT Position / OSC 3 WT Position / OSC 1 FM Amount / OSC 2 FM Amount / OSC 3 FM Amount / OSC 1 Pulse Width / OSC 2 Pulse Width / OSC 3 Pulse Width / OSC 1 Level / OSC 2 Level / OSC 3 Level / Unison Detune / Stereo Width / Filter Cutoff / Filter Resonance / Filter Drive / Filter Env Amount / Filter Mix / Amp Level / Pan / Amp Attack / Amp Decay / Amp Release / LFO 1 Rate / LFO 2 Rate / LFO 3 Rate / LFO 1 Depth / LFO 2 Depth / LFO 3 Depth / Delay Send / Reverb Send / Morph A/B / Distortion Drive / Distortion Mix / Saturation Drive / Bitcrush Amount / Chorus Mix / Phaser Mix / Flanger Mix / Delay Feedback / Delay Return / Reverb Size / Reverb Return / Arp Gate / Arp Probability / Grain Size / Grain Density | Pitch | no |  | global |
| `mod1_amt` | Mod 1 Amount | float | -100% .. +100% | +100% | yes |  | global |
| `mod1_curve` | Mod 1 Curve | choice | Linear / Exponential / Logarithmic / S-Curve / Stepped | Linear | no |  | global |
| `mod1_pol` | Mod 1 Polarity | choice | Unipolar / Bipolar | Bipolar | no |  | global |

## Macros

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `macro1` | Macro 1 | float | 0% .. 100% | 0% | yes |  | global |
| `macro2` | Macro 2 | float | 0% .. 100% | 0% | yes |  | global |
| `macro3` | Macro 3 | float | 0% .. 100% | 0% | yes |  | global |
| `macro4` | Macro 4 | float | 0% .. 100% | 0% | yes |  | global |

## Master

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `master_vol` | Master Volume | float | -60.0 dB .. +6.0 dB | -6.0 dB | yes |  | global |
| `quality` | CPU Quality | choice | Eco / Normal / High / Ultra | Normal | yes |  | global |

## Voicing

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `voice_mode` | Voice Mode | choice | Poly / Mono / Legato | Poly | yes |  | global |
| `voice_poly` | Polyphony | int | 1 .. 32 | 16 | yes |  | global |
| `voice_glide` | Glide Time | float | 0.0 ms .. 5.00 s | 80 ms | yes | yes | voice |
| `voice_glidemode` | Glide Mode | choice | Off / Always / Legato | Off | yes |  | global |

## MPE

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `mpe_mode` | MPE Mode | choice | Off (Legacy) / Lower Zone / Upper Zone / Both Zones | Lower Zone | yes |  | global |
| `mpe_bendrange` | MPE Pitch Bend Range | int | +1 st .. +96 st | +48 st | yes |  | global |
| `mpe_masterbend` | Master Pitch Bend Range | int | 0 st .. +48 st | +2 st | yes |  | global |
| `mpe_pitchsens` | MPE Pitch Sensitivity | float | 0% .. 200% | 100% | yes |  | global |
| `mpe_velcurve` | Velocity Curve | float | -100% .. +100% | 0% | yes |  | global |
| `mpe_prescurve` | Pressure Curve | float | -100% .. +100% | 0% | yes |  | global |
| `mpe_slidecurve` | Slide Curve | float | -100% .. +100% | 0% | yes |  | global |
| `mpe_smooth` | Expression Smoothing | float | 0.0 ms .. 100 ms | 8.0 ms | yes |  | global |

## Tuning

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `scale_type` | Scale | choice | Chromatic (12-TET) / Major / Natural Minor / Harmonic Minor / Dorian / Phrygian / Lydian / Mixolydian / Major Pentatonic / Minor Pentatonic / Blues / Whole Tone / Octave / User | Chromatic (12-TET) | yes |  | global |
| `scale_root` | Scale Root | choice | C / C# / D / D# / E / F / F# / G / G# / A / A# / B | C | yes |  | global |
| `scale_bendq` | Pitch Quantize | float | 0% .. 100% | 0% | yes |  | global |
| `tune_custom` | Custom Tuning | bool | off / on | Off | yes |  | global |

## LFOs

Shown for LFO 1 (`lfo1_`); LFO 2 and 3 use `lfo2_` / `lfo3_`.

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `sh_div` | S&H Rate | choice | 8/1 / 4/1 / 2/1 / 1/1 / 1/2 / 1/2T / 1/4. / 1/4 / 1/4T / 1/8. / 1/8 / 1/8T / 1/16. / 1/16 / 1/16T / 1/32 / 1/32T / 1/64 | 1/16 | yes |  | global |

## Morph

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `morph_on` | Morph Enabled | bool | off / on | Off | yes |  | global |
| `morph_pos` | Morph A/B | float | 0% .. 100% | 0% | yes |  | global |

## Master

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `midi_out` | MPE MIDI Out | bool | off / on | Off | yes |  | global |
| `osc_oversampling` | Oscillator Oversampling | choice | Off / Auto / 2x / 4x | Auto | yes |  | global |

## Effects

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `dist_on` | Distortion On | bool | off / on | Off | yes | yes | global |
| `dist_type` | Distortion Type | choice | Soft Clip / Hard Clip / Wavefold / Asymmetric / Tube | Soft Clip | yes | yes | global |
| `dist_drive` | Distortion Drive | float | 0% .. 100% | 30% | yes | yes | global |
| `dist_tone` | Distortion Tone | float | 0% .. 100% | 70% | yes | yes | global |
| `dist_mix` | Distortion Mix | float | 0% .. 100% | 100% | yes | yes | global |
| `sat_on` | Saturation On | bool | off / on | Off | yes | yes | global |
| `sat_drive` | Saturation Drive | float | 0% .. 100% | 30% | yes | yes | global |
| `sat_warmth` | Saturation Warmth | float | 0% .. 100% | 50% | yes | yes | global |
| `sat_mix` | Saturation Mix | float | 0% .. 100% | 100% | yes | yes | global |
| `crush_on` | Bitcrush On | bool | off / on | Off | yes | yes | global |
| `crush_bits` | Bitcrush Bits | float | 2.0 bit .. 16.0 bit | 10.0 bit | yes | yes | global |
| `crush_rate` | Bitcrush Downsample | float | /1.0 .. /32.0 | /1.0 | yes | yes | global |
| `crush_mix` | Bitcrush Mix | float | 0% .. 100% | 100% | yes | yes | global |
| `chorus_on` | Chorus On | bool | off / on | Off | yes | yes | global |
| `chorus_rate` | Chorus Rate | float | 0.05 Hz .. 5.00 Hz | 0.60 Hz | yes | yes | global |
| `chorus_depth` | Chorus Depth | float | 0% .. 100% | 50% | yes | yes | global |
| `chorus_mix` | Chorus Mix | float | 0% .. 100% | 40% | yes | yes | global |
| `phaser_on` | Phaser On | bool | off / on | Off | yes | yes | global |
| `phaser_rate` | Phaser Rate | float | 0.02 Hz .. 8.00 Hz | 0.30 Hz | yes | yes | global |
| `phaser_depth` | Phaser Depth | float | 0% .. 100% | 70% | yes | yes | global |
| `phaser_fb` | Phaser Feedback | float | 0% .. 95% | 50% | yes | yes | global |
| `phaser_mix` | Phaser Mix | float | 0% .. 100% | 50% | yes | yes | global |
| `flanger_on` | Flanger On | bool | off / on | Off | yes | yes | global |
| `flanger_rate` | Flanger Rate | float | 0.02 Hz .. 5.00 Hz | 0.20 Hz | yes | yes | global |
| `flanger_depth` | Flanger Depth | float | 0% .. 100% | 70% | yes | yes | global |
| `flanger_fb` | Flanger Feedback | float | -95% .. +95% | +60% | yes | yes | global |
| `flanger_mix` | Flanger Mix | float | 0% .. 100% | 50% | yes | yes | global |
| `delay_on` | Delay On | bool | off / on | Off | yes | yes | global |
| `delay_sync` | Delay Sync | bool | off / on | On | yes | yes | global |
| `delay_time` | Delay Time | float | 1.0 ms .. 2000 ms | 375 ms | yes | yes | global |
| `delay_div` | Delay Division | choice | 8/1 / 4/1 / 2/1 / 1/1 / 1/2 / 1/2T / 1/4. / 1/4 / 1/4T / 1/8. / 1/8 / 1/8T / 1/16. / 1/16 / 1/16T / 1/32 / 1/32T / 1/64 | 1/8. | yes | yes | global |
| `delay_fb` | Delay Feedback | float | 0% .. 98% | 40% | yes | yes | global |
| `delay_damp` | Delay Damping | float | 0% .. 100% | 35% | yes | yes | global |
| `delay_pingpong` | Delay Ping Pong | bool | off / on | On | yes | yes | global |
| `delay_send` | Delay Send | float | 0% .. 100% | 35% | yes | yes | voice |
| `delay_return` | Delay Return | float | 0% .. 100% | 60% | yes | yes | global |
| `reverb_on` | Reverb On | bool | off / on | Off | yes | yes | global |
| `reverb_size` | Reverb Size | float | 0% .. 100% | 60% | yes | yes | global |
| `reverb_damp` | Reverb Damping | float | 0% .. 100% | 40% | yes | yes | global |
| `reverb_predelay` | Reverb Pre-Delay | float | 0.0 ms .. 200 ms | 12 ms | yes | yes | global |
| `reverb_width` | Reverb Width | float | 0% .. 100% | 100% | yes | yes | global |
| `reverb_send` | Reverb Send | float | 0% .. 100% | 30% | yes | yes | voice |
| `reverb_return` | Reverb Return | float | 0% .. 100% | 50% | yes | yes | global |
| `eq_on` | EQ On | bool | off / on | Off | yes | yes | global |
| `eq_low` | EQ Low Gain | float | -18.0 dB .. +18.0 dB | 0.0 dB | yes | yes | global |
| `eq_midfreq` | EQ Mid Frequency | float | 150 Hz .. 10.0 kHz | 1.20 kHz | yes | yes | global |
| `eq_mid` | EQ Mid Gain | float | -18.0 dB .. +18.0 dB | 0.0 dB | yes | yes | global |
| `eq_high` | EQ High Gain | float | -18.0 dB .. +18.0 dB | 0.0 dB | yes | yes | global |
| `comp_on` | Compressor On | bool | off / on | Off | yes | yes | global |
| `comp_thresh` | Comp Threshold | float | -48.0 dB .. 0.0 dB | -18.0 dB | yes | yes | global |
| `comp_ratio` | Comp Ratio | float | 1.0:1 .. 20.0:1 | 3.0:1 | yes | yes | global |
| `comp_attack` | Comp Attack | float | 0.1 ms .. 100 ms | 10 ms | yes | yes | global |
| `comp_release` | Comp Release | float | 10 ms .. 1000 ms | 120 ms | yes | yes | global |
| `comp_makeup` | Comp Makeup | float | 0.0 dB .. +24.0 dB | 0.0 dB | yes | yes | global |
| `limiter_on` | Output Limiter | bool | off / on | On | yes |  | global |

## Arpeggiator

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `arp_on` | Arp On | bool | off / on | Off | yes |  | global |
| `arp_mode` | Arp Mode | choice | Up / Down / Up/Down / Random / Order / Chord / Custom | Up | yes |  | global |
| `arp_rate` | Arp Rate | choice | 8/1 / 4/1 / 2/1 / 1/1 / 1/2 / 1/2T / 1/4. / 1/4 / 1/4T / 1/8. / 1/8 / 1/8T / 1/16. / 1/16 / 1/16T / 1/32 / 1/32T / 1/64 | 1/16 | yes |  | global |
| `arp_gate` | Arp Gate | float | 5% .. 100% | 60% | yes |  | global |
| `arp_swing` | Arp Swing | float | 0% .. 75% | 0% | yes |  | global |
| `arp_oct` | Arp Octaves | int | 1 .. 4 | 1 | yes |  | global |
| `arp_length` | Arp Length | int | 1 .. 16 | 8 | yes |  | global |
| `arp_velmode` | Arp Velocity Mode | choice | As Played / Fixed | As Played | yes |  | global |
| `arp_vel` | Arp Velocity | float | 0% .. 100% | 80% | yes |  | global |
| `arp_prob` | Arp Probability | float | 0% .. 100% | 100% | yes |  | global |
| `arp_ratchet` | Arp Ratchet | int | 1 .. 4 | 1 | yes |  | global |
| `arp_repeat` | Arp Step Repeat | int | 1 .. 4 | 1 | yes |  | global |
| `arp_accent` | Arp Accent | float | 0% .. 100% | 0% | yes |  | global |
| `arp_accevery` | Arp Accent Every | int | 2 .. 8 | 4 | yes |  | global |

## Sequencer

| ID | Name | Type | Range | Default | Auto | Morph | Scope |
|---|---|---|---|---|---|---|---|
| `seq_on` | Sequencer On | bool | off / on | Off | yes |  | global |
| `seq_rate` | Sequencer Rate | choice | 8/1 / 4/1 / 2/1 / 1/1 / 1/2 / 1/2T / 1/4. / 1/4 / 1/4T / 1/8. / 1/8 / 1/8T / 1/16. / 1/16 / 1/16T / 1/32 / 1/32T / 1/64 | 1/16 | yes |  | global |
| `seq_length` | Sequencer Length | int | 1 .. 32 | 16 | yes |  | global |
| `seq_swing` | Sequencer Swing | float | 0% .. 75% | 0% | yes |  | global |
| `seq_transpose` | Sequencer Transpose | int | -24 st .. +24 st | 0 st | yes |  | global |
| `seq_clock` | Sequencer Clock | choice | Follow Host / Free Run | Free Run | yes |  | global |

## Modulation sources

| # | Source | Range | Per note |
|---|---|---|---|
| 1 | MPE Pitch | -1..1 | yes |
| 2 | MPE Pressure | 0..1 | yes |
| 3 | MPE Slide | 0..1 | yes |
| 4 | Velocity | 0..1 | yes |
| 5 | Release Velocity | 0..1 | yes |
| 6 | Mod Wheel | 0..1 |  |
| 7 | Pitch Bend | -1..1 |  |
| 8 | Aftertouch | 0..1 |  |
| 9 | LFO 1 | -1..1 | yes |
| 10 | LFO 2 | -1..1 | yes |
| 11 | LFO 3 | -1..1 | yes |
| 12 | Amp Env | 0..1 | yes |
| 13 | Filter Env | 0..1 | yes |
| 14 | Mod Env | 0..1 | yes |
| 15 | Random | -1..1 | yes |
| 16 | Sample & Hold | -1..1 | yes |
| 17 | Key Position | -1..1 | yes |
| 18 | Note Number | 0..1 | yes |
| 19 | Gate | 0..1 | yes |
| 20 | Macro 1 | 0..1 |  |
| 21 | Macro 2 | 0..1 |  |
| 22 | Macro 3 | 0..1 |  |
| 23 | Macro 4 | 0..1 |  |

## Modulation destinations

| # | Destination | +100% equals | Scope |
|---|---|---|---|
| 1 | Pitch | 48 st | per note |
| 2 | OSC 1 Pitch | 48 st | per note |
| 3 | OSC 2 Pitch | 48 st | per note |
| 4 | OSC 3 Pitch | 48 st | per note |
| 5 | OSC 1 WT Position | 1  | per note |
| 6 | OSC 2 WT Position | 1  | per note |
| 7 | OSC 3 WT Position | 1  | per note |
| 8 | OSC 1 FM Amount | 1  | per note |
| 9 | OSC 2 FM Amount | 1  | per note |
| 10 | OSC 3 FM Amount | 1  | per note |
| 11 | OSC 1 Pulse Width | 0.5  | per note |
| 12 | OSC 2 Pulse Width | 0.5  | per note |
| 13 | OSC 3 Pulse Width | 0.5  | per note |
| 14 | OSC 1 Level | 1  | per note |
| 15 | OSC 2 Level | 1  | per note |
| 16 | OSC 3 Level | 1  | per note |
| 17 | Unison Detune | 1  | per note |
| 18 | Stereo Width | 1  | per note |
| 19 | Filter Cutoff | 96 st | per note |
| 20 | Filter Resonance | 1  | per note |
| 21 | Filter Drive | 1  | per note |
| 22 | Filter Env Amount | 1  | per note |
| 23 | Filter Mix | 1  | per note |
| 24 | Amp Level | 1  | per note |
| 25 | Pan | 1  | per note |
| 26 | Amp Attack | 4 oct | per note |
| 27 | Amp Decay | 4 oct | per note |
| 28 | Amp Release | 4 oct | per note |
| 29 | LFO 1 Rate | 4 oct | per note |
| 30 | LFO 2 Rate | 4 oct | per note |
| 31 | LFO 3 Rate | 4 oct | per note |
| 32 | LFO 1 Depth | 1  | per note |
| 33 | LFO 2 Depth | 1  | per note |
| 34 | LFO 3 Depth | 1  | per note |
| 35 | Delay Send | 1  | per note |
| 36 | Reverb Send | 1  | per note |
| 37 | Morph A/B | 1  | per note |
| 38 | Distortion Drive | 1  | global |
| 39 | Distortion Mix | 1  | global |
| 40 | Saturation Drive | 1  | global |
| 41 | Bitcrush Amount | 1  | global |
| 42 | Chorus Mix | 1  | global |
| 43 | Phaser Mix | 1  | global |
| 44 | Flanger Mix | 1  | global |
| 45 | Delay Feedback | 1  | global |
| 46 | Delay Return | 1  | global |
| 47 | Reverb Size | 1  | global |
| 48 | Reverb Return | 1  | global |
| 49 | Arp Gate | 1  | global |
| 50 | Arp Probability | 1  | global |
| 51 | Grain Size | 3 oct | per note |
| 52 | Grain Density | 3 oct | per note |
