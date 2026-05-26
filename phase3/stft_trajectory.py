"""Time-resolved spectral trajectory — the drift/motion companion to
audio_spectrum.audio_to_spectrum (D1).

`audio_to_spectrum` collapses a recording to one representative spectrum
(Welch-averaged). That is the right Mode B *target*, but it throws away the
motion — and the sounds we want to capture are defined by their motion:

  * ARP 2600 — analog oscillator drift, slow detune wander, noise.
  * Yamaha SY99 AFM — feedback amount swept by an envelope (the classic FM
    "brightness" sweep), so the spectrum evolves over the note.

This module runs a short-time FFT, projects every frame onto the same
integer-partial grid Mode B uses, and characterises the trajectory:

  * mean_spectrum    — the time-averaged fingerprint == the Mode B target.
  * frames           — (n_frames, q_max) per-frame amplitudes (the partial
                       envelopes; column n is partial n's amplitude over time).
  * centroid(t)      — spectral centroid in partial units (brightness motion).
  * flux(t)          — frame-to-frame change (how "alive"/non-stationary).
  * loudness(t)      — per-frame energy (the amplitude envelope).
  * f0(t)            — tracked fundamental frequency ⇒ pitch-drift in cents.
  * modulation       — dominant periodic rate+depth of the centroid (LFO
                       detect), the bridge to a modulator's rate/depth.

The static core feeds Mode B; the motion features are meant to drive the
v2 modulator bank (recreate drift/sweep without modelling it).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Union

import numpy as np

from phase3.audio_spectrum import load_audio


# ---------------------------------------------------------------------------
# Result container
# ---------------------------------------------------------------------------

@dataclass
class DriftProfile:
    f_carrier: float
    q_max: int
    sample_rate: int
    hop_s: float                  # seconds between frames
    times: np.ndarray             # (T,) frame-centre times
    frames: np.ndarray            # (T, q_max) per-frame integer-partial amps
    mean_spectrum: np.ndarray     # (q_max,) == Mode B target
    centroid: np.ndarray          # (T,) spectral centroid, partial units
    flux: np.ndarray              # (T,) L1 change vs previous frame (flux[0]=0)
    loudness: np.ndarray          # (T,) per-frame energy
    f0_hz: np.ndarray             # (T,) tracked fundamental (Hz)
    # --- scalar summaries ---
    stationarity: float           # mean flux — 0 ⇒ static, higher ⇒ drifting
    centroid_mean: float
    centroid_trend: float         # net centroid change (last−first), partials
    f0_drift_cents: float         # std of f0 over time, in cents
    mod_rate_hz: float            # dominant modulation rate of the centroid
    mod_depth: float              # amplitude of that modulation (partials)

    def partial_envelope(self, n: int) -> np.ndarray:
        """Amplitude over time of integer partial n (1-indexed)."""
        return self.frames[:, n - 1]


# ---------------------------------------------------------------------------
# STFT (manual — no scipy.signal.stft deprecation risk, full control)
# ---------------------------------------------------------------------------

def _stft_mag(x: np.ndarray, sr: int, nperseg: int, hop: int):
    """Return (freqs, times, magnitude[F, T]) for a Hann-windowed STFT."""
    if x.size < nperseg:                       # pad short signals to one frame
        x = np.pad(x, (0, nperseg - x.size))
    win = np.hanning(nperseg)
    n_frames = 1 + (x.size - nperseg) // hop
    F = nperseg // 2 + 1
    mag = np.empty((F, n_frames), dtype=np.float64)
    for i in range(n_frames):
        seg = x[i * hop: i * hop + nperseg] * win
        mag[:, i] = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(nperseg, 1.0 / sr)
    times = (np.arange(n_frames) * hop + nperseg / 2.0) / sr
    return freqs, times, mag


def _interp_matrix(freqs: np.ndarray, targets: np.ndarray) -> np.ndarray:
    """Linear-interpolation matrix W (len(targets) × len(freqs)) so that
    W @ mag samples the magnitude spectrum at each target frequency — the
    per-frame analogue of audio_spectrum's integer-grid projection."""
    W = np.zeros((targets.size, freqs.size), dtype=np.float64)
    df = freqs[1] - freqs[0]
    for i, ft in enumerate(targets):
        if ft < freqs[0] or ft > freqs[-1]:
            continue
        b = (ft - freqs[0]) / df               # fractional bin
        lo = int(np.floor(b))
        frac = b - lo
        W[i, lo] = 1.0 - frac
        if lo + 1 < freqs.size:
            W[i, lo + 1] = frac
    return W


def _parabolic_peak(mag_col: np.ndarray, lo_bin: int, hi_bin: int):
    """Index (fractional) of the magnitude peak within [lo_bin, hi_bin],
    refined by 3-point parabolic interpolation. Returns None if flat/empty."""
    if hi_bin <= lo_bin:
        return None
    seg = mag_col[lo_bin:hi_bin + 1]
    if seg.size == 0 or seg.max() <= 0:
        return None
    k = int(np.argmax(seg)) + lo_bin
    if 0 < k < mag_col.size - 1:
        a, b, c = mag_col[k - 1], mag_col[k], mag_col[k + 1]
        denom = (a - 2 * b + c)
        delta = 0.5 * (a - c) / denom if denom != 0 else 0.0
        return k + float(np.clip(delta, -0.5, 0.5))
    return float(k)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------

