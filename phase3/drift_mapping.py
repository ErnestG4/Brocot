"""Drift → modulation mapping (D2).

Turn a stft_trajectory.DriftProfile (the measured motion of a recording)
into a concrete, schema-agnostic ModulationPlan, then adapt it to what the
synth can actually do.

A real ARP / SY99 sound has *several layers* of motion at once — a slow
oscillator-detune wander, a faster vibrato, a brightness sweep, a tremolo.
So we don't take just the dominant component (the old single-LFO view); we
*decompose* each trajectory into its top-K periodic layers and tag them.

The central bridge is detune: two tones detuned by c cents beat at
f0·(2^(c/1200) − 1) Hz, so a slow pitch-drift rate maps directly to an
oscillator detune. Across N oscillators this recreates analog beating
without any LFO. Faster periodic motion maps to an LFO; the amplitude
envelope maps to the ADSR.

    profile → drift_profile_to_plan(...) → ModulationPlan
                                         → plan_to_v1_brocot(plan)  # concrete

The plan is honest about what the v1 schema (one LFO→filter, one ADSR,
detune on operators) can and can't carry; layers that overflow are listed
for the v2 modulator bank.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from phase3.stft_trajectory import DriftProfile


# ---------------------------------------------------------------------------
# Detune ↔ beat-rate bridge
# ---------------------------------------------------------------------------

def detune_cents_for_beat(rate_hz: float, f_carrier: float) -> float:
    """Detune (cents) whose beat against f_carrier has frequency rate_hz.

    Two tones a apart beat at |Δf|; a tone detuned by c cents sits at
    f0·2^(c/1200), so beat = f0·(2^(c/1200) − 1)  ⇒  c = 1200·log2(1+beat/f0).
    """
    if f_carrier <= 0 or rate_hz <= 0:
        return 0.0
    return float(1200.0 * np.log2(1.0 + rate_hz / f_carrier))


def beat_rate_for_detune(cents: float, f_carrier: float) -> float:
    return f_carrier * (2.0 ** (cents / 1200.0) - 1.0)


# ---------------------------------------------------------------------------
# Multi-component decomposition
# ---------------------------------------------------------------------------

def decompose_modulation(signal: np.ndarray, dt: float, *, k: int = 3,
                         min_rel_amp: float = 0.15
                         ) -> list[tuple[float, float]]:
    """Top-k periodic components of a per-frame signal as (rate_hz, amp),
    strongest first. Detrends (removes linear sweep + mean) first, picks
    local-maximum spectral peaks above min_rel_amp · strongest, and
    corrects amplitude for the Hann window's coherent gain."""
    n = signal.size
    if n < 8 or dt <= 0:
        return []
    # Remove linear trend so a one-shot sweep doesn't masquerade as a slow LFO.
    t = np.arange(n)
    s = signal - np.polyval(np.polyfit(t, signal, 1), t)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(s * win))
    freqs = np.fft.rfftfreq(n, dt)
    if spec.size < 3:
        return []
    spec[0] = 0.0                                   # kill DC residue
    # Local maxima.
    peaks = []
    for i in range(1, spec.size - 1):
        if spec[i] > spec[i - 1] and spec[i] >= spec[i + 1]:
            peaks.append(i)
    if not peaks:
        peaks = [int(np.argmax(spec))]
    peaks.sort(key=lambda i: spec[i], reverse=True)
    strongest = spec[peaks[0]]
    if strongest <= 0:
        return []
    out = []
    for i in peaks[:k]:
        if spec[i] < min_rel_amp * strongest:
            break
        amp = 2.0 * spec[i] / (n * 0.5)             # coherent-gain corrected
        out.append((float(freqs[i]), float(amp)))
    return out


# ---------------------------------------------------------------------------
# ADSR fit from the loudness envelope
# ---------------------------------------------------------------------------

