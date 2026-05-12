# Brocot factory presets

32 presets in four categories of 8, per BROCOT-SPEC.md §10.

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

## Format

See BROCOT-SPEC.md §9 for the JSON schema. Path strings are stored as L/R
text; shared mode includes a `sharedPath` field and per-operator `position`
integers, free mode stores each operator's `path` directly.
