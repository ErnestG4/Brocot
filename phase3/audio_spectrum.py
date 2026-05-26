"""Audio → integer-partial spectrum pipeline for Mode B (P0.1).

Mode B searches the Stern-Brocot parameter tree for configurations whose
predicted spectrum minimises family-distance to an *external* target —
e.g. a recording of an ARP 2600 patch. This module is the front end: it
turns an audio file (or sample array) into an amplitude vector on the
integer-multiple-of-`f_carrier` grid, shaped exactly like
`family.target_amplitudes(q_max)` so it can be wrapped by
`families.family_from_target` and fed to the existing search unchanged.

Two halves:

  render_config_audio(config, ...) -> samples
      Sine-basis parallel-FM time-domain render. This is the *inverse* of
      partial_prediction: the analytic partial set P(C) is the Bessel
      expansion of exactly this signal,

          x(t) = sin(2π f_c t  +  Σ_i I_i · sin(2π r_i f_c t)),

      so a partial at f_c·(1 + Σ n_i r_i) with amplitude Π J_{n_i}(I_i)
      falls straight out of the FFT. Used to generate synthetic targets
      for the P0.3 round-trip validation (and, eventually, to sanity-check
      a real recording against a candidate config).

  audio_to_spectrum(source, q_max, f_carrier) -> amplitudes (q_max,)
      Hann-windowed Welch PSD, then integer-band power integration: bin n
      collects all spectral power in [(n−0.5)·f_c, (n+0.5)·f_c). That
      nearest-integer banding mirrors family_distance.quantize_to_integer_grid
      so an external target lands in the same coordinate system the
      analytic families live in.
"""

from __future__ import annotations

from pathlib import Path
from typing import Sequence, Union

import numpy as np
from scipy import signal as _sig
from scipy.io import wavfile as _wavfile

from phase3.config import Config


# ---------------------------------------------------------------------------
# Time-domain render (validation-side synthesis)
# ---------------------------------------------------------------------------

DEFAULT_SAMPLE_RATE = 48_000


def render_fm_audio(
    ratios: Sequence[float],
    depths: Sequence[float],
    *,
    f_carrier: float = 220.0,
    duration_s: float = 2.0,
    sample_rate: int = DEFAULT_SAMPLE_RATE,
    fade_s: float = 0.01,
) -> np.ndarray:
    """Sine-basis parallel-FM render of one configuration.

        x(t) = sin(2π f_c t + Σ_i I_i · sin(2π r_i f_c t))

    All modulators share the carrier phase accumulator (parallel FM into
    the carrier's frequency), matching partial_prediction's P(C) model.
    A short raised-cosine fade in/out avoids click transients leaking
    broadband energy into the PSD.
    """
    if len(ratios) != len(depths):
        raise ValueError("ratios and depths must have the same length")
    n = int(round(duration_s * sample_rate))
    t = np.arange(n, dtype=np.float64) / sample_rate

    phase = 2.0 * np.pi * f_carrier * t
    for r, I in zip(ratios, depths):
        if I == 0.0:
            continue
        phase = phase + I * np.sin(2.0 * np.pi * (r * f_carrier) * t)
    x = np.sin(phase)

    # Raised-cosine edge fade.
    nf = int(round(fade_s * sample_rate))
    if nf > 0 and 2 * nf < n:
        ramp = 0.5 * (1.0 - np.cos(np.pi * np.arange(nf) / nf))
        x[:nf] *= ramp
        x[-nf:] *= ramp[::-1]
    return x


def render_config_audio(
    config: Config,
    *,
    duration_s: float = 2.0,
    sample_rate: int = DEFAULT_SAMPLE_RATE,
    fade_s: float = 0.01,
) -> np.ndarray:
    """render_fm_audio for a Config (uses its enabled ratios/depths)."""
    rs, ds = config.ratios_and_depths()
    return render_fm_audio(
        rs, ds,
        f_carrier=config.f_carrier,
        duration_s=duration_s,
        sample_rate=sample_rate,
        fade_s=fade_s,
    )


def write_wav(path: Union[str, Path], samples: np.ndarray,
              sample_rate: int = DEFAULT_SAMPLE_RATE) -> None:
    """Write a mono float waveform to a 16-bit PCM WAV (peak-normalised)."""
    x = np.asarray(samples, dtype=np.float64)
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    if peak > 0:
        x = x / peak * 0.98
    pcm = np.clip(x * 32767.0, -32768, 32767).astype(np.int16)
    _wavfile.write(str(path), int(sample_rate), pcm)


# ---------------------------------------------------------------------------
# Audio loading
# ---------------------------------------------------------------------------

# Per-dtype full-scale for integer PCM → float [-1, 1].
_PCM_SCALE = {
    np.dtype(np.int16): 32768.0,
    np.dtype(np.int32): 2147483648.0,
    np.dtype(np.uint8): 128.0,  # uint8 WAV is offset-encoded (centre 128)
}


