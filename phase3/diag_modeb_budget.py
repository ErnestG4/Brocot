"""P0.3 diagnostic — is Mode B recovery a BUDGET problem or an ALGORITHM
problem? Multi-start the existing two_phase_search across many seeds in
parallel (a budget knob, not an algorithm change) and see whether the
recovered objective approaches the ~0 floor and audio recovery clears ε.

Run: python -m phase3.diag_modeb_budget
"""

from __future__ import annotations

import tempfile
from concurrent.futures import ProcessPoolExecutor

import numpy as np

from phase3.audio_spectrum import audio_to_spectrum, render_config_audio, write_wav
from phase3.families import family_from_target
from phase3.family_distance import _kl_divergence, family_distance
from phase3.run_mode_b_roundtrip import known_configs
from phase3.search import two_phase_search


N_PHASE1 = 3000
K_TOP = 20
BUDGET = 3.0
SEEDS = 8          # per config
Q_MAX = 16
DURATION = 2.0


def _target_for(name):
    cfg = known_configs()[name]
    audio = render_config_audio(cfg, duration_s=DURATION)
    with tempfile.NamedTemporaryFile(suffix=".wav") as tf:
        write_wav(tf.name, audio)
        tgt = audio_to_spectrum(tf.name, q_max=Q_MAX, f_carrier=cfg.f_carrier)
    return cfg, tgt


def _one_search(args):
    name, seed = args
    cfg, tgt = _target_for(name)
    fam = family_from_target(tgt, name=name, f_carrier=cfg.f_carrier)
    res = two_phase_search(
        fam, f_carrier=cfg.f_carrier, n_phase1=N_PHASE1, k_top=K_TOP,
        seed=seed, time_budget_per_candidate_s=BUDGET, q_max=Q_MAX,
    )
    best = res[0]
    return name, seed, best.objective, best.config


def _audio_recovery(name, best_config):
    cfg, tgt = _target_for(name)
    tn = tgt / tgt.sum()
    ra = render_config_audio(best_config, duration_s=DURATION)
    with tempfile.NamedTemporaryFile(suffix=".wav") as tf:
        write_wav(tf.name, ra)
        rs = audio_to_spectrum(tf.name, q_max=Q_MAX, f_carrier=cfg.f_carrier)
    rn = rs / rs.sum()
    return float(np.abs(rn - tn).sum()), _kl_divergence(rn, tn)


def main():
    names = list(known_configs().keys())
    floors = {}
    for nm in names:
        cfg, tgt = _target_for(nm)
        fam = family_from_target(tgt, name=nm, f_carrier=cfg.f_carrier)
        floors[nm] = family_distance(cfg.predict(), fam, cfg.f_carrier, q_max=Q_MAX)

    jobs = [(nm, 1000 + s) for nm in names for s in range(SEEDS)]
    print(f"multi-start: {len(names)} configs × {SEEDS} seeds = {len(jobs)} "
          f"searches  (n_phase1={N_PHASE1}, k_top={K_TOP})\n")

    best_by_name = {}
    with ProcessPoolExecutor(max_workers=min(10, len(jobs))) as ex:
        for name, seed, obj, cfg in ex.map(_one_search, jobs):
            cur = best_by_name.get(name)
            if cur is None or obj < cur[0]:
                best_by_name[name] = (obj, cfg)

    print(f"{'config':<14}{'bestObj':>10}{'floor':>10}{'audL1':>9}{'audKL':>9}")
    worst = 0.0
    for nm in names:
        obj, cfg = best_by_name[nm]
        al1, akl = _audio_recovery(nm, cfg)
        worst = max(worst, al1)
        print(f"{nm:<14}{obj:>10.5f}{floors[nm]:>10.5f}{al1:>9.4f}{akl:>9.4f}")
    print(f"\nworst audio L1 across configs = {worst:.4f}")


if __name__ == "__main__":
    main()
