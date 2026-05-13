"""Phase-3 generation CLI.

Runs `two_phase_search` once per requested family, takes the top-K
refined candidates from that single search, and writes them as
`.brocot` v2 files into `<output>/<family>/`.

Usage:
    python -m phase3.generate \\
        --output ../resources/presets/factory/discovered \\
        --presets-per-family 8 \\
        --families sawtooth,square,prime_7

    python -m phase3.generate --families all --presets-per-family 8

A single search yields up to `k_top` distinct refined results (each
starts from a different Phase-1 random sample). Asking for N presets
per family translates to k_top = N inside the search, so we get N
diverse local optima per family per run.

The full 12-family × 8-preset run takes roughly 12-20 minutes
depending on hardware (each search is ~60-120 s; tighter time budgets
trade refinement quality for speed).
"""

from __future__ import annotations

import argparse
import multiprocessing as mp
import sys
import time
from pathlib import Path

from phase3.brocot_writer import write_brocot, _ars_version_or_none, _human_family_category, _slug
from phase3.families import (
    DEFAULT_PRIMES,
    Family,
    default_families,
    family_bell_stretched,
    family_organ,
    family_prime,
    family_pulse_pw,
    family_sawtooth,
    family_square,
    family_triangle,
)
from phase3.search import two_phase_search


# ---------------------------------------------------------------------------
# Family lookup
# ---------------------------------------------------------------------------

def _families_by_name() -> dict[str, Family]:
    """Map every default family to its name. Includes the parametric
    instances default_families() produces."""
    return {f.name: f for f in default_families()}


def _resolve_families(spec: str) -> list[Family]:
    """Parse the --families CLI value into a list of Family instances.
    Accepts 'all', or comma-separated family names."""
    table = _families_by_name()
    if spec.strip().lower() == "all":
        return list(table.values())
    out: list[Family] = []
    for raw in spec.split(","):
        name = raw.strip()
        if not name:
            continue
        if name not in table:
            available = ", ".join(sorted(table.keys()))
            raise SystemExit(
                f"Unknown family '{name}'. Available: {available}"
            )
        out.append(table[name])
    if not out:
        raise SystemExit("No families requested")
    return out


# ---------------------------------------------------------------------------
# Generation
# ---------------------------------------------------------------------------

def generate_family(
    family: Family,
    *,
    output_dir: Path,
    presets_per_family: int,
    seed: int,
    time_budget_per_candidate_s: float,
    n_phase1: int,
    q_max: int,
    ars_version: str | None,
    min_enabled: int = 4,
    max_path_depth: int = 16,
    lambda_crit: float = 0.0,
    verbose: bool = True,
) -> list[Path]:
    """Run one search for `family`, write top-K results as .brocot files.
    Files land in `<output_dir>/<family_subdir>/<stem>.brocot`."""
    fam_subdir = _slug(_human_family_category(family.name))
    fam_dir = output_dir / fam_subdir
    fam_dir.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    # Over-sample candidates so we can dedupe down to the target count.
    # Coord-descent often collapses similar Phase-1 seeds onto the same
    # local optimum; without over-sampling we'd lose preset count.
    inflated_k = int(round(presets_per_family * 2.0))
    results = two_phase_search(
        family,
        n_phase1=n_phase1,
        k_top=inflated_k,
        seed=seed,
        time_budget_per_candidate_s=time_budget_per_candidate_s,
        q_max=q_max,
        min_enabled=min_enabled,
        max_path_depth=max_path_depth,
        lambda_crit=lambda_crit,
    )
    # Take the top-N unique-fingerprint results (already deduped + sorted).
    results = results[:presets_per_family]
    elapsed = time.time() - t0

    if verbose:
        if results:
            objs = [r.objective for r in results]
            fds = [r.family_distance for r in results]
            crits = [r.criticality for r in results]
            extra = ""
            if lambda_crit > 0.0:
                extra = f" | fd {fds[0]:.4f}..{fds[-1]:.4f} | crit {crits[0]:.4f}..{crits[-1]:.4f}"
            print(f"  [{family.name}] {len(results)} results in {elapsed:.1f}s "
                  f"| obj {objs[0]:.4f}..{objs[-1]:.4f}{extra}")
        else:
            print(f"  [{family.name}] no results in {elapsed:.1f}s")

    written: list[Path] = []
    for i, r in enumerate(results):
        # Name: "<Family>·N (obj=0.084)" for human-readable preset menu;
        # filename slugs from a compact "<family>_<i>" stem.
        human = _human_family_category(family.name)
        display_name = f"{human}·{i + 1}  (obj={r.objective:.4f})"
        stem = f"{_slug(family.name)}_{i + 1:02d}"
        path = write_brocot(
            r, fam_dir,
            name=display_name,
            n_phase1=n_phase1,
            k_top=presets_per_family,
            q_max=q_max,
            ars_version=ars_version,
            file_stem=stem,
        )
        written.append(path)
    return written


