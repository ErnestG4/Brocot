# Measured family axis — classification redesign (2026-06)

Replaces the provenance-string taxonomy that drove `build_pull_index.py` and
`gpu_landscape.py`. The old code set a preset's / bank-config's family to its
`provenance.category` verbatim, which conflated three unrelated taxonomies:

- real spectral families (`families.py`: sawtooth/square/triangle/pulse/organ/
  bell/prime_2..13),
- op-count **density** buckets (`Textured-Smooth · N ops`),
- vanity / reference labels (Atlas, Vowel, Truax, Avatar, …).

Over `resources/pull_index.json` (42 categories, 4362 exemplars) every category's
intra-spread exceeded its nearest-neighbour distance, ~3500/4362 exemplars were
density buckets, and the high primes were near-degenerate.

## What the probes found (phase3/sep_probe*.py, throwaway)

Separability of the **factory family-search exemplars** (41/family) in the GPU
render-fingerprint space (q_max=64), recall over the 12 families:

| classifier | recall | note |
|---|---:|---|
| L1 nearest analytic-target centroid | 14% | random baseline ~8% |
| L1 nearest measured-exemplar centroid | 22% | |
| `family_distance` (designed KL+RF metric) | 13% | margins −8…−14 |
| cosine / Jensen-Shannon, leave-one-out | 18% / 22% | |
| coarse 5-pole (primes merged, shapes merged) | 24% | |

Two root causes:

1. **`family_distance` is valid as a search objective but invalid as a
   classifier.** With the family fixed (search), the entropy-unfair KL term is a
   constant offset. With the family varying (classification), broad-support
   targets (Bell, Sawtooth) are near-everyone's argmin and sparse targets
   (primes) can never win — so the family-search itself produced *contaminated*
   exemplars (a "Square" exemplar classifies as Bell/Sawtooth, not Square).
2. The factory presets simply don't contain clean Square/Triangle/prime
   realisations — ~80% are dense `Textured-Smooth` configs that genuinely
   measure as Pulse/Bell-like.

## The fix: mine clean exemplars from the bank

The clean realisations DO exist in the 80M GPU bank. Mining the configs whose
**rendered** fingerprint is closest to each ideal target (`sep_probe4.py`):

- **Reachability** (L1 of best matches to ideal): clean shapes are easy
  (Triangle 0.048, Sawtooth 0.055, Pulse 0.086, Organ 0.092); primes are hard
  (0.26–0.59 — FM can't make a clean prime-p lattice).
- **Separability**: centroids built from the mined exemplars classify a held-out
  slice at **94% recall** — Sawtooth/Square/Triangle/Pulse/Organ/Bell/Prime
  2/7/13 all 98–100%; only Prime 5 (71%) and Prime 11 (59%) stay soft.

So the family is **defined by its cleanest bank-mined realisations**, and
everything else is classified ONTO that axis.

## Pipeline (`family_axis.py` is the source of truth)

1. `family_axis mine` — mine 12 family **centroids** from the 6-op bank (richest
   reachability), once, cached to `derived/family_centroids.npz`. Reused to
   classify every per-depth/per-waveform bank, so the family axis is stable: a
   Sawtooth output spectrum is Sawtooth regardless of which map produced it.
2. `family_axis exemplars` — mine top-K clean configs/family WITH reconstructable
   params → `derived/family_exemplars.npz` (for pull-index pull targets).
3. `gpu_landscape classify` — label each bank by nearest mined centroid (fair L1).
4. `gpu_landscape presets` — classify the factory presets onto the axis →
   `derived/preset_families.json` (provenance becomes a menu label, not a region).
5. `gpu_paths gradients` — per-family next-hop graph (auto-uses the new centroids).
6. `build_maps` — driver running 3+5 for all 13 map variants.
7. `build_pull_index` — 12 measured family categories (bank-mined exemplars) +
   the orthogonal `Textured-Smooth · N` density buckets. No vanity categories.

**Density is an orthogonal axis**, never a family: stored per node as `dense`
(gpu_paths) and as the `Textured-Smooth · N ops` pull buckets that back the
runtime density-steer row.

## Note on bank basin skew

Classifying the full bank stays skewed (Sawtooth ~67%, med L1 ~0.73) — most of
the 80M random FM configs are dense/bright and genuinely sit nearest the
bright-full pole; only a small fraction are clean realisations of any family
(those have low med L1: Prime 7 0.10, Triangle 0.25). This is honest structure,
not a bug. The map gradient anchors still pick each family's cleanest members,
and the stratified node subsample keeps rare families present.

## Runtime contract

The 12 family names are byte-identical to `SteerTab.cpp kShapeCategories` and to
`brocot_writer._human_family_category`, so the family-steer rows and the
`next_hop` lookups keep working unchanged. The maps now expose exactly these 12
families (down from 42); the density row falls back to a pull-index exemplar
morph (a proper orthogonal density control is a recommended follow-up).