def load_audio(source: Union[str, Path, np.ndarray],
               sample_rate: int | None = None) -> tuple[np.ndarray, int]:
    """Load audio as (mono float64 in ~[-1,1], sample_rate).

    `source` is a WAV path or an already-loaded sample array. For an
    array, `sample_rate` must be supplied. Multi-channel audio is
    averaged to mono.
    """
    if isinstance(source, np.ndarray):
        if sample_rate is None:
            raise ValueError("sample_rate required when source is an array")
        x = source.astype(np.float64)
        sr = int(sample_rate)
    else:
        sr, raw = _wavfile.read(str(source))
        x = raw.astype(np.float64)
        dt = np.dtype(raw.dtype)
        if dt in _PCM_SCALE:
            if dt == np.dtype(np.uint8):
                x = x - 128.0
            x = x / _PCM_SCALE[dt]
    if x.ndim > 1:                      # mix channels to mono
        x = x.mean(axis=1)
    return x, sr


# ---------------------------------------------------------------------------
# Spectrum extraction → integer partial grid
# ---------------------------------------------------------------------------

def audio_to_spectrum(
    source: Union[str, Path, np.ndarray],
    q_max: int,
    f_carrier: float = 220.0,
    *,
    sample_rate: int | None = None,
    nperseg: int | None = None,
    floor_db: float = -60.0,
    interpolate: bool = True,
) -> np.ndarray:
    """Extract integer-partial amplitudes from audio against `f_carrier`.

    Returns an amplitude vector of shape (q_max,) — same contract as
    `Family.target_amplitudes(q_max)`, index i ↔ partial position i+1.
    Not normalised (family_distance normalises inside KL); absolute scale
    is irrelevant to the search.

    Method — *peak partial extraction*, structurally matched to the
    analytic side's family_distance.quantize_to_integer_grid so a config's
    audio spectrum lands in the same representation as its predicted one
    (this is what keeps the Mode B search honest — see P0.3):

    1. Hann-windowed Welch PSD (segment-averaged for stationarity).
    2. Detect spectral peaks (the individual partials) above `floor_db`
       relative to the strongest peak.
    3. Bin each peak to its nearest integer multiple n = round(f / f_c)
       and *sum peak magnitudes* into bin n — exactly the round-and-sum-
       |amplitude| semantics of quantize_to_integer_grid. (The earlier
       band-power-integration variant summed power across whole bands,
       which over-counted multi-partial bands and inflated the analytic-
       vs-audio gap; peak-sum closes it.)

    `nperseg` defaults to a window giving frequency resolution ≈ f_c/16
    (so adjacent partials are cleanly separated), clamped to signal length.
    """
    x, sr = load_audio(source, sample_rate)
    if x.size == 0:
        return np.zeros(q_max, dtype=np.float64)

    if nperseg is None:
        # Resolution f_c/16 ⇒ nperseg ≈ 16·sr/f_c, rounded up to a power
        # of two for FFT efficiency; never exceed the signal length.
        target = 16.0 * sr / max(f_carrier, 1e-6)
        nperseg = 1 << int(np.ceil(np.log2(max(target, 256.0))))
    nperseg = int(min(nperseg, x.size))
    noverlap = nperseg // 2

    freqs, pxx = _sig.welch(
        x, fs=sr, window="hann",
        nperseg=nperseg, noverlap=noverlap,
        scaling="spectrum",         # power per bin; sqrt → per-partial amplitude
        detrend=False,
    )
    mag = np.sqrt(np.maximum(pxx, 0.0))

    out = np.zeros(q_max, dtype=np.float64)
    if mag.size == 0 or mag.max() <= 0.0:
        return out

    # Peak detection above an absolute floor relative to the loudest peak.
    height = mag.max() * (10.0 ** (floor_db / 20.0))
    peaks, _ = _sig.find_peaks(mag, height=height)
    if peaks.size == 0:
        peaks = np.array([int(np.argmax(mag))])

    # Project each detected peak onto the integer grid, matching the
    # analytic side's quantize_to_integer_grid exactly.
    x = freqs[peaks] / f_carrier          # continuous partial position
    m = mag[peaks]
    if not interpolate:
        n = np.round(x).astype(int)
        keep = (n >= 1) & (n <= q_max)
        np.add.at(out, n[keep] - 1, m[keep])
        return out

    # Linear split across the two bracketing integer positions (smoothing).
    lo = np.floor(x).astype(int)
    frac = x - lo
    for pos, w in ((lo, 1.0 - frac), (lo + 1, frac)):
        valid = (pos >= 1) & (pos <= q_max)
        np.add.at(out, pos[valid] - 1, (m * w)[valid])
    return out
