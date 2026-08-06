"""Filename conventions for the per-map jumps pipeline — pure logic, no torch.

`gpu_jumps.py` imports torch (and the GPU venv) at module level, so every
naming/resolution rule the CPU test suite needs to pin lives here instead:
map-tag extraction, per-map checkpoint/dataset/mined/eval names, the emit
summary name `verify_and_emit` writes, and the mine-time checkpoint
auto-resolution (per-map fine-tune if present, else the base net).

Conventions (all under phase3/derived/):
  landscape_graph.json.gz        tag ''      jumpnet.pt        jumps_emit.json
  landscape_graph_64mix.json.gz  tag '64mix' jumpnet_64mix.pt  jumps_emit_64mix.json
"""

from __future__ import annotations

from pathlib import Path
from typing import Tuple

MAP_PREFIX = "landscape_graph"
MAP_SUFFIX = ".json.gz"


def map_tag(map_name: str) -> str:
    """'landscape_graph_64mix.json.gz' → '64mix'; the base map → ''.

    Accepts full paths; anything not matching the landscape_graph pattern
    returns its own stem so derived names stay distinct rather than
    colliding with the base map's.
    """
    stem = Path(map_name).name
    if stem.endswith(MAP_SUFFIX):
        stem = stem[: -len(MAP_SUFFIX)]
    elif stem.endswith(".json"):
        stem = stem[: -len(".json")]
    if stem == MAP_PREFIX:
        return ""
    if stem.startswith(MAP_PREFIX + "_"):
        return stem[len(MAP_PREFIX) + 1:]
    return stem


def _suffixed(base: str, ext: str, tag: str) -> str:
    return f"{base}_{tag}{ext}" if tag else f"{base}{ext}"


def jumpnet_name(map_name: str) -> str:
    """Checkpoint filename for a map: 'jumpnet_64mix.pt'; base → 'jumpnet.pt'."""
    return _suffixed("jumpnet", ".pt", map_tag(map_name))


def eval_json_name(ckpt_name: str) -> str:
    """Eval verdict filename for a checkpoint: 'jumpnet.pt' →
    'jumpnet_eval.json', 'jumpnet_64mix.pt' → 'jumpnet_eval_64mix.json',
    any other stem → '<stem>_eval.json'."""
    stem = Path(ckpt_name).name
    if stem.endswith(".pt"):
        stem = stem[: -len(".pt")]
    if stem == "jumpnet":
        return "jumpnet_eval.json"
    if stem.startswith("jumpnet_"):
        return f"jumpnet_eval_{stem[len('jumpnet_'):]}.json"
    return f"{stem}_eval.json"


def dataset_name(map_name: str) -> str:
    """Per-map fine-tune dataset: 'jumps_ds_64mix.npz'; base → 'jumps_ds.npz'
    (the base map's full dataset keeps its historical 'jumps_dataset.npz')."""
    return _suffixed("jumps_ds", ".npz", map_tag(map_name))


def mined_name(map_name: str) -> str:
    """Mined-candidates filename: 'jumps_mined_64mix.npz'; base →
    'jumps_mined.npz' (matches the shipped fleet run)."""
    return _suffixed("jumps_mined", ".npz", map_tag(map_name))


def emit_summary_name(map_name: str) -> str:
    """The summary filename verify_and_emit writes — pinned by tests.
    'landscape_graph_64mix.json.gz' → 'jumps_emit_64mix.json';
    base map → 'jumps_emit.json'."""
    stem = (Path(map_name).name
            .replace(MAP_PREFIX, "jumps_emit")
            .replace(MAP_SUFFIX, "")) or "jumps_emit"
    return f"{stem}.json"


def resolve_jumpnet(map_name: str, derived: Path) -> Tuple[Path, bool]:
    """Checkpoint to mine with: the map's own fine-tune if it exists on disk,
    else the base net. Returns (path, is_per_map_finetune)."""
    per_map = derived / jumpnet_name(map_name)
    base = derived / "jumpnet.pt"
    if map_tag(map_name) and per_map.exists():
        return per_map, True
    return base, False
