# brocot

A multi-modulator FM synthesizer whose parameter space is structured by the Stern-Brocot tree (frequency ratios) and Bessel function landmarks (modulation depths).

The premise: patterns that FM users discover by ear over years of programming are the structural features of these two mathematical objects. Exposing the structure directly as the interface makes the discovery process immediate rather than years-long.

**Status:** in active development. The v1 slice plan in [`BROCOT-SPEC.md`](./BROCOT-SPEC.md) is complete; current work is the landscape / steering layer described below.

**Reference prototype:** [`farey-fm.html`](./farey-fm.html) — single-file React/Web Audio prototype. The audio math here is the ground truth for the JUCE port.

**Lineage:**
- Truax (1978) — *Organizational Techniques for C:M Ratios in Frequency Modulation*
- Burraston / noyzelab (2016) — Farey programming tool for PreenFM2
- Stern (1858), Brocot (1861) — the tree itself

## Classifying and steering — "a synth you can steer"

The reachable parameter space is enormous — 64 FM operators (each with its own Stern-Brocot path, depth, detune, and waveform) × 16 universal modulators × continuous depths give far more spectra than any programmer can curate by hand. Brocot makes the rest navigable by *shape* and by *place* rather than by file.

### Offline: classify

[`phase3/`](./phase3/) runs a two-phase search over the configuration space and groups the results by category:

1. **Family search** — each candidate's predicted partial spectrum is scored against a musical-family target using KL divergence on a quantised harmonic grid plus a Ramanujan-Fourier amplitude term at the family's characteristic q. Families: sawtooth, square, triangle, pulse-PW, organ stops, bell-stretched, and primes 2 / 3 / 5 / 7 / 11 / 13. The RF math comes from Will's ARS toolkit, vendored as a submodule at `phase3/ars/`.
2. **Spectral search** — a pure-shape objective with no family target: maximise spectral entropy, minimise peak-to-mean, reward partial density. Produces the "textured-smooth" carpets at strict op-counts. At depth ≤ 0.7 every operator sits in its J₀-dominant regime; parallel operators overlap into a flat dense carpet that no hand-programming can reasonably reach.

[`phase3/build_pull_index.py`](./phase3/build_pull_index.py) then condenses every saved preset into one table: for each category, the lowest-objective exemplar's operator state plus its quantised, L1-normalised amplitude vector. Output is [`resources/pull_index.json`](./resources/pull_index.json) (≈ 2.6 MB). The synth never runs Python — the runtime just reads this file.

Beyond the curated search, the `phase3/gpu_*.py` modules (see [`phase3/gpu.md`](./phase3/gpu.md)) scale the same fingerprint mathematics to bank size on a consumer GPU: tens of millions of random FM configurations are rendered, FFT'd, and binned onto an integer-harmonic amplitude grid per run. That bank feeds the landscape maps below, and it also powers **Mode B** ([`phase3/mode_b.md`](./phase3/mode_b.md)) — matching an arbitrary recorded sound's spectrum back into the configuration space, so the search target can be an audio file instead of a named family.

### Runtime: steer

[`source/pull/PullIndex.{h,cpp}`](./source/pull/) loads the table once at startup. The editor has three tabs:

- The **landscape** tab plots the whole reachable sound-space as a 2D point cloud and lets you drive through it — see the next section.
- The **drive** tab presents the categories as a column of ± buttons. Pressing `+ Sawtooth` morphs the current patch by one step (≈ 15%) toward the canonical Sawtooth exemplar. `−` walks back toward a captured "home" state (which re-anchors on Reset). The Textured-Smooth row carries an op-count spinner so the carpet-density dimension is one inline knob. The right column stacks the global ADSR, the filter, and the first two modulators — the touch-points you reach for most while shaping.
- The **under the hood** tab exposes every operator + modulator directly for hand-tweaking once the steered shape feels close.

