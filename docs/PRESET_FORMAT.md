# NeddPE preset format

User presets are UTF-8 XML files with the extension **`.neddpe`**, stored in

```
Documents/NeddPE/Presets/<Category>/<Name>.neddpe
```

Favourites are listed in `Documents/NeddPE/favorites.xml`. The factory presets are compiled into the plugin; the
same sounds are exported as `.neddpe` files in the repository's `presets/` folder (regenerate with
`NeddPETests --export-presets presets`).

The DAW project state uses exactly the same document plus the extra sections marked *project only* (stored in
binary-XML form by the host).

## Document

```xml
<NeddPE version="1" preset="Nedd Lead" category="Leads" author="NeddPE Factory"
        description="Warm unison lead. Pressure opens the filter...">
  <PARAMS>
    <PARAM id="osc1_on" value="1.0"/>
    <PARAM id="flt_cutoff" value="1800.0"/>
    <PARAM id="mod2_src" value="2.0"/>
    ...                                   (one entry per parameter)
  </PARAMS>
  <Macros>
    <Macro index="0" name="MOVEMENT"/> ... (4 entries)
  </Macros>
  <LfoShapes>
    <Lfo index="0" points="0.0000,-1.0000;0.2500,1.0000;..."/> ... (3 entries, x in 0..1, y in -1..1)
  </LfoShapes>
  <Sequencer>
    <Step i="0" on="1" note="48" vel="0.9" gate="0.45" slide="0.2" press="0.6" pitch="0"
          prob="1" ratchet="1" accent="1"/> ... (32 entries)
  </Sequencer>
  <ArpPattern steps="0 1 2 1 0 2 3 -1 0 1 2 3 4 3 2 1"/>
  <MorphB osc1_wtpos="0.9" flt_cutoff="6500" ... />          (optional: morph target B, morphable params only)

  <!-- project only -->
  <Clip length="16" loop="1" sync="0">
    <Note note="60" start="0.5" len="1.0" vel="0.8" rvel="0.5"
          pitch="0.0000:0.0000;1.0000:2.0000;" press="0.0000:0.2000;..." slide="..."/>
  </Clip>
  <Tuning name="12-TET" pitch="0.00000 1.00000 ... (128 values)" userScale="101011010101"/>
  <MidiMap>
    <Map cc="21" id="flt_cutoff"/>
  </MidiMap>
</NeddPE>
```

The root element also carries `editorScale` in project state.

## Rules

- **Parameters are stored by stable string ID with their plain (denormalised) value**: Hz, seconds, dB, semitones,
  choice index, 0/1 for switches. See [PARAMETERS.md](PARAMETERS.md) for every ID, unit and range.
- **Missing parameters load their default; unknown IDs are ignored.** Presets written by older versions keep
  loading as parameters are added, and presets from newer versions load in older ones.
- Values are clamped to the parameter range on load.
- Choice parameters store the index of the choice. Choice lists are append-only, so indices stay valid.
- Matrix slots store source and destination indices (`ModSource` / `ModDest`, append-only).
- Expression curves: `time:value;` pairs, time in beats since the note start, pitch in semitones, pressure and
  slide 0..1, linearly interpolated.
- `ArpPattern` steps: `-1` is a rest, otherwise an index into the held notes sorted by pitch (indices above the
  number of held notes continue into higher octaves).

## Versioning

`version` is the document format version (currently 1). A future incompatible change would increment it and add a
migration step in `PresetState::fromValueTree`; the stable-ID rules above mean ordinary additions never need one.
