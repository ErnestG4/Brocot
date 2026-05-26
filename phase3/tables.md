# Coverage bank & retrieval — ready for any input sound

The coverage bank is a large, deduped fingerprint index over the FM parameter
space. It turns Mode B from "run a cold search and hope" into "look up the
nearest known sound, then refine" — fast, and it fixes the cold-start
trapping that limited the early Mode B recovery.

## Build

```sh
python -m phase3.coverage_bank build --n-random 80000
python -m phase3.coverage_bank coverage         # how well is the space covered?
```

Sampling (we cover the *spectrum* space, deduping by quantised fingerprint —
many configs share a spectrum):

- **1-op**: every curated Stern-Brocot ratio (paths ≤ len 5) × a depth grid.
- **2-op**: a coarser curated ratio × depth grid, all unordered pairs.
- **3–5-op**: structured random sampling via the search sampler.

Fingerprints are the smoothed, L1-normalised integer-grid amplitude vectors at
**q_max = 128** (the audible range at f_carrier = 220). Predict params are
deliberately coarse (`-40 dB / eps 5e-3 / 400 partials`) — these are
*retrieval* fingerprints (find the basin), not final scores.

Artifacts (gitignored, derived): `derived/coverage_bank.npz` (float32 matrix)
+ `derived/coverage_bank.jsonl` (one config per row).

## Coverage (174,860 fingerprints, 2026-05-26)

Held-out random targets, nearest-neighbour L1 distance to the bank:

```
p50 0.094   p90 0.204   p95 0.240   p99 0.332   max 0.545
within 0.10: 53.5%      within 0.25: 95.2%
```

So 95% of arbitrary targets already have a bank entry within 0.25 L1 — close
enough that one coordinate-descent refinement lands an excellent match.

## Retrieval & warm-started search

```python
from phase3.coverage_bank import load_bank, bank_seeded_search, retrieve_seeds
amps, configs = load_bank()

# nearest configs to a target amplitude vector:
seeds = retrieve_seeds(target_amps, amps, configs, k=10)

# full warm-started Mode B search for an external-target Family:
results = bank_seeded_search(fam, amps=amps, configs=configs)
best = results[0].config
```

`bank_seeded_search` retrieves the k nearest bank configs, mixes in the best
of a small random draw (`n_random_aug`, for targets in the coverage tail),
and refines each by coordinate descent.

### Recovery vs cold search (synthetic round-trip, worst-of-3 audio L1)

| method                       | harmonic4 | mixed4 | metallic4 | cost  |
|-------------------------------|-----------|--------|-----------|-------|
| cold single-start (n₁=1000)   | 0.41      | 0.33   | 0.60      | ~90 s |
| cold multi-start (8×3000)     | 0.22      | 0.14   | 0.22      | minutes |
| **bank warm-start**           | **0.18**  | **0.07** | 0.37    | **3–15 s** |

The bank matches/beats the 24k-sample multi-start on 2 of 3 targets in seconds
— ~1000× fewer evaluations — because retrieval starts the refinement in the
right basin instead of hoping a random sample lands there. `metallic4` is a
coverage-tail case (no close bank seed → refinement settles at 0.37, KL ≈ 0.12,
still a moderate match); the random-augmentation lever didn't help it, so the
real fix is a denser bank, not query-time random search. For a toy matching
arbitrary sounds, fast top-K retrieval that nails most targets and gets the
rest "close-ish" is exactly the right trade.
