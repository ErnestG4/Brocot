"""Pulse-PW parity tests for the GPU renderer (gpu_fm._wave / render_batch).

CPU-only: every tensor is created on "cpu" explicitly, so this exercises the
exact kernels the bank builds use without needing (or touching) the GPU.
Skipped cleanly when torch isn't installed — the plain test venv doesn't ship
it (the repo's gpu_* modules are otherwise not unit-tested); run under
phase3/.venv-gpu to execute for real:

    phase3/.venv-gpu/bin/python -m pytest phase3/tests/test_gpu_pulse.py
"""

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from phase3.gpu_fm import _wave, render_batch  # noqa: E402  (after importorskip)

CPU = "cpu"


def _turns(n_cycles=16, n_samples=4096):
    """Phase ramp covering an exact integer number of cycles (leak-free FFT)."""
    return torch.arange(n_samples, device=CPU, dtype=torch.float64).float() \
        * (n_cycles / n_samples)


def test_wave_pulse_duty_half_is_exactly_square():
    turns = _turns().unsqueeze(0)
    sq = _wave(turns, torch.tensor([[3]], device=CPU))
    pl = _wave(turns, torch.tensor([[4]], device=CPU),
               torch.tensor([[0.5]], device=CPU))
    assert torch.equal(pl, sq)


def test_wave_pulse_default_duty_is_quarter():
    turns = _turns().unsqueeze(0)
    wc = torch.tensor([[4]], device=CPU)
    explicit = _wave(turns, wc, torch.tensor([[0.25]], device=CPU))
    default = _wave(turns, wc)                       # duty=None ⇒ 0.25
    assert torch.equal(default, explicit)


def test_wave_sine_paths_untouched_by_duty_argument():
    turns = _turns().unsqueeze(0)
    ref = torch.sin(2.0 * np.pi * turns)
    assert torch.allclose(_wave(turns, None), ref)
    assert torch.allclose(_wave(turns, None, 0.4), ref)
    assert torch.allclose(
        _wave(turns, torch.tensor([[0]], device=CPU), 0.4), ref)


def test_pulse_duty_quarter_nulls_every_4th_harmonic():
    # Harmonic n of a duty-d pulse scales as |sin(pi*n*d)|: d=0.25 nulls
    # n=4,8,... while n=3/n=5 sit at sin(0.75pi)=sin(1.25pi)=0.707 — the same
    # comb OperatorTuningTests pins on the C++ engine.
    n_cycles, n_samples = 16, 4096
    x = _wave(_turns(n_cycles, n_samples).unsqueeze(0),
              torch.tensor([[4]], device=CPU),
              torch.tensor([[0.25]], device=CPU))[0]
    mag = np.abs(np.fft.rfft(x.numpy())) / n_samples
    h = lambda n: mag[n * n_cycles]                  # bin of harmonic n
    predicted = lambda n: (2.0 / (np.pi * n)) * abs(np.sin(np.pi * n * 0.25))
    assert h(3) == pytest.approx(predicted(3), rel=0.05)
    assert h(5) == pytest.approx(predicted(5), rel=0.05)
    assert h(4) < 0.01 * h(3)                        # nulled
    assert h(8) < 0.01 * h(3)


def _render_args(b=3, k=2, t_len=2048, seed=7):
    g = torch.Generator().manual_seed(seed)
    log_ratio = torch.rand(b, k, generator=g, device=CPU) * 1.5
    depth = torch.rand(b, k, generator=g, device=CPU) * 2.0
    detune = torch.zeros(b, k, device=CPU)
    t = torch.arange(t_len, device=CPU, dtype=torch.float32) / 48_000.0
    return log_ratio, depth, detune, t


def test_render_batch_default_duties_matches_explicit_quarter():
    log_ratio, depth, detune, t = _render_args()
    waves = torch.full((3, 2), 4, device=CPU, dtype=torch.long)
    implicit = render_batch(log_ratio, depth, detune, t, waves=waves)
    explicit = render_batch(log_ratio, depth, detune, t, waves=waves,
                            duties=torch.full((3, 2), 0.25, device=CPU))
    assert torch.equal(implicit, explicit)


def test_render_batch_waves_none_path_identical_with_and_without_duties():
    # The duties kwarg must be inert for every pre-existing caller: waves=None
    # renders pure parallel sine FM regardless of duties.
    log_ratio, depth, detune, t = _render_args()
    base = render_batch(log_ratio, depth, detune, t)
    with_duties = render_batch(log_ratio, depth, detune, t,
                               duties=torch.full((3, 2), 0.4, device=CPU))
    assert torch.equal(base, with_duties)
    # And the closed-form check that the sine path itself is untouched.
    # (Loose tolerance: float32 multiply-order differs between this closed
    # form and render_batch's fmod*t-then-2pi grouping, and sin() amplifies
    # the ulp-level phase difference over tens of radians.)
    ratio = torch.exp(log_ratio)
    phase = 2.0 * np.pi * 220.0 * t.unsqueeze(0).expand(3, -1).clone()
    for kk in range(2):
        phase = phase + depth[:, kk].unsqueeze(1) * torch.sin(
            2.0 * np.pi * (220.0 * ratio[:, kk]).unsqueeze(1) * t)
    assert torch.allclose(base, torch.sin(phase), atol=5e-3)


def test_render_batch_duty_actually_changes_pulse_renders():
    log_ratio, depth, detune, t = _render_args()
    waves = torch.full((3, 2), 4, device=CPU, dtype=torch.long)
    a = render_batch(log_ratio, depth, detune, t, waves=waves,
                     duties=torch.full((3, 2), 0.10, device=CPU))
    b = render_batch(log_ratio, depth, detune, t, waves=waves,
                     duties=torch.full((3, 2), 0.45, device=CPU))
    assert not torch.equal(a, b)
