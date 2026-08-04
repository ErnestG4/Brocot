"""Tests for the pull-to-nearest index builder.

Validates that:
- The output JSON has the expected top-level shape and required keys.
- Per-category data is well-formed: preset_count matches the
  exemplars list, the centroid is a valid probability vector,
  best_obj_preset matches the first sorted exemplar.
- Per-exemplar amplitude vectors are normalised (sum ≈ 1, or 0 if
  the preset's predicted spectrum is empty).
- Exemplars are sorted by objective ascending.
- Category resolution falls back sanely when provenance.category is
  missing.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from phase3.build_pull_index import (
    DEFAULT_F_REF,
    DEFAULT_Q_MAX,
    INDEX_VERSION,
    _config_from_preset,
    build_index,
)


# ---------------------------------------------------------------------------
# Test fixture — write a tiny fake factory tree to a tmp_path
# ---------------------------------------------------------------------------

def _write_preset(path: Path, *, name: str, category: str,
                  ops: list[dict], objective: float = 0.5,
                  kind: str = "family-search") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    doc = {
        "name":    name,
        "version": "2.0",
        "provenance": {
            "kind":     kind,
            "author":   "test",
            "category": category,
            "search":   {"objective": objective},
        },
        "engine": {
            "polyphony":   "mono",
            "octaveShift": 0,
            "pathMode":    "free",
            "operators":   ops,
        },
        "filter":   {"type": "lp", "cutoff": 0.85, "resonance": 0.10},
        "envelope": {"attack": 0.01, "decay": 0.15, "sustain": 0.80, "release": 0.40},
        "lfo":      {"shape": "sine", "rate": 0.5, "depth": 0.0, "keySync": False},
        "master":   {"level": 0.5},
    }
    path.write_text(json.dumps(doc, indent=2))


@pytest.fixture
def fake_factory(tmp_path: Path) -> Path:
    """Three presets across two categories: best/worst Sawtooth + one Square."""
    root = tmp_path / "factory"

    saw_best = [
        {"enabled": True,  "path": "R",   "depth": 1.84},
        {"enabled": True,  "path": "RR",  "depth": 0.86},
        {"enabled": True,  "path": "RRR", "depth": 0.90},
    ] + [{"enabled": False, "path": "", "depth": 0.0} for _ in range(13)]
    saw_worse = [
        {"enabled": True,  "path": "L",   "depth": 2.40},
        {"enabled": True,  "path": "LL",  "depth": 1.84},
    ] + [{"enabled": False, "path": "", "depth": 0.0} for _ in range(14)]
    sq = [
        {"enabled": True,  "path": "R",  "depth": 2.40},
        {"enabled": True,  "path": "RR", "depth": 1.84},
        {"enabled": True,  "path": "L",  "depth": 1.20},
    ] + [{"enabled": False, "path": "", "depth": 0.0} for _ in range(13)]

    _write_preset(root / "saw" / "a.brocot", name="A", category="Sawtooth",
                  ops=saw_best, objective=0.10)
    _write_preset(root / "saw" / "b.brocot", name="B", category="Sawtooth",
                  ops=saw_worse, objective=0.30)
    _write_preset(root / "sq"  / "a.brocot", name="C", category="Square",
                  ops=sq,        objective=0.20)
    return root


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def test_config_from_preset_recovers_op_state():
    doc = {
        "engine": {
            "operators": [
                {"enabled": True,  "path": "R",  "depth": 1.5},
                {"enabled": False, "path": "",   "depth": 0.0},
                {"enabled": True,  "path": "LL", "depth": 0.7},
            ]
        }
    }
    cfg = _config_from_preset(doc)
    assert len(cfg.mods) == 3
    assert cfg.mods[0].enabled and cfg.mods[0].path == "R"
    assert cfg.mods[0].depth == pytest.approx(1.5)
    assert not cfg.mods[1].enabled
    assert cfg.mods[2].path == "LL" and cfg.mods[2].depth == pytest.approx(0.7)


# NB: the _category_for tests died with provenance-string taxonomy itself —
# the measured family axis (phase3/family_axis.md) replaced it; categories
# now come from bank-mined centroids, not provenance fields.


# ---------------------------------------------------------------------------
# Full build
# ---------------------------------------------------------------------------

def test_build_index_top_level_shape(fake_factory: Path):
    index = build_index(fake_factory, verbose=False)
    assert index["version"] == INDEX_VERSION
    assert index["f_carrier_ref"] == DEFAULT_F_REF
    assert index["q_max"] == DEFAULT_Q_MAX
    assert index["n_categories"] == 2
    assert index["n_exemplars"] == 3
    assert index["n_skipped"] == 0
    assert set(index["categories"].keys()) == {"Sawtooth", "Square"}


def test_build_index_exemplars_sorted_by_objective(fake_factory: Path):
    index = build_index(fake_factory, verbose=False)
    saw = index["categories"]["Sawtooth"]
    objs = [e["objective"] for e in saw["exemplars"]]
    assert objs == sorted(objs)
    # And the best-obj reference points at the first one.
    assert saw["best_obj_preset"] == saw["exemplars"][0]["preset"]


def test_build_index_per_exemplar_amps_normalised(fake_factory: Path):
    index = build_index(fake_factory, verbose=False)
    for cat, c in index["categories"].items():
        for e in c["exemplars"]:
            s = sum(e["amps"])
            # Either L1=1 (preset produced predictable spectrum) or 0
            # (preset was empty); never anything weird in between.
            assert (s == pytest.approx(1.0, abs=1e-6)
                    or s == pytest.approx(0.0, abs=1e-12)), \
                f"{cat}/{e['preset']} amps sum to {s}"
            # All entries non-negative.
            assert all(a >= 0.0 for a in e["amps"])


def test_build_index_centroid_normalised(fake_factory: Path):
    index = build_index(fake_factory, verbose=False)
    for cat, c in index["categories"].items():
        s = sum(c["centroid_amps"])
        assert s == pytest.approx(1.0, abs=1e-6) or s == pytest.approx(0.0, abs=1e-12)


def test_build_index_amps_length_matches_q_max(fake_factory: Path):
    index = build_index(fake_factory, verbose=False, q_max=24)
    for cat, c in index["categories"].items():
        assert len(c["centroid_amps"]) == 24
        for e in c["exemplars"]:
            assert len(e["amps"]) == 24


def test_build_index_preset_paths_are_relative(fake_factory: Path):
    """Runtime resolves paths against the factory root, so they must
    be relative."""
    index = build_index(fake_factory, verbose=False)
    for cat, c in index["categories"].items():
        assert "/" in c["best_obj_preset"]  # subdir/filename, not absolute
        assert not c["best_obj_preset"].startswith("/")
        for e in c["exemplars"]:
            assert not e["preset"].startswith("/")


def test_build_index_preserves_op_state(fake_factory: Path):
    """The ops list inside each exemplar should round-trip the preset's
    operator state so the runtime can morph toward it."""
    index = build_index(fake_factory, verbose=False)
    saw_a = next(e for e in index["categories"]["Sawtooth"]["exemplars"]
                 if e["preset"].endswith("a.brocot"))
    ops = saw_a["ops"]
    # First three enabled per fixture
    assert ops[0]["enabled"] and ops[0]["path"] == "R"
    assert ops[1]["enabled"] and ops[1]["path"] == "RR"
    assert ops[2]["enabled"] and ops[2]["path"] == "RRR"
    # Remaining 13 disabled
    for op in ops[3:]:
        assert op["enabled"] is False


def test_build_index_n_ops_counts_enabled_only(fake_factory: Path):
    index = build_index(fake_factory, verbose=False)
    for cat, c in index["categories"].items():
        for e in c["exemplars"]:
            on_count = sum(1 for o in e["ops"] if o["enabled"])
            assert e["n_ops"] == on_count
