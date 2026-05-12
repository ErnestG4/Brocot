# brocot

A multi-modulator FM synthesizer whose parameter space is structured by the Stern-Brocot tree (frequency ratios) and Bessel function landmarks (modulation depths).

The premise: patterns that FM users discover by ear over years of programming are the structural features of these two mathematical objects. Exposing the structure directly as the interface makes the discovery process immediate rather than years-long.

**Status:** v1 in development. See [`BROCOT-SPEC.md`](./BROCOT-SPEC.md) for the full specification and slice plan.

**Reference prototype:** [`farey-fm.html`](./farey-fm.html) — single-file React/Web Audio prototype. The audio math here is the ground truth for the JUCE port.

**Lineage:**
- Truax (1978) — *Organizational Techniques for C:M Ratios in Frequency Modulation*
- Burraston / noyzelab (2016) — Farey programming tool for PreenFM2
- Stern (1858), Brocot (1861) — the tree itself

## Building (Linux)

System dependencies (Ubuntu / Debian):

```
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxinerama-dev libxext-dev libxrandr-dev \
    libxcomposite-dev libxcursor-dev libxrender-dev \
    mesa-common-dev libglu1-mesa-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev \
    libcurl4-openssl-dev
```

JUCE is vendored as a git submodule. After cloning:

```
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Plugin artifacts land in `build/Brocot_artefacts/Release/`.

## License

AGPL-3.0. See `LICENSE`.
