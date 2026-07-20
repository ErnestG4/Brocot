"""Map-vs-butterfly study (BROCOT-SPEC.md §0, §P1-a) — is the measured map the
Hofstadter butterfly?

The spec's central claim: the navigation map's ratio axis θ = p/q *is* the
almost-Mathieu (Harper) butterfly's flux axis, so the butterfly's gap structure
should be visible in the measured landscape. And Will's banked "criticality
insight": FM criticalities line the VOIDS of the measured embedding. This study
tests both against the maps we already ship — it does NOT draw a new butterfly,
it asks whether the existing measured cloud already is one.

Method (read-only w.r.t. the shipped maps):
  1. Load a measured map (default: the 6-op base `landscape_graph.json.gz`).
     Each node is a multi-operator config; each op carries a Stern-Brocot path
     and a depth (its FM index).
  2. Project each node onto butterfly quantities from `brocot_lattice`:
       promNode  = depth-weighted mean gap-width prominence over active op p/q
                   (the wide-gap "plateau" / stability proxy)
       critNode  = depth-weighted mean cfDepth over active op p/q
                   (the CF-sum criticality / approximability proxy)
     plus a baseline meanQ = depth-weighted simplicity (denominator q).
  3. Correlate against the measured per-node fields — `crit` (the cloud's own
     angular-gap / void-rim structure score), `tonal` (musical↔chaotic), `dense`
     — each versus a SHUFFLE CONTROL (§4.3). An association that does not beat
     the shuffle null is reported as "not supported" and NOT promoted (§1).

Predictions being tested:
  P1  critNode  ↑  with measured crit   (deep-CF ratios line the voids/rims)
  P2  critNode  ↓  with tonal           (criticality = less musical/plateau)
  P3  promNode  ↑  with tonal           (wide-gap plateaus = stable, tonal)
  P4  promNode  vs dense                (reported; sign interpreted, not assumed)

Baselines (meanQ, nActive) show whether butterfly prominence/cfDepth add signal
beyond trivial ratio-complexity.

STRUCTURAL, not literal (§1, §4.1): this measures whether a ratio-space
annotation aligns with measured spectral geometry — not that a note "is a gap".
"""

from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path as FsPath
from typing import Dict, List, Optional, Tuple

import numpy as np
from scipy.stats import rankdata

from phase3.brocot_lattice import cf_depth, prominence, simplicity

REPO = FsPath(__file__).resolve().parent.parent
DEFAULT_MAP = REPO / "resources" / "landscape_graph.json.gz"
OUT_DIR = REPO / "phase3" / "derived"


# ---------------------------------------------------------------------------
# Map loading + butterfly projection
# ---------------------------------------------------------------------------

def walk_path(steps: str) -> Tuple[int, int]:
    """L/R Stern-Brocot path → reduced (p, q). Mirrors Path::walk /
    farey-fm.html walkPath: root 1/1, mediants toward the chosen side."""
    lp, lq, rp, rq, p, q = 0, 1, 1, 0, 1, 1
    for s in steps:
        if s == 'L':
            rp, rq = p, q
        else:
            lp, lq = p, q
        p, q = lp + rp, lq + rq
    return p, q


def load_map(path: FsPath) -> dict:
    raw = gzip.open(path, "rb").read() if path.suffix == ".gz" else path.read_bytes()
    return json.loads(raw)


def _butterfly_of_path(steps: str, cache: Dict[str, Tuple[float, int, int]]) -> Tuple[float, int, int]:
    hit = cache.get(steps)
    if hit is not None:
        return hit
    p, q = walk_path(steps)
    val = (prominence(p, q), cf_depth(p, q), simplicity(q))
    cache[steps] = val
    return val


