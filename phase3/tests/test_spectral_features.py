"""Tests for spectral feature descriptors."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.families import family_sawtooth, family_square
from phase3.families_extra import family_bell, family_hollow, family_pluck
from phase3.spectral_features import spectral_features


def test_zero_spectrum_is_safe():
    f = spectral_features(np.zeros(16))
    assert f.centroid == 0.0 and f.roughness == 0.0


def test_brighter_spectrum_higher_centroid():
    dark = np.zeros(32); dark[0] = 1.0; dark[1] = 0.5
    bright = np.zeros(32); bright[15] = 1.0; bright[20] = 0.8
    assert spectral_features(bright).centroid > spectral_features(dark).centroid


def test_pure_tone_low_flatness_low_roughness():
    a = np.zeros(32); a[0] = 1.0
    f = spectral_features(a)
    assert f.flatness < 0.2            # single peak ⇒ very tonal
    assert f.roughness == 0.0          # one partial ⇒ no beating
    assert f.t1 == pytest.approx(1.0)  # all energy in the fundamental


def test_square_is_odd_dominant():
    a = family_square().target_amplitudes(32)
    assert spectral_features(a).odd_even > 5.0


def test_sawtooth_tilts_down():
    a = family_sawtooth().target_amplitudes(32)
    assert spectral_features(a).tilt < 0.0   # 1/n ⇒ HF rolloff


def test_effective_count_tracks_partial_richness():
    sparse = family_hollow().target_amplitudes(32)
    rich = family_sawtooth().target_amplitudes(32)
    assert (spectral_features(rich).effective_count
            > spectral_features(sparse).effective_count)


def test_close_partials_are_rougher():
    # Two partials a semitone-ish apart (adjacent bins) beat; far apart don't.
    close = np.zeros(32); close[8] = 1.0; close[9] = 1.0
    far = np.zeros(32); far[1] = 1.0; far[20] = 1.0
    assert spectral_features(close).roughness > spectral_features(far).roughness


def test_describe_returns_adjectives():
    s = spectral_features(family_bell().target_amplitudes(48))
    assert isinstance(s.describe(), str) and len(s.describe()) > 0
    # Pluck reads as warm/dark-ish, not bright.
    pf = spectral_features(family_pluck().target_amplitudes(48))
    assert isinstance(pf.describe(), str)
