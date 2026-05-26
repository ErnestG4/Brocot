"""P0.3 — Mode B synthetic round-trip validation (the ARP gate).

    known Config → render audio → WAV → audio_to_spectrum
                 → family_from_target → two_phase_search → recovered Config

If the search cannot find back what it generated, Mode B is broken and
P1/P2 do not proceed (per the overnight brief's P0 fallback).

Recovery is measured *spectrally and config-to-config*: both the true and
the recovered config are run through the analytic predictor and projected
onto the integer grid, then compared by L1 / KL. FM spectra are many-to-one
(the same partial set has many parameter pre-images) and the search forces
≥ min_enabled operators, so an exact-parameter match is not required — a
spectral twin is a legitimate "canonical-equivalent" recovery. The search
*objective* (family_distance to the audio target) carries an irreducible
floor from the analytic-vs-audio representation gap; it is reported but is
not the recovery criterion.

Run:  python -m phase3.run_mode_b_roundtrip
"""

from __future__ import annotations

import argparse
import tempfile
import time
from dataclasses import dataclass

import numpy as np

from phase3.audio_spectrum import audio_to_spectrum, render_config_audio, write_wav
from phase3.config import Config, ModConfig
from phase3.families import family_from_target
from phase3.family_distance import _kl_divergence, family_distance, quantize_to_integer_grid
from phase3.search import two_phase_search


# ---------------------------------------------------------------------------
# Known ground-truth configurations (≥4 enabled ops so exact recovery is
# reachable under the search's min_enabled=4 floor).
# ---------------------------------------------------------------------------

def _cfg(ops: list[tuple[str, float]], f_carrier: float = 220.0,
         n_slots: int = 64) -> Config:
    mods = [ModConfig(path=p, depth=d, enabled=True) for p, d in ops]
    mods += [ModConfig() for _ in range(n_slots - len(mods))]
    return Config(mods=mods, f_carrier=f_carrier)


def known_configs() -> dict[str, Config]:
    """A small spread of recoverable ground-truth patches.

    Depths are kept *off* the exact Bessel zeros (2.405, 3.832, …): a
    modulator parked on a J0 zero nulls the carrier through a delicate
    cancellation of many coherent intermodulation products, which the
    pruned analytic predictor and the full time-domain render resolve
    differently — a pathological case that inflates the floor without
    testing the search. We validate on musically-typical configs instead.
    """
    return {
        # Pure-harmonic stack (integer ratios): the cleanest case —
        # analytic and render agree to L1 ≈ 0.03.
        "harmonic4": _cfg([
            ("",    1.20),   # 1/1
            ("R",   1.00),   # 2/1
            ("RR",  0.80),   # 3/1
            ("RRR", 0.60),   # 4/1
        ]),
        # Mixed integer + half-integer ratios: exercises the grid-
        # projection tie-breaking floor on fractional-ratio FM.
        "mixed4": _cfg([
            ("R",   2.00),   # 2/1
            ("RL",  1.30),   # 3/2
            ("RLL", 1.00),   # 4/3
            ("L",   1.50),   # 1/2
        ]),
        # Deeper-index metallic flavour.
        "metallic4": _cfg([
            ("R",   3.50),   # 2/1
            ("RL",  2.00),   # 3/2
            ("RRL", 1.50),   # 5/3
            ("L",   1.20),   # 1/2
        ]),
    }


# ---------------------------------------------------------------------------
# Recovery measurement
# ---------------------------------------------------------------------------

def _analytic_vec(cfg: Config, q_max: int) -> np.ndarray:
    v = quantize_to_integer_grid(cfg.predict(), cfg.f_carrier, q_max)
    s = v.sum()
    return v / s if s > 0 else v


@dataclass
class RoundTripResult:
    name: str
    audio_l1: float           # PRIMARY: render(recovered) vs render(true), audio space
    audio_kl: float           # PRIMARY: same, KL
    recovery_l1: float        # config↔config analytic L1 (secondary)
    recovery_kl: float        # config↔config analytic KL (secondary)
    search_objective: float   # family_distance(best, audio target) — has floor
    floor: float              # family_distance(true cfg, audio target)
    exact_param_match: bool   # any top-K == true canonical fingerprint
    n_true_ops: int
    n_recovered_ops: int
    seconds: float


