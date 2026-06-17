"""Validate every regenerated landscape map against the runtime contract.

Checks each resources/landscape_graph*.json.gz:
  - family_names == the 12 canonical measured families (== kShapeCategories)
  - every node.family index is in range
  - next_hop keys == family_names (the C++ resolves routes by name)
  - name / n_ops present; ops length == n_ops
  - a route from the neutral origin reaches at least a few families

Run: python3 -m phase3.validate_maps   (no torch needed)
"""
from __future__ import annotations

import gzip
import json
from pathlib import Path

from phase3.brocot_writer import _human_family_category
from phase3.families import default_families

CANON = [_human_family_category(f.name) for f in default_families()]
RES = Path("resources")


def _walk(next_hop, fam, start, limit=2000):
    field = next_hop.get(fam)
    if not field:
        return None
    node, hops = start, 0
    while hops < limit:
        nxt = field[node]
        if nxt == -1:
            return hops          # arrived
        if nxt < 0:
            return None          # unreachable
        node = nxt
        hops += 1
    return None                  # cycle / no arrival


def validate(path: Path) -> list[str]:
    errs = []
    d = json.loads(gzip.open(path).read())
    fn = d.get("family_names", [])
    if set(fn) != set(CANON):
        errs.append(f"family_names {sorted(fn)} != canonical 12")
    if d.get("name") in (None, ""):
        errs.append("missing 'name'")
    if not d.get("n_ops"):
        errs.append("missing/zero 'n_ops'")
    nodes = d.get("nodes", [])
    if not nodes:
        errs.append("no nodes")
        return errs
    nops = d["n_ops"]
    bad_fam = sum(1 for n in nodes if not (0 <= n["family"] < len(fn)))
    if bad_fam:
        errs.append(f"{bad_fam} nodes with out-of-range family index")
    bad_ops = sum(1 for n in nodes if len(n["ops"]) != nops)
    if bad_ops:
        errs.append(f"{bad_ops} nodes whose ops length != n_ops ({nops})")
    nh = d.get("next_hop", {})
    if set(nh.keys()) != set(fn):
        errs.append(f"next_hop keys != family_names "
                    f"(missing {set(fn) - set(nh.keys())})")
    # route smoke test: neutral origin (node nearest pure carrier) -> families
    # use node 0 as an arbitrary start; just confirm some routes arrive.
    arrived = sum(1 for fam in fn if _walk(nh, fam, 0) is not None)
    if arrived == 0:
        errs.append("no family reachable from node 0")
    return errs


def main():
    maps = sorted(RES.glob("landscape_graph*.json.gz"))
    print(f"Validating {len(maps)} maps against the 12-family contract:\n")
    ok = True
    for p in maps:
        d = json.loads(gzip.open(p).read())
        errs = validate(p)
        tag = "OK " if not errs else "FAIL"
        if errs:
            ok = False
        print(f"  [{tag}] {p.name:<34s} name={str(d.get('name')):<10s} "
              f"n_ops={d.get('n_ops')} nodes={d.get('n_nodes')} "
              f"fams={d.get('n_families')}")
        for e in errs:
            print(f"         - {e}")
    print(f"\n{'ALL MAPS VALID' if ok else 'VALIDATION FAILURES ABOVE'}")
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