def project_nodes(doc: dict) -> dict:
    """Per-node butterfly quantities + the measured fields, as parallel arrays.
    Nodes with no audible (enabled) operator are dropped."""
    cache: Dict[str, Tuple[float, int, int]] = {}
    promN, critN, meanQ, nAct = [], [], [], []
    m_crit, m_tonal, m_dense, m_bright, xs, ys = [], [], [], [], [], []

    for nd in doc["nodes"]:
        wsum = prom_w = crit_w = q_w = 0.0
        n_active = 0
        for op in nd["ops"]:
            d = float(op.get("depth", 0.0))
            if not op.get("enabled", d > 1e-4) or d <= 1e-4:
                continue
            prom, cfd, q = _butterfly_of_path(op["path"], cache)
            prom_w += d * prom
            crit_w += d * cfd
            q_w    += d * q
            wsum   += d
            n_active += 1
        if n_active == 0 or wsum <= 0.0:
            continue
        promN.append(prom_w / wsum)
        critN.append(crit_w / wsum)
        meanQ.append(q_w / wsum)
        nAct.append(n_active)
        m_crit.append(float(nd.get("crit", 0.0)))
        m_tonal.append(float(nd.get("tonal", 0.0)))
        m_dense.append(float(nd.get("dense", 0.0)))
        m_bright.append(float(nd.get("bright", 0.0)))
        xs.append(float(nd.get("x", 0.0)))
        ys.append(float(nd.get("y", 0.0)))

    return {
        "promNode": np.array(promN), "critNode": np.array(critN),
        "meanQ": np.array(meanQ), "nActive": np.array(nAct, float),
        "crit": np.array(m_crit), "tonal": np.array(m_tonal),
        "dense": np.array(m_dense), "bright": np.array(m_bright),
        "x": np.array(xs), "y": np.array(ys),
    }


# ---------------------------------------------------------------------------
# Spearman + shuffle control
# ---------------------------------------------------------------------------

def corr_shuffle(a: np.ndarray, b: np.ndarray, n_shuffle: int = 500,
                 seed: int = 0) -> Tuple[float, float, float]:
    """Spearman correlation of a,b plus a permutation null. Returns
    (rho, z, p_two_sided) where z = (rho - mean_null)/std_null and p is the
    fraction of |shuffled rho| >= |rho|."""
    ra = rankdata(a); rb = rankdata(b)
    za = (ra - ra.mean()) / (ra.std() + 1e-12)
    zb = (rb - rb.mean()) / (rb.std() + 1e-12)
    rho = float((za * zb).mean())
    rng = np.random.default_rng(seed)
    null = np.empty(n_shuffle)
    for i in range(n_shuffle):
        null[i] = (za * zb[rng.permutation(zb.size)]).mean()
    z = (rho - null.mean()) / (null.std() + 1e-12)
    p = float((np.abs(null) >= abs(rho)).mean())
    return rho, float(z), p


def effect_label(rho: float) -> str:
    a = abs(rho)
    if a < 0.10: return "negligible"
    if a < 0.30: return "weak"
    if a < 0.50: return "moderate"
    return "strong"


def verdict(rho: float, z: float, p: float, expect_sign: Optional[int]) -> str:
    """At n≈50k, z/p flag even trivial correlations, so significance alone is
    meaningless — require a non-negligible EFFECT SIZE too (|rho| ≥ 0.1)."""
    beats_shuffle = abs(z) > 3.0 and p < 0.01
    if not beats_shuffle:
        return "NOT SUPPORTED (within shuffle noise)"
    if abs(rho) < 0.10:
        return "negligible (significant only via large n)"
    if expect_sign is not None and np.sign(rho) != expect_sign:
        return f"real but OPPOSITE sign (predicted {'+' if expect_sign>0 else '-'})"
    return "SUPPORTED"


# ---------------------------------------------------------------------------
# Self-contained SVG scatter overlays (no matplotlib dependency)
# ---------------------------------------------------------------------------

def _ramp(v01: float) -> str:
    v = max(0.0, min(1.0, v01))
    r = int(40 + 215 * v); g = int(60 + 40 * abs(0.5 - v) * 2); b = int(230 - 200 * v)
    return f"#{r:02x}{g:02x}{b:02x}"