def run_one(
    name: str,
    cfg_true: Config,
    *,
    q_max: int = 16,
    duration_s: float = 2.0,
    n_phase1: int = 1000,
    k_top: int = 12,
    time_budget_per_candidate_s: float = 3.0,
    seed: int = 42,
    verbose: bool = True,
) -> RoundTripResult:
    t0 = time.time()
    f_c = cfg_true.f_carrier

    # 1) render → WAV → spectrum (exercises the real WAV loader).
    audio = render_config_audio(cfg_true, duration_s=duration_s)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as tf:
        write_wav(tf.name, audio)
        target = audio_to_spectrum(tf.name, q_max=q_max, f_carrier=f_c)
    target_audio_n = target / target.sum() if target.sum() > 0 else target
    fam = family_from_target(target, name=name, f_carrier=f_c)

    # Floor: even the true config can't reach 0 against its own audio
    # spectrum (analytic vs power-band representation gap).
    floor = family_distance(cfg_true.predict(), fam, f_c, q_max=q_max)

    # 2) search.
    results = two_phase_search(
        fam, f_carrier=f_c, n_phase1=n_phase1, k_top=k_top, seed=seed,
        time_budget_per_candidate_s=time_budget_per_candidate_s, q_max=q_max,
    )
    best = results[0]

    # 3a) PRIMARY: audio-space recovery — render the recovered config and
    #     compare its measured spectrum to the target's. This asks the only
    #     question that matters perceptually ("does it sound the same?") and
    #     is immune to the analytic-vs-render representation gap, since both
    #     sides go through the identical render→audio_to_spectrum path.
    rec_audio = render_config_audio(best.config, duration_s=duration_s)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as tf:
        write_wav(tf.name, rec_audio)
        rec_audio_spec = audio_to_spectrum(tf.name, q_max=q_max, f_carrier=f_c)
    rec_audio_n = (rec_audio_spec / rec_audio_spec.sum()
                   if rec_audio_spec.sum() > 0 else rec_audio_spec)
    audio_l1 = float(np.abs(rec_audio_n - target_audio_n).sum())
    audio_kl = _kl_divergence(rec_audio_n, target_audio_n)

    # 3b) SECONDARY: analytic config↔config recovery (carries grid-
    #     projection tie-breaking noise on fractional-ratio partials).
    truth_vec = _analytic_vec(cfg_true, q_max)
    rec_vec = _analytic_vec(best.config, q_max)
    rec_l1 = float(np.abs(rec_vec - truth_vec).sum())
    rec_kl = _kl_divergence(rec_vec, truth_vec)

    true_fp = cfg_true.canonicalised().fingerprint()
    exact = any(r.config.fingerprint() == true_fp for r in results)

    res = RoundTripResult(
        name=name,
        audio_l1=audio_l1,
        audio_kl=audio_kl,
        recovery_l1=rec_l1,
        recovery_kl=rec_kl,
        search_objective=best.objective,
        floor=floor,
        exact_param_match=exact,
        n_true_ops=cfg_true.num_enabled(),
        n_recovered_ops=best.config.num_enabled(),
        seconds=time.time() - t0,
    )
    if verbose:
        print(f"  target  partials : {np.argsort(target_audio_n)[::-1][:6] + 1}")
        print(f"  recov   partials : {np.argsort(rec_audio_n)[::-1][:6] + 1}")
        print(f"  AUDIO  L1={audio_l1:.4f} KL={audio_kl:.4f}   "
              f"analytic L1={rec_l1:.4f} KL={rec_kl:.4f}")
        print(f"  obj={best.objective:.4f} (floor={floor:.4f})  exact={exact}  "
              f"ops {res.n_true_ops}->{res.n_recovered_ops}  {res.seconds:.1f}s")
    return res


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--q-max", type=int, default=16)
    ap.add_argument("--n-phase1", type=int, default=1000)
    ap.add_argument("--k-top", type=int, default=12)
    ap.add_argument("--budget", type=float, default=3.0)
    ap.add_argument("--duration", type=float, default=2.0)
    ap.add_argument("--l1-epsilon", type=float, default=0.15,
                    help="pass threshold on audio-space recovery L1")
    args = ap.parse_args()

    print(f"Mode B round-trip — q_max={args.q_max} n_phase1={args.n_phase1} "
          f"k_top={args.k_top} budget={args.budget}s/cand\n")

    results: list[RoundTripResult] = []
    for name, cfg in known_configs().items():
        print(f"[{name}]")
        results.append(run_one(
            name, cfg,
            q_max=args.q_max, duration_s=args.duration,
            n_phase1=args.n_phase1, k_top=args.k_top,
            time_budget_per_candidate_s=args.budget,
        ))
        print()

    # Verdict — on PRIMARY audio-space recovery.
    worst_audio = max(r.audio_l1 for r in results)
    n_pass = sum(1 for r in results if r.audio_l1 < args.l1_epsilon)
    print("=" * 72)
    print(f"{'config':<14}{'audL1':>8}{'audKL':>8}{'anaL1':>8}{'anaKL':>8}"
          f"{'obj':>8}{'floor':>8}{'exact':>7}")
    for r in results:
        print(f"{r.name:<14}{r.audio_l1:>8.4f}{r.audio_kl:>8.4f}"
              f"{r.recovery_l1:>8.4f}{r.recovery_kl:>8.4f}"
              f"{r.search_objective:>8.4f}{r.floor:>8.4f}"
              f"{str(r.exact_param_match):>7}")
    print("-" * 72)
    print(f"worst AUDIO recovery L1 = {worst_audio:.4f}   ε = {args.l1_epsilon}")
    verdict = "PASS" if n_pass == len(results) else "FAIL"
    print(f"VERDICT: {verdict}  ({n_pass}/{len(results)} within ε on audio recovery)")


if __name__ == "__main__":
    main()
