"""Tests for the map-vs-butterfly study helpers (the stats/plotting run against
the real 50k map and aren't unit-tested here)."""

from __future__ import annotations

import numpy as np

from phase3.butterfly_map_study import corr_shuffle, project_nodes, walk_path


def test_walk_path_matches_stern_brocot():
    assert walk_path("") == (1, 1)
    assert walk_path("R") == (2, 1)
    assert walk_path("L") == (1, 2)
    assert walk_path("RLL") == (4, 3)
    assert walk_path("RLLLR") == (9, 7)


def test_project_nodes_drops_silent_and_depth_weights():
    doc = {
        "nodes": [
            {  # active: two enabled ops
                "crit": 0.5, "tonal": 0.4, "dense": 0.3, "bright": 0.2, "x": 0.1, "y": -0.2,
                "ops": [
                    {"path": "R", "depth": 2.0, "enabled": True},
                    {"path": "RLL", "depth": 1.0, "enabled": True},
                    {"path": "L", "depth": 0.0, "enabled": False},
                ],
            },
            {  # silent: all ops disabled → dropped
                "crit": 0.9, "tonal": 0.1, "dense": 0.0, "bright": 0.0, "x": 0.0, "y": 0.0,
                "ops": [{"path": "R", "depth": 0.0, "enabled": False}],
            },
        ]
    }
    d = project_nodes(doc)
    assert d["promNode"].size == 1          # silent node dropped
    assert d["nActive"][0] == 2
    assert d["crit"][0] == 0.5
    # meanQ is depth-weighted: (2·q(R=2/1)=1 + 1·q(RLL=4/3)=3) / 3 = 5/3
    assert d["meanQ"][0] == np.float64(5.0 / 3.0)


def test_corr_shuffle_detects_and_rejects():
    rng = np.random.default_rng(1)
    a = rng.normal(size=2000)
    b = a + 0.1 * rng.normal(size=2000)          # strongly correlated
    rho, z, p = corr_shuffle(a, b, n_shuffle=200)
    assert rho > 0.9 and abs(z) > 10 and p < 0.01

    c = rng.normal(size=2000)                    # independent
    rho2, z2, p2 = corr_shuffle(a, c, n_shuffle=200)
    assert abs(rho2) < 0.1 and p2 > 0.01
