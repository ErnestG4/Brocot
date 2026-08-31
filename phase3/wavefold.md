# The wavefold census — verdict and warrant

**Generator:** `phase3/wavefold_census.py`, sealed at commit `3821ce1`
(committed before any output of it existed). Deterministic — rerun it to
reproduce every number here (`derived/wavefold_census.json`, untracked by
the repo's derived-artifact convention).

**Question sealed:** does a per-modulator sine fold `s → sin(β·s)` act as a
continuous q-side horizon extension — the shipping rationale for a fold
knob?

**Verdict (2026-08-31): `SHIP_CONTINUOUS_EXTENSION`.** All arms as sealed:

| arm | bar | result |
|---|---|---|
| A1 asymmetry | max ringing numerator ≤ 2B everywhere | **pass** — pinned at 8 across every (I, β) cell |
| A1r rival ("fold is inert") | must FAIL | **fails as required** — the fold extends |
| A2 continuous extension | ≥ 3 distinct max-denominator values at I = 0.9 | **pass** — 4 levels: 8 → 14 → 30 → 35 |
| A3 nested growth | ringing set nested along β, both indices | **pass** — fully monotone on [0, 2.5]; no range restriction |
| A4 red path | golden-convergent 10946/6765 never rings | **pass** |

At I = 0.9 the ringing-node count over the census window ([0.20, 1.40],
q ≤ 39) grows 25 → 48 → 86 → 93, with steps near β ≈ 0.25 / 1.5 / 2.5. So
the axis is continuous in the same sense the depth slider is: quantised by
order-bound steps, monotone, never un-ringing a ratio.

**Scope, restated (inherited from the sealed prereg):**
- Support-level enumeration in exact integer arithmetic, the horizon doc's
  own methodological layer — **not** a rendered-audio result, and it
  licenses **no audibility claim**. The channels that open at small β are
  weak (per-harmonic index `I·aₖ` barely clears the order-bound floor).
- Two-operator frame, sine reference at 1, folded sine at α, matched index.
  For non-sine modulators the fold is just a waveshaper; the extension
  claim is sine-scoped.
- Coincidences counted on the (n_ref, M) lattice, which quotients the
  universal self-cancellation family by construction; A4 red-paths the
  defect anyway.

**One considered-and-dropped arm**, recorded per the non-evidence rule:
"same harmonic support ⇒ same ringing set" is INERT for amplitude-tapered
stacks (the per-harmonic order bounds depend on the amplitudes, so the
premise cannot be matched across amplitude vectors) — an arm that cannot
fire is not evidence either way.

**Engine consequence:** `op.fold` ships (β ∈ [0, 2.5], default 0 = bypass,
unit-peak normalisation `sin(β·s)/c(β)`, `c = sin β` for β ≤ π/2 else 1 —
the census's own normalisation). The UI may say "extends the horizon's
denominator side" and may not say any node it admits is audible.
