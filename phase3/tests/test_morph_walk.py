"""Tests for the pure-Python mirror of the C++ runtime morph.

Conformance target: PullIndex::pathWalk / PullIndex::morph
(source/pull/PullIndex.cpp:179-269) and SteerTab::sampleRoute /
groovedRatios (source/ui/SteerTab.cpp). The rounding tests pin the
JUCE roundToInt semantics — ties-to-even on the float32 product — NOT
floor(x + 0.5).
"""

from __future__ import annotations

import math

import numpy as np
import pytest

from phase3.config import walk_path
from phase3.morph_walk import (
    FEATURE_NAMES,
    IMPLICIT_OFF,
    Op,
    events,
    feature_vector,
    glide_ratios,
    lerp_feedback,
    morph,
    pair_features,
    path_walk,
)


# ---------------------------------------------------------------------------
# path_walk conformance
# ---------------------------------------------------------------------------

def test_path_walk_endpoints_and_clamping():
    assert path_walk("RRL", "LL", 0.0) == "RRL"
    assert path_walk("RRL", "LL", 1.0) == "LL"
    assert path_walk("RRL", "LL", -0.5) == "RRL"   # clamped below
    assert path_walk("RRL", "LL", 1.5) == "LL"     # clamped above


def test_path_walk_identical_paths():
    for a in (0.0, 0.3, 0.5, 0.7, 1.0):
        assert path_walk("RLR", "RLR", a) == "RLR"
        assert path_walk("", "", a) == ""


def test_path_walk_empty_to_deep_and_back():
    # "" -> "RRLL": pure descent (up=0, down=4).
    assert path_walk("", "RRLL", 0.3) == "R"       # 0.3*4=1.2 -> step 1
    assert path_walk("", "RRLL", 0.6) == "RR"      # 2.4 -> step 2
    assert path_walk("", "RRLL", 0.9) == "RRLL"    # 3.6 -> step 4
    # "LLLL" -> "": pure ascent (up=4, down=0).
    assert path_walk("LLLL", "", 0.3) == "LLL"
    assert path_walk("LLLL", "", 0.6) == "LL"
    assert path_walk("LLLL", "", 0.9) == ""


def test_path_walk_prefix_pairs():
    # up=0: frm is a prefix of to.
    assert path_walk("RL", "RLRR", 0.6) == "RLR"   # 0.6*2=1.2 -> step 1
    # down=0: to is a prefix of frm.
    assert path_walk("RLRL", "RL", 0.6) == "RLR"   # step 1 trims one char


def test_path_walk_half_boundary_ties_to_even():
    # JUCE roundToInt resolves exact .5 ties to the nearest EVEN integer
    # (magic-number trick), NOT floor(x+0.5). The design sketch's
    # "total=2, alpha=0.25 -> step=1" is what floor(x+0.5) would give;
    # the C++ gives step=0.
    # frm="RR", to="RL": lca=1, up=1, down=1, total=2.
    assert path_walk("RR", "RL", 0.25) == "RR"     # x=0.5 -> 0, not 1
    assert path_walk("RR", "RL", 0.75) == "RL"     # x=1.5 -> 2, not 1
    # frm="RRRR", to="": up=4, down=0, total=4.
    assert path_walk("RRRR", "", 0.125) == "RRRR"  # x=0.5 -> 0
    assert path_walk("RRRR", "", 0.375) == "RR"    # x=1.5 -> 2
    assert path_walk("RRRR", "", 0.625) == "RR"    # x=2.5 -> 2 (even), not 3
    assert path_walk("RRRR", "", 0.875) == ""      # x=3.5 -> 4


def test_path_walk_floor_half_would_differ():
    # Explicit cases where floor(x+0.5) != the actual C++ result.
    # x=2.5: floor(3.0)=3 but ties-to-even -> 2.
    assert path_walk("LLLL", "", 0.625) == "LL"    # step 2, floor-half says 3
    # x=0.5: floor(1.0)=1 but ties-to-even -> 0.
    assert path_walk("LL", "LR", 0.25) == "LL"     # total=2, step 0


def test_path_walk_float32_product_semantics():
    # alpha = nextafter(2.5/3, 1): float64 product is 2.5000000000000004
    # (rounds to 3) but the C++ computes alpha * (float) total in
    # float32, where the product is exactly 2.5 -> ties-to-even -> 2.
    a = math.nextafter(2.5 / 3.0, 1.0)
    assert round(a * 3) == 3                       # what a float64 mirror does
    assert path_walk("LLL", "", a) == "L"          # step 2, not 3