def _fit_adsr(loudness: np.ndarray, times: np.ndarray) -> dict:
    """Heuristic ADSR from a per-frame loudness envelope. Sustained samples
    (no note-off captured) get a small default release."""
    if loudness.size < 2 or loudness.max() <= 0:
        return {"attack": 0.01, "decay": 0.1, "sustain": 0.8, "release": 0.3}
    L = loudness / loudness.max()
    pk = int(np.argmax(L))
    attack = float(times[pk] - times[0])
    tail = L[pk:]
    sustain = float(np.median(tail)) if tail.size else float(L[-1])
    # Decay: time from peak until within 10% of the sustain level.
    decay = 0.0
    thr = sustain + 0.1 * (1.0 - sustain)
    below = np.where(tail <= thr)[0]
    if below.size:
        decay = float(times[pk + below[0]] - times[pk])
    # Release: if the very end falls off, estimate from the final descent.
    release = 0.3
    if L[-1] < 0.5 * sustain:
        falling = np.where(L >= 0.5 * sustain)[0]
        if falling.size:
            release = max(0.02, float(times[-1] - times[falling[-1]]))
    return {"attack": round(max(0.001, attack), 4),
            "decay": round(max(0.0, decay), 4),
            "sustain": round(float(np.clip(sustain, 0.0, 1.0)), 4),
            "release": round(release, 4)}


# ---------------------------------------------------------------------------
# Plan dataclasses
# ---------------------------------------------------------------------------

@dataclass
class ModLayer:
    source: str        # 'pitch' | 'brightness' | 'amplitude'
    kind: str          # 'detune_beat' | 'lfo' | 'sweep' | 'tremolo'
    rate_hz: float
    depth: float       # natural units: cents (pitch) / partials (bright) / frac (amp)
    shape: str = "sine"
    confidence: float = 1.0


@dataclass
class ModulationPlan:
    f_carrier: float
    amp_env: dict
    layers: list[ModLayer] = field(default_factory=list)
    osc_detune_cents: list[float] = field(default_factory=list)  # per-osc offsets
    n_osc: int = 3
    notes: list[str] = field(default_factory=list)

    def describe(self) -> str:
        lines = [f"ModulationPlan @ {self.f_carrier:.1f} Hz, {self.n_osc} oscs"]
        lines.append(f"  amp ADSR: {self.amp_env}")
        if self.osc_detune_cents:
            beats = [f"{beat_rate_for_detune(c, self.f_carrier):.2f}Hz"
                     for c in self.osc_detune_cents]
            lines.append(f"  osc detune (cents): "
                         f"{[round(c,2) for c in self.osc_detune_cents]} "
                         f"→ beats {beats}")
        for L in self.layers:
            lines.append(f"  [{L.source}/{L.kind}] {L.rate_hz:.2f} Hz "
                         f"depth={L.depth:.3f} {L.shape} (conf {L.confidence:.2f})")
        for n in self.notes:
            lines.append(f"  note: {n}")
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Profile → plan
# ---------------------------------------------------------------------------

SLOW_DRIFT_MAX_HZ = 2.0    # ≤ this ⇒ realisable as oscillator-detune beating


