"""Build the runtime pull-to-nearest index from the factory preset tree.

The synth needs to answer two questions cheaply at runtime, without
re-running ARS or Python:

    1. "Of all the Sawtooth exemplars in the factory bank, which
       one has a spectrum closest to my current spectrum?"
    2. "Walk my parameters toward that exemplar by α∈[0,1]."

This module produces the offline table that powers (1). The output
JSON lives at `resources/pull_index.json` and is read by the C++
runtime. The runtime predicts the current voice's spectrum (or
samples it once when the user presses a shape button), then does an
L1 compare against the precomputed amplitude vectors stored here.

Index layout:
    {
      "version": "1.0",
      "f_carrier_ref": 220.0,
      "q_max": 32,
      "categories": {
        "Sawtooth": {
          "preset_count": 8,
          "best_obj_preset": "discovered/sawtooth/sawtooth_01.brocot",
          "centroid_amps": [..32 floats..],
          "exemplars": [
            {
              "preset":    "discovered/sawtooth/sawtooth_01.brocot",
              "objective": 0.1112,
              "n_ops":     3,
              "amps":      [..32 floats sum to 1..],
              "ops":       [{"path": "R", "depth": 1.84, "enabled": true}, ..]
            }, ...
          ]
        },
        ...
      }
    }

The amplitude vector is the quantised spectrum projected onto the
first q_max harmonics of the reference carrier (220 Hz) and
normalised to sum=1.

Taxonomy (redesigned — see phase3.family_axis). Categories are two
orthogonal kinds, no longer a single conflated provenance string:

  1. The 12 MEASURED spectral families (Sawtooth, Square, …, Prime 13).
     Populated with clean exemplars MINED from the GPU bank by render-
     fingerprint proximity to each ideal target — the same canonical
     realisations the landscape maps steer toward, and uniform across
     all families (the factory presets barely cover Square/primes; the
     old provenance grouping left those near-empty and contaminated).
  2. The Textured-Smooth · N-ops DENSITY buckets, kept verbatim from the
     factory tree — explicitly an op-count axis backing the runtime's
     density-steer row, NOT spectral families.

Vanity / reference labels (Atlas, Vowel, Truax, …) no longer define
their own regions: those presets are classified ONTO the family axis
(recorded in derived/preset_families.json for the menu) rather than
appearing as pull categories.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Any

import numpy as np

from phase3.config import Config, ModConfig
from phase3.family_distance import quantize_to_integer_grid


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

DEFAULT_Q_MAX = 32      # integer-harmonic grid size; covers q=1..32
DEFAULT_F_REF = 220.0   # quantisation reference (an octave below A4)
INDEX_VERSION = "1.0"


# ---------------------------------------------------------------------------
# Preset → exemplar entry
# ---------------------------------------------------------------------------

def _config_from_preset(doc: dict) -> Config:
    """Build a Config from a parsed .brocot v2 dict."""
    eng = doc.get("engine", {})
    ops = eng.get("operators", []) or []
    mods = [
        ModConfig(
            path=o.get("path", "") or "",
            depth=float(o.get("depth", 0.0)),
            enabled=bool(o.get("enabled", False)),
            detune_cents=float(o.get("detune", 0.0)),
        )
        for o in ops
    ]
    return Config(mods=mods, f_carrier=DEFAULT_F_REF)


def _amplitude_vector(cfg: Config, q_max: int) -> np.ndarray:
    """Predict spectrum, project onto q_max-bin integer grid, L1-normalise.

    Predict params are intentionally tight: with kNumOperators at 64
    (P3.12), the full -50 dB / 1e-3 / no-cap default takes seconds per
    64-op-deep preset, and the runtime only needs amplitude *shape*
    for L1 comparisons. -30 dB / 5e-3 / 500-partial cap gets us to
    ~20 ms/eval at 64 ops with the same relative ranking."""
    sp = cfg.predict(threshold_db=-30.0, eps=5e-3, max_partials=500)
    amps = quantize_to_integer_grid(sp, cfg.f_carrier, q_max)
    s = float(amps.sum())
    if s > 0:
        amps = amps / s
    return amps


def _exemplar_for(doc: dict, rel_path: str, q_max: int) -> dict[str, Any]:
    cfg = _config_from_preset(doc)
    amps = _amplitude_vector(cfg, q_max)
    n_ops = sum(1 for m in cfg.mods if m.enabled)

    # Objective (lower = better) lives in different sub-keys depending on
    # the search kind. We surface whatever's there or 0 for hand-authored.
    prov = doc.get("provenance", {})
    s = prov.get("search", {}) or {}
    objective = s.get("objective", 0.0)

    return {
        "preset":    rel_path,
        "objective": float(objective),
        "n_ops":     int(n_ops),
        "amps":      [float(x) for x in amps.tolist()],
        "ops": [
            {"path": m.path, "depth": float(m.depth), "enabled": bool(m.enabled)}
            for m in cfg.mods
        ],
    }


# ---------------------------------------------------------------------------
# Family exemplars from the measured family axis (bank-mined, clean)
# ---------------------------------------------------------------------------

FAMILY_EXEMPLARS_NPZ = Path("phase3/derived/family_exemplars.npz")
TEXTURE_PREFIX = "Textured-Smooth"


def _ops_from_params(ridx_row, dep_row, paths: list[str],
                     depth_eps: float = 1e-4) -> list[ModConfig]:
    """Reconstruct an OpState list from a mined bank config's params."""
    return [
        ModConfig(path=paths[int(ridx_row[s])], depth=float(dep_row[s]),
                  enabled=float(dep_row[s]) > depth_eps)
        for s in range(len(ridx_row))
    ]


