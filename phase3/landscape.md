# q_max convergence & corpus landscape (P1.3 / P1.4)

Findings from the overnight sweep (2026-05-26), after the smoothing (S1) and
RF-LUT (P1.2) changes.

## q_max convergence (P1.3 — `convergence_q_max.py`)

`family_distance` as a function of grid size q_max ∈ {256, 512, 1024, 2048}:

| family type                  | behaviour vs q_max | converges? |
|------------------------------|--------------------|------------|
| external target (Mode B, KL-only) | flat, Δ = 0    | ✅ exactly |
| bell_stretched (fixed max_n) | flat, Δ ≈ 2e-7     | ✅         |
| sawtooth / square            | drifts ~0.1        | ✗ (by design) |
| prime_p                      | drifts 7–12        | ✗ (by design) |

- **Mode B (KL-only external targets) is perfectly q_max-stable.** Growing
  the grid only adds resolution; the distance is unchanged. High q_max is
  free to use for arbitrary-sound matching.
- **Integer / prime family distances drift with q_max — and this is a
  design property, not a bug.** `family_distance` is defined for q_max = 16
  ("useful families don't reach past q = 13"). Two causes: (1) sawtooth /
  square have *infinite support* (the 1/n tail), so the target literally
  changes shape as more harmonics are admitted; (2) the RF / p-adic term
  normalises the characteristic-q amplitude by Σ rf[1:q_max], which keeps
  accruing high-q noise. **It is not bell_stretched.max_n** — bell is stable
  at fixed max_n; *scaling* max_n is what makes it drift.
- **Recommendation:** keep integer/prime family search at **q_max = 16**
  (its design point). Use large q_max only for Mode B.

## Corpus re-fingerprint (P1.4 — `refingerprint_corpus.py`)

All 4,310 `.brocot` presets, q_max = 256 vs q_max = 2048 (smoothed grid):

```
low_region_L1   median 0.0000   p95 0.0000   max 0.0000
mass_above_256  median 0.0000   p95 0.0000   max 0.0000
mass_above_1024 median 0.0000   p95 0.0000   max 0.0000
landscape moved >0.05 L1:  0 / 4310  (0.0%)
```

**No preset has any spectral mass above partial 256 — in fact above ~90.**
`predict_partials` caps partials at `f_max = 20 kHz`; at `f_carrier = 220 Hz`
that is 20000 / 220 ≈ **90 integer partials**. So q_max = 256 already covers
the entire audible range, and **q_max = 2048 captures nothing additional for
this carrier** — the landscape position is identical.

- The "q_max = 2048 for finer resolution" goal is moot at f_carrier = 220.
  The natural ceiling is q_max ≈ 96–128 (audible range + margin). q_max would
  only need to grow for a much lower reference carrier.
- The RF-LUT speedup (372 ms → 1.6 ms at q2048) still matters: it removes any
  performance reason to *cap* q_max, so the grid can be set purely by the
  f_max/f_carrier physics rather than by compute budget.

Derived artifact: `phase3/derived/corpus_q2048.jsonl` (gitignored).
