"""Tests for the mod-map pipeline's pure helpers (the bank/map builds are GPU
runs against the real 2M trajectory bank and aren't unit-tested here — same
split as test_butterfly_map_study). CPU-only: gpu_modbank keeps torch imports
lazy so these helpers load without a GPU environment."""

from __future__ import annotations

import json

import numpy as np

from phase3.gpu_modbank import (KIND_NAMES, SHAPE_NAMES, apply_modulator,
                                lfo_value, mod_scale, mods_entry, motion_raw,
                                node_extras, norm_unit, rate_norm)


# --- LFO waveform + frame-scaling math ---------------------------------------

def test_lfo_sine_hand_values():
    ph = np.array([0.0, np.pi / 2, np.pi, 3 * np.pi / 2])
    v = lfo_value(ph, np.zeros(4, int))
    assert np.allclose(v, [0.0, 1.0, 0.0, -1.0], atol=1e-12)


def test_lfo_tri_hand_values():
    # tri = (2/pi)·asin(sin(ph)): linear ramps, peaks ±1 at ±pi/2.
    ph = np.array([0.0, np.pi / 4, np.pi / 2, 3 * np.pi / 4, np.pi,
                   3 * np.pi / 2])
    v = lfo_value(ph, np.ones(6, int))
    assert np.allclose(v, [0.0, 0.5, 1.0, 0.5, 0.0, -1.0], atol=1e-12)


def test_lfo_shape_selects_per_element():
    ph = np.full(2, np.pi / 4)
    v = lfo_value(ph, np.array([0, 1]))
    assert np.allclose(v, [np.sin(np.pi / 4), 0.5])


def test_mod_scale_is_one_at_phase_zero_and_clamps():
    # Phase 0 → scale exactly 1 (frame 0 is the unmodulated timbre).
    assert np.allclose(mod_scale(np.zeros(2), np.array([0.3, 1.0]),
                                 np.array([0, 1])), 1.0)
    # Trough with mdepth 1 → exactly 0; a synthetic mdepth > 1 clamps at 0.
    trough = np.array([3 * np.pi / 2])
    assert np.allclose(mod_scale(trough, np.array([1.0]), np.array([0])), 0.0)
    assert np.allclose(mod_scale(trough, np.array([1.5]), np.array([0])), 0.0)
    # Peak scales up: 1 + mdepth.
    peak = np.array([np.pi / 2])
    assert np.allclose(mod_scale(peak, np.array([0.6]), np.array([0])), 1.6)


def test_apply_modulator_depth_kind_scales_only_target_op():
    depths = np.array([[1.0, 2.0, 0.5]])
    d, fb = apply_modulator(depths, np.array([0.4 * np.pi]),
                            kind=np.array([0]), op=np.array([1]),
                            scale=np.array([1.5]))
    assert np.allclose(d, [[1.0, 3.0, 0.5]])          # only op 1 scaled
    assert np.allclose(fb, [0.4 * np.pi])             # feedback untouched
    assert np.allclose(depths, [[1.0, 2.0, 0.5]])     # input not mutated


def test_apply_modulator_feedback_kind_clamps_param_to_unit():
    depths = np.array([[1.0, 2.0], [1.0, 2.0]])
    fb = np.array([0.8 * np.pi, 0.2 * np.pi])
    d, fb2 = apply_modulator(depths, fb, kind=np.array([1, 1]),
                             op=np.array([0, 0]), scale=np.array([2.0, 0.5]))
    assert np.allclose(d, depths)                     # depths untouched
    # 0.8·2 = 1.6 clamps to 1.0 (param) → pi radians; 0.2·0.5 = 0.1 → 0.1·pi.
    assert np.allclose(fb2, [np.pi, 0.1 * np.pi])


def test_apply_modulator_zero_feedback_stays_zero():
    d, fb = apply_modulator(np.array([[1.0]]), np.array([0.0]),
                            kind=np.array([1]), op=np.array([0]),
                            scale=np.array([1.9]))
    assert fb[0] == 0.0


# --- mods metadata → JSON ----------------------------------------------------

def test_mods_entry_stringifies_and_rounds():
    m = mods_entry(kind=1, op=0, shape=0, rate=1.73456, mdepth=0.61234)
    assert m == [{"kind": "feedback", "op": 0, "shape": "sine",
                  "rate": 1.735, "depth": 0.612}]
    m = mods_entry(kind=0, op=3, shape=1, rate=0.3, mdepth=1.0)
    assert m[0]["kind"] == "depth" and m[0]["shape"] == "tri"
    assert m[0]["op"] == 3
    assert KIND_NAMES == ("depth", "feedback")
    assert SHAPE_NAMES == ("sine", "tri")


# --- motion / rate normalisation ---------------------------------------------

def test_motion_raw_static_frames_are_zero():
    frames = np.tile(np.random.default_rng(0).random((3, 1, 8)), (1, 4, 1))
    assert np.allclose(motion_raw(frames), 0.0)


def test_motion_raw_hand_value():
    # Two frames differing by 0.25 in two bins → L1 = 0.5, one diff → mean 0.5.
    f0 = np.zeros(8); f1 = np.zeros(8); f1[0] = f1[3] = 0.25
    frames = np.stack([f0, f1])[None]                  # (1, 2, 8)
    assert np.allclose(motion_raw(frames), 0.5)


def test_norm_unit_p98_and_clip():
    raw = np.concatenate([np.linspace(0.0, 1.0, 100), [50.0]])  # one outlier
    n = norm_unit(raw)
    assert n.max() == 1.0                              # outlier clipped
    assert n.min() == 0.0
    hi = np.percentile(raw, 98.0)
    assert np.allclose(n[50], raw[50] / hi)


def test_rate_norm_log_mapping_and_clip():
    assert np.allclose(rate_norm(0.3), 0.0)
    assert np.allclose(rate_norm(7.0), 1.0)
    assert np.allclose(rate_norm(np.sqrt(0.3 * 7.0)), 0.5)     # log midpoint
    assert rate_norm(0.01) == 0.0 and rate_norm(99.0) == 1.0   # clipped


# --- JSON node schema round-trip ---------------------------------------------

def test_node_extras_json_round_trip():
    nd = {"family": 3, "feedback": 0.25, "x": 0.1, "y": -0.2, "bright": 0.4,
          "dense": 0.2, "odd": 0.5, "tonal": 0.9, "crit": 0.1}
    nd.update(node_extras(kind=0, op=2, shape=1, rate=2.0, mdepth=0.5,
                          motion_n=0.34567))
    nd["ops"] = [{"path": "RL", "depth": 1.5, "enabled": True}]
    back = json.loads(json.dumps(nd))
    assert set(back) == {"family", "feedback", "x", "y", "bright", "dense",
                         "odd", "tonal", "crit", "motion", "rate_n", "mods",
                         "ops"}
    assert back["motion"] == 0.3457                    # rounded to 4 dp
    assert 0.0 <= back["rate_n"] <= 1.0
    assert back["mods"] == [{"kind": "depth", "op": 2, "shape": "tri",
                             "rate": 2.0, "depth": 0.5}]
