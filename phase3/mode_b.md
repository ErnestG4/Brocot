# Mode B — reference-spectrum match

Mode B lets the Phase-3 search target an **arbitrary external audio sample**
instead of one of the built-in musical families. You hand it a recording
(e.g. an ARP 2600 patch), it extracts the spectrum onto the integer-partial
grid, wraps it as a `Family`, and runs the existing two-phase search to find
Stern-Brocot FM configurations whose *predicted* spectrum is closest to that
target.

No changes to the search or the distance core were needed: an external target
is just a `Family` with `characteristic_q=None` (KL-only distance), reusing the
`bell_stretched` precedent.

## API

```python
from phase3.audio_spectrum import audio_to_spectrum
from phase3.families import family_from_target
from phase3.search import two_phase_search

# 1. audio file → integer-partial amplitude vector, shape (q_max,)
spec = audio_to_spectrum("arp_patch01.wav", q_max=64, f_carrier=220.0)

# 2. wrap as a Family (characteristic_q=None ⇒ KL-only distance)
fam = family_from_target(spec, name="arp_patch01", f_carrier=220.0)

# 3. search — same call as any built-in family
results = two_phase_search(fam, f_carrier=220.0, q_max=64, n_phase1=1000)

best = results[0]            # SearchResult: .config, .objective, ...
```

`results` is the top-K uniquely-fingerprinted configs, sorted by objective
(lower = closer). Convert any `SearchResult.config` to a `.brocot` preset with
`brocot_writer`.

## Pipeline details

### `audio_to_spectrum(source, q_max, f_carrier, *, sample_rate=None, nperseg=None, floor_db=-60.0)`

- `source` — a WAV path **or** an already-loaded sample array (then pass
  `sample_rate`). Multi-channel audio is averaged to mono. Integer PCM
  (`int16`/`int32`/`uint8`) is scaled to float.
- **Hann-windowed Welch PSD** — segment-averaged for stationarity. `nperseg`
  defaults to frequency resolution ≈ `f_carrier/16`, clamped to signal length.
- **Peak partial extraction** — detects spectral peaks above `floor_db`
  relative to the strongest peak, bins each to its nearest integer multiple
  `n = round(f / f_carrier)`, and sums peak magnitudes into bin `n`. This is
  *structurally identical* to the analytic side's
  `family_distance.quantize_to_integer_grid` (round-and-sum-|amplitude|), which
  is what keeps the search honest — a config's audio spectrum lands in the same
  coordinate system as its predicted spectrum.
- Returns an unnormalised `(q_max,)` vector; `family_distance` normalises
  inside KL, so absolute scale is irrelevant.

### `family_from_target(amplitudes, name="external", f_carrier=220.0)`

- `characteristic_q=None` ⇒ the distance metric drops the Ramanujan-Fourier
  term and uses **KL on the smoothed spectra only** (same path as
  `bell_stretched`).
- `target_amplitudes(q_max)` returns the supplied vector truncated/zero-padded
  to length `q_max`, cached per `q_max`.

## Validation — synthetic round-trip (P0.3)

`python -m phase3.run_mode_b_roundtrip` runs the non-negotiable gate:

```
known Config → render audio → WAV → audio_to_spectrum
             → family_from_target → two_phase_search → recovered Config
```

Recovery is judged in **audio space** (does `render(recovered)` measure the
same spectrum as `render(true)`?) — the only perceptually-meaningful question,
and one immune to the analytic-vs-render representation gap because both sides
traverse the identical `render → audio_to_spectrum` path.

> **A note on metrics.** Comparing a config's *analytic* predicted spectrum to
> its *rendered* audio spectrum carries two irreducible error sources that are
> **not** search failures: (1) `predict_partials` prunes weak intermodulation
> products that the full render retains — negligible for harmonic configs,
> larger where a modulator sits on a Bessel (J₀) zero and the carrier survives
> only through delicate cancellation; (2) **grid-projection tie-breaking** —
> fractional-ratio FM puts partials at half-integer positions (e.g. 2.5·f_c)
> that sit exactly between integer bins, where a sub-Hz measurement error flips
> the assignment. Both inflate analytic-space distance without meaning the
> sound is wrong; audio-space recovery sidesteps them.

