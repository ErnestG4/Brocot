# FM pitch-dependence — register drift & fold gap (R1)

Measured 2026-05-26 to decide how to handle FM's pitch-dependent timbre
(bass/mid/high). Data-first, before any synth UI. Targets that motivate this:
sampling ARP 2600 (analog drift) and Yamaha SY99 AFM (feedback/sweeps) and
approximating them in **pure parallel-FM space** — sounds that get played
across the keyboard, where FM timbre shifts.

## Why pitch matters

A partial sits at `f_carrier·(1 + Σ nᵢrᵢ)` with a **carrier-independent**
amplitude `Π J_{nᵢ}(Iᵢ)`. So transposition changes the spectrum only via the
20 kHz ceiling: audible partial count = `20000 / f_carrier`, which swings
~16× across the keyboard (bass A1 ≈ 360, mid A3 ≈ 90, treble A5 ≈ 22). High
partials present in the bass fall off the top in the treble. The **analytic
model culls** them; the **real synth folds** them back as aliasing.

## (A) Analytic register drift — full corpus (`register_drift.py`)

4,309 presets, fingerprinted at 55 / 220 / 880 Hz, L1 of normalised
integer-grid fingerprints:

| metric (bass→treble) | median | p90 | max |
|----------------------|-------:|----:|----:|
| drift L1             | 0.086  |0.30 |1.32 |
| treble_survival (mass ≤ partial 22) | 1.00 | — | — |

- **54% transpose cleanly** (drift < 0.10); **78% within 0.25**.
- **98.8% keep ≥90% of mass below partial ~22** — the corpus is
  overwhelmingly low-order, so most patches survive transposition.
- A **~20% tail drifts** (p90 = 0.30) and **~1–2% fragments** (bright /
  high-order patches). Per-patch drift is in `derived/register_drift.jsonl`.

## (B) Predict-vs-render fold gap — sample (`register_fold_gap.py`)

31 presets, analytic vs rendered (folding) fingerprint per register:

| register | gap L1 median | gap L1 p90 | gap KL p90 |
|----------|--------------:|-----------:|-----------:|
| bass     | 0.138         | 0.34       | 0.43       |
| mid      | 0.156         | 0.35       | 0.51       |
| treble   | 0.166         | **0.52**   | **1.35**   |

Median ≈ flat (baseline extraction noise); **p90 blows up in treble** =
aliasing/folding the analytic model misses, concentrated in bright patches.

## Verdict & recommendation

The data says **don't blanket-tab the synth into bass/mid/high** — most of the
library doesn't need register handling. Instead:

1. **Key-scaling (per-op depth-vs-pitch curve)** as the baseline coherence
   tool — continuous, covers the ~80% that transpose acceptably, one patch
   spans the keyboard. This is the must-have.
2. **Per-patch drift metadata** (from `register_drift.jsonl`): flag the ~20%
   high-drift / bright patches; steering can auto-engage register-awareness
   only for those, rather than triple the UI for everyone.
3. **For Mode B on real ARP/SY99 material:** match at the register the sound
   is actually used in (or multi-pitch for the drifting tail). In the treble,
   prefer a **render-based objective** over the analytic one — the fold gap
   shows the analytic predictor is least trustworthy exactly there, which is
   where bright/feedback timbres live.
4. **Non-stationarity** (ARP drift, AFM feedback-sweeps): match the
   representative (Welch-averaged) spectrum for the static core, then recreate
   the motion with the modulator bank — a future time-resolved extractor
   (STFT trajectory) can drive those modulators.

Derived (gitignored): `derived/register_drift.jsonl`, `derived/corpus_q2048.jsonl`.
