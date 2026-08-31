# BROCOT — Synthesizer Specification

**Version:** 1.0 (greenfield JUCE port from web prototype `farey-fm.html`)
**Author:** Combust / Avatar
**Status:** spec frozen for v1 build
**Reference prototype:** `farey-fm.jsx` / `farey-fm.html` (web, React) — ground truth for all audio math
**Reference lineage:** Truax (1978) *Organizational Techniques for C:M Ratios in Frequency Modulation*; Burraston/noyzelab (2016) Farey programming tool for PreenFM2

---

## 0. Reading order

This is a complete specification, not an outline. Hand the relevant section to Claude Code when starting a slice. Sections are:

1. **Vision and non-goals** — what Brocot is, what it deliberately is not
2. **Architectural decisions** — the load-bearing choices, with rationale
3. **Project layout** — directory and file structure
4. **Audio engine** — the math, the DSP, the verification criteria
5. **Path module** — Stern-Brocot tree as a first-class data structure
6. **Parameter system** — modulation source/target abstraction with v2 hooks
7. **UI layout** — JUCE Component breakdown, panel by panel
8. **MIDI handling** — note allocation, controllers, pitch bend
9. **Preset format** — JSON schema, examples, factory content
10. **Factory presets and ARS-derived tables** — what ships with v1
11. **Build, CI, packaging** — JUCE/CMake, cross-platform, Forgejo CI
12. **Slice-by-slice work plan** — sequential tasks for Claude Code with verification

Read 1, 2, 4 (audio math), 5 (path module) before starting any slice. Each slice references the section it depends on.

---

## 1. Vision and non-goals

### What Brocot is

Brocot is a multi-modulator FM synthesizer whose entire parameter space is structured by the Stern-Brocot tree (frequency-ratio axis) and Bessel function landmarks (modulation-depth axis). The instrument's design thesis is that the patterns FM users discover by ear over years of programming are actually the structural features of these two mathematical objects, and that exposing the structure directly as the interface makes the discovery process immediate rather than years-long.

Brocot is a *forward* instrument. It synthesizes audio in real time, plays from MIDI, has filter and amplitude envelopes, and behaves like a synthesizer that musicians use. It is not an analyzer. It is not a research tool in v1.

### What Brocot deliberately is not

- Not a DX7 emulator. Operator routing topology, algorithm selection, and curated DX7 ratio lists are all explicitly absent. Operators are parallel modulators only.
- Not a "free FM" synth. Ratios cannot be set to arbitrary floats. The user navigates the tree; the tree controls what ratios are reachable. This constraint is the feature.
- Not an analysis instrument in v1. ARS code is *not* a dependency. The parameter system has hooks for v2 integration, but no v2 code ships in v1.
- Not polyphonic in v1. Mono or duo. Polyphony is v2.
- Not a modulation matrix. Routing is hardcoded. One LFO target, one envelope target for filter modulation. Configurable mod routing is v2.

### The defining timbral regime

This is the part of the spec most likely to be overlooked because it contradicts the DX7-trained mental model.

Classical FM uses modulators at integer or near-integer ratios above the carrier (2/1, 3/1, 4/1, occasionally 3/2 or 5/4) with modulation indices high enough to produce clear sideband structure (I = 2 to 8). This produces bell-like, metallic, harmonic-but-rich timbres — the DX7 sound palette.

Brocot's defining regime is different. Modulators sit at *near-unity* ratios — clustered in the immediate neighborhood of 1/1, slightly above (4/3 ≈ 1.333, 9/7 ≈ 1.286, 14/11 ≈ 1.273, 43/34 ≈ 1.265) or slightly below (3/4 = 0.75, 7/9 ≈ 0.778, 11/14 ≈ 0.786). Modulation indices are *low* — around half the first Bessel peak, so I ≈ 0.9 typically, with the meaningful range being 0.1 to 3.0 rather than the DX7's 0.5 to 8.0. And the modulators are stacked on a *shared path* through the Stern-Brocot tree, each at a successive prefix-extension of the previous (path-mode operation).

The audible character of this regime is subtle, slow, organic. The carrier dominates; the modulators contribute as fine-grained spectral coloration. The spectrum has structure but no clear harmonic series. The timbre evolves slowly with small parameter changes. Held notes have internal motion. This is the timbral space the prototype made discoverable for the first time, and Brocot v1 must make this regime *easy and natural* to inhabit, not buried under DX7-style defaults.

**Operational consequence:** the depth slider's perceptually useful range is 0 to ~3, not 0 to 8. The default modulation index when adding an operator should be ~0.5, not 1.0. Factory presets should heavily feature the near-unity regime, not the DX7 regime. The UI should not visually privilege the high-I "clangy" side of the depth space.

### Two-week target

V1 ships as a standalone application (and ideally also VST3/AU plugin) on macOS, Windows, and Linux, with the audio engine, path navigator, depth controls with Bessel landmarks, SVF filter, ADSR envelope, one LFO, MIDI input, JSON preset format, and ~32 factory presets. Two weeks of evenings or one focused week. Scope creep kills the date. Cut features, don't extend the date.

---

## 2. Architectural decisions

These are the choices made before writing code. Each has a rationale because Claude Code will at some point suggest alternatives.

### Operators are parallel modulators, not nested

Each operator's oscillator output (gained) connects directly to the carrier's frequency parameter. Modulators sum into the carrier's frequency via summation at the AudioParam (Web Audio) or via explicit per-sample summation (JUCE). Modulators do *not* modulate other modulators.

Rationale: parallel structure gives clean Bessel-comb math per modulator. Sidebands from mod *k* sit at f_c ± n·f_mk with amplitude J_n(I_k). With N parallel mods, the full spectrum is a sum of N independent combs. When the ratios sit on a shared path, the combs share denominator structure and partial coincidence is structurally enforced. Nested FM produces complex Bessel-of-Bessel math that is harder to reason about and harder to navigate by hand. The whole point of Brocot is navigability.

### Path is the canonical representation of frequency ratio

Each operator stores its frequency ratio as a `Path` — a sequence of L/R characters — *not* as a float, *not* as a p/q pair. The path is the source of truth. p, q, decimal ratio, continued-fraction expansion are all *derived* from the path.

Rationale: paths are the navigation primitive. They serialize cleanly (preset format), they navigate naturally (L/R buttons), they expose the mediant relationship between adjacent nodes, and they preserve the structural information that float ratios discard. Two ratios that happen to equal the same float are different musical objects if their paths differ, and the path is what tells you that.

### Modulation index, not raw gain in Hz

The depth parameter the user sees is the modulation index *I*, not the modulator's gain in Hz. The gain in Hz is derived: gain = I × f_modulator. This means the modulator's effect on the carrier scales with the modulator's frequency, which is what the Bessel-function math expects.

Rationale: I is the parameter that has Bessel landmarks at fixed values (1.841, 2.405, 3.054, etc.) regardless of pitch. Raw gain in Hz has landmarks that move with the played note. The whole Bessel-landmark UI breaks if depth is in Hz.

### Bessel landmarks are first-class UI elements

The depth slider per operator has visible tick marks at the first several Bessel peaks (J₁ peak at 1.841, J₂ peak at 3.054, J₃ peak at 4.201, J₄ peak at 5.318, J₅ peak at 6.416) and zeros (J₀ zero at 2.405, J₁ zero at 3.832, J₂ zero at 5.136, second J₀ zero at 5.520, second J₁ zero at 7.016). Peaks and zeros are visually distinguished. The current depth value's nearest landmark (within ~0.12) is displayed as text.

Rationale: same as above. The Bessel landmarks are where specific sideband orders dominate or vanish. They are the "scale degrees" of FM amplitude space. The web prototype already implements this; the JUCE version inherits it.

### Both path-modes (free and shared), shared as default