### Results (2026-05-26, hard-`round` grid projection)

Verdict: **the pipeline is sound but the search underperforms.** The
`family_from_target` wrapper, the audio→spectrum extractor, and the KL
distance are all correct — the true config is the deep global minimum of
the objective (obj ≈ 0.0009 for the harmonic case) and coordinate descent
*holds* there. But blind recovery from a cold start fails, and more budget
does not fix it (8 seeds × 3000 samples per config, ~24k evals):

| config     | best obj | floor   | audio L1 | diagnosis |
|------------|----------|---------|----------|-----------|
| harmonic4  | 0.090    | 0.0009  | 0.46     | proxy aligned (floor≈0) but search **traps** in local optima |
| mixed4     | 0.0094   | 0.126   | 0.94     | search obj *below* truth's — **objective-gaming** the predicted-vs-rendered gap; sounds nothing alike |
| metallic4  | 0.048    | 0.068   | 0.60     | milder gaming |

Two root causes, both addressable (and both feed the broader
search/LUT-smoothing arc rather than new FM topologies):

1. **Search trapping** — greedy coordinate descent + landmark-biased
   sampling is a weak global optimizer; it cannot recover a specific 4-op
   patch even when a perfect answer exists.
2. **Objective misalignment** — minimising `family_distance(predicted, audio
   target)` optimises a *proxy*. Where predicted ≠ rendered (fractional
   ratios), the proxy's optimum is not the perceptual match. The dominant
   contributor is the **hard `round(f/f_c)` grid projection**: half-integer
   partials (e.g. 2.5·f_c) sit on a bin-flip knife-edge, which both inflates
   the floor and carves discontinuities into the search landscape.

### Results (2026-05-26, after smoothing + sampler fixes)

Two fixes landed:

- **S1 — interpolated grid projection** (`quantize_to_integer_grid` /
  `audio_to_spectrum`, `interpolate=True`): each partial is split linearly
  across its two bracketing integer bins. This collapsed the floor 56–63× on
  fractional-ratio configs (mixed4 0.126 → 0.0020) and **eliminated the
  objective-gaming** — the proxy is now aligned with audio truth.
- **S2 — sampler coverage**: Phase-1 can now draw the empty path (ratio 1/1,
  the harmonic fundamental — previously unreachable, `path_len ≥ 1`), and
  `landmark_bias` dropped 0.70 → 0.55 to cover sub-landmark depths.

Recovery (8-seed multi-start, `diag_modeb_budget`):

| metric (worst over 3 configs) | hard-round | +S1 | +S1+S2 |
|-------------------------------|-----------:|----:|-------:|
| audio L1                      | 0.94       |0.41 | **0.22** |
| audio KL                      | 1.09       |0.19 | **0.042** |

All configs now recover to **audio KL ≤ 0.042** (perceptually close). The
residual (best obj ≈ 0.04 vs floor ≈ 0.001) is **coordinate-descent
trapping** — the search finds a perceptually-similar config but not the exact
generating one. For real-target matching (no exact pre-image exists) this is
the correct bar; driving to exact synthetic recovery needs a stronger local
optimizer (basin-hopping / annealing), a separate arc.

**Verdict:** Mode B is validated — pipeline correct, objective aligned,
recovery perceptually sound. Cleared to use for ARP-style targets.

## Scope / limits

- KL-only distance (no `characteristic_q` assumed for external targets).
- The search models candidates with the pruned analytic predictor; for targets
  dominated by fragile near-cancellations (modulators on Bessel zeros) the
  predicted-vs-rendered gap is larger. Musically-typical material is fine.
- Real ARP recordings are matched the same way once they exist; until then the
  round-trip validates the machinery on synthetic targets.
