"""Tests for per-op detune (model) and the drift→modulation mapping (D2)."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.config import Config, ModConfig
from phase3.drift_mapping import (
    ModulationPlan,
    beat_rate_for_detune,
    decompose_modulation,
    detune_cents_for_beat,
    drift_profile_to_plan,
    plan_to_v1_brocot,
)
from phase3.stft_trajectory import audio_to_drift_profile

SR = 48_000
F_C = 220.0


# ---------------------------------------------------------------------------
# Per-op detune in the model
# ---------------------------------------------------------------------------

def test_detune_shifts_ratio():
    m0 = ModConfig(path="R", depth=1.0, enabled=True)             # 2/1
    m1 = ModConfig(path="R", depth=1.0, enabled=True, detune_cents=100.0)
    assert m0.ratio() == pytest.approx(2.0)
    assert m1.ratio() == pytest.approx(2.0 * 2 ** (1 / 12), rel=1e-9)  # +1 semitone


def test_zero_detune_is_exact_rational():
    assert ModConfig(path="RLL", depth=1.0, enabled=True).ratio() == pytest.approx(4 / 3)


def test_detune_in_fingerprint():
    a = Config(mods=[ModConfig("R", 1.0, True, 0.0)] + [ModConfig() for _ in range(7)])
    b = Config(mods=[ModConfig("R", 1.0, True, 5.0)] + [ModConfig() for _ in range(7)])
    assert a.fingerprint() != b.fingerprint()


def test_detune_writer_roundtrips():
    from phase3.build_pull_index import _config_from_preset
    doc = {"engine": {"operators": [
        {"enabled": True, "path": "R", "depth": 1.0, "detune": 7.5},
        {"enabled": False, "path": "", "depth": 0.0},
    ]}}
    cfg = _config_from_preset(doc)
    assert cfg.mods[0].detune_cents == pytest.approx(7.5)


# ---------------------------------------------------------------------------
# detune ↔ beat bridge
# ---------------------------------------------------------------------------

def test_detune_beat_roundtrip():
    for rate in (0.5, 1.0, 3.0):
        c = detune_cents_for_beat(rate, F_C)
        assert beat_rate_for_detune(c, F_C) == pytest.approx(rate, rel=1e-6)


def test_detune_for_beat_small_at_low_rate():
    # ~0.76 Hz beat at 220 Hz ⇒ ~6 cents (the smoke-test number).
    assert detune_cents_for_beat(0.764, F_C) == pytest.approx(6.0, abs=0.2)


# ---------------------------------------------------------------------------
# decomposition: multiple layers
# ---------------------------------------------------------------------------

def test_decompose_finds_two_components():
    dt = 0.01
    t = np.arange(0, 6, dt)
    sig = 1.0 * np.sin(2 * np.pi * 0.7 * t) + 0.5 * np.sin(2 * np.pi * 4.0 * t)
    comps = decompose_modulation(sig, dt, k=3)
    rates = sorted(r for r, _ in comps)
    assert any(abs(r - 0.7) < 0.2 for r in rates)
    assert any(abs(r - 4.0) < 0.3 for r in rates)


# ---------------------------------------------------------------------------
# end-to-end: synthetic two-layer drift → plan
# ---------------------------------------------------------------------------

def _fm_depth(I_of_t, ratio=2.0):
    t = np.arange(I_of_t.size, dtype=np.float64) / SR
    return np.sin(2 * np.pi * F_C * t + I_of_t * np.sin(2 * np.pi * ratio * F_C * t))


def test_slow_pitch_drift_maps_to_osc_detune():
    # Slow (~0.8 Hz) pitch wander ⇒ a detune_beat layer + osc detune offsets.
    t = np.arange(int(4.0 * SR)) / SR
    f_inst = F_C * (1.0 + 0.004 * np.sin(2 * np.pi * 0.8 * t))
    x = np.sin(2 * np.pi * np.cumsum(f_inst) / SR
               + 1.5 * np.sin(2 * np.pi * np.cumsum(2 * f_inst) / SR))
    plan = drift_profile_to_plan(
        audio_to_drift_profile(x, q_max=48, f_carrier=F_C, sample_rate=SR), n_osc=3)
    pitch = [L for L in plan.layers if L.source == "pitch"]
    assert any(L.kind == "detune_beat" for L in pitch)
    assert len(plan.osc_detune_cents) == 3
    # symmetric spread around 0, beating near the drift rate
    assert plan.osc_detune_cents[1] == pytest.approx(0.0)
    assert plan.osc_detune_cents[0] == pytest.approx(-plan.osc_detune_cents[2])
    assert beat_rate_for_detune(plan.osc_detune_cents[2], F_C) < 2.0


def test_brightness_sweep_maps_to_lfo_and_envelope_adsr():
    t = np.arange(int(2.0 * SR)) / SR
    I = np.linspace(0.3, 6.0, t.size)
    plan = drift_profile_to_plan(
        audio_to_drift_profile(_fm_depth(I), q_max=48, f_carrier=F_C, sample_rate=SR))
    v1 = plan_to_v1_brocot(plan)
    assert set(v1["envelope"]) == {"attack", "decay", "sustain", "release"}
    assert "lfo" in v1 and v1["lfo"]["depth"] >= 0.0
    # A monotonic index ramp registers as a brightness sweep.
    assert any(L.source == "brightness" for L in plan.layers)


def test_plan_describe_runs():
    t = np.arange(int(2.0 * SR)) / SR
    plan = drift_profile_to_plan(
        audio_to_drift_profile(_fm_depth(np.full(t.size, 2.0)),
                               q_max=32, f_carrier=F_C, sample_rate=SR))
    assert isinstance(plan.describe(), str)
    assert "ModulationPlan" in plan.describe()