def test_path_walk_bruteforce_sweep_sequences():
    pairs = [
        ("RRLR", "RLL"),
        ("", "RRLL"),
        ("LLL", ""),
        ("RLRL", "RL"),
        ("RL", "RLRR"),
        ("LRL", "RLR"),
        ("RRLLRL", "RRLR"),
    ]
    for frm, to in pairs:
        # Expected walk: frm's prefixes down to the LCA, then to's
        # prefixes out to to.
        lca = 0
        while lca < min(len(frm), len(to)) and frm[lca] == to[lca]:
            lca += 1
        expected = [frm[: len(frm) - s] for s in range(len(frm) - lca + 1)]
        expected += [to[: lca + d] for d in range(1, len(to) - lca + 1)]

        outputs = [path_walk(frm, to, a) for a in np.linspace(0.0, 1.0, 1001)]
        distinct = [outputs[0]]
        for o in outputs[1:]:
            if o != distinct[-1]:
                distinct.append(o)
        # Monotone up-then-down traversal, no skips, no backtracking.
        assert distinct == expected, (frm, to)


# ---------------------------------------------------------------------------
# morph
# ---------------------------------------------------------------------------

def test_morph_padding_lengths():
    cur = [Op("R", 1.0, True, 0), Op("L", 0.5, True, 1)]
    tgt = [Op("RL", 2.0, True, 0)]
    out = morph(cur, tgt, 1.0)
    assert len(out) == 2
    # Extra cur slot fades OUT against IMPLICIT_OFF: off and silent at 1.
    assert out[1].enabled is False
    assert out[1].depth == 0.0
    assert out[1].path == ""
    # Shorter cur: extra tgt slot fades IN from silence.
    out2 = morph(tgt, cur, 0.5)
    assert len(out2) == 2
    assert out2[1].enabled is True          # OR with the implicit-off slot
    assert out2[1].depth == pytest.approx(0.25)   # 0 -> 0.5 at alpha=.5


def test_morph_effective_depth_ignores_disabled_stored_depth():
    # Disabled cur slot with a big stored depth contributes 0 to the lerp.
    cur = [Op("R", 3.0, False, 0)]
    tgt = [Op("R", 1.0, True, 0)]
    out = morph(cur, tgt, 0.5)
    assert out[0].depth == pytest.approx(0.5)     # lerp(0, 1.0, 0.5)
    # And a disabled target ramps the slot down to 0.
    out = morph(tgt, cur, 0.75)
    assert out[0].depth == pytest.approx(0.25)    # lerp(1.0, 0, 0.75)


def test_morph_enabled_or_between_endpoints():
    cur = [Op("R", 1.0, True, 0)]
    tgt = [Op("R", 1.0, False, 0)]
    assert morph(cur, tgt, 0.0)[0].enabled is True     # == cur.enabled
    assert morph(cur, tgt, 0.5)[0].enabled is True     # OR
    assert morph(cur, tgt, 1.0)[0].enabled is False    # == tgt.enabled
    assert morph(tgt, cur, 0.0)[0].enabled is False
    assert morph(tgt, cur, 0.001)[0].enabled is True   # OR kicks in


def test_morph_exact_endpoints():
    # Dyadic depths so the float32 jmap is exact at both ends.
    cur = [Op("RRL", 0.5, True, 2), Op("L", 1.25, True, 1)]
    tgt = [Op("RL", 1.5, True, 3), Op("", 0.75, True, 0)]
    at0 = morph(cur, tgt, 0.0)
    at1 = morph(cur, tgt, 1.0)
    for got, want in zip(at0, cur):
        assert (got.path, got.depth, got.enabled, got.wave) == (
            want.path, want.depth, want.enabled, want.wave)
    for got, want in zip(at1, tgt):
        assert (got.path, got.depth, got.enabled, got.wave) == (
            want.path, want.depth, want.enabled, want.wave)


def test_morph_wave_target_wins_at_exact_half():
    # C++: (alpha < 0.5f ? cur : tgt).waveform — tgt wins AT 0.5.
    cur = [Op("R", 1.0, True, 1)]
    tgt = [Op("R", 1.0, True, 3)]
    assert morph(cur, tgt, 0.49)[0].wave == 1
    assert morph(cur, tgt, 0.5)[0].wave == 3
    assert morph(cur, tgt, 0.51)[0].wave == 3


def test_morph_alpha_clamped():
    cur = [Op("R", 1.0, True, 0)]
    tgt = [Op("L", 2.0, True, 1)]
    lo = morph(cur, tgt, -1.0)[0]
    hi = morph(cur, tgt, 2.0)[0]
    assert (lo.path, lo.depth, lo.wave) == ("R", 1.0, 0)
    assert (hi.path, hi.depth, hi.wave) == ("L", 2.0, 1)


# ---------------------------------------------------------------------------
# lerp_feedback
# ---------------------------------------------------------------------------