# ---------------------------------------------------------------------------
# Multiprocessing helper
# ---------------------------------------------------------------------------

def _generate_family_by_name(fam_name: str, seed: int, kwargs: dict) -> list[Path]:
    """Worker entry point — reconstructs the Family from its name so
    we don't have to pickle target_amplitudes closures across the
    fork() boundary."""
    family = _families_by_name()[fam_name]
    return generate_family(family, seed=seed, **kwargs)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate ARS-discovered .brocot factory presets.")
    parser.add_argument(
        "--output", required=True, type=Path,
        help="Directory to write .brocot files into (per-family subdirs).")
    parser.add_argument(
        "--families", default="all",
        help="Comma-separated family names, or 'all' (default).")
    parser.add_argument(
        "--presets-per-family", type=int, default=8,
        help="Number of refined results per family (default 8).")
    parser.add_argument(
        "--seed", type=int, default=314,
        help="Search seed (default 314).")
    parser.add_argument(
        "--n-phase1", type=int, default=1000,
        help="Phase-1 random sample count (default 1000).")
    parser.add_argument(
        "--time-per-candidate", type=float, default=4.0,
        help="Phase-2 time budget per refinement, seconds (default 4.0).")
    parser.add_argument(
        "--q-max", type=int, default=16,
        help="Grid size for family-distance KL + RF terms (default 16).")
    parser.add_argument(
        "--min-enabled", type=int, default=4,
        help="Min enabled operators per config (default 4).")
    parser.add_argument(
        "--max-path-depth", type=int, default=16,
        help="Max Stern-Brocot path length (default 16).")
    parser.add_argument(
        "--lambda-crit", type=float, default=0.0,
        help="Weight on criticality bonus in the objective "
             "(default 0; >0 biases toward spectrally lively configs).")
    parser.add_argument(
        "--jobs", type=int, default=1,
        help="Run this many families in parallel (default 1, serial). "
             "Each family is single-threaded numpy; pick a number ≤ physical cores.")
    parser.add_argument(
        "--quiet", action="store_true",
        help="Suppress per-family progress lines.")

    args = parser.parse_args(argv)
    families = _resolve_families(args.families)
    ars_version = _ars_version_or_none()

    verbose = not args.quiet
    if verbose:
        print(f"Generating {len(families)} families "
              f"× {args.presets_per_family} presets "
              f"= {len(families) * args.presets_per_family} total")
        if ars_version:
            print(f"ARS version: {ars_version}")
        print(f"Output: {args.output.resolve()}")
        print()

    t0 = time.time()
    total_written = 0

    if args.jobs <= 1:
        # Serial path — same as P3.6 behaviour, predictable output order.
        for i, fam in enumerate(families):
            if verbose:
                print(f"[{i + 1}/{len(families)}] {fam.name}")
            paths = generate_family(
                fam,
                output_dir=args.output,
                presets_per_family=args.presets_per_family,
                seed=args.seed + i,
                time_budget_per_candidate_s=args.time_per_candidate,
                n_phase1=args.n_phase1,
                q_max=args.q_max,
                min_enabled=args.min_enabled,
                max_path_depth=args.max_path_depth,
                lambda_crit=args.lambda_crit,
                ars_version=ars_version,
                verbose=verbose,
            )
            total_written += len(paths)
    else:
        # Parallel path — one process per family, up to args.jobs at a
        # time. Each search is single-threaded numpy/scipy so this
        # scales near-linearly until we hit physical cores.
        #
        # Family objects contain target_amplitudes closures that don't
        # pickle, so we pass family *names* across the process boundary
        # and rebuild on the worker side via _families_by_name().
        ctx = mp.get_context("fork")  # closures + ARS submodule path
        tasks = [
            (fam.name, args.seed + i) for i, fam in enumerate(families)
        ]
        common_kwargs = dict(
            output_dir=args.output,
            presets_per_family=args.presets_per_family,
            time_budget_per_candidate_s=args.time_per_candidate,
            n_phase1=args.n_phase1,
            q_max=args.q_max,
            min_enabled=args.min_enabled,
            max_path_depth=args.max_path_depth,
            lambda_crit=args.lambda_crit,
            ars_version=ars_version,
            verbose=verbose,
        )
        with ctx.Pool(processes=args.jobs) as pool:
            jobs = [
                pool.apply_async(
                    _generate_family_by_name,
                    args=(fam_name, seed, common_kwargs),
                )
                for (fam_name, seed) in tasks
            ]
            for j, async_result in enumerate(jobs):
                paths = async_result.get()
                total_written += len(paths)
                if verbose:
                    print(f"  ({j + 1}/{len(jobs)} families done)")

    if verbose:
        elapsed = time.time() - t0
        print(f"\nDone. {total_written} presets in {elapsed:.1f}s.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
