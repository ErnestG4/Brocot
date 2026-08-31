"""Pure-Python mirror of the C++ runtime morph (smooth-jump slice 1).

This module is the ground-truth generator for the GPU smooth-jump
pipeline: it reproduces, in numpy/stdlib Python, exactly what the
synth runtime does when scrubbing between two patches, so that
anything trained offline against these trajectories transfers to the
real engine. It mirrors:

* ``source/pull/PullIndex.cpp`` lines 179-269 —
  ``PullIndex::pathWalk`` (stepped Stern-Brocot tree walk between two
  L/R path strings) and ``PullIndex::morph`` (per-slot depth lerp,
  enable OR, waveform snap, implicit-off padding).
* ``source/ui/SteerTab.cpp`` — ``SteerTab::sampleRoute`` (lines
  ~417-437; the per-segment feedback lerp) and
  ``SteerTab::groovedRatios`` (lines ~439-475; the continuous
  log2-ratio glide), here WITHOUT the Farey-gravity pull.

Two runtime trajectory semantics coexist and both are mirrored:

1. **SB path-walk** (``path_walk`` / ``morph``): the op's path takes
   discrete tree steps (parent-trims up to the LCA, then L/R appends
   down to the target); each step is a ratio jump the audio-thread
   smoother absorbs.
2. **Steer-tab glide** (``glide_ratios``): SteerTab bypasses the
   walked ratio for the *audible* ratio via ``applySteeredRatios`` —
   a continuous lerp in log2-ratio space between the endpoint
   rationals (plus a gravity pull we deliberately omit here).

Divergences from the design sketch (the C++ wins, per the panel):

* ``juce::roundToInt`` is NOT ``floor(x + 0.5)`` (round-half-up). It
  is the magic-number trick ``(double) v + 6755399441055744.0`` which,
  under the default FPU rounding mode, rounds half-way cases to the
  nearest EVEN integer — the JUCE header says so explicitly. So e.g.
  total=2, alpha=0.25 gives x=0.5 -> step 0 (stays at ``frm``), and
  total=4, alpha=0.625 gives x=2.5 -> step 2, not 3. Python's builtin
  ``round()`` on a binary double has identical ties-to-even
  semantics, verified against a bit-exact emulation of the JUCE trick.
* The product is computed in float32 (``alpha * (float) totalSteps``)
  before being widened to double inside ``roundToInt``; mirrored via
  ``np.float32``. This is observable: for alpha =
  nextafter(2.5/3, 1), total=3, the float64 product rounds to 3 but
  the float32 product is exactly 2.5 -> step 2.
* All float arithmetic that the C++ performs in 32-bit (depth jmap,
  feedback lerp, the final (float) casts in groovedRatios) is done in
  float32 here too, so values are bit-comparable to the runtime.

Feedback units: ``lerp_feedback`` works in the engine's 0..1 feedback
units (what ``applyOp0Feedback`` receives). The renderer's feedback
phase coefficient is ``beta = fb * pi`` radians.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from phase3.config import walk_path

# Mirrors params::kNumOperators (source/params/ParameterTree.h).
K_NUM_OPERATORS = 64

# Tolerance used to dedup event alphas.
_EVENT_DEDUP_EPS = 1e-9


# ---------------------------------------------------------------------------
# Op state (mirrors PullIndex::OpState; `wave` <-> C++ `waveform`)
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Op:
    path: str = ""        # Stern-Brocot L/R path; "" = 1/1
    depth: float = 0.0    # modulation index
    enabled: bool = False
    wave: int = 0         # 0=sine, 1=tri, 2=saw, 3=square, 4=pulse
    duty: float = 0.25    # pulse duty (wave 4 only; the param default)


#: Padding value for length-mismatched morphs (C++ `kImplicitOff`).
IMPLICIT_OFF = Op("", 0.0, False, 0, 0.25)


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def _clamp_alpha_f32(alpha: float) -> float:
    """Clamp to [0,1] and pass through float32, as the C++ float alpha is."""
    return float(np.float32(min(max(float(alpha), 0.0), 1.0)))


def _lca_len(frm: str, to: str) -> int:
    """Longest-common-prefix length = position of the SB LCA on the walk."""
    lca = 0
    n = min(len(frm), len(to))
    while lca < n and frm[lca] == to[lca]:
        lca += 1
    return lca


def _walk_totals(frm: str, to: str) -> tuple[int, int, int]:
    """(lca, up_steps, total_steps) for the walk frm -> LCA -> to."""
    lca = _lca_len(frm, to)
    up = len(frm) - lca
    total = up + (len(to) - lca)
    return lca, up, total


def _walk_strings(frm: str, to: str) -> list[str]:
    """Path string at every integer step s = 0..total of the walk."""
    lca, up, total = _walk_totals(frm, to)
    out = [frm[: len(frm) - s] for s in range(up + 1)]
    out += [to[: lca + d] for d in range(1, total - up + 1)]
    return out


def _ratio_or_one(path: str) -> float:
    """brocot::Path(path).ratio() with the C++ `!(r > 0) -> 1.0` guard."""
    p, q = walk_path(path)
    r = p / q
    return r if r > 0.0 else 1.0


# ---------------------------------------------------------------------------
# 1. path_walk — mirrors PullIndex::pathWalk (PullIndex.cpp:179-211)
# ---------------------------------------------------------------------------

def path_walk(frm: str, to: str, alpha: float) -> str:
    """Stepped Stern-Brocot walk between two paths at morph position alpha.

    Round-to-nearest gives equal-width segments centred on the
    transition points; halves resolve ties-to-even on the float32
    product, exactly like ``juce::roundToInt`` (see module docstring).
    """
    a = _clamp_alpha_f32(alpha)
    if frm == to:
        return frm
    if a <= 0.0:
        return frm
    if a >= 1.0:
        return to

    lca, up, total = _walk_totals(frm, to)
    if total == 0:
        return frm  # identical paths, already handled

    # float32 product, then ties-to-even (== JUCE magic-number round).
    x = float(np.float32(a) * np.float32(total))
    step = min(max(round(x), 0), total)

    if step <= up:
        return frm[: len(frm) - step]
    return to[: lca + (step - up)]


# ---------------------------------------------------------------------------
# 2. morph — mirrors PullIndex::morph (PullIndex.cpp:213-269)
# ---------------------------------------------------------------------------

def morph(cur: list[Op], tgt: list[Op], alpha: float) -> list[Op]:
    """Per-slot morph between two op lists at position alpha in [0,1].

    * Shorter list is padded with IMPLICIT_OFF so extra slots fade to /
      from silence instead of staying loud.
    * depth = jmap(alpha, cur_eff, tgt_eff) with x_eff = depth if
      enabled else 0 (click-free enable transitions), in float32.
    * enabled: cur.enabled at alpha<=0, tgt.enabled at alpha>=1, OR of
      both in between (slot stays on so depth can ramp through).
    * path: stepped SB walk (path_walk).
    * wave: discrete snap at the midpoint — ``alpha < 0.5 ? cur : tgt``,
      so the TARGET wins at exactly 0.5.
    * duty: continuous jmap like depth (float32) — mirrors the C++
      PullIndex::morph duty lerp added with pulse-in-maps.
    """
    a = _clamp_alpha_f32(alpha)
    n = max(len(cur), len(tgt))
    a32 = np.float32(a)

    out: list[Op] = []
    for i in range(n):
        c = cur[i] if i < len(cur) else IMPLICIT_OFF
        t = tgt[i] if i < len(tgt) else IMPLICIT_OFF

        cur_eff = np.float32(c.depth) if c.enabled else np.float32(0.0)
        tgt_eff = np.float32(t.depth) if t.enabled else np.float32(0.0)
        depth = float(cur_eff + a32 * (tgt_eff - cur_eff))  # juce::jmap

        if a <= 0.0:
            enabled = c.enabled
        elif a >= 1.0:
            enabled = t.enabled
        else:
            enabled = c.enabled or t.enabled

        path = path_walk(c.path, t.path, a)
        wave = c.wave if a < 0.5 else t.wave
        duty = float(np.float32(c.duty)
                     + a32 * (np.float32(t.duty) - np.float32(c.duty)))

        out.append(Op(path=path, depth=depth, enabled=enabled, wave=wave,
                      duty=duty))
    return out


# ---------------------------------------------------------------------------
# 3. lerp_feedback — mirrors SteerTab::sampleRoute's feedback lerp
# ---------------------------------------------------------------------------

def lerp_feedback(fb_a: float, fb_b: float, alpha: float) -> float:
    """Feedback lerp in engine 0..1 units (SteerTab.cpp:436), float32.

    ``A.feedback + (B.feedback - A.feedback) * frac``. The renderer's
    feedback phase coefficient is ``beta = fb * pi`` radians. The C++
    frac is a per-segment fraction already in [0,1); we clamp alpha to
    [0,1] for API safety.
    """
    a = np.float32(min(max(float(alpha), 0.0), 1.0))
    return float(np.float32(fb_a) + (np.float32(fb_b) - np.float32(fb_a)) * a)


# ---------------------------------------------------------------------------
# 4. events — discontinuity alphas of the stepped morph
# ---------------------------------------------------------------------------

def events(cur: list[Op], tgt: list[Op]) -> list[float]:
    """Sorted, deduped alphas where the morph output jumps discontinuously.

    * For each padded slot whose paths differ (total_i > 0), the
      round-to-nearest step boundaries: alpha = (s - 0.5) / total_i
      for s = 1..total_i. (Whether the boundary point itself lands on
      the old or new step depends on ties-to-even parity, but the
      discontinuity location is the boundary either way.) Path events
      are enumerated for ALL differing slots, matching path_walk's
      output changes irrespective of audibility.
    * 0.5 if any slot's waves differ — but only slots enabled on either
      end are counted, since a wave flip on a slot that is off at both
      ends has zero effective depth throughout and is inaudible
      (documented design choice).

    Dedup tolerance 1e-9. Depth and enable transitions are continuous
    by construction (effective-depth lerp) and contribute no events.
    """
    n = max(len(cur), len(tgt))
    alphas: list[float] = []
    wave_event = False
    for i in range(n):
        c = cur[i] if i < len(cur) else IMPLICIT_OFF
        t = tgt[i] if i < len(tgt) else IMPLICIT_OFF
        if c.path != t.path:
            _, _, total = _walk_totals(c.path, t.path)
            alphas.extend((s - 0.5) / total for s in range(1, total + 1))
        if (c.enabled or t.enabled) and c.wave != t.wave:
            wave_event = True
    if wave_event:
        alphas.append(0.5)

    alphas.sort()
    out: list[float] = []
    for a in alphas:
        if not out or a - out[-1] > _EVENT_DEDUP_EPS:
            out.append(a)
    return out


# ---------------------------------------------------------------------------
# 5. glide_ratios — mirrors SteerTab::groovedRatios WITHOUT gravity
# ---------------------------------------------------------------------------

def glide_ratios(cur: list[Op], tgt: list[Op], alpha: float) -> list[float]:
    """Per-slot continuous log2-ratio glide between endpoint rationals.

    Mirrors SteerTab::groovedRatios (SteerTab.cpp:439-475) minus the
    Farey-gravity pull: logL = (1-frac)*log2(rA) + frac*log2(rB), then
    exp2 — with the C++'s (float) casts. As in the C++:

    * output length = min(len(cur), len(tgt), kNumOperators) — NO
      implicit-off padding here;
    * slots disabled on BOTH ends are left at 0.0 (not glided);
    * a non-positive ratio falls back to 1.0 (defensive; SB ratios are
      always > 0).
    """
    a = _clamp_alpha_f32(alpha)
    n = min(len(cur), len(tgt), K_NUM_OPERATORS)
    out = [0.0] * n
    for i in range(n):
        c, t = cur[i], tgt[i]
        if not (c.enabled or t.enabled):
            continue
        r_a = _ratio_or_one(c.path)
        r_b = _ratio_or_one(t.path)
        log_l = (1.0 - a) * math.log2(r_a) + a * math.log2(r_b)
        out[i] = float(np.exp2(np.float32(log_l)))
    return out


# ---------------------------------------------------------------------------
# 6. pair_features — engineered features of a (cur, tgt) jump
# ---------------------------------------------------------------------------

#: Deterministic feature ordering for feature_vector().
FEATURE_NAMES: list[str] = [
    "n_slots",             # padded slot count max(len(cur), len(tgt))
    "n_path_changed",      # slots whose paths differ (total_i > 0)
    "total_sum",           # sum of walk lengths over changed slots
    "total_max",
    "lca_depth_sum",       # LCA prefix length, changed slots only
    "lca_depth_max",
    "q_at_lca_sum",        # denominator of the LCA rational, changed slots
    "q_at_lca_max",
    "max_log2_step_sum",   # per-slot max |log2 ratio jump| along the walk
    "max_log2_step_max",
    "depth_delta_sum",     # |tgt_eff - cur_eff| (effective depths), all slots
    "depth_delta_max",
    "n_wave_differs",      # audible slots (enabled either end) w/ wave flip
    "wave_differs_any",
    "n_enable_changes",    # slots with cur.enabled != tgt.enabled
    "enable_change_any",
    "n_events",            # len(events(cur, tgt))
]


def pair_features(cur: list[Op], tgt: list[Op]) -> dict[str, float]:
    """Flat dict of engineered jump features (see FEATURE_NAMES).

    Path-structure features (total, lca_depth, q_at_lca, max_log2_step)
    are aggregated over CHANGED slots only — an unchanged path
    contributes no discontinuity, so folding its lca/q into the sums
    would just add noise unrelated to the jump. All features are
    symmetric under swapping cur and tgt: the walk visits the same
    strings in reverse, effective-depth deltas are absolute, and the
    path-event alpha set {(s-0.5)/total} is symmetric about 0.5.
    """
    n = max(len(cur), len(tgt))

    totals: list[float] = []
    lca_depths: list[float] = []
    q_at_lcas: list[float] = []
    max_log2_steps: list[float] = []
    depth_deltas: list[float] = []
    n_wave = 0
    n_enable = 0

    for i in range(n):
        c = cur[i] if i < len(cur) else IMPLICIT_OFF
        t = tgt[i] if i < len(tgt) else IMPLICIT_OFF

        cur_eff = c.depth if c.enabled else 0.0
        tgt_eff = t.depth if t.enabled else 0.0
        depth_deltas.append(abs(tgt_eff - cur_eff))

        if (c.enabled or t.enabled) and c.wave != t.wave:
            n_wave += 1
        if c.enabled != t.enabled:
            n_enable += 1

        if c.path != t.path:
            lca, _, total = _walk_totals(c.path, t.path)
            totals.append(float(total))
            lca_depths.append(float(lca))
            q_at_lcas.append(float(walk_path(c.path[:lca])[1]))
            logs = [math.log2(_ratio_or_one(s))
                    for s in _walk_strings(c.path, t.path)]
            max_log2_steps.append(
                max(abs(b - a) for a, b in zip(logs, logs[1:])))

    def _agg(xs: list[float]) -> tuple[float, float]:
        return (sum(xs), max(xs)) if xs else (0.0, 0.0)

    total_sum, total_max = _agg(totals)
    lca_sum, lca_max = _agg(lca_depths)
    q_sum, q_max = _agg(q_at_lcas)
    step_sum, step_max = _agg(max_log2_steps)
    dd_sum, dd_max = _agg(depth_deltas)

    return {
        "n_slots": float(n),
        "n_path_changed": float(len(totals)),
        "total_sum": total_sum,
        "total_max": total_max,
        "lca_depth_sum": lca_sum,
        "lca_depth_max": lca_max,
        "q_at_lca_sum": q_sum,
        "q_at_lca_max": q_max,
        "max_log2_step_sum": step_sum,
        "max_log2_step_max": step_max,
        "depth_delta_sum": dd_sum,
        "depth_delta_max": dd_max,
        "n_wave_differs": float(n_wave),
        "wave_differs_any": float(n_wave > 0),
        "n_enable_changes": float(n_enable),
        "enable_change_any": float(n_enable > 0),
        "n_events": float(len(events(cur, tgt))),
    }


def feature_vector(cur: list[Op], tgt: list[Op]) -> np.ndarray:
    """pair_features as a float64 vector in FEATURE_NAMES order."""
    feats = pair_features(cur, tgt)
    return np.array([feats[name] for name in FEATURE_NAMES], dtype=np.float64)