def _family_categories(q_max: int, verbose: bool) -> dict[str, Any]:
    """The 12 measured spectral families, each populated with clean bank-mined
    exemplars (the same canonical realisations the maps steer toward). This is
    the family axis — positions are MEASURED, not provenance strings. Density is
    a separate axis (the Textured-Smooth row); vanity labels are folded onto the
    family each preset measures into and don't appear here."""
    from phase3.coverage_bank import curated_ratios

    if not FAMILY_EXEMPLARS_NPZ.exists():
        raise FileNotFoundError(
            f"{FAMILY_EXEMPLARS_NPZ} missing — run "
            f"`phase3/.venv-gpu/bin/python -m phase3.family_axis exemplars` first.")
    ex = np.load(FAMILY_EXEMPLARS_NPZ, allow_pickle=True)
    fam_names = list(ex["family_names"])
    ridx, depths, dist = ex["ridx"], ex["depths"], ex["dist"]
    paths = curated_ratios(5)

    categories: dict[str, Any] = {}
    for fi, fam in enumerate(fam_names):
        entries: list[dict[str, Any]] = []
        # objective = render-space L1 to the ideal target (lower = cleaner),
        # so exemplars.front() — what getBestOps() pulls toward — is the
        # cleanest realisation of this family.
        order = np.argsort(dist[fi])
        for rank in order:
            mods = _ops_from_params(ridx[fi, rank], depths[fi, rank], paths)
            cfg = Config(mods=mods, f_carrier=DEFAULT_F_REF)
            amps = _amplitude_vector(cfg, q_max)
            entries.append({
                "preset":    f"mined/{fam}/{int(rank):03d}",
                "objective": round(float(dist[fi, rank]), 5),
                "n_ops":     sum(1 for m in mods if m.enabled),
                "amps":      [float(x) for x in amps.tolist()],
                "ops": [{"path": m.path, "depth": float(m.depth),
                         "enabled": bool(m.enabled)} for m in mods],
            })
        centroid = np.mean([e["amps"] for e in entries], axis=0)
        s = float(centroid.sum())
        if s > 0:
            centroid = centroid / s
        categories[fam] = {
            "preset_count":    len(entries),
            "best_obj_preset": entries[0]["preset"],
            "centroid_amps":   [float(x) for x in centroid.tolist()],
            "exemplars":       entries,
        }
        if verbose:
            print(f"  family {fam:<22s}{len(entries):>4d} exemplars  "
                  f"(cleanest L1 {entries[0]['objective']:.3f})")
    return categories


