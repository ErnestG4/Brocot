# Brocot factory presets

32 hand-authored presets in four categories of 8, per BROCOT-SPEC.md §10.
Phase 3 adds an ARS-discovered tier on top (target: ~96 more across 12
musical-family categories).

## Categories

### 01-08 First principles
Reference timbres derived from the web prototype: pure carrier, the J₀ null,
the J₁ peak, single-Bessel-landmark and small-stack constructions.

### 09-16 Avatar
Will's personal canon. **Slot 9 (Avatar 1) is the authoritative
discovery preset; slots 10-16 currently ship as placeholders for Will to
swap in during prototype testing.**

### 17-24 Path canon
Structural paths through the Stern-Brocot tree — R-trunk, L-trunk, alternating
spiral, convergents, golden / silver continued fractions, prime trunk.

### 25-32 Truax classics
Reconstructions inspired by Truax 1978 *Organizational Techniques for C:M
Ratios in Frequency Modulation*. Farey-series harmonic and inharmonic
constructions.

## Format (v2)

`.brocot` files are JSON; the loader requires schema version `"2.0"` with
a top-level `provenance` block. Older v1 files (no `provenance`) are
auto-migrated on load. The saver always emits v2.

Top-level fields:

```jsonc
{
  "name":     "Avatar 1",          // required
  "version":  "2.0",               // required (or "1.0" for legacy)
  "provenance": {
    "kind":   "manual",            // "manual" | "family-search"
                                   //          | "criticality-search"
                                   //          | "reference-match"
    "author":      "Combust",
    "description": "Free text",
    "category":    "discovered",   // grouping hint for the preset menu

    // present only for search-discovered presets (P3.5 forward):
    "ars_version": "0.x",
    "search": {
      "family":    "sawtooth",
      "seed":      12345,
      "objective": 0.084,
      "params":    { "n_samples": 1000, "n_refine": 20, "q_max": 16 },
      "timestamp": "2026-05-12T22:00:00Z"
    }
  },

  "engine":   { /* operators, path mode, polyphony, octave shift */ },
  "filter":   { /* type, cutoff, resonance */ },
  "envelope": { /* legacy ADSR — retained for v1 compat, inert at runtime in V2 */ },
  "lfo":      { /* legacy single LFO — retained for v1 compat, inert at runtime in V2 */ },
  "master":   { "level": 0.5 }
}
```

Path strings stay L/R text. Shared mode includes a `sharedPath` field and
per-operator `position` integers, free mode stores each operator's `path`
directly.

The V2 modulator-bank state (`mod0..mod15.*` APVTS params) round-trips
through the saver/loader as part of standard plugin state, not via this
JSON file. Phase 3 search operates on FM-operator state only; the LFO
bank stays as the user / preset configures it.
