"""Tests for the STFT drift-trajectory extractor (D1).

Synthetic signals with known motion:
  * stationary FM tone        → low flux, ~0 pitch drift, flat centroid
  * vibrato (pitch LFO)        → f0_drift_cents detects it, mod rate ≈ LFO
  * brightness sweep (I ramp)  → rising centroid trend, high flux
  * brightness LFO (I wobble)  → centroid mod rate ≈ LFO rate
"""

from __future__ import annotations

import numpy as np
import pytest

from phase3.audio_spectrum import render_fm_audio
from phase3.stft_trajectory import audio_to_drift_profile

SR = 48_000
F_C = 220.0


def _t(dur):
    return np.arange(int(dur * SR), dtype=np.float64) / SR


def _fm_const(I, dur=2.0, ratio=2.0):
    return render_fm_audio([ratio], [I], f_carrier=F_C, duration_s=dur,
                           sample_rate=SR, fade_s=0.0)


def _vibrato(delta, f_lfo, dur=2.0):
    """Pure carrier with sinusoidal pitch modulation (instantaneous-freq
    integration). delta = fractional pitch deviation."""
    t = _t(dur)
    f_inst = F_C * (1.0 + delta * np.sin(2 * np.pi * f_lfo * t))
    phase = 2 * np.pi * np.cumsum(f_inst) / SR
    return np.sin(phase)


def _fm_depth(I_of_t, ratio=2.0):
    """FM with a time-varying modulation index I(t)."""
    t = np.arange(I_of_t.size, dtype=np.float64) / SR
    return np.sin(2 * np.pi * F_C * t + I_of_t * np.sin(2 * np.pi * ratio * F_C * t))


# ---------------------------------------------------------------------------
# Basic shape / stationary behaviour
# ---------------------------------------------------------------------------

def test_profile_shapes():
    p = audio_to_drift_profile(_fm_const(2.0), q_max=32, f_carrier=F_C,
                               sample_rate=SR)
    T = p.frames.shape[0]
    assert p.frames.shape == (T, 32)
    assert p.times.shape == (T,)
    assert p.centroid.shape == (T,) and p.flux.shape == (T,)
    assert p.loudness.shape == (T,) and p.f0_hz.shape == (T,)
    assert T > 10                                  # 2 s gives many frames


def test_stationary_is_low_motion():
    p = audio_to_drift_profile(_fm_const(2.0), q_max=32, f_carrier=F_C,
                               sample_rate=SR)
    assert p.stationarity < 0.05                   # frame-to-frame ~unchanged
    assert p.f0_drift_cents < 3.0                  # no pitch drift
    assert abs(p.centroid_trend) < 0.5             # centroid flat


def test_mean_spectrum_matches_static_extractor():
    # The time-averaged fingerprint should peak at the same partial as the
    # Welch-averaged audio_to_spectrum (the Mode B target).
    from phase3.audio_spectrum import audio_to_spectrum
    audio = _fm_const(2.0)
    p = audio_to_drift_profile(audio, q_max=32, f_carrier=F_C, sample_rate=SR)
    static = audio_to_spectrum(audio, q_max=32, f_carrier=F_C, sample_rate=SR)
    assert np.argmax(p.mean_spectrum) == np.argmax(static)


# ---------------------------------------------------------------------------
# Pitch drift (vibrato)
# ---------------------------------------------------------------------------

def test_vibrato_detected_as_pitch_drift():
    stat = audio_to_drift_profile(_vibrato(0.0, 5.0), f_carrier=F_C, sample_rate=SR)
    vib = audio_to_drift_profile(_vibrato(0.01, 5.0), f_carrier=F_C, sample_rate=SR)
    # ~1% pitch deviation ≈ 17 cents amplitude; drift std clearly above static.
    assert vib.f0_drift_cents > 4.0
    assert vib.f0_drift_cents > stat.f0_drift_cents + 3.0


def test_vibrato_rate_recovered_from_f0():
    # The f0 track itself should wobble at ~5 Hz; check via its own spectrum.
    from phase3.stft_trajectory import _dominant_modulation
    vib = audio_to_drift_profile(_vibrato(0.01, 5.0), f_carrier=F_C, sample_rate=SR)
    rate, _ = _dominant_modulation(vib.f0_hz, vib.hop_s)
    assert abs(rate - 5.0) < 1.5


# ---------------------------------------------------------------------------
# Brightness motion (modulation-index sweep / wobble)
# ---------------------------------------------------------------------------

def test_brightness_sweep_rises():
    t = _t(2.0)
    I = np.linspace(0.2, 6.0, t.size)             # ramp index up
    p = audio_to_drift_profile(_fm_depth(I), q_max=48, f_carrier=F_C, sample_rate=SR)
    assert p.centroid_trend > 1.0                 # brightness climbs
    assert p.stationarity > 0.02                  # spectrum is moving


def test_brightness_lfo_rate_detected():
    t = _t(3.0)
    f_lfo = 3.0
    I = 3.0 + 1.5 * np.sin(2 * np.pi * f_lfo * t)  # index wobbles at 3 Hz
    p = audio_to_drift_profile(_fm_depth(I), q_max=48, f_carrier=F_C, sample_rate=SR)
    assert abs(p.mod_rate_hz - f_lfo) < 1.0
    assert p.mod_depth > 0.05


def test_empty_audio_safe():
    p = audio_to_drift_profile(np.array([]), q_max=16, f_carrier=F_C, sample_rate=SR)
    assert p.frames.shape[1] == 16
    assert p.stationarity == 0.0