def test_lerp_feedback():
    assert lerp_feedback(0.25, 0.75, 0.0) == 0.25
    assert lerp_feedback(0.25, 0.75, 1.0) == 0.75
    assert lerp_feedback(0.25, 0.75, 0.5) == pytest.approx(0.5)
    assert lerp_feedback(0.25, 0.75, -1.0) == 0.25   # clamped
    assert lerp_feedback(0.25, 0.75, 2.0) == 0.75


# ---------------------------------------------------------------------------
# events
# ---------------------------------------------------------------------------

def _grid_change_brackets(frm: str, to: str, n: int = 4001):
    """(lo, hi) grid intervals where path_walk's output changes."""
    grid = np.linspace(0.0, 1.0, n)
    outs = [path_walk(frm, to, float(a)) for a in grid]
    return [(float(grid[k]), float(grid[k + 1]))
            for k in range(n - 1) if outs[k] != outs[k + 1]]


def test_events_match_grid_scan_of_path_walk():
    for frm, to in [("RRLR", "RLL"), ("", "RRLL"), ("LLL", ""),
                    ("RL", "RLRR"), ("LRL", "RLR")]:
        cur = [Op(frm, 1.0, True, 0)]
        tgt = [Op(to, 1.0, True, 0)]
        ev = events(cur, tgt)
        brackets = _grid_change_brackets(frm, to)
        assert len(ev) == len(brackets), (frm, to)
        for a, (lo, hi) in zip(ev, brackets):
            assert lo - 1e-9 <= a <= hi + 1e-9, (frm, to, a, lo, hi)


def test_events_no_change_no_events():
    cur = [Op("RL", 1.0, True, 0)]
    assert events(cur, cur) == []


def test_events_wave_event_iff_waves_differ_audibly():
    same_path = Op("R", 1.0, True, 0)
    # Wave differs on an enabled slot -> 0.5 event (no path events).
    assert events([same_path], [Op("R", 1.0, True, 2)]) == [0.5]
    # Wave same -> nothing.
    assert events([same_path], [Op("R", 2.0, True, 0)]) == []
    # Wave differs but the slot is off at BOTH ends -> inaudible, no event.
    assert events([Op("R", 1.0, False, 0)], [Op("R", 1.0, False, 2)]) == []
    # Enabled on one end only still counts.
    assert events([Op("R", 1.0, False, 0)], [Op("R", 1.0, True, 2)]) == [0.5]


def test_events_dedup_wave_and_path_at_half():
    # total=1 path walk has its single boundary at exactly 0.5; a wave
    # flip also lands at 0.5 -> deduped to one event.
    cur = [Op("R", 1.0, True, 0)]
    tgt = [Op("RL", 1.0, True, 2)]
    assert events(cur, tgt) == [0.5]


def test_events_multi_slot_union_sorted():
    cur = [Op("", 1.0, True, 0), Op("LL", 1.0, True, 0)]
    tgt = [Op("RR", 1.0, True, 0), Op("", 1.0, True, 0)]
    # Slot 0: total=2 -> {0.25, 0.75}; slot 1: total=2 -> {0.25, 0.75}.
    assert events(cur, tgt) == [0.25, 0.75]


def test_events_padding_slot_counts_paths():
    # Padded slot morphs "LR" -> "" (total=2): boundaries at .25/.75.
    cur = [Op("R", 1.0, True, 0), Op("LR", 1.0, True, 0)]
    tgt = [Op("R", 1.0, True, 0)]
    assert events(cur, tgt) == [0.25, 0.75]


# ---------------------------------------------------------------------------
# glide_ratios
# ---------------------------------------------------------------------------

def test_glide_ratios_endpoints():
    cur = [Op("R", 1.0, True, 0)]    # 2/1
    tgt = [Op("L", 1.0, True, 0)]    # 1/2
    assert glide_ratios(cur, tgt, 0.0)[0] == pytest.approx(2.0, rel=1e-6)
    assert glide_ratios(cur, tgt, 1.0)[0] == pytest.approx(0.5, rel=1e-6)


def test_glide_ratios_monotone_in_log_space():
    cur = [Op("L", 1.0, True, 0)]     # 1/2
    tgt = [Op("RRL", 1.0, True, 0)]   # 5/2
    logs = [math.log2(glide_ratios(cur, tgt, a)[0])
            for a in np.linspace(0.0, 1.0, 401)]
    assert all(b >= a for a, b in zip(logs, logs[1:]))
    # Midpoint is the geometric mean of the endpoint ratios.
    mid = glide_ratios(cur, tgt, 0.5)[0]
    assert mid == pytest.approx(math.sqrt(0.5 * 2.5), rel=1e-6)


