"""Tests for the extra instrument/waveform target families."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.config import Config, ModConfig
from phase3.families_extra import (
    extra_families,
    family_bell,
    family_brass,
    family_glass,
    family_hollow,
    family_pluck,
    family_reed,
    family_vowel,
)
from phase3.family_distance import family_distance


@pytest.mark.parametrize("fam", extra_families())
def test_target_is_finite_nonneg_nonzero(fam):
    a = fam.target_amplitudes(64)
    assert a.shape == (64,)
    assert np.all(np.isfinite(a))
    assert np.all(a >= 0.0)
    assert a.sum() > 0.0


@pytest.mark.parametrize("fam", extra_families())
def test_integrates_with_family_distance(fam):
    cfg = Config(mods=[ModConfig(path="R", depth=1.841, enabled=True),
                       ModConfig(path="RL", depth=2.405, enabled=True)]
                      + [ModConfig() for _ in range(6)], f_carrier=220.0)
    d = family_distance(cfg.predict(), fam, 220.0, q_max=16)
    assert np.isfinite(d) and d >= 0.0


def test_pluck_decays_with_harmonic():
    a = family_pluck(0.2).target_amplitudes(32)
    assert a[0] > a[4] > a[15]                      # monotone-ish HF rolloff


def test_brass_bumps_in_upper_mids():
    a = family_brass(center=6.0, width=3.0).target_amplitudes(32)
    # Harmonic 6 sits above a pure 1/sqrt(n) baseline at that index.
    n = np.arange(1, 33)
    baseline = 1.0 / np.sqrt(n)
    assert a[5] / baseline[5] > a[0] / baseline[0]


def test_reed_odd_dominates():
    a = family_reed().target_amplitudes(16)
    assert a[2] > a[3]                              # 3rd (odd) > 4th (even)
    assert a[4] > a[5]                              # 5th > 6th


def test_hollow_sparse_odd():
    a = family_hollow().target_amplitudes(16)
    assert np.argmax(a) == 0                        # fundamental strongest
    assert a[1] == 0.0 and a[3] == 0.0              # evens empty


def test_vowel_formants_peak():
    # 'i' has a high F2 (~2000 Hz ≈ partial 9 at 220 Hz); 'o' is low/dark.
    ai = family_vowel("i").target_amplitudes(20)
    ao = family_vowel("o").target_amplitudes(20)
    # 'i' carries more upper-partial energy than 'o'.
    hi = slice(7, 14)
    assert ai[hi].sum() / ai.sum() > ao[hi].sum() / ao.sum()


def test_bell_and_glass_are_kl_only():
    assert family_bell().characteristic_q is None
    assert family_glass().characteristic_q is None
    assert family_bell().target_amplitudes(48).sum() > 0
    assert family_glass().target_amplitudes(48).sum() > 0


def test_vowel_rejects_unknown():
    with pytest.raises(ValueError):
        family_vowel("q")
