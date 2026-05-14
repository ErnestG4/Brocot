"""Tests for the .brocot v2 writer.

Verifies the output matches what PresetSchema.cpp expects to find:
- name, version, provenance, engine, filter, envelope, lfo, master at
  the top level
- provenance.kind = "family-search" with a search lineage sub-object
- engine.operators is a length-N array of {enabled, path, depth}
  (N = kNumOperators = 64 after P3.12)
- pathMode is "free"
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from phase3.brocot_writer import (
    _human_family_category,
    _slug,
    result_to_brocot_json,
    write_brocot,
)
from phase3.config import Config, ModConfig
from phase3.search import SearchResult


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_result(family: str = "sawtooth", seed: int = 42,
                 objective: float = 0.123) -> SearchResult:
    # 64 mod slots (P3.12 bumped kNumOperators from 16 to 64); 3 enabled,
    # 61 disabled — exercises the writer's pad-disabled-tail behaviour.
    mods = [
        ModConfig(path="R",     depth=1.841, enabled=True),
        ModConfig(path="RLL",   depth=0.86,  enabled=True),
        ModConfig(path="RLLLR", depth=0.90,  enabled=True),
    ] + [ModConfig(path="", depth=0.0, enabled=False) for _ in range(61)]
    cfg = Config(mods=mods, f_carrier=220.0)
    return SearchResult(
        config=cfg, objective=objective, family=family,
        seed=seed, phase1_objective=objective + 0.05,
    )


# ---------------------------------------------------------------------------
# Slug + human-name helpers
# ---------------------------------------------------------------------------

def test_human_family_category():
    assert _human_family_category("sawtooth") == "Sawtooth"
    assert _human_family_category("prime_7") == "Prime 7"
    assert _human_family_category("pulse_pw_4") == "Pulse-PW 4"
    assert _human_family_category("organ_1_3_5") == "Organ 1/3/5"
    assert _human_family_category("bell_beta_1_05") == "Bell (stretched)"


def test_slug_strips_non_alnum_preserving_hyphens():
    # Hyphens preserved so names like "Pulse-PW" survive cleanly.
    assert _slug("Sawtooth (seed 42)") == "sawtooth_seed_42"
    assert _slug("Pulse-PW 4 (seed 99)") == "pulse-pw_4_seed_99"


# ---------------------------------------------------------------------------
# JSON shape
# ---------------------------------------------------------------------------

def test_json_has_required_top_level_keys():
    result = _make_result()
    doc = result_to_brocot_json(result)
    for k in ("name", "version", "provenance",
              "engine", "filter", "envelope", "lfo", "master"):
        assert k in doc, f"missing top-level field: {k}"


def test_json_version_is_v2():
    result = _make_result()
    doc = result_to_brocot_json(result)
    assert doc["version"] == "2.0"


def test_provenance_block_shape():
    result = _make_result(family="prime_7", seed=271, objective=0.084)
    doc = result_to_brocot_json(result, n_phase1=1000, k_top=20, q_max=16)

    prov = doc["provenance"]
    assert prov["kind"] == "family-search"
    assert prov["author"] == "ARS Phase 3"
    assert "Prime 7" in prov["category"]
    assert "Prime 7" in prov["description"]

    s = prov["search"]
    assert s["family"] == "prime_7"
    assert s["seed"] == 271
    assert s["objective"] == pytest.approx(0.084)
    assert s["params"] == {"n_phase1": 1000, "k_top": 20, "q_max": 16}
    assert "timestamp" in s


def test_engine_block_is_free_mode_with_n_operators():
    result = _make_result()
    doc = result_to_brocot_json(result)

    eng = doc["engine"]
    assert eng["polyphony"] == "mono"
    assert eng["octaveShift"] == 0
    assert eng["pathMode"] == "free"

    ops = eng["operators"]
    assert isinstance(ops, list)
    assert len(ops) == 64

    # First three from the make_result fixture should match.
    assert ops[0] == {"enabled": True, "path": "R",     "depth": 1.841}
    assert ops[1] == {"enabled": True, "path": "RLL",   "depth": 0.86}
    assert ops[2] == {"enabled": True, "path": "RLLLR", "depth": 0.90}

    for op in ops[3:]:
        assert op["enabled"] is False
        assert op["depth"] == 0.0


def test_legacy_blocks_have_sane_defaults():
    result = _make_result()
    doc = result_to_brocot_json(result)

    # Legacy v1 blocks should round-trip without surprising the loader.
    assert doc["filter"]["type"] == "lp"
    assert 0.0 <= doc["filter"]["cutoff"] <= 1.0
    assert 0.0 <= doc["filter"]["resonance"] <= 1.0

    env = doc["envelope"]
    for k in ("attack", "decay", "sustain", "release"):
        assert k in env
        assert env[k] >= 0.0

    lfo = doc["lfo"]
    assert lfo["shape"] in ("sine", "triangle", "square", "saw")
    assert lfo["depth"] == 0.0  # inert by default

    assert 0.0 <= doc["master"]["level"] <= 1.0


def test_ars_version_passed_through():
    result = _make_result()
    doc = result_to_brocot_json(result, ars_version="abcdef123456")
    assert doc["provenance"]["ars_version"] == "abcdef123456"


def test_explicit_timestamp_preserved():
    result = _make_result()
    ts = "2026-05-13T01:23:45+00:00"
    doc = result_to_brocot_json(result, timestamp=ts)
    assert doc["provenance"]["search"]["timestamp"] == ts


# ---------------------------------------------------------------------------
# File I/O
# ---------------------------------------------------------------------------

def test_write_brocot_creates_valid_json_file(tmp_path: Path):
    result = _make_result(family="sawtooth", seed=42)
    out_path = write_brocot(result, tmp_path)
    assert out_path.exists()
    assert out_path.suffix == ".brocot"

    # Round-trip through json.loads.
    parsed = json.loads(out_path.read_text())
    assert parsed["version"] == "2.0"
    assert parsed["provenance"]["search"]["family"] == "sawtooth"


def test_write_brocot_creates_output_directory(tmp_path: Path):
    target = tmp_path / "nested" / "factory" / "discovered"
    assert not target.exists()
    result = _make_result()
    out_path = write_brocot(result, target)
    assert out_path.parent == target
    assert target.is_dir()


def test_write_brocot_default_stem(tmp_path: Path):
    result = _make_result(family="prime_7", seed=271)
    out_path = write_brocot(result, tmp_path)
    # Default stem slugs the (human-name "Prime 7 (seed 271)") output.
    assert "prime_7" in out_path.stem
    assert "271" in out_path.stem


def test_write_brocot_explicit_stem(tmp_path: Path):
    result = _make_result()
    out_path = write_brocot(result, tmp_path, file_stem="sawtooth_a")
    assert out_path.stem == "sawtooth_a"


# ---------------------------------------------------------------------------
# Schema-compatibility — match what PresetSchema.cpp will read
# ---------------------------------------------------------------------------

def test_matches_v2_factory_preset_shape(tmp_path: Path):
    """The written JSON should structurally match a factory preset so the
    C++ loader handles it identically."""
    result = _make_result(family="square", seed=99)
    out_path = write_brocot(result, tmp_path)
    parsed = json.loads(out_path.read_text())

    # Required v2 fields
    assert set(parsed.keys()) >= {
        "name", "version", "provenance",
        "engine", "filter", "envelope", "lfo", "master",
    }
    # Provenance always carries `kind` (loader's discriminator).
    assert "kind" in parsed["provenance"]
    # Engine always has 8 operators.
    assert len(parsed["engine"]["operators"]) == 64