def audio_to_drift_profile(
    source: Union[str, Path, np.ndarray],
    q_max: int = 64,
    f_carrier: float = 220.0,
    *,
    sample_rate: int | None = None,
    freq_resolution_ratio: float = 8.0,   # window gives res ≈ f_c / this
    overlap: float = 0.75,
) -> DriftProfile:
    """Characterise a recording's spectral *trajectory* on the integer grid.

    The window length targets a frequency resolution of f_carrier /
    `freq_resolution_ratio` so adjacent partials are resolved, while the
    `overlap` keeps enough frames to follow slow drift/sweeps.
    """
    x, sr = load_audio(source, sample_rate)
    if x.size == 0:
        z = np.zeros(q_max)
        return DriftProfile(
            f_carrier, q_max, sr, 0.0, np.zeros(1), np.zeros((1, q_max)), z,
            np.zeros(1), np.zeros(1), np.zeros(1), np.zeros(1),
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0)

    # Window / hop.
    target = freq_resolution_ratio * sr / max(f_carrier, 1e-6)
    nperseg = 1 << int(np.ceil(np.log2(max(target, 256.0))))
    nperseg = int(min(nperseg, x.size))
    hop = max(1, int(round(nperseg * (1.0 - overlap))))

    freqs, times, mag = _stft_mag(x, sr, nperseg, hop)

    # Per-frame integer-partial fingerprints (T, q_max).
    partial_freqs = np.arange(1, q_max + 1, dtype=np.float64) * f_carrier
    W = _interp_matrix(freqs, partial_freqs)
    frames = (W @ mag).T                        # (T, q_max)
    T = frames.shape[0]

    mean_spectrum = frames.mean(axis=0)

    # Per-frame normalised distribution for centroid/flux.
    row_sums = frames.sum(axis=1, keepdims=True)
    norm = np.divide(frames, row_sums, out=np.zeros_like(frames),
                     where=row_sums > 0)
    positions = np.arange(1, q_max + 1, dtype=np.float64)
    centroid = norm @ positions                 # (T,) in partial units
    loudness = frames.sum(axis=1)
    flux = np.zeros(T)
    if T > 1:
        flux[1:] = np.abs(np.diff(norm, axis=0)).sum(axis=1)

    # Fundamental tracking near f_carrier ⇒ pitch drift.
    df = freqs[1] - freqs[0]
    lo_bin = int(np.floor((f_carrier * 0.5) / df))
    hi_bin = int(np.ceil((f_carrier * 1.5) / df))
    f0 = np.full(T, f_carrier)
    for i in range(T):
        pk = _parabolic_peak(mag[:, i], lo_bin, hi_bin)
        if pk is not None:
            f0[i] = pk * df
    # Drift in cents (std relative to the median, voiced frames only).
    voiced = loudness > (0.05 * loudness.max() if loudness.max() > 0 else 0)
    if voiced.sum() > 1:
        f0v = f0[voiced]
        ref = np.median(f0v)
        cents = 1200.0 * np.log2(np.clip(f0v / ref, 1e-9, None))
        f0_drift_cents = float(np.std(cents))
    else:
        f0_drift_cents = 0.0

    # Centroid modulation: dominant periodic component (LFO detect).
    mod_rate, mod_depth = _dominant_modulation(centroid, hop / sr)
    centroid_trend = float(centroid[-1] - centroid[0]) if T > 1 else 0.0

    return DriftProfile(
        f_carrier=f_carrier, q_max=q_max, sample_rate=sr, hop_s=hop / sr,
        times=times, frames=frames, mean_spectrum=mean_spectrum,
        centroid=centroid, flux=flux, loudness=loudness, f0_hz=f0,
        stationarity=float(flux[1:].mean()) if T > 1 else 0.0,
        centroid_mean=float(np.nanmean(centroid)) if T else 0.0,
        centroid_trend=centroid_trend,
        f0_drift_cents=f0_drift_cents,
        mod_rate_hz=mod_rate, mod_depth=mod_depth,
    )


def _dominant_modulation(signal: np.ndarray, dt: float) -> tuple[float, float]:
    """Dominant non-DC periodic component of a per-frame signal: returns
    (rate_hz, amplitude). Used to detect an LFO-like wobble in the centroid
    so we can set a modulator's rate/depth. Returns (0, 0) if too short."""
    n = signal.size
    if n < 8 or dt <= 0:
        return 0.0, 0.0
    s = signal - signal.mean()
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(s * win))
    rfreqs = np.fft.rfftfreq(n, dt)
    if spec.size < 2:
        return 0.0, 0.0
    k = int(np.argmax(spec[1:])) + 1            # skip DC
    # Coherent-gain–corrected amplitude estimate for a Hann-windowed tone.
    amp = 2.0 * spec[k] / (n * 0.5)
    return float(rfreqs[k]), float(amp)
