"""Reproducible driver: (re)build every landscape map from its GPU bank.

Replaces the hand-run sequence of `gpu_landscape classify` + `gpu_paths
gradients` invocations that produced the 13 shipped maps. For each variant it:
  1. classifies the bank onto the MEASURED family axis (phase3.family_axis
     mined centroids — shared across all maps, so the family axis is stable),
  2. builds the runtime gradient graph (k-NN + per-family next-hop fields).

The family centroids must be mined first:
    phase3/.venv-gpu/bin/python -m phase3.family_axis mine

Then build all maps (or a subset):
    phase3/.venv-gpu/bin/python -m phase3.build_maps              # all 13
    phase3/.venv-gpu/bin/python -m phase3.build_maps 4 6sine      # just these

Run under phase3/.venv-gpu (torch). Reads derived/gpu_bank*.npz, writes
derived/landscape_labels*.npz + resources/landscape_graph*.json.gz.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

DERIVED = Path("phase3/derived")
RES = Path("resources")

# variant key -> (bank, labels, graph, gradients --name, subsample nodes).
# name=None ⇒ gpu_paths defaults to "<n_ops>-op"; makeMapName (C++) normalises
# the "<N>op <desc>" embedded names to "<N>-op <desc>" selector labels.
VARIANTS: dict[str, tuple[str, str, str, str | None, int]] = {
    "":      ("gpu_bank.npz",      "landscape_labels.npz",      "landscape_graph.json.gz",      None,       50_000),
    "4":     ("gpu_bank_4.npz",    "landscape_labels_4.npz",    "landscape_graph_4.json.gz",    None,       30_000),
    "8":     ("gpu_bank_8.npz",    "landscape_labels_8.npz",    "landscape_graph_8.json.gz",    None,       30_000),
    "16":    ("gpu_bank_16.npz",   "landscape_labels_16.npz",   "landscape_graph_16.json.gz",   None,       30_000),
    "32":    ("gpu_bank_32.npz",   "landscape_labels_32.npz",   "landscape_graph_32.json.gz",   None,       30_000),
    "64":    ("gpu_bank_64.npz",   "landscape_labels_64.npz",   "landscape_graph_64.json.gz",   None,       30_000),
    "4mix":  ("gpu_bank_4mix.npz", "landscape_labels_4mix.npz", "landscape_graph_4mix.json.gz", "4op mix",  30_000),
    "6mix":  ("gpu_bank_6mix.npz", "landscape_labels_6mix.npz", "landscape_graph_6mix.json.gz", "6op mix",  30_000),
    "8mix":  ("gpu_bank_8mix.npz", "landscape_labels_8mix.npz", "landscape_graph_8mix.json.gz", "8op mix",  30_000),
    "16mix": ("gpu_bank_16mix.npz","landscape_labels_16mix.npz","landscape_graph_16mix.json.gz","16op mix", 30_000),
    "32mix": ("gpu_bank_32mix.npz","landscape_labels_32mix.npz","landscape_graph_32mix.json.gz","32op mix", 30_000),
    "64mix": ("gpu_bank_64mix.npz","landscape_labels_64mix.npz","landscape_graph_64mix.json.gz","64op mix", 30_000),
    "6saw":  ("gpu_bank_6saw.npz", "landscape_labels_6saw.npz", "landscape_graph_6saw.json.gz", "6op saw",  30_000),
    "6sine": ("gpu_bank_6sine.npz","landscape_labels_6sine.npz","landscape_graph_6sine.json.gz","6op sine", 30_000),
    "6sqr":  ("gpu_bank_6sqr.npz", "landscape_labels_6sqr.npz", "landscape_graph_6sqr.json.gz", "6op sqr",  30_000),
    "6tri":  ("gpu_bank_6tri.npz", "landscape_labels_6tri.npz", "landscape_graph_6tri.json.gz", "6op tri",  30_000),
    "6pls":  ("gpu_bank_6pls.npz", "landscape_labels_6pls.npz", "landscape_graph_6pls.json.gz", "6op pls",  30_000),
}

K = 12              # k-NN graph degree (gpu_paths default)


def build(keys: list[str]) -> None:
    from phase3.gpu_landscape import classify
    from phase3.gpu_paths import gradients

    t0 = time.time()
    for i, key in enumerate(keys, 1):
        bank, labels, graph, name, nodes = VARIANTS[key]
        bankp, labelsp, graphp = DERIVED / bank, DERIVED / labels, RES / graph
        if not bankp.exists():
            print(f"[{i}/{len(keys)}] !! skip '{key or '6'}': missing {bankp}")
            continue
        print(f"\n[{i}/{len(keys)}] === map '{key or '6'}'  bank={bank}  "
              f"nodes={nodes:,}  name={name!r} ===")
        classify(bank_npz=bankp, out=labelsp)
        gradients(nodes, K, out=graphp, bank_npz=bankp, labels_npz=labelsp, name=name)
    print(f"\nAll {len(keys)} maps built in {(time.time()-t0)/60:.1f} min.")


def main():
    keys = sys.argv[1:] or list(VARIANTS.keys())
    unknown = [k for k in keys if k not in VARIANTS]
    if unknown:
        print(f"unknown variant(s): {unknown}\nvalid: {list(VARIANTS)}")
        sys.exit(2)
    build(keys)


if __name__ == "__main__":
    main()
