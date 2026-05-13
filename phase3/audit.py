"""Quick audit of a generated .brocot tree.

Reads every .brocot under a root directory, prints a per-family
summary table (count, objective range, mean depth of enabled
operators, mean path length). Helps eyeball whether the search
landed somewhere musically plausible before committing the output
to the factory tree.

Usage:
    python -m phase3.audit resources/presets/factory/discovered
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path


def _stats(presets: list[dict]) -> dict:
    objs, depths, path_lens, enabled_counts = [], [], [], []
    for p in presets:
        prov = p.get("provenance", {})
        search = prov.get("search", {})
        if "objective" in search:
            objs.append(float(search["objective"]))
        ops = p.get("engine", {}).get("operators", [])
        n_enabled = sum(1 for o in ops if o.get("enabled"))
        enabled_counts.append(n_enabled)
        for o in ops:
            if not o.get("enabled"):
                continue
            depths.append(float(o.get("depth", 0)))
            path_lens.append(len(o.get("path", "")))
    return {
        "n":              len(presets),
        "obj_min":        min(objs) if objs else None,
        "obj_max":        max(objs) if objs else None,
        "obj_median":     sorted(objs)[len(objs) // 2] if objs else None,
        "depth_mean":     sum(depths) / max(1, len(depths)),
        "path_len_mean":  sum(path_lens) / max(1, len(path_lens)),
        "enabled_mean":   sum(enabled_counts) / max(1, len(enabled_counts)),
    }


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: audit.py <directory>", file=sys.stderr)
        return 1

    root = Path(argv[1])
    if not root.exists():
        print(f"not found: {root}", file=sys.stderr)
        return 1

    by_family: dict[str, list[dict]] = defaultdict(list)
    total = 0
    for f in sorted(root.rglob("*.brocot")):
        try:
            d = json.loads(f.read_text())
        except json.JSONDecodeError as e:
            print(f"  ! {f}: {e}", file=sys.stderr)
            continue
        fam = d.get("provenance", {}).get("search", {}).get("family", "(unknown)")
        by_family[fam].append(d)
        total += 1

    print(f"\n{total} presets across {len(by_family)} families "
          f"under {root}\n")
    print(f"  {'family':<22s}{'n':>4s}{'obj min':>10s}{'med':>8s}"
          f"{'max':>8s}{'depth̄':>9s}{'plen̄':>8s}{'on̄':>6s}")
    print("  " + "-" * 65)

    for fam in sorted(by_family.keys()):
        s = _stats(by_family[fam])

        def fmt(v):
            return f"{v:.3f}" if v is not None else "—"

        print(f"  {fam:<22s}{s['n']:>4d}"
              f"{fmt(s['obj_min']):>10s}"
              f"{fmt(s['obj_median']):>8s}"
              f"{fmt(s['obj_max']):>8s}"
              f"{s['depth_mean']:>9.2f}"
              f"{s['path_len_mean']:>8.2f}"
              f"{s['enabled_mean']:>6.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
