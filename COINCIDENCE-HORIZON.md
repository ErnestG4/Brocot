# The Coincidence Horizon

**An exact result about which ratios can ring, and at what modulation index.**

Derived and verified 2026-08-23/24 in the ARS programme
(`~/fmexplorer/criticality_tool/cross_substrate/`). Unlike everything else that programme has
produced about Brocot, this is not a correlation surviving a threshold — it is a closed-form
boundary with no fit, no aggregation and no free parameter.

---

## The statement

For Brocot's parallel-FM topology with modulators at ratios `1` and `α = p/q` (in lowest terms),
both at modulation index `I`:

> **A sideband coincidence is reachable ⟺ max(p, q) ≤ 2·order_bound(I)**

`order_bound(I)` is already in the codebase — `phase3/partial_prediction.py`, the largest `|n|` with
`|J_n(I)| > ε`. So the boundary costs nothing new to compute.

### Proof

The partial set is `{ 1 + n₁ + n₂·α : |n₁|, |n₂| ≤ B }` with `B = order_bound(I)`. Two partials
coincide when

```
n₁ + n₂·α  =  n₁′ + n₂′·α
```

Write `a = n₁ − n₁′` and `b = n₂′ − n₂`. Then `a = b·(p/q)`, so `a·q = b·p`. Since `gcd(p,q) = 1`,
`q` divides `b` and `p` divides `a`: there is an integer `m ≠ 0` with `b = m·q` and `a = m·p`.

Both differences are bounded by the sideband range: `|a| ≤ 2B` and `|b| ≤ 2B`. Taking `|m| = 1`
(the least demanding case) gives `p ≤ 2B` **and** `q ≤ 2B`, i.e. `max(p,q) ≤ 2B`. Above that no
integer `m` satisfies both bounds, so no coincidence exists. ∎

### Verification

Checked per node against exact rational arithmetic — no aggregation, no threshold:

| index I | order_bound | horizon max(p,q) ≤ | nodes tested | predicate agrees |
|---|---|---|---|---|
| 0.9 | 4 | 8 | 127 | **127 / 127** |
| 1.5 | 5 | 10 | 127 | **127 / 127** |
| 2.0 | 6 | 12 | 127 | **127 / 127** |
| 3.0 | 7 | 14 | 127 | **127 / 127** |

**508 / 508 cells, accuracy 1.0000.** Nodes are the Stern–Brocot rationals in [0.70, 1.40] with
q ≤ 24 — the near-unity range this spec names as the defining regime.

The boundary is sharp, not asymptotic:

```
I=0.9  B=4   at the horizon (max==8) : 7/8 ✓  8/7 ✓
             one past it   (max==9) : 7/9 ✗  8/9 ✗  9/8 ✗  9/7 ✗
I=3.0  B=7   at the horizon (max==14): 11/14 ✓ 13/14 ✓ 14/13 ✓ 14/11 ✓
             one past it   (max==15): 11/15 ✗ 13/15 ✗ 14/15 ✗ 15/14 ✗
```

---

## Why it matters for the instrument

### 1. It explains why complex ratios sound like noise

Above the horizon every sideband lands on its own frequency — nothing fuses. Measured at I = 0.9,
every node with q ≥ 7 returns the *same* aggregate spectrum: 31 partials, top-5 energy 0.716,
spectral entropy 0.709. The ratios differ; the *structure* does not, because there is none.

### 2. The rationals are not a restriction — they are the entire habitable zone

An irrational has no finite `q`, so it is **permanently above the horizon**. Every ratio the tree
cannot reach is exactly a ratio with no coincidence structure. §1 of the spec calls the tree
constraint "the feature"; this says the constraint is better founded than it claimed — the tree is
not approximating the interesting space, it is **enumerating it exactly**.

### 3. The spec's own curated ratios form a depth-ordered ladder

§1 names the defining regime by example. Those examples turn out to be sorted by the index at which
they come alive:

| spec ratio | max(p,q) | rings from |
|---|---|---|
| `4/3`, `3/4` | 4 | I ≥ 0.10 |
| `9/7`, `7/9` | 9 | I ≥ 1.33 |
| `14/11`, `11/14` | 14 | I ≥ 2.60 |
| **`43/34`** | 43 | **I > 8 — never inside the spec's own 0.1–3.0 range** |

At the spec's typical I ≈ 0.9 only `4/3` and `3/4` can fuse. `43/34` needs `order_bound ≥ 22`,
which the stated depth range never reaches — so it can be *played*, but it cannot *ring*.

### 4. It couples two controls the instrument leaves independent

Tree depth and the depth slider are separate parameters over a space where the mathematics binds
them. Navigating past `max(p,q) = 2·order_bound(I)` adds no coincidence structure at all.

> **Proposed feature — the horizon.** Compute `2·order_bound(depth)` live and mark tree nodes
> beyond it. This gives the depth slider a second, honest meaning: raising depth does not merely
> brighten, it **extends the horizon and brings new ratios into structure**.

---

## What the theorem does NOT say — measured, not assumed

`brocot_horizon_perceptual.json` tested the obvious over-reading before any UI wording was drafted.

**Above-horizon ratios are not alike.** They share the *absence* of coincidence structure and
nothing else — every sideband still lands wherever `α` puts it. On an ERB excitation-pattern
distance, above-horizon pairs sit **2573×** (I=0.9) and **441×** (I=2.0) above a 1-cent detune
floor, and are **0.50×** / **1.06×** as far apart as below-horizon pairs. They do not collapse
together.

> **So the annotation may claim COINCIDENCE STRUCTURE and nothing more.** Wording like "how much of
> the tree is alive" is *not* supported: above-horizon ratios are perfectly distinct from one
> another, they simply do not fuse. The honest label is about a property — *does this ratio ring?* —
> never about whether a region is worth visiting.

**Stated limits.** Steady-state magnitude spectra only; phase, transients and anything the synthesis
path adds downstream are out of scope. And a distance ratio is not an audibility verdict — only
listening settles what a player hears.

---

## Consequence for morphing

`brocot_linear_morph.json` tried to linearise a ratio morph by arc-length reparameterisation and
**failed**: variability fell only 1.6× against a 3× bar, and the worst single step got *worse*.

The reason is this theorem. Coincidences appear and vanish **discontinuously** at every p/q below
the horizon, so ratio space is **punctuated**, not merely steep — and `u(α)` is deterministic, so
the roughness is the terrain rather than estimation noise. A densely punctuated space cannot be
linearised by reparameterising a continuous coordinate.

The redirect: choose **waypoints over tree nodes**, spaced by measured timbral distance, with the
horizon deciding which nodes are eligible. That is what Stern–Brocot navigation already is — the
linearisation belongs in waypoint *selection*, not in the interpolator, and all of it is
offline-computable with nothing added to the audio thread.

---

## Provenance

| artifact | what it establishes |
|---|---|
| `brocot_useful_depth.json` | the theorem, 508/508 |
| `brocot_horizon_perceptual.json` | above-horizon nodes remain audibly distinct |
| `brocot_ratio_sensitivity.json` | timbral gradient 12× non-uniform across the range |
| `brocot_linear_morph.json` | reweighting does not linearise; the space is punctuated |
| `brocot_musical_depth.json` | all of the above measured at the instrument's own indices |

Each generator was committed before its output, and every prediction scored — including the ones
that missed, which is where §3 and the wording narrowing came from.
