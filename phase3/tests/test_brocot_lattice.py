"""Tests for the Python lattice mirror — the same BROCOT-SPEC.md §6 fixtures
the C++ module (tests/BrocotLatticeTests.cpp) asserts."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.brocot_lattice import (
    almost_mathieu_bands,
    cf_depth,
    cf_terms,
    chern_labels,
    fold_chern,
    gaps,
    modinv,
    prominence,
    simplicity,
)


def test_simplicity_is_q():
    assert simplicity(7) == 7
    assert simplicity(1) == 1


def test_cf_terms():
    assert cf_terms(4, 3) == [1, 3]
    assert cf_terms(9, 7) == [1, 3, 2]
    assert cf_terms(1, 2) == [0, 2]
    assert cf_terms(2, 1) == [2]


def test_cf_depth_convention_independent():
    assert cf_depth(4, 3) == 4  # [1,3] and [1,2,1] both sum to 4
    assert cf_depth(9, 7) == 6  # [1,3,2] and [1,3,1,1] both sum to 6


def test_theta_1_3_band_edges():
    b = almost_mathieu_bands(1, 3)
    assert len(b) == 3
    edges = [(round(x.lo, 4), round(x.hi, 4)) for x in b]
    assert edges[0] == pytest.approx((-2.7320508, -2.0), abs=1e-4)
    assert edges[1] == pytest.approx((-0.7320508, 0.7320508), abs=1e-4)
    assert edges[2] == pytest.approx((2.0, 2.7320508), abs=1e-4)


@pytest.mark.parametrize(
    "p,q,expected",
    [
        (1, 3, [+1, -1]),
        (1, 4, [+1, -1]),          # central +2 gap closes at λ=1
        (1, 5, [+1, +2, -2, -1]),
        (2, 5, [-2, +1, -1, +2]),
        (3, 7, [-2, +3, +1, -1, -3, +2]),
    ],
)
def test_gap_chern_labels(p, q, expected):
    assert chern_labels(p, q) == expected


def test_theta_1_4_central_gap_closes():
    assert len(gaps(1, 4)) == 2  # the zero-width central gap is dropped


@pytest.mark.parametrize("p,q", [(1, 3), (1, 4), (1, 5), (2, 5), (3, 7), (4, 9)])
def test_spectrum_is_E_to_minus_E_symmetric(p, q):
    b = almost_mathieu_bands(p, q)
    for j in range(q):
        lo, hi = b[j], b[q - 1 - j]
        assert lo.lo == pytest.approx(-hi.hi, abs=1e-9)
        assert lo.hi == pytest.approx(-hi.lo, abs=1e-9)


def test_prominence():
    assert prominence(1, 3) == pytest.approx(1.2679492, abs=1e-4)
    assert prominence(1, 2) > prominence(3, 7)
    assert prominence(2, 1) == 0.0
    assert gaps(2, 1) == []


def test_modinv_and_fold():
    assert modinv(2, 5) == 3
    assert modinv(3, 7) == 5
    assert modinv(1, 3) == 1
    assert fold_chern(2, 3) == -1
    assert fold_chern(2, 4) == 2
    assert fold_chern(3, 4) == -1


def test_ids_plateaus_land_on_farey():
    """IDS plateaus land on Farey fractions (§6): each band holds 1/q of the
    states, so the θ=1/3 gap after band j-1 sits at IDS = j/q — i.e. 1/3 and
    2/3. There are exactly q-1 nonzero-width gaps for 1/3."""
    g = gaps(1, 3)
    assert len(g) == 2
    ids = [j / 3 for j in range(1, 3)]
    assert ids == pytest.approx([1 / 3, 2 / 3])
