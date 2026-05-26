"""Tests for the Mode B audio→spectrum pipeline (P0.1) and the
external-target family (P0.2)."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.audio_spectrum import (
    audio_to_spectrum,
    load_audio,
    render_config_audio,
    render_fm_audio,
    write_wav,
)
from phase3.config import Config, ModConfig
from phase3.families import family_from_target
from phase3.family_distance import family_distance


F_C = 220.0
SR = 48_000


# ---------------------------------------------------------------------------
# render
# ---------------------------------------------------------------------------

def test_render_length_and_bounded():
    x = render_fm_audio([2.0], [1.5], f_carrier=F_C, duration_s=1.0, sample_rate=SR)
    assert x.shape == (SR,)
    assert np.all(np.abs(x) <= 1.0 + 1e-9)


def test_render_zero_depth_is_pure_carrier():
    x = render_fm_audio([2.0], [0.0], f_carrier=F_C, duration_s=1.0,
                        sample_rate=SR, fade_s=0.0)
    amps = audio_to_spectrum(x, q_max=8, f_carrier=F_C, sample_rate=SR)
    # All energy at the fundamental (partial 1); no sidebands.
    assert np.argmax(amps) == 0
    assert amps[0] > 50.0 * (amps[1:].max() + 1e-12)


# ---------------------------------------------------------------------------
# audio_to_spectrum — pure tones land in the right integer band
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("n", [1, 2, 3, 5, 8])
def test_pure_tone_lands_in_partial_band(n):
    t = np.arange(SR, dtype=np.float64) / SR
    x = np.sin(2.0 * np.pi * (n * F_C) * t)
    amps = audio_to_spectrum(x, q_max=12, f_carrier=F_C, sample_rate=SR)
    assert np.argmax(amps) == n - 1


def test_detuned_tone_routes_to_nearest_integer():
    # 2.4·f_c rounds to partial 2; 2.6·f_c rounds to partial 3.
    t = np.arange(SR, dtype=np.float64) / SR
    lo = np.sin(2.0 * np.pi * (2.4 * F_C) * t)
    hi = np.sin(2.0 * np.pi * (2.6 * F_C) * t)
    assert np.argmax(audio_to_spectrum(lo, 8, F_C, sample_rate=SR)) == 1
    assert np.argmax(audio_to_spectrum(hi, 8, F_C, sample_rate=SR)) == 2


def test_shape_matches_q_max():
    x = render_fm_audio([2.0], [2.0], f_carrier=F_C, duration_s=0.5, sample_rate=SR)
    for q in (8, 16, 64, 128):
        assert audio_to_spectrum(x, q_max=q, f_carrier=F_C, sample_rate=SR).shape == (q,)


def test_empty_audio_returns_zeros():
    amps = audio_to_spectrum(np.array([]), q_max=16, f_carrier=F_C, sample_rate=SR)
    assert amps.shape == (16,)
    assert np.all(amps == 0.0)


# ---------------------------------------------------------------------------
# WAV I/O round-trip
# ---------------------------------------------------------------------------

def test_wav_write_read_roundtrip(tmp_path):
    x = render_fm_audio([2.0], [1.841], f_carrier=F_C, duration_s=0.5, sample_rate=SR)
    p = tmp_path / "t.wav"
    write_wav(p, x, sample_rate=SR)
    y, sr = load_audio(p)
    assert sr == SR
    # 16-bit PCM round-trip: spectra agree closely even if samples quantise.
    sx = audio_to_spectrum(x, 16, F_C, sample_rate=SR)
    sy = audio_to_spectrum(y, 16, F_C, sample_rate=SR)
    sx /= sx.sum(); sy /= sy.sum()
    assert np.abs(sx - sy).sum() < 0.02


def test_load_audio_array_requires_sample_rate():
    with pytest.raises(ValueError):
        load_audio(np.zeros(10))


def test_load_audio_mixes_to_mono():
    stereo = np.stack([np.ones(100), -np.ones(100)], axis=1)
    x, sr = load_audio(stereo, sample_rate=SR)
    assert x.ndim == 1
    assert np.allclose(x, 0.0)


# ---------------------------------------------------------------------------
# family_from_target (P0.2)
# ---------------------------------------------------------------------------

def test_family_from_target_kl_only():
    fam = family_from_target(np.array([1.0, 0.5, 0.25]), name="ext")
    assert fam.characteristic_q is None
    assert fam.name == "ext"


def test_family_from_target_truncate_and_pad():
    base = np.array([1.0, 0.5, 0.25, 0.1])
    fam = family_from_target(base)
    np.testing.assert_allclose(fam.target_amplitudes(2), [1.0, 0.5])  # truncate
    padded = fam.target_amplitudes(6)                                  # pad
    assert padded.shape == (6,)
    np.testing.assert_allclose(padded[:4], base)
    assert np.all(padded[4:] == 0.0)


def test_family_from_target_caches_by_q_max():
    fam = family_from_target(np.array([1.0, 0.5]))
    a = fam.target_amplitudes(16)
    b = fam.target_amplitudes(16)
    assert a is b  # same cached object


def test_family_from_target_integrates_with_family_distance():
    # An external family is a valid argument to family_distance (KL-only).
    cfg = Config(mods=[ModConfig(path="R", depth=1.841, enabled=True),
                       ModConfig(path="RL", depth=2.405, enabled=True)]
                      + [ModConfig() for _ in range(6)], f_carrier=F_C)
    x = render_config_audio(cfg, duration_s=1.0)
    target = audio_to_spectrum(x, q_max=16, f_carrier=F_C, sample_rate=SR)
    fam = family_from_target(target, name="recorded")
    d = family_distance(cfg.predict(), fam, F_C, q_max=16)
    assert np.isfinite(d) and d >= 0.0
