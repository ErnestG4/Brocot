# brocot

A multi-modulator FM synthesizer whose parameter space is structured by the Stern-Brocot tree (frequency ratios) and Bessel function landmarks (modulation depths).

The premise: patterns that FM users discover by ear over years of programming are the structural features of these two mathematical objects. Exposing the structure directly as the interface makes the discovery process immediate rather than years-long.

**Status:** v1 in development. See [`BROCOT-SPEC.md`](./BROCOT-SPEC.md) for the full specification and slice plan.

**Reference prototype:** [`farey-fm.html`](./farey-fm.html) — single-file React/Web Audio prototype. The audio math here is the ground truth for the JUCE port.

**Lineage:**
- Truax (1978) — *Organizational Techniques for C:M Ratios in Frequency Modulation*
- Burraston / noyzelab (2016) — Farey programming tool for PreenFM2
- Stern (1858), Brocot (1861) — the tree itself

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
