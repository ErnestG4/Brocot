"""Tests for the coverage bank (retrieval mechanics — no large artifact)."""

from __future__ import annotations

import numpy as np

from phase3.coverage_bank import (
    config_from_entry,
    curated_ratios,
    enumerate_configs,
    query,
    retrieve_seeds,
)
from phase3.config import path_ratio


def test_curated_ratios_unique_and_include_unity():
    rs = curated_ratios(4)
    ratios = [path_ratio(p) for p in rs]
    assert 1.0 in ratios                          # empty path / fundamental
    assert len(ratios) == len(set(round(r, 6) for r in ratios))  # deduped
    assert rs[0] == ""                            # shortest first


def test_enumerate_yields_descriptors():
    gen = enumerate_configs(n_random=5)
    first = next(gen)
    paths, depths = first
    assert isinstance(paths, tuple) and isinstance(depths, tuple)
    assert len(paths) == len(depths)


def test_query_finds_exact_row():
    amps = np.eye(6, dtype=np.float32)            # 6 one-hot fingerprints
    target = amps[3]
    idx, dist = query(amps, target, k=1, metric="l1")
    assert idx[0] == 3
    assert dist[0] == 0.0


def test_query_kl_metric_runs():
    amps = np.abs(np.random.default_rng(0).standard_normal((20, 8))).astype(np.float32)
    amps /= amps.sum(axis=1, keepdims=True)
    idx, dist = query(amps, amps[5], k=3, metric="kl")
    assert idx[0] == 5                            # itself is nearest
    assert np.all(np.diff(dist) >= -1e-6)         # sorted ascending


def test_retrieve_seeds_reconstructs_configs():
    amps = np.eye(4, dtype=np.float32)
    configs = [{"paths": ["R"], "depths": [1.5]},
               {"paths": ["RL", "L"], "depths": [2.0, 1.0]},
               {"paths": [""], "depths": [0.5]},
               {"paths": ["RR"], "depths": [3.0]}]
    seeds = retrieve_seeds(amps[1], amps, configs, k=1)
    assert len(seeds) == 1
    cfg = seeds[0]
    assert cfg.num_enabled() == 2                 # the 2-op entry
    assert {m.path for m in cfg.mods if m.enabled} == {"RL", "L"}


def test_config_from_entry_roundtrip():
    cfg = config_from_entry({"paths": ["R", "RLL"], "depths": [1.2, 2.4]})
    rs, ds = cfg.ratios_and_depths()
    assert ds == [1.2, 2.4]
    assert len(rs) == 2
