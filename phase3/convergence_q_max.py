"""P1.3 — q_max convergence gate.

Confirm family_distance values stabilise as the integer grid grows
q_max ∈ {256, 512, 1024, 2048}. Two things can drift:

  * KL term — converges once q_max exceeds the partial content (extra bins
    are ~zero; the eps-smoothing mass q_max·1e-9 stays negligible).
  * RF term — for integer/prime families the characteristic-q amplitude is
    normalised by Σ rf[1:q_max], and that sum keeps accruing high-q RF
    components, so the normalised term can drift with q_max. This is what
    we measure here; if it drifts, cap the RF range.

Also reports bell_stretched, whose positions n^β for n≤max_n top out near
max_n^β — so at large q_max its target is mostly zero unless max_n scales.

Run: python -m phase3.convergence_q_max
"""

from __future__ import annotations

import numpy as np

from phase3.config import Config, ModConfig
from phase3.families import (
    default_families,
    family_bell_stretched,
    family_from_target,
)
from phase3.family_distance import family_distance

Q_MAXES = (256, 512, 1024, 2048)


def _bench_configs() -> dict[str, Config]:
    def mk(ops):
        return Config(mods=[ModConfig(p, d, True) for p, d in ops]
                      + [ModConfig() for _ in range(64 - len(ops))], f_carrier=220.0)
    return {
        "harmonic": mk([("", 1.2), ("R", 1.0), ("RR", 0.8), ("RRR", 0.6)]),
        "deep_fm":  mk([("R", 3.5), ("RL", 2.0), ("RRL", 1.5), ("L", 1.2)]),
    }


def _table(title, rows, cols=Q_MAXES):
    print(title)
    print("  " + "name".ljust(20) + "".join(f"{q:>12}" for q in cols)
          + f"{'max|Δ|':>12}")
    for name, vals in rows:
        deltas = [abs(vals[i + 1] - vals[i]) for i in range(len(vals) - 1)]
        maxd = max(deltas) if deltas else 0.0
        print("  " + name.ljust(20) + "".join(f"{v:>12.6f}" for v in vals)
              + f"{maxd:>12.2e}")
    print()


def main():
    cfgs = _bench_configs()
    fams = default_families()
    by_name = {f.name: f for f in fams}

    # Probe a representative spread: KL-only (bell, external) + RF (saw, prime).
    probe_names = ["sawtooth", "square", "prime_2", "prime_7",
                   "bell_beta_1_05"]

    for cfg_name, cfg in cfgs.items():
        sp = cfg.predict()
        rows = []
        for fname in probe_names:
            fam = by_name.get(fname)
            if fam is None:
                continue
            rows.append((fname, [family_distance(sp, fam, 220.0, q_max=q)
                                 for q in Q_MAXES]))
        # External target (KL-only) derived from this config's own spectrum.
        from phase3.family_distance import quantize_to_integer_grid
        ext_amps = quantize_to_integer_grid(sp, 220.0, 2048)
        ext = family_from_target(ext_amps, name="self")
        rows.append(("external(self)",
                     [family_distance(sp, ext, 220.0, q_max=q) for q in Q_MAXES]))
        _table(f"=== family_distance vs q_max — config '{cfg_name}' ===", rows)

    # bell_stretched max_n scaling: does extending positions across the grid
    # change the distance? Compare fixed max_n=30 vs scaled max_n=q_max^(1/β).
    print("=== bell_stretched max_n scaling (config 'deep_fm') ===")
    sp = cfgs["deep_fm"].predict()
    beta = 1.05
    rows = []
    fixed = [family_distance(sp, family_bell_stretched(beta, max_n=30), 220.0, q_max=q)
             for q in Q_MAXES]
    scaled = [family_distance(
        sp, family_bell_stretched(beta, max_n=int(q ** (1.0 / beta))), 220.0, q_max=q)
        for q in Q_MAXES]
    rows.append(("max_n=30 (fixed)", fixed))
    rows.append(("max_n=q^(1/β)", scaled))
    _table("", rows)


if __name__ == "__main__":
    main()
