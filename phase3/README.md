# Brocot Phase 3 — ARS-driven preset generation

Offline Python tool that uses Will's ARS toolkit
(https://codeberg.org/combust/ARS) to discover Brocot configurations
matching musical-family targets (sawtooth, square, prime-p, etc.) and
write them as factory `.brocot` v2 presets.

The synth itself is unchanged at runtime — no Python dependency, no
sidecar, no OSC. This tool runs once (or periodically) to generate the
preset library; Brocot loads the resulting JSON files like any other
preset.

## Layout

```
phase3/
├── partial_prediction.py    # FM partial-set generation (P3.2)
├── families.py              # 12 core family defs + family_from_target (Mode B)
├── families_extra.py        # instrument/waveform flavours (pluck/brass/vowel/bell…)
├── family_distance.py       # KL + RF hybrid distance; interpolated grid (smoothing)
├── search.py                # two-phase search loop (P3.4)
├── brocot_writer.py         # emits .brocot v2 JSON (incl. per-op detune)
├── generate.py              # CLI entry point (P3.6)
│
│  # Mode B — match arbitrary input sounds  (mode_b.md)
├── audio_spectrum.py        # audio → integer-partial spectrum (Hann/Welch peak)
├── run_mode_b_roundtrip.py  # synthetic round-trip validation gate
├── coverage_bank.py         # fingerprint bank + warm-started search (tables.md)
│
│  # Drift / motion  (register.md)
├── stft_trajectory.py       # time-resolved drift/sweep extractor
├── drift_mapping.py         # DriftProfile → ModulationPlan (detune/LFO/ADSR)
├── register_drift.py        # how timbre shifts across the keyboard
│
│  # Measurement & analysis
├── spectral_features.py     # sound-DNA descriptors (centroid/roughness/…)
├── profile_q_max.py         # perf profiling across q_max
├── convergence_q_max.py     # q_max convergence checks
├── build_pull_index.py      # runtime steering index over the factory
│
│  # Preset-pack generators (write resources/presets/factory/<pack>/)
├── generate_flavours.py     # instrument flavours via warm-start
├── generate_analog.py       # detune-demo patches
├── generate_atlas.py        # farthest-point diverse tour of the space
│
├── ars/                     # git submodule of ARS toolkit
├── derived/                 # gitignored: coverage bank, corpus fingerprints
├── tests/                   # pytest tests
└── requirements.txt
```

## Docs

- `mode_b.md` — match an external audio sample (the ARP/SY99 path)
- `tables.md` — the coverage bank + warm-started retrieval
- `register.md` — FM pitch-dependence (key-scaling vs per-patch flags)
- `landscape.md` — q_max convergence + corpus landscape

## Setup

```sh
cd phase3
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
pytest tests/
```

Once P3.3 lands the submodule:
```sh
git submodule update --init phase3/ars
```

## Run

Discover presets for the built-in families:
```sh
python -m phase3.generate \
    --families sawtooth,square,triangle,prime_7 \
    --presets-per-family 8 \
    --output ../resources/presets/factory/discovered/
```

Match an arbitrary input sound (Mode B):
```python
from phase3.audio_spectrum import audio_to_spectrum
from phase3.families import family_from_target
from phase3.coverage_bank import load_bank, bank_seeded_search

spec = audio_to_spectrum("input.wav", q_max=128, f_carrier=220.0)
fam  = family_from_target(spec, name="input")
amps, configs = load_bank()                 # build once: python -m phase3.coverage_bank build
best = bank_seeded_search(fam, amps=amps, configs=configs)[0].config
```

Build the coverage bank / preset packs:
```sh
python -m phase3.coverage_bank build        # the retrieval table (gitignored)
python -m phase3.generate_flavours          # instrument-flavour pack
python -m phase3.generate_analog            # detune demos
python -m phase3.generate_atlas             # diverse space tour
python -m phase3.build_pull_index           # refresh the runtime steering index
```
