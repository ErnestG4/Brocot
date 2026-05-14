# brocot

A multi-modulator FM synthesizer whose parameter space is structured by the Stern-Brocot tree (frequency ratios) and Bessel function landmarks (modulation depths).

The premise: patterns that FM users discover by ear over years of programming are the structural features of these two mathematical objects. Exposing the structure directly as the interface makes the discovery process immediate rather than years-long.

**Status:** v1 in development. See [`BROCOT-SPEC.md`](./BROCOT-SPEC.md) for the full specification and slice plan.

**Reference prototype:** [`farey-fm.html`](./farey-fm.html) — single-file React/Web Audio prototype. The audio math here is the ground truth for the JUCE port.

**Lineage:**
- Truax (1978) — *Organizational Techniques for C:M Ratios in Frequency Modulation*
- Burraston / noyzelab (2016) — Farey programming tool for PreenFM2
- Stern (1858), Brocot (1861) — the tree itself

## Classifying and steering — "a synth you can steer"

The reachable parameter space is enormous — 16 (soon 64) FM operators × Stern-Brocot paths × continuous depths give far more spectra than any programmer can curate by hand. Brocot makes the rest navigable by *shape* rather than by file.

### Offline: classify

[`phase3/`](./phase3/) runs a two-phase search over the configuration space and groups the results by category:

1. **Family search** — each candidate's predicted partial spectrum is scored against a musical-family target using KL divergence on a quantised harmonic grid plus a Ramanujan-Fourier amplitude term at the family's characteristic q. Families: sawtooth, square, triangle, pulse-PW, organ stops, bell-stretched, and primes 2 / 3 / 5 / 7 / 11 / 13. The RF math comes from Will's ARS toolkit, vendored as a submodule at `phase3/ars/`.
2. **Spectral search** — a pure-shape objective with no family target: maximise spectral entropy, minimise peak-to-mean, reward partial density. Produces the "textured-smooth" carpets at strict op-counts. At depth ≤ 0.7 every operator sits in its J₀-dominant regime; parallel operators overlap into a flat dense carpet that no hand-programming can reasonably reach.

[`phase3/build_pull_index.py`](./phase3/build_pull_index.py) then condenses every saved preset into one table: for each category, the lowest-objective exemplar's operator state plus its quantised, L1-normalised amplitude vector. Output is [`resources/pull_index.json`](./resources/pull_index.json) (≈ 2.6 MB). The synth never runs Python — the runtime just reads this file.

### Runtime: steer

[`source/pull/PullIndex.{h,cpp}`](./source/pull/) loads the table once at startup. The editor has two tabs:

- The **Steer** tab presents the categories as a column of ± buttons. Pressing `+ Sawtooth` morphs the current patch by one step (≈ 15%) toward the canonical Sawtooth exemplar. `−` walks back toward a captured "home" state (which re-anchors on Reset). The Textured-Smooth row carries an op-count spinner so the carpet-density dimension is one inline knob. The right column stacks the global ADSR, the filter, and the first two modulators — the touch-points you reach for most while shaping.
- The **Under the Hood** tab exposes every operator + modulator directly for hand-tweaking once the steered shape feels close.

The morph itself is a *stepped Stern-Brocot tree walk*: from the current path up to the longest-common-prefix ancestor, then down to the target path. Each step is one L/R character — a small ratio nudge that the audio-thread ratio smoother absorbs cleanly. Depth lerps continuously through enable transitions (a newly-enabling operator ramps in from silence; a fading-out one decays smoothly).

The thesis: **shaping is sound selection.** The preset menu carries ≈ 48 canonical anchors; the steer buttons reach everything in-between by walking the SB tree continuously. When the tooling makes a space cheap to traverse, push the space wider — the 4 → 8 → 16 → 64-operator progression is the recurring move.

## Building

VST3 is a per-platform binary; you need a native build per target OS.

After cloning, initialise the JUCE submodule on each machine:

```
git submodule update --init --recursive
```

### Linux

```
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev libasound2-plugins \
    libfreetype-dev libfontconfig-dev \
    libx11-dev libxinerama-dev libxext-dev libxrandr-dev \
    libxcomposite-dev libxcursor-dev libxrender-dev \
    mesa-common-dev libglu1-mesa-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev \
    libcurl4-openssl-dev catch2

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs:
- `build/Brocot_artefacts/Release/Standalone/Brocot`
- `build/Brocot_artefacts/Release/VST3/Brocot.vst3` — copy to `~/.vst3/`

Note: `libasound2-plugins` provides the ALSA→PulseAudio bridge that
JUCE needs on Linux when there's no kernel ALSA device (e.g., WSL2 +
WSLg). On bare-metal Linux with a real soundcard, it's still
recommended but optional.

### Windows

Requires Visual Studio 2022 (or Build Tools) with the C++ workload,
plus CMake. From a "x64 Native Tools Command Prompt":

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs:
- `build\Brocot_artefacts\Release\Standalone\Brocot.exe`
- `build\Brocot_artefacts\Release\VST3\Brocot.vst3` — copy to
  `%COMMONPROGRAMFILES%\VST3\` for system-wide install.

### macOS

Requires Xcode Command Line Tools (`xcode-select --install`) and CMake.
From Terminal:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs:
- `build/Brocot_artefacts/Release/Standalone/Brocot.app`
- `build/Brocot_artefacts/Release/VST3/Brocot.vst3` — copy to
  `~/Library/Audio/Plug-Ins/VST3/`
- `build/Brocot_artefacts/Release/AU/Brocot.component` — copy to
  `~/Library/Audio/Plug-Ins/Components/`

### Tests

```
ctest --test-dir build --output-on-failure
```

## License

AGPL-3.0. See `LICENSE`.
