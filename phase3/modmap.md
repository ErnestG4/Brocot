# The mod map + new oscillators — design (feasibility confirmed)

The mod map was deferred on one open question: the bank fingerprint is one
93 ms window, so time-varying modulation is invisible to it. The cheapest
extension that stays inside the project's L1 mathematics is the
**trajectory fingerprint**: K frames of the ordinary 64-bin fingerprint
rendered at staggered points of the modulation cycle, concatenated
(K·64 dims, still L1-comparable, still GPU-batchable). Frames are exact
static renders at sampled LFO phases — valid while the modulation is slow
against the window (rate ≪ ~10 Hz), which is the tremolo/wobble regime a
mod map targets.

## Probe results (phase3/modmap_probe.py, 8k configs, K=4 — 2026-08-04)

| question | result |
|---|---|
| P1 modulation visible? | **yes** — median frame-to-frame variation 0.21 (timbre-scale); 100% of configs > 10× the re-render floor (which is exactly 0 — renders are deterministic) |
| P2 rate rankable? | **yes** — 3-NN rate-class (slow/med/fast) from 120 ms-spaced frames: **0.63** vs static fingerprint at exactly chance (0.33 — blind by construction) |
| P3 timbre retained? | **yes** — the phase-0 frame retrieves the unmodulated config at rank 1, 100% |

Verdict: a mod map is buildable with existing machinery. `derived/modmap_probe.json`.

## Proposed pipeline (next pass)

1. **Config space**: 6-op base space + one or two modulator assignments per
   config: target ∈ {op-k depth, op-0 feedback}, shape ∈ {sine, tri}, rate
   log-uniform 0.3–7 Hz, mod depth 0.3–1.0. (Matches the engine's universal
   modulator bank — CarrierAm/filter targets can join once the offline
   renderer models them; depth/feedback targets need nothing new.)
2. **Fingerprint**: K=4 frames — one at phase 0 (the base-timbre frame,
   keeps ordinary retrieval working) + three at fixed real-time offsets
   (dt = 120 ms) so RATE enters the geometry (P2 depends on time-spaced,
   not phase-spaced, frames).
3. **Bank/map**: `gpu_bank --mods` variant writing (N, 256) fp16;
   `gpu_landscape` classifies on the phase-0 sub-vector (families stay
   timbre-defined); `gpu_paths` builds the k-NN graph on the FULL 256-dim
   vector so map neighbourhoods respect motion, with x/y from the same.
   Per-node JSON gains `mods: [{target, shape, rate, depth}]`; runtime
   `Landscape::applyNode` maps them onto the mod bank (the .brocotmod
   machinery already applies modulator state).
4. **Descriptors**: motion descriptors from the frames — `motion` (mean
   frame-to-frame L1), `rate_proxy` (first-lag autocorrelation of the frame
   sequence) — as new CloudView colour modes.
5. **Costs**: 4× render cost per config → a 2M-config mod bank ≈ one
   ordinary 8M bank. 30k-node map, ~5 MB gz — same as today.

## New oscillator kinds (recommendation)

- **Pulse-PW (variable duty)** — the strongest candidate: reaches the
  Pulse-PW family properly (currently only approximated by op stacks),
  trivially bandlimit-safe as a modulator, one extra per-op param
  (`duty` 0.05–0.5). Engine: `Operator::Wave::Pulse` + `_wave` case in
  `gpu_fm` + duty param + UI combo entry. Solo/mix map variants follow the
  existing waveform-variant pattern (`--waveform 4`, duty sampled).
  **⚠ State-compat gate (verified, do not skip):** `opId(i,"waveform")` is
  a 4-entry `AudioParameterChoice`; host-saved APVTS state stores the
  NORMALISED value, so extending to 5 entries remaps every saved
  square (3/3 = 1.0) to the new top index — the same hazard class as the
  old polyphony choice. Preset files are safe (they store the raw index),
  but host sessions are not. Ship pulse only with a migration: a
  version-bumped replacement parameter (e.g. `waveform` v2 with headroom
  to 8 entries) plus a `setStateInformation` remap of v1 values. That
  design decision wants Will's sign-off, which is why pulse did not ship
  in this pass.
- **Noise-tinted modulator** (white noise × depth into the phase) — cheap,
  gives breath/texture axes the bank lacks entirely; worth a probe before
  committing (may just read as the chaos bulk).
- **Not recommended**: audio-rate hard-sync / supersaw-in-one-op — both
  break the sideband mathematics the whole Stern-Brocot premise stands on
  (ratios stop meaning sideband positions), and the map would inherit the
  confusion. If wanted, they belong in a separate experimental space, not
  the navigable maps.

Nothing here ships in this pass — this is the design + the feasibility
evidence, per "consider a map with mod options and different oscillators".

## Pipeline status (2026-08-06 — workstream M)

**M1 SHIPPED** — `phase3/gpu_modbank.py build`: 2,000,000 6-op configs
(gpu_bank sampling style, sine ops only this pass) + ONE modulator each
(target uniformly depth-of-a-random-active-op or op-0 feedback; shape
sine/tri; rate ln-uniform 0.3–7 Hz; mod depth U(0.3, 1.0)). Trajectory
fingerprint = 4 frames at real-time offsets dt = 120 ms (frame 0 at LFO
phase 0 = the unmodulated timbre). Saved as `derived/gpu_modbank.npz`
(1.09 GB): `amps` (N, 4, 64) fp16 plus gpu_bank's ridx/depths/feedback/
waves keys (so `gpu_paths._subsample` reads it unchanged) plus
`mod_kind/mod_op/mod_shape/mod_rate/mod_mdepth`. Build wall time: **20 s**
on the 4090 (~107k cfg/s × 4 frames). Frame-scaling math is in pure numpy
helpers, unit-tested CPU-side in `tests/test_modbank.py`.

One deliberate deviation from gpu_bank's sampler: configs whose modulator
targets feedback always get nonzero feedback (the 50% feedback gate applies
only to depth-target configs) — an LFO on zero feedback is silent, and
statically-labelled "modulated" nodes would pollute the motion geometry.

Classification (`classify` subcommand) runs on the PHASE-0 frame only, so
families stay timbre-defined; labels npz mirrors gpu_landscape.classify's
keys. 2M rows in 2 s.
