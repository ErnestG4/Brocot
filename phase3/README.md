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
├── families.py              # 12 musical-family definitions (P3.3)
├── family_distance.py       # KL + RF hybrid distance metric (P3.3)
├── search.py                # two-phase search loop (P3.4)
├── brocot_writer.py         # emits .brocot v2 JSON (P3.5)
├── generate.py              # CLI entry point (P3.6)
├── ars/                     # git submodule of ARS toolkit (P3.3)
├── tests/                   # pytest tests
└── requirements.txt
```

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

## Run (P3.6 onwards)

```sh
python -m phase3.generate \
    --families sawtooth,square,triangle,prime_7 \
    --presets-per-family 8 \
    --output ../resources/presets/factory/discovered/
```
