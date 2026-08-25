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

> **A DIRECT sideband coincidence is reachable ⟺ max(p, q) ≤ 2·order_bound(I)**

`order_bound(I)` is already in the codebase — `phase3/partial_prediction.py`, the largest `|n|` with
`|J_n(I)| > ε`. So the boundary costs nothing new to compute. **ε is load-bearing and is treated
in §Scope below.**

### Which lattice — stated, because the synth folds and the proof does not

`predict_partials` **reflects** negative frequencies onto the positive axis (`partial_prediction.py`
:20–21, :117–123 — Web Audio behaviour). The statement above is about the **unfolded** lattice, and
that scope is not cosmetic: the folded lattice has a *second* coincidence channel,

```
1 + n₁ + n₂α = −(1 + n₁′ + n₂′α)   ⟹   2 + (n₁+n₁′) + (n₂+n₂′)α = 0
```

whose `k = 0` branch — `n₁ = n₁′ = −1`, `n₂ = −n₂′ = m`, giving `+mα` and `−mα` — folds one onto the
other **for every α whatsoever, rational or irrational**. Measured (`brocot_theorem_scope.json`): it
fires at every node, including **89/89** above the horizon at I = 3.0.

Running the *same* divisibility argument on that equation: with `a = 2 + n₁ + n₁′` and
`b = n₂ + n₂′` it reads `a·q = −b·p`, so `gcd(p,q) = 1` forces `a = mp`, `b = −mq`. Two solution
classes, and they behave completely differently:

| class | content | horizon |
|---|---|---|
| **m = 0** | the universal family `+mα / −mα` | **none** — fires for every α, rational or irrational (measured: every node, **89/89** above the horizon at I=3.0) |
| **m ≠ 0** | ratio-pinned | `|mq| ≤ 2B` and `|mp| ≤ 2B+2` — **the same horizon, shifted by +2 in p** |

Measured (`brocot_theorem_scope.json`, T5): 10 / 17 / 25 / 34 nodes across I = 0.9 / 1.5 / 2.0 / 3.0
carry an `m ≠ 0` reflected coincidence, and **every one** satisfies `q ≤ 2B` and `p ≤ 2B+2`.
Worked example: `3/4` at B=4, the pair `(−3,3)` and `(−2,1)`, giving `a = −3`, `b = 4`, `m = −1`.

> **So: the reflected channel's ratio-dependent coincidences obey the same horizon; its only
> horizon-free content is the universal m = 0 family, which is vacuous.** A predicate stated over
> folded values would be true of every ratio and therefore incapable of distinguishing any — which
> is why the horizon is stated over the direct channel. Verified under the split: the direct channel
> alone reproduces the horizon exactly at every node and every index.

*An earlier draft said the reflected channel "carries no ratio-dependent information at all." That
was a universal negative resting on a measurement of the universal-**positive** family only, and it
is corrected above. The horizon result is untouched — the sharp-boundary exhibit and the 508/508
verification both ran on the direct channel.*

### Proof

The partial set is `{ 1 + n₁ + n₂·α : |n₁|, |n₂| ≤ B }` with `B = order_bound(I)`. Two partials
coincide when

```
n₁ + n₂·α  =  n₁′ + n₂′·α
```

Write `a = n₁ − n₁′` and `b = n₂′ − n₂`. Then `a = b·(p/q)`, so `a·q = b·p`. Since `gcd(p,q) = 1`,
`q` divides `b` and `p` divides `a`: there is an integer `m ≠ 0` with `b = m·q` and `a = m·p`.

**Necessity.** Both differences are bounded by the sideband range: `|a| = |m|·p ≤ 2B` and
`|b| = |m|·q ≤ 2B`. Since `m ≠ 0`, already `|m| = 1` gives `p ≤ 2B` **and** `q ≤ 2B`, i.e.
`max(p,q) ≤ 2B`. Above that no integer `m` satisfies both bounds, so no coincidence exists.

**Sufficiency — the witness, constructed.** Suppose `max(p,q) ≤ 2B`. Take

```
(n₁, n₂)   = ( ⌈p/2⌉, −⌊q/2⌋ )
(n₁′, n₂′) = ( −⌊p/2⌋,  ⌈q/2⌉ )
```

All four coordinates lie in `[−B, B]` precisely because `p ≤ 2B` and `q ≤ 2B`. And

```
(n₁ + n₂α) − (n₁′ + n₂′α) = (⌈p/2⌉ + ⌊p/2⌋) − (⌈q/2⌉ + ⌊q/2⌋)·(p/q) = p − q·(p/q) = 0
```

so the two distinct index pairs land on the same frequency. ∎

*(Checked exhaustively: the construction is in-bounds and coincident for all 1274 `(p,q,B)` triples
with `max(p,q) ≤ 2B`, `B ≤ 11`. Without it the 508/508 verification below would be carrying the
sufficiency direction empirically — fine as verification, but it should not do proof work silently.)*

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

