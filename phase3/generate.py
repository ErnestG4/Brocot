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
    verbose: bool = True,
) -> list[Path]:
    """Run one search for `family`, write top-K results as .brocot files.
    Files land in `<output_dir>/<family_subdir>/<stem>.brocot`."""
    fam_subdir = _slug(_human_family_category(family.name))
    fam_dir = output_dir / fam_subdir
    fam_dir.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    results = two_phase_search(
        family,
        n_phase1=n_phase1,
        k_top=presets_per_family,
        seed=seed,
        time_budget_per_candidate_s=time_budget_per_candidate_s,
        q_max=q_max,
    )
    elapsed = time.time() - t0

    if verbose:
        objs = [r.objective for r in results]
        print(f"  [{family.name}] {len(results)} results in {elapsed:.1f}s "
              f"| obj {objs[0]:.4f}..{objs[-1]:.4f}")

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
    for i, fam in enumerate(families):
        if verbose:
            print(f"[{i + 1}/{len(families)}] {fam.name}")
        paths = generate_family(
            fam,
            output_dir=args.output,
            presets_per_family=args.presets_per_family,
            seed=args.seed + i,  # decorrelate seeds across families
            time_budget_per_candidate_s=args.time_per_candidate,
            n_phase1=args.n_phase1,
            q_max=args.q_max,
            ars_version=ars_version,
            verbose=verbose,
        )
        total_written += len(paths)

    if verbose:
        elapsed = time.time() - t0
        print(f"\nDone. {total_written} presets in {elapsed:.1f}s.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
