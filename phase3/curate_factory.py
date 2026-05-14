"""Build the curated factory menu — 48 canonical anchors.

The factory tree now ships 2240 preset files, most of them
search-output bulk that feeds the runtime pull index. The user only
wants to *see* a handful of canonical "base forms" in the preset menu;
the rest of the bank is the PullIndex's job, not the UI's.

This script picks 48 anchors and copies them into
`resources/presets/factory/curated/`. PresetManager is configured to
scan only the top-level (32 hand-authored) + `curated/` subdir (16
search-derived), giving the user a 48-entry menu while the PullIndex
keeps the full library accessible.

Selection:
  - 12 family bests — the lowest-objective exemplar of each of
    sawtooth / square / triangle / pulse_pw_4 / organ_1_3_5 /
    bell_beta_1_05 / prime_{2,3,5,7,11,13}.
  - 4 textured-smooth bests — lowest-objective at op-counts
    {4, 8, 12, 16} from the spectral-search bank.

Re-runnable. Wipes and recreates the curated/ directory each time.

Usage:
    python -m phase3.curate_factory
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path
from typing import Any


FACTORY = Path("resources/presets/factory")
CURATED = FACTORY / "curated"
INDEX   = Path("resources/pull_index.json")


# Categories the user explicitly called out as worth seeing as menu
# anchors, plus the full prime set.
FAMILY_CATEGORIES = [
    "Sawtooth",
    "Square",
    "Triangle",
    "Pulse-PW 4",
    "Organ 1/3/5",
    "Bell (stretched)",
    "Prime 2",
    "Prime 3",
    "Prime 5",
    "Prime 7",
    "Prime 11",
    "Prime 13",
]

# Textured-smooth tier sampling. The full sweep is 4..16 op-counts;
# four points across that span give a coherent feel for the texture
# dimension without flooding the menu.
TEXTURED_OPS = [4, 8, 12, 16]


def _load_index() -> dict[str, Any]:
    if not INDEX.exists():
        raise SystemExit(
            f"pull_index.json not found at {INDEX}. "
            f"Run 'python -m phase3.build_pull_index' first."
        )
    return json.loads(INDEX.read_text())


def _best_preset_path(idx: dict[str, Any], category: str) -> Path:
    """Return the relative path of the best-objective exemplar in a
    category, relative to factory/."""
    cat = idx["categories"].get(category)
    if cat is None:
        raise SystemExit(
            f"category '{category}' not in pull index. "
            f"Known: {sorted(idx['categories'].keys())}"
        )
    rel = cat["best_obj_preset"]
    return Path(rel)


def _curate(idx: dict[str, Any]) -> list[Path]:
    """Resolve all chosen exemplar paths against the factory root."""
    picks: list[Path] = []
    for cat in FAMILY_CATEGORIES:
        picks.append(_best_preset_path(idx, cat))
    for n in TEXTURED_OPS:
        picks.append(_best_preset_path(idx, f"Textured-Smooth · {n} ops"))
    return picks


def main(argv: list[str] | None = None) -> int:
    idx = _load_index()
    picks = _curate(idx)

    # Wipe + recreate the curated dir for idempotence.
    if CURATED.exists():
        shutil.rmtree(CURATED)
    CURATED.mkdir(parents=True)

    print(f"Curating {len(picks)} search-derived anchors → {CURATED}/")
    for rel in picks:
        src = FACTORY / rel
        if not src.exists():
            print(f"  ! missing: {src}", file=sys.stderr)
            continue
        # Flatten the per-family subdir layout for the menu — drop the
        # category directory, just keep the filename so the user sees
        # one tidy list.
        dst = CURATED / src.name
        # Collisions are extremely unlikely (file names are
        # category-prefixed) but defend against them anyway.
        if dst.exists():
            i = 1
            while True:
                cand = CURATED / f"{dst.stem}_{i}{dst.suffix}"
                if not cand.exists():
                    dst = cand
                    break
                i += 1
        shutil.copyfile(src, dst)
        print(f"  copied  {rel}  →  curated/{dst.name}")

    n_handauthored = sum(1 for f in FACTORY.glob("*.brocot"))
    n_curated      = sum(1 for f in CURATED.glob("*.brocot"))
    total_visible  = n_handauthored + n_curated
    print(f"\nMenu summary:")
    print(f"  {n_handauthored:>3d} hand-authored (top-level)")
    print(f"  {n_curated:>3d} search-derived (curated/)")
    print(f"  {'─'*3}")
    print(f"  {total_visible:>3d} visible in preset menu after PresetManager re-scan")
    return 0


if __name__ == "__main__":
    sys.exit(main())