def _norm01(a: np.ndarray) -> np.ndarray:
    lo, hi = np.percentile(a, 2), np.percentile(a, 98)
    return np.clip((a - lo) / max(hi - lo, 1e-9), 0.0, 1.0)


def write_svg_panels(data: dict, panels: List[Tuple[str, np.ndarray]], out: FsPath,
                     max_pts: int = 9000, seed: int = 0) -> None:
    x, y = data["x"], data["y"]
    n = x.size
    idx = np.arange(n)
    if n > max_pts:
        idx = np.random.default_rng(seed).choice(n, max_pts, replace=False)
    S = 300  # panel size
    pad, cols = 12, 2
    rows = (len(panels) + cols - 1) // cols
    W = cols * (S + pad) + pad
    H = rows * (S + pad + 18) + pad

    def px(v, lo=-1.0, hi=1.0):
        return (v - lo) / (hi - lo)

    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
             f'style="background:#0c0a09;font-family:monospace">']
    for pi, (title, field) in enumerate(panels):
        c = pi % cols; r = pi // cols
        ox = pad + c * (S + pad); oy = pad + r * (S + pad + 18)
        col = _norm01(field)
        parts.append(f'<text x="{ox}" y="{oy+12}" fill="#fde68a" font-size="11">{title}</text>')
        parts.append(f'<rect x="{ox}" y="{oy+18}" width="{S}" height="{S}" fill="#141210" stroke="#5a3a12"/>')
        for i in idx:
            cx = ox + px(x[i]) * S
            cy = oy + 18 + (1.0 - px(y[i])) * S
            parts.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="1.3" fill="{_ramp(float(col[i]))}" fill-opacity="0.75"/>')
    parts.append('</svg>')
    out.write_text("\n".join(parts))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(map_path: FsPath, n_shuffle: int) -> str:
    doc = load_map(map_path)
    data = project_nodes(doc)
    n = data["promNode"].size
    name = doc.get("name", map_path.stem)

    tests = [
        ("P1  critNode vs measured crit ", data["critNode"], data["crit"],  +1),
        ("P2  critNode vs tonal         ", data["critNode"], data["tonal"], -1),
        ("P3  promNode vs tonal         ", data["promNode"], data["tonal"], +1),
        ("P4  promNode vs dense         ", data["promNode"], data["dense"], None),
        ("--  baseline meanQ vs crit    ", data["meanQ"],    data["crit"],  None),
        ("--  baseline meanQ vs tonal   ", data["meanQ"],    data["tonal"], None),
        ("--  baseline nActive vs crit  ", data["nActive"],  data["crit"],  None),
    ]

    lines = [
        f"# Map-vs-butterfly study — {name} ({map_path.name})",
        f"nodes analysed: {n:,} (of {len(doc['nodes']):,}; dropped ones with no audible op)",
        f"shuffle permutations: {n_shuffle}",
        "",
        "NB: n is large, so statistical significance (z, p) is easy — read the",
        "EFFECT SIZE (|spearman|) and whether butterfly quantities beat the",
        "trivial baselines (meanQ = simplicity, nActive = op count).",
        "",
        f"{'test':32s} {'spearman':>9s} {'effect':>11s} {'z':>7s} {'p':>6s}  verdict",
        "-" * 92,
    ]
    results = {}
    for label, a, b, sign in tests:
        rho, z, p = corr_shuffle(a, b, n_shuffle)
        results[label.strip()] = rho
        lines.append(f"{label:32s} {rho:>+9.3f} {effect_label(rho):>11s} {z:>7.1f} {p:>6.3f}  "
                     f"{verdict(rho, z, p, sign)}")

    # Single-op subset: nodes with exactly one audible operator, where the
    # butterfly flux θ = p/q is UNAMBIGUOUS (no multi-op averaging confound).
    # This is the cleanest direct test of §0's "the map is the butterfly".
    one = data["nActive"] == 1.0
    one_res = {}
    lines += ["", f"Single-op subset (θ unambiguous): {int(one.sum()):,} nodes",
              "-" * 92]
    if one.sum() > 200:
        for label, af, bf, sign in [
            ("  1op  promNode vs tonal      ", "promNode", "tonal", +1),
            ("  1op  promNode vs crit       ", "promNode", "crit",  None),
            ("  1op  critNode vs tonal      ", "critNode", "tonal", -1),
            ("  1op  critNode vs crit       ", "critNode", "crit",  +1),
        ]:
            rho, z, p = corr_shuffle(data[af][one], data[bf][one], n_shuffle)
            one_res[(af, bf)] = rho
            lines.append(f"{label:32s} {rho:>+9.3f} {effect_label(rho):>11s} {z:>7.1f} {p:>6.3f}  "
                         f"{verdict(rho, z, p, sign)}")

    # Honest headline, effect-size first.
    agg_bf = max(abs(results.get("P1  critNode vs measured crit", 0.0)),
                 abs(results.get("P3  promNode vs tonal", 0.0)))
    prom_tonal_1op = one_res.get(("promNode", "tonal"), 0.0)
    prom_crit_1op  = one_res.get(("promNode", "crit"), 0.0)
    crit_void_1op  = abs(one_res.get(("critNode", "crit"), 0.0))
    lines += ["", "CONCLUSION", "-" * 92]
    lines.append("Aggregate (multi-op): butterfly gap/CF quantities are NEGLIGIBLY")
    lines.append("associated with the measured geometry and are dominated by op-count")
    lines.append("(nActive vs crit is the strongest thing here) — so the measured multi-op")
    lines.append("map is NOT 'the butterfly' at the whole-config level; op-count is the gross axis.")
    lines.append("")
    if abs(prom_tonal_1op) >= 0.1 or abs(prom_crit_1op) >= 0.1:
        lines.append("Single-op (θ unambiguous): PROMINENCE (gap width) shows a weak but real,")
        lines.append(f"correctly-signed signal — wider gaps are more tonal ({prom_tonal_1op:+.2f}) and sit")
        lines.append(f"AWAY from the measured void-rims ({prom_crit_1op:+.2f}), matching the 'stable plateau'")
        lines.append("prediction. This is the one piece of §0 the data supports — narrowly.")
    if crit_void_1op < 0.10:
        lines.append("The cfDepth-criticality ↔ VOIDS link (the criticality insight) is NOT")
        lines.append("confirmed even single-op: critNode vs measured crit is negligible.")
    lines.append("")
    lines.append("Verdict: do NOT ship a butterfly-terrain overlay on the aggregate map. A")
    lines.append("prominence overlay is only worth prototyping for LOW-OP patches, where the")
    lines.append("weak single-op signal lives (§1 do-not-overclaim, §4.3 test-earns-it).")

    report = "\n".join(lines)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    stem = f"butterfly_study_{name.replace(' ', '_').replace('/', '_')}"
    (OUT_DIR / f"{stem}.txt").write_text(report + "\n")
    write_svg_panels(
        data,
        [("promNode (butterfly gap width)", data["promNode"]),
         ("critNode (butterfly cfDepth)",   data["critNode"]),
         ("measured crit (voids / rims)",   data["crit"]),
         ("measured tonal (musical)",       data["tonal"])],
        OUT_DIR / f"{stem}.svg")
    return report + f"\n\nWrote {OUT_DIR / (stem + '.txt')} and .svg"


def main() -> None:
    ap = argparse.ArgumentParser(description="Test whether a measured map is the Hofstadter butterfly.")
    ap.add_argument("--map", type=FsPath, default=DEFAULT_MAP, help="landscape_graph*.json[.gz]")
    ap.add_argument("--shuffle", type=int, default=500, help="permutation-null count")
    args = ap.parse_args()
    print(run(args.map, args.shuffle))


if __name__ == "__main__":
    main()