def test_glide_ratios_disabled_and_min_length():
    cur = [Op("R", 1.0, False, 0), Op("R", 1.0, True, 0), Op("L", 1.0, True, 0)]
    tgt = [Op("L", 1.0, False, 0), Op("R", 1.0, True, 0)]
    out = glide_ratios(cur, tgt, 0.5)
    assert len(out) == 2                      # min length, NOT padded
    assert out[0] == 0.0                      # disabled both ends -> skipped
    assert out[1] == pytest.approx(2.0, rel=1e-6)
    # Empty path -> ratio 1.0.
    out2 = glide_ratios([Op("", 1.0, True, 0)], [Op("", 1.0, True, 0)], 0.3)
    assert out2[0] == pytest.approx(1.0, rel=1e-6)


# ---------------------------------------------------------------------------
# pair_features / feature_vector
# ---------------------------------------------------------------------------

def _example_pair():
    cur = [Op("RRLR", 1.5, True, 0), Op("L", 0.5, True, 1),
           Op("RL", 2.0, False, 0)]
    tgt = [Op("RLL", 0.5, True, 2), Op("L", 1.0, True, 1)]
    return cur, tgt


def test_pair_features_symmetric_under_swap():
    cur, tgt = _example_pair()
    f_ab = pair_features(cur, tgt)
    f_ba = pair_features(tgt, cur)
    assert set(f_ab) == set(FEATURE_NAMES)
    for name in FEATURE_NAMES:
        assert f_ab[name] == pytest.approx(f_ba[name]), name


def test_pair_features_against_string_reference():
    cur = [Op("RRLR", 1.5, True, 0)]
    tgt = [Op("RLL", 0.5, True, 2)]
    f = pair_features(cur, tgt)
    # String-based reference: common prefix "R" -> lca=1, up=3, down=2.
    assert f["n_slots"] == 1.0
    assert f["n_path_changed"] == 1.0
    assert f["total_sum"] == f["total_max"] == 5.0
    assert f["lca_depth_sum"] == f["lca_depth_max"] == 1.0
    # q at the LCA rational: walk_path("R") == (2, 1) -> q = 1.
    assert walk_path("R") == (2, 1)
    assert f["q_at_lca_sum"] == f["q_at_lca_max"] == 1.0
    # Max |log2 step| along the enumerated walk strings.
    strings = ["RRLR", "RRL", "RR", "R", "RL", "RLL"]
    logs = [math.log2(walk_path(s)[0] / walk_path(s)[1]) for s in strings]
    want = max(abs(b - a) for a, b in zip(logs, logs[1:]))
    assert f["max_log2_step_sum"] == pytest.approx(want)
    assert f["max_log2_step_max"] == pytest.approx(want)
    # Effective-depth delta, wave flip (audible), no enable change.
    assert f["depth_delta_sum"] == pytest.approx(1.0)
    assert f["n_wave_differs"] == 1.0 and f["wave_differs_any"] == 1.0
    assert f["n_enable_changes"] == 0.0 and f["enable_change_any"] == 0.0
    assert f["n_events"] == float(len(events(cur, tgt)))


def test_pair_features_unchanged_slots_contribute_no_path_features():
    cur = [Op("RL", 1.0, True, 0)]
    f = pair_features(cur, cur)
    assert f["n_path_changed"] == 0.0
    assert f["total_sum"] == f["lca_depth_sum"] == f["q_at_lca_sum"] == 0.0
    assert f["max_log2_step_sum"] == 0.0
    assert f["n_events"] == 0.0


def test_pair_features_padding_and_enable_counts():
    cur = [Op("R", 1.0, True, 0), Op("LL", 0.5, True, 1)]
    tgt = [Op("R", 1.0, True, 0)]
    f = pair_features(cur, tgt)
    assert f["n_slots"] == 2.0
    assert f["n_enable_changes"] == 1.0        # padded slot: True -> False
    assert f["depth_delta_sum"] == pytest.approx(0.5)
    # Padded slot walks "LL" -> "" (total 2).
    assert f["n_path_changed"] == 1.0
    assert f["total_sum"] == 2.0
    # Wave differs on the padded slot (1 vs implicit 0) and it is
    # enabled on one end -> counted.
    assert f["n_wave_differs"] == 1.0


def test_feature_vector_order_and_dtype():
    cur, tgt = _example_pair()
    v = feature_vector(cur, tgt)
    f = pair_features(cur, tgt)
    assert v.shape == (len(FEATURE_NAMES),)
    assert v.dtype == np.float64
    for k, name in enumerate(FEATURE_NAMES):
        assert v[k] == f[name], name


def test_implicit_off_matches_cpp_default():
    assert IMPLICIT_OFF == Op("", 0.0, False, 0)