def _density_categories(factory_dir: Path, q_max: int, verbose: bool):
    """The orthogonal DENSITY axis: the Textured-Smooth · N-ops buckets that back
    the runtime's density-steer row (SteerTab resolves them via the
    'Textured-Smooth' prefix). Kept verbatim from the factory tree — these are
    explicitly op-count buckets, not spectral families, so they no longer share
    the family axis with Sawtooth/Square/… (the conflation this redesign fixes).
    """
    by_cat: dict[str, list[dict[str, Any]]] = defaultdict(list)
    skipped = 0
    for f in sorted(factory_dir.rglob("*.brocot")):
        try:
            doc = json.loads(f.read_text())
        except json.JSONDecodeError:
            skipped += 1
            continue
        cat = (doc.get("provenance", {}) or {}).get("category", "").strip()
        if not cat.startswith(TEXTURE_PREFIX):
            continue                       # non-density presets live on the family axis
        rel = str(f.relative_to(factory_dir))
        try:
            by_cat[cat].append(_exemplar_for(doc, rel, q_max))
        except Exception:
            skipped += 1

    categories: dict[str, Any] = {}
    for cat, entries in sorted(by_cat.items()):
        entries.sort(key=lambda e: e["objective"])
        centroid = np.mean([e["amps"] for e in entries], axis=0)
        s = float(centroid.sum())
        if s > 0:
            centroid = centroid / s
        categories[cat] = {
            "preset_count":    len(entries),
            "best_obj_preset": entries[0]["preset"],
            "centroid_amps":   [float(x) for x in centroid.tolist()],
            "exemplars":       entries,
        }
    if verbose:
        print(f"  density buckets: {len(categories)} (skipped {skipped})")
    return categories, skipped


# ---------------------------------------------------------------------------
# Index builder
# ---------------------------------------------------------------------------

def build_index(
    factory_dir: Path,
    *,
    q_max: int = DEFAULT_Q_MAX,
    verbose: bool = True,
) -> dict[str, Any]:
    """Build the pull index: 12 MEASURED family categories (bank-mined clean
    exemplars) + the orthogonal Textured-Smooth density buckets. Reference /
    vanity presets are classified onto the family axis (recorded in
    derived/preset_families.json) rather than forming their own regions."""
    if verbose:
        print("Building measured family categories ...")
    categories = _family_categories(q_max, verbose)
    if verbose:
        print("Building density (Textured-Smooth) categories ...")
    density, skipped = _density_categories(factory_dir, q_max, verbose)
    categories.update(density)

    total = sum(c["preset_count"] for c in categories.values())
    return {
        "version":       INDEX_VERSION,
        "f_carrier_ref": DEFAULT_F_REF,
        "q_max":         q_max,
        "n_categories":  len(categories),
        "n_exemplars":   total,
        "n_skipped":     skipped,
        "categories":    categories,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _print_summary(index: dict[str, Any]) -> None:
    print(f"\nPull index built:")
    print(f"  version       {index['version']}")
    print(f"  f_carrier_ref {index['f_carrier_ref']} Hz")
    print(f"  q_max         {index['q_max']}")
    print(f"  exemplars     {index['n_exemplars']}")
    print(f"  categories    {index['n_categories']}")
    if index['n_skipped']:
        print(f"  skipped       {index['n_skipped']}")
    print()
    print(f"  {'category':<40s}{'n':>6s}  {'best preset':<60s}")
    print(f"  {'-'*40}  {'-':>4s}  {'-'*60}")
    for cat, c in index['categories'].items():
        print(f"  {cat:<40s}{c['preset_count']:>6d}  {c['best_obj_preset']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build the runtime pull-to-nearest index.")
    parser.add_argument("--factory-dir", type=Path,
                        default=Path("resources/presets/factory"))
    parser.add_argument("--output", type=Path,
                        default=Path("resources/pull_index.json"))
    parser.add_argument("--q-max", type=int, default=DEFAULT_Q_MAX)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    verbose = not args.quiet
    t0 = time.time()
    index = build_index(args.factory_dir, q_max=args.q_max, verbose=verbose)
    elapsed = time.time() - t0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # `ensure_ascii=False` keeps non-ASCII characters (e.g., the
    # middle-dot in "Textured-Smooth · 8 ops") as raw UTF-8 bytes
    # rather than · escapes. JUCE's JSON parser doesn't round-trip
    # the escape back to the same byte sequence the C++ literal uses,
    # so the lookup `getCategory("Textured-Smooth · N ops")` was silently
    # returning nullptr for every N. With raw UTF-8 in the file, the
    # bytes match the literal and the lookup succeeds.
    args.output.write_text(
        json.dumps(index, ensure_ascii=False) + "\n")
    size_kb = args.output.stat().st_size / 1024.0

    if verbose:
        _print_summary(index)
        print(f"\nWrote {args.output} ({size_kb:.1f} KB) in {elapsed:.1f}s.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