def drift_profile_to_plan(profile: DriftProfile, *, n_osc: int = 3,
                          max_layers_per_source: int = 2) -> ModulationPlan:
    dt = profile.hop_s
    plan = ModulationPlan(f_carrier=profile.f_carrier,
                          amp_env=_fit_adsr(profile.loudness, profile.times),
                          n_osc=n_osc)

    # --- pitch (from the f0 track, in cents relative to its median) ---
    f0 = profile.f0_hz
    if f0.size > 4 and np.all(f0 > 0):
        cents = 1200.0 * np.log2(f0 / np.median(f0))
        pitch_comps = decompose_modulation(cents, dt, k=max_layers_per_source)
        strongest = pitch_comps[0][1] if pitch_comps else 1.0
        slow_rate = None
        for rate, amp in pitch_comps:
            kind = "detune_beat" if rate <= SLOW_DRIFT_MAX_HZ else "lfo"
            plan.layers.append(ModLayer("pitch", kind, rate, amp,
                                        confidence=amp / strongest))
            if kind == "detune_beat" and slow_rate is None:
                slow_rate = rate
        # Spread n_osc oscillators so adjacent pairs beat at the slow rate.
        if slow_rate is not None:
            d = detune_cents_for_beat(slow_rate, profile.f_carrier)
            if n_osc >= 3:
                plan.osc_detune_cents = [-d, 0.0, d][:n_osc]
            elif n_osc == 2:
                plan.osc_detune_cents = [0.0, d]
            else:
                plan.osc_detune_cents = [0.0]
        if any(L.source == "pitch" and L.kind == "lfo" for L in plan.layers):
            plan.notes.append("pitch vibrato (>2 Hz) needs an LFO→pitch route "
                              "(v2 modulator bank; v1 LFO is filter-only).")

    # --- brightness (from the centroid trajectory, partial units) ---
    bright = decompose_modulation(profile.centroid, dt, k=max_layers_per_source)
    if abs(profile.centroid_trend) > 0.5:
        plan.layers.append(ModLayer("brightness", "sweep", 0.0,
                                     abs(profile.centroid_trend)))
    strongest_b = bright[0][1] if bright else 1.0
    for rate, amp in bright:
        plan.layers.append(ModLayer("brightness", "lfo", rate, amp,
                                    confidence=amp / strongest_b))

    # --- amplitude tremolo (loudness periodicity beyond the ADSR shape) ---
    if profile.loudness.size > 8 and profile.loudness.max() > 0:
        Ln = profile.loudness / profile.loudness.max()
        trem = decompose_modulation(Ln, dt, k=1, min_rel_amp=0.2)
        for rate, amp in trem:
            if rate > 0.5:    # below this it's the attack/decay shape, not tremolo
                plan.layers.append(ModLayer("amplitude", "tremolo", rate, amp))

    if not plan.layers:
        plan.notes.append("no significant motion detected — static patch.")
    return plan


# ---------------------------------------------------------------------------
# Plan → concrete v1 .brocot fields
# ---------------------------------------------------------------------------

def plan_to_v1_brocot(plan: ModulationPlan) -> dict:
    """Adapt a ModulationPlan to what v1 can carry: one ADSR, one LFO routed
    to filter cutoff, and per-operator detune. Returns the field values plus
    the operator detune offsets and a list of layers that overflow v1.

    `lfo.depth` is a 0–1 fraction; brightness depth (partials) is mapped by
    centroid-span heuristic depth/q-ish — left coarse on purpose, since the
    musical move is the rate; depth is fine-tuned by ear / search.
    """
    out: dict = {"envelope": dict(plan.amp_env), "operator_detune_cents":
                 list(plan.osc_detune_cents), "unmapped": []}

    # Pick the strongest brightness LFO for the single v1 LFO (filter cutoff).
    bright_lfos = [L for L in plan.layers
                   if L.source == "brightness" and L.kind in ("lfo", "sweep")]
    bright_lfos.sort(key=lambda L: L.confidence * (L.rate_hz > 0), reverse=True)
    if bright_lfos:
        top = bright_lfos[0]
        rate = top.rate_hz if top.kind == "lfo" else max(0.05, 1.0 / 4.0)
        shape = "saw" if top.kind == "sweep" else "sine"
        depth = float(np.clip(top.depth / 8.0, 0.05, 1.0))
        out["lfo"] = {"shape": shape, "rate": round(rate, 3),
                      "depth": round(depth, 3), "keySync": top.kind == "sweep"}
        for L in bright_lfos[1:]:
            out["unmapped"].append(L)
    else:
        out["lfo"] = {"shape": "sine", "rate": 0.5, "depth": 0.0, "keySync": False}

    # Everything that v1 can't route lands in 'unmapped' for the v2 bank.
    for L in plan.layers:
        if L.source == "pitch" and L.kind == "lfo":
            out["unmapped"].append(L)
        if L.source == "amplitude" and L.kind == "tremolo":
            out["unmapped"].append(L)
    return out
