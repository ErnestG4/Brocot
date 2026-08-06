"""Naming/resolution contracts for the per-map jumps pipeline.

Pure logic only — imports phase3.jumps_naming (no torch), the module
gpu_jumps.py itself uses for every per-map filename: checkpoint
auto-resolution for `mine`, the eval-JSON name `train` writes, and the
emit-summary name `verify_and_emit` writes (pinned here against the
filenames the shipped fleet run actually produced in phase3/derived/).
"""

from __future__ import annotations

import pytest

from phase3.jumps_naming import (
    dataset_name,
    emit_summary_name,
    eval_json_name,
    jumpnet_name,
    map_tag,
    mined_name,
    resolve_jumpnet,
)

FLEET_TAGS = ["4", "4mix", "6mix", "6saw", "6sine", "6sqr", "6tri",
              "8", "8mix", "16", "16mix", "32", "32mix", "64", "64mix"]


# ---------------------------------------------------------------------------
# map_tag
# ---------------------------------------------------------------------------

def test_map_tag_base():
    assert map_tag("landscape_graph.json.gz") == ""


def test_map_tag_64mix():
    assert map_tag("landscape_graph_64mix.json.gz") == "64mix"


@pytest.mark.parametrize("tag", FLEET_TAGS)
def test_map_tag_fleet(tag):
    assert map_tag(f"landscape_graph_{tag}.json.gz") == tag


def test_map_tag_accepts_full_paths():
    assert map_tag("resources/landscape_graph_8.json.gz") == "8"
    assert map_tag("/abs/path/to/landscape_graph.json.gz") == ""


def test_map_tag_uncompressed_json():
    assert map_tag("landscape_graph_16.json") == "16"


def test_map_tag_foreign_name_stays_distinct():
    # A non-conventional map must not collide with the base map's artefacts.
    assert map_tag("mymap.json.gz") == "mymap"


# ---------------------------------------------------------------------------
# Checkpoint / dataset / mined names
# ---------------------------------------------------------------------------

def test_jumpnet_name_base():
    assert jumpnet_name("landscape_graph.json.gz") == "jumpnet.pt"


def test_jumpnet_name_per_map():
    assert jumpnet_name("landscape_graph_64mix.json.gz") == "jumpnet_64mix.pt"
    assert jumpnet_name("landscape_graph_6sine.json.gz") == "jumpnet_6sine.pt"


def test_dataset_and_mined_names():
    assert dataset_name("landscape_graph_32mix.json.gz") == "jumps_ds_32mix.npz"
    assert mined_name("landscape_graph_32mix.json.gz") == "jumps_mined_32mix.npz"
    # Base-map mined name matches the shipped fleet run's file.
    assert mined_name("landscape_graph.json.gz") == "jumps_mined.npz"


# ---------------------------------------------------------------------------
# Eval JSON name (train --out → eval file)
# ---------------------------------------------------------------------------

def test_eval_json_name_base():
    assert eval_json_name("jumpnet.pt") == "jumpnet_eval.json"


def test_eval_json_name_per_map():
    assert eval_json_name("jumpnet_64mix.pt") == "jumpnet_eval_64mix.json"
    assert eval_json_name("jumpnet_16.pt") == "jumpnet_eval_16.json"


def test_eval_json_name_accepts_paths_and_foreign_stems():
    assert eval_json_name("phase3/derived/jumpnet_8mix.pt") \
        == "jumpnet_eval_8mix.json"
    assert eval_json_name("custom.pt") == "custom_eval.json"


# ---------------------------------------------------------------------------
# Emit summary name — pins verify_and_emit's mapping (it calls this helper)
# ---------------------------------------------------------------------------

def test_emit_summary_name_base():
    assert emit_summary_name("landscape_graph.json.gz") == "jumps_emit.json"


@pytest.mark.parametrize("tag", FLEET_TAGS)
def test_emit_summary_name_fleet(tag):
    # These exact filenames exist in phase3/derived/ from the fleet run.
    assert emit_summary_name(f"landscape_graph_{tag}.json.gz") \
        == f"jumps_emit_{tag}.json"


# ---------------------------------------------------------------------------
# Checkpoint auto-resolution (mine's default when --net is omitted)
# ---------------------------------------------------------------------------

def test_resolve_falls_back_to_base(tmp_path):
    (tmp_path / "jumpnet.pt").write_bytes(b"x")
    path, per_map = resolve_jumpnet("landscape_graph_64mix.json.gz", tmp_path)
    assert path == tmp_path / "jumpnet.pt"
    assert per_map is False


def test_resolve_prefers_per_map_finetune(tmp_path):
    (tmp_path / "jumpnet.pt").write_bytes(b"x")
    (tmp_path / "jumpnet_64mix.pt").write_bytes(b"x")
    path, per_map = resolve_jumpnet("landscape_graph_64mix.json.gz", tmp_path)
    assert path == tmp_path / "jumpnet_64mix.pt"
    assert per_map is True


def test_resolve_base_map_never_uses_finetune(tmp_path):
    (tmp_path / "jumpnet.pt").write_bytes(b"x")
    path, per_map = resolve_jumpnet("landscape_graph.json.gz", tmp_path)
    assert path == tmp_path / "jumpnet.pt"
    assert per_map is False


def test_resolve_ignores_other_maps_finetunes(tmp_path):
    (tmp_path / "jumpnet.pt").write_bytes(b"x")
    (tmp_path / "jumpnet_64mix.pt").write_bytes(b"x")
    path, per_map = resolve_jumpnet("landscape_graph_16.json.gz", tmp_path)
    assert path == tmp_path / "jumpnet.pt"
    assert per_map is False