**The I-column is ε-relative.** `order_bound` is defined by `|J_n(I)| > ε`, so every threshold below
depends on that constant. Measured across two decades (`brocot_theorem_scope.json`):

| spec ratio | max(p,q) | ε=10⁻² | **ε=10⁻³ (default)** | ε=10⁻⁴ |
|---|---|---|---|---|
| `4/3`, `3/4` | 4 | 0.29 | **0.09** | 0.05 |
| `9/7`, `7/9` | 9 | 2.16 | **1.33** | 0.84 |
| `14/11`, `11/14` | 14 | *3.74* | **2.60** | 1.85 |
| **`43/34`** | 43 | >12 | **>12** | >12 |

**The ordering is ε-invariant; the thresholds are not** — they move roughly 3× across two decades,
and the ladder's *membership* moves with them: at ε = 10⁻² even `14/11` needs I = 3.74, outside the
spec's own 0.1–3.0 range.

> **This retracts the stronger reading of the exhibit, explicitly.** The claim had two levels.
> *(a)* The curated ratios are **ordered** by horizon depth — ε-invariant, and it survives in full.
> *(b)* They **sit at horizon crossings inside the spec's own playable range** — ε-dependent, and it
> does not survive: at ε = 10⁻² the third rung leaves the range entirely.
>
> **The retrodiction is the ordering.** That is still a blind test passed — the spec's list was
> curated by ear years before any of this was computed — but at one level of abstraction up from
> where it was first claimed. The absolute thresholds are convention.

A perceptually-motivated ε would sit well above the default: `J₂(0.1) ≈ 0.00125` barely clears
10⁻³, and a partial's amplitude is a *product* of Bessel terms, so a single surviving `J_n` does not
make an audible partial. Choosing ε on hearing rather than on numerical convenience would shift
every rung upward without reordering them — **which leaves the membership question open rather than
settled by the default ε.** Whether the audible ladder still lands inside 0.1–3.0 is measurable and
unmeasured; it needs a listening-calibrated ε, not a numerical one.

At the spec's typical I ≈ 0.9 only `4/3` and `3/4` can fuse under any ε tested. `43/34` needs
`order_bound ≥ 22`, which the stated depth range never reaches at any ε — so it can be *played*,
but it cannot *ring*.

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

That redirect was then tested and **also failed**, twice. Three attempts, each on the same path:

| method | CV reduction | bar |
|---|---|---|
| continuous arc-length reparameterisation | 1.6× | 3× |
| waypoint selection over tree nodes by timbral distance | 1.31× | 3× |
| the same, with the horizon widened by raising the index | 1.74× best | 3× |

The last one is decisive. Raising the index admits more ringing nodes — measured, 9 → 16 → 22 → 32
across I = 0.9 → 3.0 — so if the residual roughness were *granularity*, it should have smoothed.
It did not, and the reduction is not even monotone in the index (0.92, 1.48, 1.03, 1.74). At
I = 0.9 selecting by timbral distance is **worse** than selecting by ratio.

> **The punctuation is scale-free within the instrument's range.** The space is equally rough at
> every resolution the depth slider can reach. A smooth wormhole is not available by any waypoint
> policy **on either coordinate tested** — this is a property of the soundspace, not a failure of
> method.

**Scope, stated because the first version of this sentence was a bare universal.** All three
attempts above ran on **u**, the Brody statistic — and u is *not* what a player hears: measured
Spearman against an ERB excitation-pattern distance is only **+0.202**, CI [+0.122, +0.282]. The two
coordinates agree that jumps happen and disagree about where.