The morph itself is a *stepped Stern-Brocot tree walk*: from the current path up to the longest-common-prefix ancestor, then down to the target path. Each step is one L/R character — a small ratio nudge that the audio-thread ratio smoother absorbs cleanly. Depth lerps continuously through enable transitions (a newly-enabling operator ramps in from silence; a fading-out one decays smoothly).

The thesis: **shaping is sound selection.** The preset menu carries ≈ 48 canonical anchors; the steer buttons reach everything in-between by walking the SB tree continuously. When the tooling makes a space cheap to traverse, push the space wider — the 4 → 8 → 16 → 64-operator progression is the recurring move.

## The landscape map — "where am I, where can I go"

The landscape tab is a picture of the sound-space itself: every dot is a playable FM configuration, placed so that *screen distance ≈ timbral distance*. It is built entirely offline by [`phase3/gpu_paths.py`](./phase3/gpu_paths.py) `gradients`; the synth does no real-time spectral analysis.

How a map is made:

1. **Bank** — millions of random configurations at a fixed operator count (`gpu_bank.py`) are rendered and fingerprinted on the GPU: render → FFT → interpolated binning onto a q_max = 64 integer-harmonic amplitude grid at the 220 Hz reference carrier. The fingerprint *is* the timbre.
2. **Families** — every bank row is labelled with its nearest factory-family centroid by L1 fingerprint distance (`gpu_landscape.py classify`).
3. **Nodes** — a stratified subsample becomes the map (50 k nodes on the default 6-op map, 30 k on the others; every family is guaranteed a floor so rare basins stay reachable). Each node stores its full reconstructable operator state — paths, depths, waveforms, op-0 feedback — so every dot can be applied to the engine.
4. **Placement** — node positions are a t-SNE embedding (PCA-init) of the fingerprint vectors, normalised to [−1, 1] per axis. Families cluster into continents; the voids between them are real discontinuities of FM space, not layout artefacts.
5. **Descriptors** — each node carries brightness (spectral centroid), density (partial count), hollowness (odd-harmonic fraction), musicality (1 − spectral flatness), and **criticality**: the max of a 2D structure score (largest angular gap among a node's nearest neighbours in the layout — filaments, junctions, and void rims score high) and an intrinsic score from local PCA in full fingerprint space (participation ratio of the singular values — lights up the 1-D filaments buried inside dense projections). The critical points are exactly what speckles and lines the voids.
6. **Gradient fields** — for each family, a multi-source Dijkstra over the k-NN fingerprint graph into the family's cleanest members (lowest spectral entropy near the centroid) leaves a next-hop pointer at every node: the shortest *smooth* route to that family from anywhere on the map, precomputed. Runtime steering is pointer-following, never search.

The export is [`resources/landscape_graph*.json.gz`](./resources/) — one map per operator depth (4 / 6 / 8 / 16 / 32 / 64) plus waveform variants (solo 6-op sine / triangle / saw / square, and 4 / 6 / 8-op waveform mixes). Each map edits ops 0..depth−1 as a nested prefix, so switching maps preserves the rest of the patch. Maps load lazily — only the header is read until a map is actually navigated.

At runtime ([`source/pull/Landscape.{h,cpp}`](./source/pull/), [`source/ui/CloudView.{h,cpp}`](./source/ui/)):

- Dots are drawn small and translucent so overlap density reads as shading and tails / outliers stay visible. Colour by **family** (one hue per family, names floated at the centroids) or by any descriptor — brightness, density, feedback, hollowness, musicality, criticality — on a blue → red ramp.
- "You are here" is located by *parameter* distance, not analysis: a depth-weighted log₂-ratio histogram of the current patch, L1-matched against each node's stored signature. No FFT, nothing on the audio thread.
- **Click** sets a destination and the dot glides there at the seek-rate knob's speed, applying each nearest node as it passes (the ratio/depth smoothers absorb the steps, so the journey is the sound morphing). **Drag** scrubs 1:1 for hunting by hand. **Double-click** drops the dot and snaps the patch. **Hover** inspects a node's profile and lights up every node in its family, revealing how a sound type scatters into pockets across the map.

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