In **free mode**, each operator has its own path through the tree. In **shared mode**, there is a single shared path string, and each operator has a *position* (integer 0..len) along it; the operator's ratio is `walkPath(sharedPath[:position])`. The toggle between modes is a single UI element. Switching from free to shared collapses each operator's path to its longest common prefix with the others (or to the first operator's path, configurable). Switching from shared to free unpacks per-operator paths to their current resolved values.

Rationale: the defining-timbral-regime discovery used shared-path operation (RLL, RLLLR, RLLLRLLLR all prefix-extending). Shared mode is faster to use, easier to reason about, and produces the most musically coherent results because path-prefix coherence equals sideband-structure coherence. Free mode exists for full exploration but most users — and the factory presets — should live in shared mode.

**Default at app launch: shared mode, empty shared path (1/1), 3 operators enabled out of 8, all at the same position.** From there the user extends the path and spreads operators along it.

### 8 operators in v1

Eight modulator slots, all parallel. Operators have an "enabled" flag (off = depth 0 and oscillator not running, for CPU savings). Default is 3 enabled, 5 disabled at app launch; presets can enable any number.

Rationale: eight is musically generous (more parallel modulators than any DX7 algorithm allows), well within CPU budget at duo polyphony, and structurally significant for the path-mode stacking pattern (a path of length 8 with one operator per depth gives a rich progression from 1/1 toward a specific limit ratio). Sixteen is v2 if anyone asks. Four is too few once you start stacking.

### Mono/Duo polyphony

V1 is monophonic or duophonic, user-selectable. Monophonic uses latest-note-priority with envelope retrigger. Duophonic allocates the new note to a free voice if available, otherwise steals the oldest voice. Full polyphony (8+ voices) is v2.

Rationale: monophonic ships first and is easiest to get right. Duo is a one-line change from mono once mono is solid (JUCE's Synthesiser/SynthesiserVoice machinery handles voice allocation cleanly). Full polyphony introduces voice-stealing logic, polyphonic CPU budgeting, and per-voice parameter consistency that aren't worth the v1 schedule.

### SVF filter, not ladder

State-variable filter with low-pass, band-pass, high-pass, notch outputs and a mix/morph control between them. Cutoff and resonance as standard parameters. JUCE provides `dsp::StateVariableTPTFilter` which is the canonical implementation.

Rationale: FM produces spectra that often have interesting content across the full frequency range. A morphing filter is more useful for sculpting these spectra than a ladder LP. The TPT (topology-preserving transform) variant is stable at high resonance and at modulation extremes, which matters because the LFO will be sweeping the cutoff.

### One LFO, hardcoded to filter cutoff with selectable shape

One LFO. Shape: sine / triangle / square / saw (user-selectable). Rate: 0.01 Hz to ~20 Hz. Depth: 0 to 1. Target: filter cutoff (hardcoded). Optional per-note sync (LFO phase resets on note-on) toggleable.

Rationale: one LFO routed to one target is the minimum useful modulation setup. Filter cutoff is the most common target. Multi-target LFO routing and multiple LFOs are v2.

### ADSR amp envelope, no filter envelope

One ADSR envelope, hardcoded to amplitude. Attack 0-5s, Decay 0-5s, Sustain 0-1, Release 0-10s. No dedicated filter envelope in v1.

Rationale: amp envelope is mandatory for an instrument. Filter envelope is a nice-to-have that can be approximated by LFO-to-filter at slow rate with appropriate shape. Adding a second envelope means a second routing matrix and per-parameter modulation depth, which exceeds v1 scope.

### Modulation system with v2 hooks

The parameter system internally distinguishes between *parameter* (the value seen by the audio engine) and *control* (the user-facing knob or slider). Between them sits a modulation graph: each parameter accepts contributions from zero or more sources, each contribution has its own depth scalar, and the parameter's effective value is `base + Σ source_k × depth_k` (or, optionally for some parameters, a multiplicative version).

In v1, the registered sources are:
- `MIDIVelocity` (per-note)
- `MIDIModWheel` (CC1)
- `MIDIPitchBend`
- `MIDIAftertouch` (CC remembered but not routed in v1; channel pressure goes to one target)
- `LFO1` (the single LFO)
- `AmpEnv` (the single ADSR)

In v2 the registered sources expand to include `FareyPLLBank`, `NNSClassifier`, `SidechainAudioEnvelope`, `SpectralCentroid`, etc. None of these are implemented in v1 — but the interface they will plug into is.

Rationale: this costs maybe a day of upfront design and saves a rewrite when v2 happens. The interface is small (one virtual function: `float getModulationValue() const`) and adding implementations later doesn't touch v1 code.

### Sidechain audio input as deferred-not-skipped

There is no sidechain in v1. But the plugin's bus configuration leaves a stereo sidechain input declared but unused. In v1.x this becomes a one-day feature: register a `SidechainAudioEnvelope` source that follows the sidechain's amplitude envelope, expose its routing.

Rationale: stubbing the bus in v1 means v1.x doesn't require rebuilding the plugin's I/O configuration. It also doesn't add complexity to v1's audio path because nothing is routed to it yet.

### JSON preset format, human-readable

Presets are JSON files. Paths are represented as L/R strings. All other parameters are numeric. Preset files include a `name`, `author`, `description`, and `version` field. Factory presets ship as JSON files in a resources directory; user presets save to the OS-standard application data directory.

Rationale: human-readable presets are part of the instrument's identity. The math is structured; the preset format should reflect that. Sharing a preset is sharing a path through a space, not exchanging a binary blob.

---

## 3. Project layout

```
brocot/
├── CMakeLists.txt
├── README.md
├── LICENSE                    (AGPL-3.0, matching The Spot's licensing)
├── BROCOT-SPEC.md             (this document)
├── source/
│   ├── PluginProcessor.h/.cpp     (JUCE AudioProcessor entry point)
│   ├── PluginEditor.h/.cpp        (JUCE AudioProcessorEditor entry point)
│   ├── audio/
│   │   ├── BrocotVoice.h/.cpp     (single FM voice: carrier + N mods + env)
│   │   ├── Operator.h/.cpp        (one modulator oscillator + path + depth)
│   │   ├── Path.h/.cpp            (Stern-Brocot tree path operations)
│   │   ├── Bessel.h/.cpp          (Bessel landmark constants, nearestLandmark)
│   │   ├── FilterBank.h/.cpp      (SVF wrapper)
│   │   ├── LFO.h/.cpp             (single LFO)
│   │   └── AmpEnvelope.h/.cpp     (ADSR wrapper)
│   ├── params/
│   │   ├── ParameterTree.h/.cpp   (AudioProcessorValueTreeState setup)
│   │   ├── ModSource.h            (ModulationSource interface)
│   │   ├── ModTarget.h/.cpp       (ModulationTarget with source list)
│   │   └── sources/               (concrete ModulationSource implementations)
│   │       ├── MIDISource.h/.cpp
│   │       ├── LFOSource.h/.cpp
│   │       └── EnvSource.h/.cpp
│   ├── ui/
│   │   ├── BrocotEditor.h/.cpp        (top-level editor)
│   │   ├── OperatorPanel.h/.cpp       (one mod card)
│   │   ├── PathNavigator.h/.cpp       (shared-path widget)
│   │   ├── DepthSlider.h/.cpp         (custom slider with Bessel ticks)
│   │   ├── RatioReadout.h/.cpp        (freq + depth ratio displays)
│   │   ├── Oscilloscope.h/.cpp
│   │   ├── FilterPanel.h/.cpp
│   │   ├── EnvelopePanel.h/.cpp
│   │   ├── LFOPanel.h/.cpp
│   │   ├── PresetBar.h/.cpp
│   │   └── BrocotLookAndFeel.h/.cpp   (colors, fonts, custom drawing)
│   ├── presets/
│   │   ├── PresetManager.h/.cpp       (JSON load/save)
│   │   └── PresetSchema.h/.cpp        (validation)
│   └── util/
│       ├── Fonts.h/.cpp               (EB Garamond, JetBrains Mono loading)
│       └── Colors.h                   (palette constants)
├── resources/
│   ├── presets/
│   │   ├── factory/
│   │   │   ├── 01_pure.brocot
│   │   │   ├── 02_J0_null.brocot
│   │   │   ├── 03_octave_J1.brocot
│   │   │   ├── 04_three_peaks.brocot
│   │   │   ├── 05_1_2_3.brocot
│   │   │   ├── 06_mediants.brocot
│   │   │   ├── 07_detune.brocot
│   │   │   ├── 08_deep.brocot
│   │   │   ├── 09_Avatar_1.brocot
│   │   │   └── ... (32 total)
│   │   └── README.md                  (notes on preset categories)
│   ├── tables/
│   │   ├── bessel_landmarks.json
│   │   ├── characteristic_paths.json  (ARS-derived, see §10)
│   │   └── truax_constructions.json   (recreated from Truax 1978)
│   └── fonts/
│       ├── EBGaramond-Regular.ttf
│       ├── EBGaramond-Italic.ttf
│       ├── JetBrainsMono-Regular.ttf
│       └── JetBrainsMono-Medium.ttf
├── tests/
│   ├── PathTests.cpp
│   ├── BesselTests.cpp
│   ├── PresetRoundtripTests.cpp
│   └── AudioRegressionTests.cpp       (compare against prototype)
└── .gitea/workflows/
    └── build.yml                       (Forgejo CI)
```

---

## 4. Audio engine

### Single-voice FM math

For each voice at each sample, given:
- Carrier frequency f_c (Hz, set by MIDI note + pitch bend)
- N enabled modulators, each with:
  - Path → resolves to (p_k, q_k) with ratio r_k = p_k / q_k
  - Modulation index I_k (the depth slider value, 0..~8)
  - Phase φ_k (accumulated)

The instantaneous modulator frequency f_mk = r_k × f_c. The modulator's contribution to the carrier's frequency offset, in Hz, is:

```
Δf_k(t) = I_k × f_mk × sin(2π × φ_k(t))
```

The total carrier instantaneous frequency:

```
f_c_inst(t) = f_c + Σ_k Δf_k(t)
```

The carrier phase advances per sample by:

```
φ_c(t+1) = φ_c(t) + f_c_inst(t) / sample_rate
```

And the output sample:

```
out(t) = sin(2π × φ_c(t)) × envelope(t)
```

This is *phase modulation* implementation (which is mathematically equivalent to frequency modulation for sinusoidal modulators, and is what Yamaha actually implemented under the "FM" label). The implementation is single-pass per voice: one loop over samples, inner loop over operators.

### JUCE implementation notes

- Use `juce::dsp::Oscillator<float>` for each modulator and the carrier, or implement a simple `SineOscillator` class with explicit phase accumulators (slightly faster and gives you direct access to the phase, which simplifies adding sync later).
- Per-voice processing: render block-by-block (e.g., 64-sample sub-blocks for parameter smoothing) but the FM math itself must run per-sample because the carrier's instantaneous frequency changes every sample.
- Parameter smoothing: depth slider changes, path changes (which change ratio), and ratio changes should be smoothed over ~20-50ms to avoid clicks. Use `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>`.
- Filter and envelope process the output stream block-by-block after the FM voice mixes down.

### Aliasing

At low modulation indices (I < 3) and near-unity ratios (which is Brocot's primary regime), aliasing is minimal because the highest meaningful sidebands stay well below Nyquist. At high I or high-ratio mods (mod at 5/1, 7/1, etc., at I > 5), sidebands can exceed Nyquist and alias.

V1 mitigation: clamp I to ≤ 8. Don't oversample. This is acceptable for v1 given the defining regime. V1.x improvement: add optional 2x oversampling for high-I work.

### Verification against prototype

The web prototype's `useFMEngine` function is the canonical reference. Any divergence between JUCE output and the prototype's output (at the same parameters) is a bug. Tests:

1. **Single mod, I=1.841, ratio=2/1, carrier=110 Hz**: produces a spectrum with peaks at 110, 330, 550, 770, ... Hz, with the 110 Hz (carrier) and 330 Hz (first sideband) being roughly equal amplitude. This is the "J₁ peak" preset.
2. **Single mod, I=2.405, ratio=1/1, carrier=110 Hz**: produces a spectrum with the 110 Hz carrier suppressed below -40 dB relative to the first sideband. This is the "J₀ null" preset.
3. **Three mods on shared path RLLLR at positions 1, 3, 5 (ratios 2/1, 4/3, 9/7), depths 5.13, 0.86, 0.90, carrier=130.81 Hz**: produces the "Avatar·1" timbre, verified by spectral comparison against the prototype's output.

These are the audio regression tests.

---

## 5. Path module

The path module is the heart of the synth. It must be correct, fast, and well-tested.

### Path class

```cpp
class Path {
public:
    Path();                                 // empty path = 1/1
    Path(const juce::String& steps);        // construct from "RLLR" etc.
    
    // Mutation (returns new Path; original unchanged for value-semantics)
    Path extended(char step) const;         // step ∈ {'L', 'R'}
    Path parent() const;                    // drop last step; empty if already root
    
    // Resolution
    void walk(int& p, int& q) const;        // current p/q (always lowest terms)
    void walkWithChildren(int& p, int& q,
                          int& lp, int& lq,
                          int& rp, int& rq) const;
    double ratio() const;                   // p / q as double
    
    // Properties
    int depth() const;                      // length of step sequence
    juce::String toString() const;          // "RLLR"
    bool isEmpty() const;                   // depth() == 0
    
    // Prefix operations (used for shared-path mode)
    Path prefix(int length) const;          // first `length` steps
    bool startsWith(const Path& other) const;
    Path commonPrefix(const Path& other) const;
    
    // Equality, hash, etc. for use in collections
    bool operator==(const Path& other) const;
    bool operator!=(const Path& other) const;
    
private:
    juce::String steps;  // string of 'L' and 'R'
};
```

### Path resolution algorithm

```cpp
void Path::walk(int& p, int& q) const {
    int leftP = 0, leftQ = 1;
    int rightP = 1, rightQ = 0;
    int curP = 1, curQ = 1;
    
    for (auto step : steps) {
        if (step == 'L') {
            rightP = curP; rightQ = curQ;
        } else {
            leftP = curP; leftQ = curQ;
        }
        curP = leftP + rightP;
        curQ = leftQ + rightQ;
    }
    
    p = curP;
    q = curQ;
}
```

This is the mediant algorithm. By construction, the result is always in lowest terms (no GCD reduction needed). Cost: O(depth) integer additions per resolution. For depth = 20 (a very deep path), that's 20 additions — negligible.

### Tests

```cpp
TEST_CASE("Empty path is 1/1") {
    Path p;
    int n, d; p.walk(n, d);
    REQUIRE(n == 1); REQUIRE(d == 1);
}

TEST_CASE("R is 2/1") { /* ... */ }
TEST_CASE("L is 1/2") { /* ... */ }
TEST_CASE("RLL is 4/3") { /* ... */ }
TEST_CASE("RLLLR is 9/7") { /* ... */ }
TEST_CASE("RLLLRLLLR is 43/34") { /* ... */ }
TEST_CASE("Parent of RLL is RL → 3/2") { /* ... */ }
TEST_CASE("Common prefix of RLL and RLLLR is RLL") { /* ... */ }
TEST_CASE("Children of root are 1/2 and 2/1") { /* ... */ }
```

These tests must pass before any audio code is written. The path module is foundational.

---

## 6. Parameter system

### Two-layer architecture

**Layer 1: AudioProcessorValueTreeState (APVTS).** JUCE's standard parameter system. Exposes parameters to the host (DAW), handles automation, MIDI learn, preset save/load via JUCE's built-in machinery. Every user-visible parameter is an APVTS parameter.

**Layer 2: Modulation graph.** Sits between APVTS and the audio engine. Each parameter that can be modulated has a `ModulationTarget` that combines the APVTS base value with contributions from registered sources.

The audio engine reads from the modulation graph, not directly from APVTS.

### ModulationSource interface

```cpp
class ModulationSource {
public:
    virtual ~ModulationSource() = default;
    
    /** Returns the current value of this source, normalized to [-1, +1]. */
    virtual float getModulationValue() const = 0;
    
    /** Stable identifier for this source (for preset serialization). */
    virtual juce::String getSourceId() const = 0;
};
```

In v1, concrete implementations:
- `MIDIVelocitySource` (per-voice, value from last note-on velocity)
- `MIDIModWheelSource` (channel-wide, CC1)
- `MIDIPitchBendSource` (channel-wide, mapped to [-1, +1])
- `MIDIChannelPressureSource` (channel-wide)
- `LFOSource` (wraps the LFO object)
- `AmpEnvSource` (wraps the amp envelope)

In v2, additional implementations (not in v1):
- `FareyPLLBankSource` (from ARS, one per tracked rational)
- `NNSClassifierSource` (from ARS)
- `SidechainEnvelopeSource` (from declared-but-unused sidechain input)
- `SpectralCentroidSource`
- etc.

### ModulationTarget

```cpp
struct ModulationConnection {
    ModulationSource* source;
    float depth;  // -1 to +1
    bool bipolar; // if true, source contributes ±depth; if false, 0..depth
};

class ModulationTarget {
public:
    void setBase(float v);
    float getBase() const;
    
    void addConnection(ModulationSource* source, float depth, bool bipolar = true);
    void removeConnection(ModulationSource* source);
    void clearConnections();
    
    /** Returns the effective value, base + Σ contributions, clamped to range. */
    float getEffectiveValue() const;
    
    void setRange(float min, float max);
    
private:
    float baseValue { 0.0f };
    float minValue { 0.0f }, maxValue { 1.0f };
    std::vector<ModulationConnection> connections;
};
```

### V1 routing (hardcoded)

For v1, the modulation routing is hardcoded — there is no user-facing mod matrix. The connections are:

- **Filter cutoff** ← APVTS base + LFOSource × LFODepth + AmpEnvSource × FilterEnvDepth(=0 in v1)
- **Carrier pitch** ← MIDI note (base) + PitchBendSource × 2 semitones
- **Carrier amplitude** ← AmpEnvSource (multiplied)
- **Modulator depth** (per-op) ← APVTS base × MIDIVelocitySource (optional, off by default)

Everything else is unrouted in v1. The infrastructure exists for v2 to route more sources to more targets without rewriting the audio engine.

### Why this matters for v2

When the v2 lab features arrive — feeding ARS analysis output into Brocot's parameters — the only changes required are:

1. Implement new `ModulationSource` subclasses for ARS outputs
2. Add UI affordance for the user to wire those sources to existing targets
3. Done

Nothing in the audio engine needs to change. Nothing in the parameter system needs to change. The hooks are already there.

---

## 7. UI layout

### Window and aspect

Target plugin window size: 1100 × 720 pixels at 1x. Resizable down to 880 × 580 (compact), up to 1600 × 1050 (expanded). Aspect roughly 1.5:1.

Standalone window opens at 1100 × 720 default with menu bar.

### Top-level layout

```
┌────────────────────────────────────────────────────────────────────────────┐
│  brocot · v1.0                                              [preset menu]  │
├────────────────────────────────────────────────────────────────────────────┤
│  [MIDI activity]    octave shift: [<] 0 [>]    master: [────●────] -6 dB  │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│  [ oscilloscope canvas, full width, ~120px tall ]                          │
│                                                                            │
├──────────────────────────────────────┬─────────────────────────────────────┤
│  frequency ratios                    │  depth ratios                       │
│  2/1 · 4/3 · 9/7 · · · · · ·         │  72 : 12 : 13  raw 5.13:0.86:0.90  │
├──────────────────────────────────────┴─────────────────────────────────────┤
│                                                                            │
│  [path mode toggle: ● shared  ○ free ]   shared path: R L L L R [+L] [+R] │
│                                                                            │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐            │
│  │ MOD 1        │ MOD 2        │ MOD 3        │ MOD 4        │            │
│  │ pos: 1 (R)   │ pos: 3 (RLL) │ pos: 5(RLLLR)│ off          │            │
│  │ 2/1          │ 4/3          │ 9/7          │              │            │
│  │ [depth: ●──] │ [depth: ──●] │ [depth: ──●] │              │            │
│  │ I=5.13       │ I=0.86       │ I=0.90       │              │            │
│  └──────────────┴──────────────┴──────────────┴──────────────┘            │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐            │
│  │ MOD 5        │ MOD 6        │ MOD 7        │ MOD 8        │            │
│  │ off          │ off          │ off          │ off          │            │
│  └──────────────┴──────────────┴──────────────┴──────────────┘            │
│                                                                            │
├────────────────────────────────────────────────────────────────────────────┤
│  filter                  │  amp envelope             │  lfo                │
│  type: LP BP HP NOTCH    │  A [●──] D [─●─]          │  shape: sine ▼      │
│  cutoff [────●────]      │  S [───●] R [──●──]       │  rate [──●──] 1.2Hz │
│  reso   [──●──]          │                           │  depth [●──] 0.20   │
│                          │                           │  ▢ key sync         │
└────────────────────────────────────────────────────────────────────────────┘
```

### Per-component breakdown

**`BrocotEditor`** — the top-level AudioProcessorEditor. Holds and arranges all child components. Owns the `BrocotLookAndFeel`. Handles resize.

**`PresetBar`** — top-right preset selector. Dropdown menu listing factory + user presets. Save/save-as/revert buttons. Preset name display.

**`Oscilloscope`** — canvas component. Pulls audio data from the processor's output buffer (or from an analyzer node) and draws the waveform. Amber waveform on dark background. Faint grid. Animation at ~60fps via `Timer`. Pauses when not playing.

**`RatioReadout`** — two-panel readout. Left: frequency ratios for all enabled operators. Right: depth ratios with simplest-integer approximation and residue. Both update live.

**`PathModeToggle`** — single toggle between "shared" and "free". When shared, the `PathNavigator` is visible; when free, it's hidden.

**`PathNavigator`** — the shared-path widget. Shows the current path as a sequence of L/R cells. `[+L]` and `[+R]` buttons to extend. `[-]` button to retract. Each step in the path is clickable to highlight; clicking a step sets the "viewing depth" but does not move operators.

**`OperatorPanel`** — one mod card. Eight instances. Contents differ slightly between shared and free mode:
- **Shared mode**: enabled toggle, position-along-path slider/spinbox (0..pathLength), ratio display, depth slider with Bessel ticks.
- **Free mode**: enabled toggle, L/R navigation buttons + parent + root, ratio display, depth slider with Bessel ticks.

The two modes share the depth slider and ratio display components. The difference is the navigation widget on top.

**`DepthSlider`** — custom slider class extending `juce::Slider`. Draws the standard track and thumb plus the Bessel landmark ticks above the track. Amber ticks for peaks, cyan ticks for zeros. Major (n ≤ 2) ticks taller than minor. Hover shows "J_n peak" or "J_n null" tooltip. Below the slider, a label showing the regime ("tongue edge", "near-locked", "rich sidebands", "clangy") and the nearest landmark (if within 0.12). Range 0 to 8.

**`FilterPanel`** — filter type selector (4 buttons or a row of toggle buttons: LP, BP, HP, Notch), cutoff slider (20 Hz to 20 kHz, logarithmic), resonance slider (0 to 1). Filter cutoff slider shows the current modulated cutoff value as a secondary indicator when LFO is sweeping.

**`EnvelopePanel`** — four sliders (A, D, S, R) in a row. Optionally a small visualization of the envelope shape that updates as sliders move.

**`LFOPanel`** — shape selector (4 buttons or dropdown: sine, triangle, square, saw), rate slider (0.01 to 20 Hz, logarithmic), depth slider (0 to 1), key-sync toggle.

### Look and feel

**Colors** (CSS-like names for reference):
- Background: very dark stone — RGB(12, 10, 9)
- Surface (cards): slightly lighter — RGB(20, 18, 16)
- Border: dim amber — RGB(120, 80, 30) at low alpha
- Primary text: warm white / cream — RGB(254, 243, 199)
- Secondary text: amber-200 — RGB(253, 230, 138) at 60% alpha
- Accent / amber: amber-500 — RGB(245, 158, 11)
- Bright amber: amber-300 — RGB(252, 211, 77)
- Cyan (for Bessel zeros): RGB(103, 232, 249)
- Disabled: stone-600 — RGB(87, 83, 78)

**Fonts**:
- Headings, math (ratios), display: EB Garamond, italic for ratios
- Numbers, parameters, monospace labels: JetBrains Mono
- Both loaded from `resources/fonts/`, accessed via JUCE's `Typeface::createSystemTypefaceFor()`

**Custom drawing**:
- All sliders, buttons, and panels use the custom `BrocotLookAndFeel` subclass of `LookAndFeel_V4`.
- Sliders: thin horizontal track, small amber circular thumb, glow effect on hover.
- Buttons: flat backgrounds, amber text, amber bottom-border on hover, inverted (amber background, dark text) when active.
- Cards: 1px amber-at-low-alpha border, slightly raised surface color.

The aesthetic target is "vintage scientific instrument meets demoscene meets math notebook." Dark mode only in v1. Light mode is v2 if anyone asks (nobody will).

### Resize behavior

The layout has three breakpoints:
- ≥1100px wide: full layout, 4×2 operator grid
- ≥880px: compact layout, 2×4 operator grid (vertical), filter/env/LFO panel becomes tabbed
- <880px: minimum, single-column with collapsing panels (probably not worth implementing fully in v1)

---

## 8. MIDI handling

### Note allocation

V1 monophonic:
- Note-on with active note: retrigger envelope, update pitch
- Note-off matching active note: release envelope, hold playing tail
- Note-off non-matching: ignore

V1 duophonic:
- Note-on with one voice free: assign to free voice, retrigger that voice's envelope
- Note-on with both voices busy: steal the older voice (longer time since note-on), retrigger
- Note-off matching a playing voice: release that voice's envelope

Use `juce::Synthesiser` with two `BrocotVoice` instances for duo, or a custom mono manager for mono. The Synthesiser machinery is fine for both if voice count = 1 or 2.

### Controllers

- CC1 (mod wheel) → `MIDIModWheelSource`, routed to nothing by default in v1, but exposed as a routing target option in UI for at least filter cutoff and LFO depth
- Channel pressure → `MIDIChannelPressureSource`, similar
- Pitch bend → wired to carrier pitch, ±2 semitones (or configurable in v1.x)
- Sustain pedal (CC64) → standard sustain behavior (hold envelopes in sustain stage)
- Velocity → per-voice `MIDIVelocitySource`, optionally routes to amp env initial level and/or modulator depth (toggleable per-op in v1.x)

### MIDI learn

Out of scope for v1. JUCE's host integration covers automation; MIDI learn at the plugin level can be added later if requested.

---

## 9. Preset format

### Schema

```json
{
  "name": "Avatar·1",
  "author": "Combust",
  "description": "First discovered preset — shared-path RLLLR with three ops",
  "version": "1.0",
  "category": "discovered",
  
  "engine": {
    "polyphony": "mono",
    "octaveShift": 0,
    "pathMode": "shared",
    "sharedPath": "RLLLR",
    "operators": [
      {"enabled": true,  "position": 1, "depth": 5.13},
      {"enabled": true,  "position": 3, "depth": 0.86},
      {"enabled": true,  "position": 5, "depth": 0.90},
      {"enabled": false, "position": 0, "depth": 0.0 },
      {"enabled": false, "position": 0, "depth": 0.0 },
      {"enabled": false, "position": 0, "depth": 0.0 },
      {"enabled": false, "position": 0, "depth": 0.0 },
      {"enabled": false, "position": 0, "depth": 0.0 }
    ]
  },
  
  "filter": {
    "type": "lp",
    "cutoff": 0.65,
    "resonance": 0.15
  },
  
  "envelope": {
    "attack":  0.01,
    "decay":   0.15,
    "sustain": 0.80,
    "release": 0.40
  },
  
  "lfo": {
    "shape":   "sine",
    "rate":    0.5,
    "depth":   0.0,
    "keySync": false
  },
  
  "master": {
    "level": 0.5
  }
}
```

### Free-mode variant

If `pathMode == "free"`, each operator has a `path` string instead of a `position`, and `sharedPath` is omitted:

```json
"engine": {
  "pathMode": "free",
  "operators": [
    {"enabled": true, "path": "R",      "depth": 1.0},
    {"enabled": true, "path": "RLL",    "depth": 0.5},
    {"enabled": true, "path": "RLLLR",  "depth": 0.3},
    ...
  ]
}
```

### File extension

`.brocot` — gives the format a distinct identity and avoids confusion with generic `.json`.

### Locations

- Factory presets: ship in plugin bundle under `resources/presets/factory/`
- User presets: OS-standard application support directory
  - macOS: `~/Library/Application Support/Brocot/Presets/`
  - Windows: `%APPDATA%\Brocot\Presets\`
  - Linux: `~/.config/brocot/presets/`

User can also save to arbitrary path via Save As.

### Versioning

The `version` field allows future format migrations. v1 only reads v1 presets. v2 reads both.

---

## 10. Factory presets and ARS-derived tables

Ship with ~32 factory presets, organized in four categories of 8.

### Category 1 — First principles (from web prototype)

1. **Pure** — carrier only, all mods off. The reference timbre.
2. **J₀ null** — single mod at 1/1, depth 2.405. Carrier vanishes; sidebands only.
3. **Octave·J₁** — single mod at 2/1, depth 1.841. First sideband peak.
4. **Three peaks** — three mods at 3/2, 4/3, 5/3 with depths at J₁, J₂, J₃ peaks.
5. **1:2:3** — three mods at 2/1, 3/2, 4/3 with depths 1.0, 2.0, 3.0.
6. **Mediants** — three near-1 mods at low depth (the "soft" preset).
7. **Detune** — two mods at adjacent deep Stern-Brocot nodes at very low depth.
8. **Deep** — three mods at clangy high depths.

### Category 2 — Avatar's discoveries

9. **Avatar·1** — RLLLR shared path, positions 1/3/5 at depths 5.13, 0.86, 0.90.
10–16. **Avatar·2 through Avatar·8** — to be filled in by Will during prototype testing. Each is a hand-picked timbral landmark with descriptive name. These are the personal canon.

### Category 3 — Path canon

17. **Trunk·R** — shared path R, ops at positions 1, 2, 3, 4. The right spine.
18. **Trunk·L** — shared path L, ops at positions 1, 2, 3, 4. The left spine.
19. **Spiral·RLR** — shared path RLRLRLR (alternating), ops at every other position.
20. **Convergent·RL∞** — shared path RLLLLLLL (R then deep L), ops spread across.
21. **Convergent·LR∞** — mirror of above.
22. **Golden** — shared path that converges toward 1/φ (the golden ratio): R L R L R L R L ... (Fibonacci-like). 8 ops at increasing positions.
23. **Silver** — shared path converging toward 1+√2: R R L R R L R R ... (the silver-ratio continued fraction).
24. **Prime·trunk** — shared path constructed from successive denominators that are prime (3, 5, 7, 11, 13). One of the ARS-derived tables provides this.

### Category 4 — Truax classics

25–32. **Truax·polyphonic·1 through Truax·polyphonic·8** — recreations of the polyphonic timbral constructions from Truax's 1978 paper. Each uses 4-6 modulators at Farey-series ratios with classical FM depths (I = 1-3). These are the historical canon and the lineage acknowledgment.

### ARS-derived tables (resources/tables/)

The ARS work has discovered structural facts about Farey/Stern-Brocot space that can be precomputed offline and shipped as static tables Brocot reads at startup. **None of this requires ARS code to ship with Brocot** — the tables are JSON files containing the results.

**`bessel_landmarks.json`** — the Bessel peak and zero positions used by the depth slider. Constants:

```json
{
  "peaks": [
    {"n": 1, "I": 1.8412, "label": "J1_peak"},
    {"n": 2, "I": 3.0542, "label": "J2_peak"},
    {"n": 3, "I": 4.2012, "label": "J3_peak"},
    {"n": 4, "I": 5.3176, "label": "J4_peak"},
    {"n": 5, "I": 6.4156, "label": "J5_peak"}
  ],
  "zeros": [
    {"n": 0, "I": 2.4048, "label": "J0_null"},
    {"n": 1, "I": 3.8317, "label": "J1_null"},
    {"n": 2, "I": 5.1356, "label": "J2_null"},
    {"n": 0, "I": 5.5201, "label": "J0_null_2"},
    {"n": 1, "I": 7.0156, "label": "J1_null_2"}
  ]
}
```

**`characteristic_paths.json`** — paths through the Stern-Brocot tree that ARS analysis has identified as producing distinctive spectral or NNS signatures. Each entry has a path, a name, and metadata. Example structure (to be filled in by Will from ARS output):

```json
{
  "version": "1.0",
  "source": "ARS analysis runs, see codeberg.org/combust/ARS",
  "paths": [
    {
      "path": "RLLLR",
      "name": "9/7 trunk",
      "description": "Path Will discovered manually; emerges from ARS Phase X as a convergence point of...",
      "metadata": {
        "limit_ratio": 1.2857,
        "continued_fraction": [1, 3, 1, 1],
        "ars_classification": "GUE-like at certain depths"
      }
    },
    ...
  ]
}
```

The exact content here is determined by what ARS findings Will wants to embed. The point is the *format* — a JSON list of paths with metadata that the synth can offer as "characteristic paths" in a menu, without depending on the analysis machinery.

**`truax_constructions.json`** — the C:M ratios from Truax's 1978 paper, organized by his categories (harmonic ratios, inharmonic-but-related, "missing fundamental" constructions, etc.). Used to seed the Truax-category presets and as a reference table users can browse in a "library" view (v1.x feature).

### Path table as v2 hook

The `characteristic_paths.json` is also where the v2 ARS integration first appears. In v1, the file is static — Will hand-edits it with paths he wants to ship. In v2, the file can be updated by an ARS analysis run: feed audio or spike trains into ARS, get a list of "characteristic paths" out, write the JSON, restart Brocot. The plumbing already exists.

---

## 11. Build, CI, packaging

### Build system

- **CMake** ≥ 3.22
- **JUCE** 7.x or 8.x (use whichever is current at start of project; pin in CMake)
- Vendored as a git submodule under `external/JUCE`
- C++ standard: C++20

### Plugin formats

- VST3 (Windows, macOS, Linux)
- AU (macOS)
- Standalone (Windows, macOS, Linux)
- AAX is not a v1 target (requires Avid SDK license)

### Build matrix

| OS         | Architecture | Compiler  | Standalone | VST3 | AU  |
|------------|--------------|-----------|------------|------|-----|
| macOS      | universal    | Clang     | ✓          | ✓    | ✓   |
| Windows    | x64          | MSVC      | ✓          | ✓    | —   |
| Linux      | x64          | GCC/Clang | ✓          | ✓    | —   |

### CI

Self-hosted Forgejo Actions (matching The Spot's setup). Workflow file at `.gitea/workflows/build.yml`:

- Trigger on push to main and on pull request
- Three jobs: build-macos, build-windows, build-linux
- Each job: checkout, cache CMake/JUCE, configure, build, run tests, archive artifacts
- Artifacts: standalone binary + VST3 bundle per platform, signed if certs available (v1.x), unsigned otherwise

### Signing

V1 ships unsigned. macOS users will need to right-click → open the first time. Windows users will see SmartScreen warning. This is fine for v1.

V1.x: macOS signing + notarization (requires Apple Developer account, $99/yr), Windows signing (requires code-signing cert, $200-400/yr from a CA). Defer until there are paying users or it becomes a friction point.

### Distribution

- Forgejo releases page (source + binaries)
- Eventually a `brocot.fm` download page
- For v1, GitHub mirror also if desired (good for discoverability)

---

## 12. Slice-by-slice work plan

Each slice is a single task to hand Claude Code. Slices have:
- **Goal**: what gets built
- **Spec section**: which part of this document is the reference
- **Verification**: how to know it's done
- **Dependencies**: which prior slices must be complete

Work the slices in order. Do not skip ahead. Do not combine slices.

### Slice 1 — Project scaffold

**Goal**: JUCE plugin project that builds clean on macOS, Windows, Linux, opens an empty window, makes no sound, passes through MIDI silently.

**Spec section**: §3 (project layout), §11 (build).

**Verification**:
- `cmake --build` succeeds on all three platforms.
- Standalone runs and shows a blank dark-stone window.
- VST3 loads in a DAW host (Reaper / Ableton / etc.).
- No audio output regardless of input.

**Dependencies**: none.

**Files created**: CMakeLists.txt, source/PluginProcessor.{h,cpp}, source/PluginEditor.{h,cpp}, basic .gitignore, README stub.

### Slice 2 — Path module

**Goal**: `Path` class with full API, no audio integration yet. Unit tests pass.

**Spec section**: §5.

**Verification**: all listed unit tests pass. The test suite from §5 must run as part of the test target.

**Dependencies**: Slice 1.

**Files created**: source/audio/Path.{h,cpp}, tests/PathTests.cpp, test target added to CMakeLists.txt.

### Slice 3 — Single-voice sine FM core

**Goal**: A single hard-coded FM voice plays a continuous tone when audio is requested. Carrier at 220 Hz, one modulator at ratio 2/1, I=1.841. The "octave·J₁" preset, hard-coded.

**Spec section**: §4 (audio math).

**Verification**:
- Plugin makes a sound (audible drone) when loaded.
- Spectrum (via DAW spectrum analyzer or visual inspection) shows expected sideband structure for that preset.
- Output matches web prototype's "octave·J₁" preset, verified by ear or by recorded comparison.

**Dependencies**: Slice 2 (uses `Path` to resolve the 2/1 ratio).

**Files created**: source/audio/BrocotVoice.{h,cpp}, source/audio/Operator.{h,cpp}. PluginProcessor wired up to instantiate one voice and route to output.

### Slice 4 — Multi-modulator audio engine

**Goal**: BrocotVoice supports N=8 operators in parallel. Hard-coded test setup with 3 operators on shared path RLLLR at positions 1/3/5, depths 5.13/0.86/0.90.

**Spec section**: §4.

**Verification**:
- Sound matches web prototype's "Avatar·1" timbre (spectrum compared).
- Audio regression test passes: render N seconds of audio at known parameters, compare RMS or spectral features to a reference WAV exported from the prototype, RMS difference under threshold.

**Dependencies**: Slice 3.

**Files created**: extends BrocotVoice and Operator. Adds tests/AudioRegressionTests.cpp with the reference WAV in tests/fixtures/.

### Slice 5 — MIDI input + amp envelope

**Goal**: Voice responds to MIDI note-on/note-off. ADSR amplitude envelope. Mono polyphony with latest-note priority. Carrier frequency follows MIDI pitch.

**Spec section**: §8 (MIDI), §2 (envelope decision).

**Verification**:
- Playing a MIDI keyboard produces sounded notes at correct pitches.
- Note-off releases the envelope with audible decay.
- Retrigger on overlapping notes works correctly.

**Dependencies**: Slice 4.

**Files created**: source/audio/AmpEnvelope.{h,cpp}. PluginProcessor updated for MIDI handling. BrocotVoice updated to consume MIDI note + envelope.

### Slice 6 — Parameter system

**Goal**: All audio parameters exposed via APVTS. ModulationSource/Target abstraction in place but with only MIDI-derived sources registered. Host-level parameter automation works.

**Spec section**: §6.

**Verification**:
- All parameters appear in DAW host's automation list with correct names and ranges.
- Recording automation in DAW and playing back produces audible parameter changes.
- Parameter persistence: save plugin state in DAW project, reload, parameters restored.

**Dependencies**: Slice 5.

**Files created**: source/params/ParameterTree.{h,cpp}, source/params/ModSource.h, source/params/ModTarget.{h,cpp}, source/params/sources/*.

### Slice 7 — SVF filter + LFO

**Goal**: SVF filter on output bus, LFO routed to filter cutoff.

**Spec section**: §2 (filter and LFO decisions).

**Verification**:
- Filter cutoff control audibly sweeps the spectrum.
- Filter type buttons switch between LP/BP/HP/Notch with appropriate sonic results.
- LFO with depth > 0 produces audible cutoff modulation at the set rate.
- LFO shape switching produces appropriate waveform.

**Dependencies**: Slice 6.

**Files created**: source/audio/FilterBank.{h,cpp}, source/audio/LFO.{h,cpp}. PluginProcessor pipeline updated.

### Slice 8 — UI scaffold + operator panels (free mode)

**Goal**: Custom editor with the layout from §7. Eight operator panels in free mode only. L/R/Parent/Root navigation buttons functional. Depth sliders without Bessel ticks yet.

**Spec section**: §7.

**Verification**:
- Editor opens with described layout.
- Clicking L/R changes the operator's path and the audible ratio updates.
- Dragging depth slider changes modulation audibly.
- Plugin window resizes acceptably.

**Dependencies**: Slice 7.

**Files created**: source/ui/BrocotEditor.{h,cpp}, source/ui/OperatorPanel.{h,cpp}, source/ui/BrocotLookAndFeel.{h,cpp}, source/util/Fonts.{h,cpp}, source/util/Colors.h. Resource files for fonts.

### Slice 9 — Depth slider with Bessel landmarks

**Goal**: Custom `DepthSlider` component with visible peak/zero ticks, regime label, nearest-landmark display.

**Spec section**: §7 (DepthSlider).

**Verification**:
- Each operator's depth slider shows amber and cyan ticks at the documented positions.
- Hovering a tick shows tooltip with landmark name.
- Sliding past a landmark updates the "nearest landmark" text live.
- Regime label updates as depth changes ("tongue edge" → "near-locked" → ...).

**Dependencies**: Slice 8.

**Files created**: source/ui/DepthSlider.{h,cpp}, source/audio/Bessel.{h,cpp}, resources/tables/bessel_landmarks.json.

### Slice 10 — Shared path mode + path navigator

**Goal**: Path-mode toggle switches between free and shared. Shared mode shows the `PathNavigator` and operator panels switch to position-based UI.

**Spec section**: §2 (path-mode decision), §7 (PathNavigator).

**Verification**:
- Toggle switches mode visually without crash.
- In shared mode, extending the shared path and changing operator positions audibly updates the synth.
- Switching modes preserves audio output (no jump in sound).
- Default app state is shared mode with empty path and 3 enabled operators at the same position.

**Dependencies**: Slice 9.

**Files created**: source/ui/PathNavigator.{h,cpp}, source/ui/PathModeToggle.{h,cpp}. OperatorPanel updated to switch UI based on mode.

### Slice 11 — Analysis displays (oscilloscope + ratio readouts)

**Goal**: Oscilloscope drawing live waveform. Frequency-ratio and depth-ratio readouts updating live.

**Spec section**: §7.

**Verification**:
- Oscilloscope shows live audio output, refreshes smoothly.
- Frequency ratio readout shows current ratios of enabled operators.
- Depth ratio readout shows simplest-integer approximation and residue, updates as depths change.

**Dependencies**: Slice 10.

**Files created**: source/ui/Oscilloscope.{h,cpp}, source/ui/RatioReadout.{h,cpp}.

### Slice 12 — Filter / Env / LFO panels

**Goal**: Three side-by-side panels for filter, envelope, LFO controls.

**Spec section**: §7.

**Verification**: all controls connected to their audio counterparts and audible.

**Dependencies**: Slice 11.

**Files created**: source/ui/FilterPanel.{h,cpp}, source/ui/EnvelopePanel.{h,cpp}, source/ui/LFOPanel.{h,cpp}.

### Slice 13 — Preset system

**Goal**: JSON preset load and save. Preset menu in top bar. Save-as dialog. Current preset name display.

**Spec section**: §9.

**Verification**:
- Saving current state to a `.brocot` file produces valid JSON matching the schema.
- Loading a saved preset restores all parameters exactly.
- Roundtrip test: save preset, modify all params, load preset, all params restored.

**Dependencies**: Slice 12.

**Files created**: source/presets/PresetManager.{h,cpp}, source/presets/PresetSchema.{h,cpp}, tests/PresetRoundtripTests.cpp.

### Slice 14 — Factory presets

**Goal**: All 32 factory presets shipped with the plugin. Preset menu lists them under category headers.

**Spec section**: §10.

**Verification**: every factory preset loads without error and produces audible output. Each sounds distinct from neighbors. Avatar·1 (preset #9) sounds correct.

**Dependencies**: Slice 13.

**Files created**: resources/presets/factory/*.brocot (32 files), resources/tables/characteristic_paths.json, resources/tables/truax_constructions.json.

### Slice 15 — Duophonic voice

**Goal**: Polyphony mode selector (mono / duo). Duo plays two simultaneous notes.

**Spec section**: §8.

**Verification**:
- In duo mode, playing two notes simultaneously produces two distinct pitches.
- Voice stealing on third overlapping note works correctly.
- Switching modes mid-play doesn't crash or produce stuck notes.

**Dependencies**: Slice 14.

**Files created**: BrocotVoice unchanged; PluginProcessor updated to manage voice allocation.

### Slice 16 — Polish, About, ship

**Goal**: About dialog (credits Truax 1978, Burraston/noyzelab 2016, Stern, Brocot, Will Combust). Tooltips on UI elements. Final pass on resize behavior. v1.0 tag and release.

**Spec section**: this whole document is now reference material.

**Verification**:
- About dialog renders correctly.
- Tooltips appear on hover for primary controls.
- Resize works at all three breakpoints.
- v1.0 git tag exists; release artifacts built and uploaded.

**Dependencies**: Slice 15.

---

## Appendix A — Things to remember

The discoveries that produced this design:

1. **Path-prefix coherence equals sideband-structure coherence.** Operators on a shared path through the tree share denominator structure, which means their sidebands coincide on shared partials, which means the spectrum sounds organized. This is the architectural justification for shared-path mode being the default.

2. **The interesting regime is low-I, near-unity.** Not DX7's high-I, harmonic-ratio regime. The default UI ranges, factory presets, and the visual emphasis on the depth slider should all reflect this.

3. **Bessel landmarks are scale degrees.** Treat them as named structural positions, not as math trivia. Users navigate by landmark name as much as by feel.

4. **The math is the interface.** Every constraint comes from a mathematical fact. Don't expose floats where paths belong. Don't expose raw Hz where modulation index belongs. Don't expose unrestricted ratios where Stern-Brocot navigation belongs.

5. **Modulation source/target hooks are cheap to add and expensive to skip.** Add them in v1 even though only MIDI/LFO/Env use them. v2 will thank v1.

6. **Sidechain bus declared, not implemented.** Future-proofs the I/O config.

7. **Truax 1978 is the lineage.** Credit him in the About box. Send him a copy when v1 ships.

## Appendix B — Things not to do

1. **Do not add a modulation matrix in v1.** Hardcoded routing.
2. **Do not add a second envelope in v1.** Amp only.
3. **Do not add curated ratio shortcuts that bypass the tree.** "Snap to musical interval" buttons defeat the entire instrument.
4. **Do not allow path strings to be entered manually as text.** Forces the user to navigate. Loading presets is fine because it's preset-via-author, not user-typed.
5. **Do not implement nested FM as a hidden mode.** Parallel only.
6. **Do not implement polyphony beyond duo in v1.** Holding the line on this saves a week.
7. **Do not implement a modulation source registry that loads from a plugin directory.** v2 problem. Hardcoded list in v1.
8. **Do not write a custom audio thread.** Use JUCE's AudioProcessor block-processing model.
9. **Do not add presets that aren't in one of the four documented categories.** Eight per category, 32 total. Trim later if any are weak.
10. **Do not let scope creep extend v1.** If a feature isn't in this spec, it's v2. Write it down in `BROCOT-V2-IDEAS.md` and move on.

## Appendix B½ — v1 constraints deliberately superseded since ship

The v1 spec froze; the instrument kept moving. These Appendix B lines were
consciously retired in later phases (each with its own tests), not violated:

- **B1 (no mod matrix) / B2 (amp env only)** — superseded by the v2 universal
  modulator bank (16 modulators, source/target routing, `.brocotmod` files).
- **B6 (no polyphony beyond duo)** — superseded: mono/duo/poly up to 16 voices
  with equal-power per-voice level.
- **B9 (32 presets, four categories)** — superseded by the Phase-3 factory
  packs (~4.3k files; the menu shows the ~47 curated anchors).
- **§2 "8 operators"-era sizing** — the engine now carries 64 operators; the
  render benchmark showed 64 costs only ~1.5× of 32.
- Still standing: B3 (no snap-to-interval shortcuts), B4 (no typed path
  strings), B5 (no nested FM — op-0 *self*-feedback shipped, which is
  self-PM, not a modulator chain), B8, B10.

## Appendix C — V2 / Future roadmap (informational only)

- Lab mode: ARS integration. Farey-PLL bank, NNS classifier, spectral analyzers as modulation sources.
- Sidechain audio input wired through envelope follower.
- Full polyphony (up to 8 voices).
- 16 operators.
- Modulation matrix UI.
- Second envelope (filter env).
- Multiple LFOs.
- MPE support.
- Microtuning via Scala (.scl) files — fits naturally because Stern-Brocot tree generates rational tunings.
- Hardware version (Daisy Seed / RP2350 / ESP32-S3 platform).
- Preset sharing site at `presets.brocot.fm`.

V2 is not v1's problem. Do not let it become v1's problem.

---

## Addendum D — The butterfly / lattice pass (Phase 4, 2026-07)

The 2026-07 refinement pass was driven by a separate handoff spec (the
Hofstadter / almost-Mathieu ↔ Brocot ratio-axis programme) whose section
numbers were cited throughout the code but never committed to this repo.
This addendum reproduces that numbering so every citation resolves. **Any
`§0/§1/§2/§4.x/§6/§P·` citation in `BrocotLattice.h`, `Coherence.h`,
`CoherenceSuggest.h`, `RatioReadout`, `FareyView`, `brocot_lattice.py`, or
`butterfly_map_study.py` refers to THIS addendum, not the v1 sections above.**

### D§0 — Central claim (tested, mostly falsified)

The navigation map's ratio axis θ = p/q *is* the almost-Mathieu (Harper)
model's magnetic-flux axis, so the measured timbre landscape should exhibit
the butterfly's band/gap structure; corollary (the "criticality insight"):
deep-CF ratios should line the measured voids. **Status:** tested
falsifiably in `phase3/butterfly.md` across six maps. The aggregate map is
NOT the butterfly (op-count dominates). Single-op, gap prominence correlates
with the measured geometry with the predicted sign — but is fully shadowed
by plain Farey simplicity (q). The cfDepth↔voids corollary is not confirmed.

### D§1 — Do-not-overclaim

No association ships as a visual layer unless it beats a shuffle null AND a
non-negligible effect-size floor AND the trivial baselines. Negative results
are written down and gate future work (see D§P0-b).

### D§2 — Lattice foundation

`source/audio/BrocotLattice.h` (header-only, self-contained Jacobi
eigensolver) and its Python mirror `phase3/brocot_lattice.py`: almost-Mathieu
spectrum via the q×q Harper matrix evaluated at the 4 magnetic-BZ corners
(band-edge method), gaps, Chern integers via j·modinv(p,q), prominence.

### D§4 — Tenets (cited as §4.1–§4.5)

- **§4.1 Structural, not literal.** The butterfly is an organizing /
  annotation layer over ratio space — never a claim that a played note "is a
  band or a gap."
- **§4.2 Precise notions only.** `cfDepth` is the continued-fraction
  partial-quotient SUM (= SB path depth + 1), NOT liminf-K or
  Borel–Bernstein; do not substitute look-alike quantities.
- **§4.3 Test-earns-it.** Every proposed map↔butterfly link is tested against
  a permutation null with effect size read ahead of significance
  (`butterfly_map_study.py`), and only what survives may drive UI.
- **§4.4 Four quantities, kept strictly distinct.** `simplicity(q) = q`;
  `cfDepth`; `prominence` (widest spectral gap); `chern` (Wannier/Středa
  integer). Each owns exactly one concept; no conflation (and none with
  GrooveField's 1/(p·q) well weight).
- **§4.5 λ is a parameter.** The almost-Mathieu coupling defaults to the
  self-dual critical point λ = 1 but is never hardcoded as a constant.

### D§6 — Regression fixtures

Hand-derived almost-Mathieu fixtures (θ = 1/3 band edges; Chern sequences
via modular inverse) enforced by `tests/BrocotLatticeTests.cpp` and
`phase3/tests/test_brocot_lattice.py` — the C++ and Python implementations
must agree with the fixtures and with each other.

### D§P — The prioritized programme

- **P0-a — Path-prefix coherence (SHIPPED 2026-07-20).** `Coherence.h`
  meter (fraction of sideband energy on partials shared by ≥ 2 modulators,
  cents-binned, scale-invariant) in `RatioReadout`; `CoherenceSuggest.h`
  suggest-mode (#1) with clickable prefix-extension chips on the drive tab.
- **P0-b — Prominence terrain overlay (CLOSED as a documented negative,
  2026-08-04).** The pre-registered gate failed: simplicity beats prominence
  on every map, both fields, both low-op subsets. Earned fallback: a
  no-claims "simplicity" colour mode on the cloud.
- **P1-a — Farey navigator (SHIPPED 2026-07-20).** `FareyView`: baked
  butterfly (q ≤ 32, gaps Chern-tinted), per-op flux columns, SB cursor with
  clickable L/R children sized by prominence; plus the map study itself.
- **P1-b — Criticality shimmer (this pass).** Voids/criticality rendering on
  the cloud from MEASURED per-node fields only (density underlay + high-crit
  twinkle) — no butterfly quantities involved, per the P0-b verdict.
- **P2 — Chern-timbre and near-unity experiments (OPEN).** No committed
  design; must clear D§1/D§4.3 before any of it reaches the UI.

## Addendum E — The coincidence-horizon pass (Phase 5, 2026-08)

The ARS programme's Brocot arc produced an exact result and a display spec,
both committed at the repo root; this addendum records what shipped from
them and the wording discipline they impose. **Source of truth:**
[`COINCIDENCE-HORIZON.md`](./COINCIDENCE-HORIZON.md) (the theorem, its
proof, its measured validity boundary) and [`MAP-FIELD.md`](./MAP-FIELD.md)
(the two-layer field, the design list re-ranked by masking). Every claim
below inherits their scope verbatim.

### E§1 — The theorem, one line

For two sine modulators at ratios r1, r2 (α = r2/r1 = p/q lowest terms),
a direct sideband coincidence is reachable ⟺ `p ≤ 2·order_bound(I1)` AND
`q ≤ 2·order_bound(I2)`. Exact at N = 2 sine; stack-extended (q-side only)
for harmonic waves; dissolved at N ≥ 4. The rationals are the entire
habitable zone — the tree enumerates the interesting space exactly.

### E§2 — Shipped engine machinery

- `source/audio/Horizon.h` — `orderBound` (bit-faithful mirror of
  `partial_prediction.order_bound`), the asymmetric `ringable` predicate,
  the proof's sufficiency witness, `scopeFor` (Exact / StackExtended /
  Degraded / Dissolved). All re-verified in-tree against brute-force
  enumeration (`tests/HorizonTests.cpp`).
- `source/audio/MapField.h` — `parentOf` (the denominator-weighted argmin;
  matched box = the proved convergent frame, per-side box = the derivation's
  own bounds), `separationHz` (the load-bearing f_c·r1 unit factor),
  regime bands, `solveBeat` with per-side basin refusal.
- `source/util/FieldFrame.h` — the pair frame over a real patch, and
  `solveBeatCents` (beat targeting as a detune, refuse-don't-clamp).

### E§3 — Shipped interface

- **Beat targeting** (MAP-FIELD item 5, the primary perceptual
  deliverable): right-click the detune slider → target beat rates solved in
  closed form; basin-leaving targets show disabled with the reason.
- **Field readout** (item 2): RatioReadout's field line — nearest ringing
  ratio, separation in Hz at the played carrier, regime band.
- **Horizon overlay** (item 3 + the §4 proposed feature): FareyView marks
  child nodes with no coincidence structure at the current depth — drawn
  ONLY in Exact scope (two effective sine modulators), judged per node
  under both index assignments, redrawn as depth moves.
- **CoherenceSuggest reachability filter** (item 6): candidates must be
  horizon-reachable from a current operator at that operator's own index;
  SteerTab distinguishes "none improves coherence" from "none can coincide".

### E§4 — Continuous waveform axes, each on its own warrant

- **`op.evensup`** — Fritz double-pulse even suppression (Pulse only).
  Warrant: `brocot_double_pulse_sweep` — 20 dB even-harmonic reach, zero
  ratios moved at any β. Parity is orthogonal to the ringing set.
- **`op.fold`** — sine fold `sin(β·s)`, unit peak. Warrant: the SEALED
  census `phase3/wavefold_census.py` → `phase3/wavefold.md`
  (`SHIP_CONTINUOUS_EXTENSION`): a continuous, monotone, q-side-only
  horizon extension. Support-level result; no audibility claim.

### E§5 — Wording gates (do not relax without the cited measurement)

- Annotations claim **coincidence structure**, never "alive/dead/worth
  visiting" — above-horizon ratios are perfectly distinct; they simply
  cannot fuse.
- Morph UI captions **structural events**, never "jumps"; morphs are not
  linearisable (four attempts, two coordinates — see COINCIDENCE-HORIZON
  §morphing) and the honest feature is marking events, not smoothing.
- Perceptual language rides on the **separation** (regime bands), never on
  the region; "heard as a detuned p/q" is unearned until the listening
  gates in MAP-FIELD say otherwise.
- The horizon annotation is scope-gated in the interface (E§3), stated,
  never discovered by a player on a large patch.

---

*End of specification. Total length: ~3000 lines. Hand sections to Claude Code one slice at a time. Resist combining slices. Resist scope creep. Ship v1 in two weeks.*
