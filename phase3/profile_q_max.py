"""P1.1 — profile the family-distance hot paths across q_max.

Times the three pipeline stages independently so we can see what actually
scales badly before optimising anything (profile-before-optimizing):

    target_amplitudes(q_max)         — per family
    config.predict()                 — q_max-independent (spectrum gen)
    family_distance(sp, fam, q_max)   — KL + (optional) RF term

Run: python -m phase3.profile_q_max
"""

from __future__ import annotations

import time

import numpy as np

from phase3.config import Config, ModConfig
from phase3.families import default_families, family_from_target
from phase3.family_distance import family_distance

Q_MAXES = (256, 512, 1024, 2048)


def _timeit(fn, *, repeats=5, min_time=0.05):
    """Median seconds over `repeats`, auto-scaling inner loops to >min_time."""
    # Calibrate inner loop count.
    n = 1
    while True:
        t0 = time.perf_counter()
        for _ in range(n):
            fn()
        dt = time.perf_counter() - t0
        if dt >= min_time or n >= 1 << 16:
            break
        n *= 2
    times = []
    for _ in range(repeats):
        t0 = time.perf_counter()
        for _ in range(n):
            fn()
        times.append((time.perf_counter() - t0) / n)
    return float(np.median(times))


def _bench_config() -> Config:
    """A representative mid-complexity patch (4 enabled ops)."""
    mods = [
        ModConfig(path="R", depth=2.0, enabled=True),
        ModConfig(path="RL", depth=1.3, enabled=True),
        ModConfig(path="RLL", depth=3.054, enabled=True),
        ModConfig(path="L", depth=1.841, enabled=True),
    ] + [ModConfig() for _ in range(60)]
    return Config(mods=mods, f_carrier=220.0)


def main():
    fams = default_families()
    cfg = _bench_config()

    print("=== predict() — q_max-independent ===")
    t_pred = _timeit(lambda: cfg.predict())
    print(f"  predict() (4-op bench): {t_pred*1e3:.3f} ms\n")
    sp = cfg.predict()

    print("=== target_amplitudes(q_max) — ms per family ===")
    hdr = "family".ljust(18) + "".join(f"{q:>10}" for q in Q_MAXES)
    print(hdr)
    for fam in fams:
        row = fam.name.ljust(18)
        for q in Q_MAXES:
            t = _timeit(lambda q=q, fam=fam: fam.target_amplitudes(q))
            row += f"{t*1e3:>10.3f}"
        print(row)

    # External target (KL-only path) — what Mode B uses.
    ext = family_from_target(np.abs(np.random.default_rng(0).standard_normal(2048)),
                             name="external")
    row = "external".ljust(18)
    for q in Q_MAXES:
        t = _timeit(lambda q=q: ext.target_amplitudes(q))
        row += f"{t*1e3:>10.3f}"
    print(row)

    print("\n=== family_distance(sp, fam, q_max) — ms ===")
    print(hdr)
    # Representative families: integer (RF term), prime (padic), bell/external (KL-only).
    probe = {f.name: f for f in fams}
    probe["external"] = ext
    for name in ("sawtooth", "square", "prime_2", "prime_13",
                 "bell_beta_1.05".replace(".", "_"), "external"):
        fam = probe.get(name) or next((f for f in fams if f.name == name), None)
        if fam is None:
            continue
        row = name.ljust(18)
        for q in Q_MAXES:
            t = _timeit(lambda q=q, fam=fam: family_distance(sp, fam, 220.0, q_max=q),
                        repeats=3)
            row += f"{t*1e3:>10.3f}"
        print(row)


if __name__ == "__main__":
    main()
