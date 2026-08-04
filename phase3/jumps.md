# Smooth jumps between basins — "wormholes"

The runtime morph between two map nodes is a slot-wise Stern-Brocot tree
walk (`PullIndex::morph`): up from A's path to the longest-common-prefix
ancestor, then down to B, one L/R step at a time, depths lerping, waveforms
snapping at the midpoint, op-0 feedback lerping separately (`SteerTab::
sampleRoute`). Roughness is dominated by **low-q ancestor passage** — two
deep paths on opposite sides of the tree force the walk through q=1..2
territory where the ratio collapses toward 1/1 and the sidebands fold onto
the carrier before re-forming. Conversely, two nodes in *different families*
whose slot paths share deep prefixes — the timbral difference carried by
depths, feedback, or waveform — morph almost silently.

Endpoint fingerprint L1 can't see this (it knows nothing about the walk).
Graph distance can't either (k-NN edges are endpoint-L1 edges). The gap
between "far by every existing metric" and "smooth under the actual walk"
is the **wormhole**, and why a walk-aware scorer is well-posed where the
inverse-net was not: roughness is a deterministic function of the two
configs (scoring a transition), not a one-to-many inversion.

## Pipeline (phase3/gpu_jumps.py)

1. **Mirror** — `phase3/morph_walk.py` reproduces the C++ walk exactly
   (JUCE round-half-up stepping, effective-depth lerp, OR'd enables, wave
   snap at 0.5), plus `events()` (the discontinuity alphas) and engineered
   pair features. CPU-only, unit-tested against brute-force sweeps.
2. **Ground truth** — render each pair's morph trajectory on the GPU in the
   bank's exact space (q_max=64, n_fft=4096, f_carrier=220): 17 uniform
   alphas plus ±ε brackets around every event. Metrics per pair:
   - `R_max` — max event-bracket fingerprint L1 (the worst instantaneous
     "bump"; PRIMARY),
   - `TV` — total variation along the merged alpha sequence,
   - `TV_excess` = TV − endpoint L1 (detour cost),
   - `alpha_worst` — where the worst bump sits.
3. **Calibration gate (pre-registered)** — medians must order
   `R_max(k-NN edge) < R_max(within-family) < R_max(cross-family)`, and
   rank-correlation(R_max, endpoint L1) must sit well below 1. If the walk
   metric is indistinguishable from endpoint L1, the feature collapses to
   existing edges — documented negative, stop.
4. **JumpNet** — siamese per-node towers (fingerprint ⊕ node scalars),
   symmetric pooling [h_a+h_b, |h_a−h_b|] ⊕ engineered features → predicts
   log R_max, log TV. Node-disjoint validation split (pair-level splits leak
   node identity). Baselines it must beat: endpoint L1, graph distance,
   engineered-features-only. Success (pre-registered): Spearman ≥ 0.90 AND
   ≥ +0.05 over the best baseline AND mining precision@1k ≥ 0.5. If the
   engineered scorer ties the net (within 0.02), ship the engineered scorer
   and retire the net — cheaper miner, same product feature.
5. **Mining** — score cross-family pairs in GPU blocks, keep top candidates
   per family-pair (caps neutralize the ~67% Sawtooth basin skew), then
   **render-verify every survivor** — shipped numbers are never predictions.
   Wormhole iff rendered `R_max ≤ τ` (τ = 25th percentile of k-NN-edge
   R_max: smoother than a typical intra-basin hop) AND graph distance ≥ 8
   (it must actually be a shortcut).
6. **Emit** — into the map JSON (after `"nodes"`, so header peeking is
   unaffected):

   ```json
   "jumps": {"version": 1, "metric": "maxstep_fp_l1", "tau": ...,
             "edges": [{"a": 1042, "b": 38771, "r": 0.11, "t": 1.92,
                        "g": 23.4, "w": 0.42}]}
   ```

   a<b node indices; r = rendered R_max; t = rendered TV; g = graph
   distance at mine time; w = worst-bump alpha. Caps: ≤ 16 per family-pair,
   ≤ 4 per node, ≤ 2048 total. Absent key = no jumps (old maps load fine).
   Regenerating a map **invalidates its jumps**; re-mining is part of the
   map pipeline.

Runtime: `Landscape` parses the block (`getJumps()`); CloudView renders
wormhole arcs (hover reveals a node's arcs; a toggle shows the top set).
Traversal needs zero new morph code — a jump is a 2-waypoint route and
`sampleRoute` already morphs arbitrary waypoint pairs.

Known scope limits: the steer tab's audible trajectory is the *grooved
glide*, not the SB walk (`ratio_mode="glide"` measures that variant); the
Farey-gravity pull itself is unmodeled; detune is not part of `OpState`.

## Results (2026-08-04 overnight, 6-op base map, 50k nodes)

**Calibration — GATE PASS** (300k pairs, 5 strata): median rendered `R_max`
orders exactly as required — k-NN edge **0.41** < within-family **0.48** <
cross-family **0.53** — and Spearman(R_max, endpoint L1) = **0.24**, far
below the 0.9 kill-line. Walk roughness is a genuinely distinct signal:
endpoint distance explains almost none of where the morph bumps are.
(TV correlates 0.52 with R_max; 0 of 300k pairs hit the event cap.)

**Glide mode** (15k pairs, steer-tab semantics): rendered `R_max` = **0 for
every pair** — the continuous log-ratio glide has no instantaneous events at
all (no wave flips sampled on this map), so the steer tab is inherently
bump-free and pays only trajectory length (TV medians 2.3/2.8/3.0, same
stratum ordering). Consequence: **wormholes are a preset/route-morph
feature** (the SB-walk semantics where bumps live); steering needs no
separate jumps table.

**JumpNet — PARTIAL** (node-disjoint 5% val, 29,533 pairs): Spearman with
log R_max — JumpNet **0.714** vs endpoint L1 0.246, landmark graph distance
0.263, engineered-features MLP 0.529. The net beats every baseline by ≥
+0.18 (so the towers learn real pair-specific structure the hand-physics
misses) but misses the pre-registered 0.90 SUCCESS bar → recorded as
PARTIAL, per the rule. Product consequence: the net is the *proposer* only;
nothing shipped is a prediction (every emitted edge is render-verified), so
PARTIAL costs mining recall, not correctness.

**Mining/emit** — 20M random cross-family pairs screened (endpoint-L1 ≤ p40
= 1.09, landmark graph dist ≥ 8 median-edge units) → 6.6M survivors → 30k
proposed by the net (500/family-pair caps) → **all 30k render-verified**:
13,494 (45.0%) pass the wormhole rule R_max ≤ τ = 0.275, and the shipped
1,385 (after ≤16/family-pair, ≤4/node, ≤2048 caps) have **median rendered
R_max = 0.002** — cross-family morphs ~100× smoother than a typical
intra-basin k-NN hop, at a median 12-hop-equivalent graph distance.
Pre-registered mining precision@1k = **0.487** (bar 0.5; the net's rank
ordering inside the smooth tail is noisy — consistent with PARTIAL), which
is why ranking uses rendered values only. Shipped in the `jumps` block of
`resources/landscape_graph.json.gz`; A/B WAVs in `derived/jumps_audio/`
(top wormholes vs random cross-family morphs, same pacing) for the by-ear
check of the metric itself.