That reopened the question properly, because ERB's roughness looked like a different *kind*: only
**5.7%** of its top-decile jumps sit at a partial-set change (baseline 1.9%), which suggested
continuous motion rather than discontinuity — and continuous roughness is exactly what arc-length
reparameterisation *can* linearise. Rougher is not the same as punctuated, and ERB is rougher
(step CV **9.553** against u's **5.022**).

**Run, and it fails there too** (`brocot_linear_morph_erb.json`): reduction **0.99×** — marginally
worse than uniform — and the worst single step **more than doubled**, 0.31528 → 0.73178.

> **The inference was wrong, and the error is worth keeping: *"not at set changes" does not mean
> "continuous."*** It means the discontinuities have a different mechanism — the natural reading
> being partials crossing critical-band boundaries, discrete in effect though produced by smooth
> motion. Both coordinates are punctuated, by different mechanisms, with the same failure signature:
> arc length equalises cumulative distance, packing waypoints into a few unresolved discontinuities
> and inflating the worst step.

So the claim now rests on **two coordinates that fail differently and disagree about where the jumps
are** — which is stronger than one statistic, and is what the sentence should have said from the
start.

**So the feature is not "make morphs smooth" — that is unavailable — but "show the player where the
jumps are."** The horizon already computes it: the jumps are the coincidence events, they sit at
the below-horizon rationals, and their positions are known in closed form before a note is played.
An honest morph UI marks them rather than pretending to interpolate through them.

One residual that *is* useful: horizon restriction gives the best worst-case step (max |Δu| 1.780
against 2.959 unrestricted, on fewer waypoints). Excluding ratios that cannot ring does not smooth
the path, but it does avoid its largest single jumps.

### What the display may say — gated, not assumed

The natural feature is to mark the coincidence events. Measured before writing any caption
(`brocot_marker_erb_gate.json`), on the coordinate a player hears:

| | at a coincidence marker | away | separation |
|---|---|---|---|
| **ERB** | 0.00010 | 0.0000064 | **16.13×** |
| Brody u | 0.00954 | 0.00452 | 2.11× |

Markers are the largest ERB steps around — **more** perceptually distinctive than statistically so.
But the absolute scale settles the caption: a **1-cent detune** produces **0.000154** in the same
units, so a marker event is **0.67×** a change nobody can hear.

> **The display may caption these STRUCTURAL EVENTS and may not call them jumps.** "The partial count
> drops here; these sidebands fuse" is true, closed-form, and worth showing. It is a different
> caption, not a weaker version of the same one — and a relative separation, however large, cannot
> carry an audibility claim on its own.

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

---

## Prior art — what is already known, and what is not

**Searched 2026-08-24. The phenomenon is known qualitatively; the boundary does not appear to be.**

### Already established, and not claimed here

| result | source |
|---|---|
| The multi-modulator spectrum is the Cartesian product of Bessel terms — **the lattice this proof runs on** | LeBrun, *A derivation of the spectrum of FM with a complex modulating wave*, CMJ 1(4), 1977; Schottstaedt, CMJ 1(4), 1977 |
| A rational c:m gives a harmonic spectrum (single modulator), **index-independently** | Chowning 1973; Truax 1977 |
| Farey-sequence / Stern–Brocot organisation of C:M ratios | Truax, *Organizational Techniques for C:M Ratios in Frequency Modulation*, 1977; Burraston's `FareyPreenFM2` |
| Negative frequencies reflect, and reflection drives inharmonicity | classical |
| **"For a clearly audible pitch, keep numerator and denominator small after dividing out common factors"** | textbook folk rule, widely stated |

That last row is the important one. **It is the same insight, un-quantified** — and it is the reason this result should be read as a sharpening rather than a discovery. Every practitioner already knows complex ratios sound like noise.

### What the search did not find

- The **exact** threshold `max(p,q) ≤ 2·order_bound(I)`.
- Its **index-dependence** — that the set of ratios able to fuse *grows with the depth control*, so "small enough" is not a fixed bar but a moving one.
- The consequence that **useful tree depth is bounded by the modulation index**, which is what makes it an instrument feature rather than an observation.

### Honest limits on that claim

**The algebra is elementary.** `gcd(p,q)=1 ⟹ a=mp, b=mq` is Bézout-adjacent and standard; nothing here is new mathematics. The contribution, if any, is precision and the coupling — not the argument.

**Absence of evidence is not evidence of absence, and the gap is specific.** Web search cannot read inside CMJ 1(4) 1977 or Truax's monograph, which are print and paywalled. **Truax 1977 is the single most likely place for this to already exist** — it is the work that organises C:M ratios by Farey sequence, which is precisely the structure the horizon cuts. Anyone with that text should check it before the claim is repeated.

The result stands on its verification either way. What would change is the framing: *newly derived* versus *re-derived, and now measured*.

---

## Methodology note — the reconstruction ledger

Twice in this document, an idealised reconstruction of the shipped lattice was written down before
the checker was consulted. The ledger is worth keeping honest:

| reconstruction | outcome |
|---|---|
| single-operator topology, forcing `q ≤ 2` via `n + m = −2p/q` | **wrong** — it contradicted the measured horizon; the shipped object is parallel FM with two modulators, and under that topology the divisibility argument runs clean |
| the reflected `m ≠ 0` class being ratio-pinned on the same horizon | **right** — confirmed at 10 / 17 / 25 / 34 nodes across the four indices, every one obeying `q ≤ 2B`, `p ≤ 2B+2` |

**The discipline was identical in both cases: the reconstruction is a hypothesis for the checker,
never a correction to the document.** It produced the correct outcome both times regardless of which
way the reconstruction went — and that is the property that makes it a rule rather than a habit.
*It does not require the reconstructor to be right, only checkable.*

The corresponding rule on the other side is `certifier imports the predicate`. It paid out twice
here too: first catching that the idealised topology was not the shipped one, then that the unfolded
checker was not the shipped frequency convention. Both times the divergence between the idealised
model and the shipped object was exactly where the mathematics turned out to live.

And one defect worth recording against this document specifically: the sentence *"the reflected
channel carries no ratio-dependent information at all"* was a **universal negative resting on a
measurement of the universal-positive family only**. The run that produced it had already collected
the refuting data and buried it under a median. The structural fix is `existence.py` — an existence
question now needs an existence statistic, typed at the reporting site rather than remembered.
