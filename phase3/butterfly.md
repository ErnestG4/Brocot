# Map vs. Hofstadter butterfly — is the measured map the butterfly?

`butterfly_map_study.py` tests BROCOT-SPEC.md §0's central claim — that the
navigation map's ratio axis θ = p/q *is* the almost-Mathieu (Harper) butterfly's
flux axis, so the butterfly's gap structure should already be visible in the
maps we ship — plus Will's banked **criticality insight**: that FM criticalities
line the **voids** of the measured embedding.

It does **not** draw a new butterfly. It asks whether the existing measured
cloud already is one, and it is a **falsification** study (§4.3): every
correspondence is scored against a shuffle control, and effect size is read
ahead of significance (at n ≈ 50k, z/p are significant for even trivial
correlations).

## Method

- Load a measured map (`resources/landscape_graph*.json.gz`; default the 6-op
  base). Each node is a multi-operator config; each op carries a Stern-Brocot
  `path` and a `depth` (its FM index).
- Project each node onto butterfly quantities from `brocot_lattice`
  (`walk_path` → p/q per op):
  - **promNode** = depth-weighted mean gap-width `prominence(p,q)` — the
    wide-gap "plateau" / stability proxy.
  - **critNode** = depth-weighted mean `cfDepth(p,q)` — the CF-sum criticality
    proxy.
  - **meanQ** = depth-weighted `simplicity(q)` — a trivial baseline.
- Spearman-correlate against the measured per-node fields — `crit` (the cloud's
  own angular-gap / void-rim structure score), `tonal` (musical↔chaotic),
  `dense` — each vs a permutation null. Report `nActive` (op count) and `meanQ`
  as baselines.
- **Single-op subset** (`nActive == 1`): the one place θ = p/q is unambiguous,
  so it's the clean direct test of §0 without multi-op averaging.

Run: `python3 -m phase3.butterfly_map_study [--map …] [--shuffle N]`. Writes a
`.txt` report and a self-contained 4-panel `.svg` overlay (promNode / critNode /
measured crit / measured tonal, same x/y layout) to `phase3/derived/`.

## Finding (6-op base map, 50k nodes, 2026-07-20)

**Aggregate (multi-op): the measured map is NOT the butterfly.** Butterfly
prominence and cfDepth are *negligibly* associated with the measured geometry
(all |ρ| ≤ 0.06) and are dwarfed by trivial baselines — op count vs measured
crit is ρ = **−0.54** (strong), simplicity vs tonal ρ = +0.18. The gross
organizing axis of the multi-op cloud is **op count**, not the fine butterfly
gap structure. (This matches the RICHNESS-DIRECTION note that high-op parallel
maps run toward white noise, and the criticality insight that interesting sounds
are sparse/structural, not dense.)

**Single-op (θ unambiguous): prominence carries a weak but real signal, in the
predicted direction.** Among the 11,765 one-operator nodes:

| relation | Spearman ρ | effect | verdict |
|---|---|---|---|
| promNode vs tonal | **+0.14** | weak | wider gaps ⇒ more tonal ✓ |
| promNode vs measured crit | **−0.26** | weak | wide-gap plateaus sit *away* from void-rims ✓ |
| critNode vs tonal | +0.02 | negligible | not supported |
| critNode vs measured crit | −0.04 | negligible | not supported |

So the "stable plateau" half of §0 (prominence = wide gaps = consonant footings)
is the one piece the data narrowly supports — but only single-op, and weakly.
The **cfDepth-criticality ↔ voids** link (the criticality insight) is **not
confirmed**, even single-op.

## Verdict / what this earns

- **Do not** ship a butterfly-terrain prominence overlay on the aggregate map —
  the evidence doesn't support it (§1 do-not-overclaim).
- A prominence overlay is only worth prototyping for **low-op** patches, where
  the weak single-op signal actually lives. That is the gated next step for
  P0-b / P1-a, not a blanket recolour.
- The lattice math itself is validated independently (the §6 fixtures); this
  study governs only whether it should drive a *visual* map layer, and the
  answer for now is "narrowly, low-op only."

Re-run per map (`--map resources/landscape_graph_4.json.gz`, the solo-waveform
maps, etc.) to see whether lower-op or solo-timbre maps sharpen the single-op
signal before investing in any overlay.

## 2026-08-04 — the gated re-run (6 maps) and the overlay decision

The re-run the verdict above demanded, with the study extended first: per-subset
`meanQ` baselines (the ship rule requires prominence to beat trivial simplicity
*in-subset*), an `nActive ≤ 2` block (the candidate ship population), and
machine-readable JSON verdicts (`derived/butterfly_study_*.json`).

**Decision rule, fixed before looking (panel pre-registration):** ship a low-op
prominence overlay iff, single-op, ρ(prom, tonal) ≥ +0.25 or
ρ(prom, crit) ≤ −0.35 on the 4-op map or on ≥ 2 solo-wave maps, **and**
prominence beats the in-subset meanQ baseline by ≥ 0.08 on the same field.

| map | n (1op) | prom↔tonal | prom↔crit | meanQ↔tonal | meanQ↔crit |
|---|---|---|---|---|---|
| 6-op base | 11,765 | +0.14 | −0.26 | **+0.24** | **−0.35** |
| 4-op | 9,151 | +0.12 | −0.26 | **+0.24** | **−0.33** |
| 6op sine | 6,756 | +0.14 | −0.25 | **+0.27** | **−0.31** |
| 6op tri | 6,907 | +0.06 | −0.24 | **+0.16** | **−0.34** |
| 6op saw | 7,293 | −0.04 | −0.25 | −0.06 | **−0.34** |
| 6op sqr | 7,014 | −0.21 ⚠ | −0.39 | −0.11 | **−0.48** |

(⚠ = replicates with the *opposite* predicted sign. The ≤2-op blocks show the
same pattern, uniformly attenuated.)

**Findings:**
1. `prom↔crit` (wide gaps sit away from void-rims) **replicates on all six
   maps** (−0.24…−0.39) — the sign is robust.
2. `prom↔tonal` does **not** replicate: it flips sign on saw/sqr. The "wide
   gaps are more tonal" half was waveform-contingent, not structural.
3. **The margin criterion fails everywhere**: plain depth-weighted simplicity
   (meanQ) beats prominence on *every map, both subsets, both fields*. Gap
   prominence carries no signal beyond its correlation with q.

**Verdict: NO-GO, permanently.** The butterfly-specific quantity is fully
shadowed by Farey simplicity on the measured maps — the "stable plateau"
signal was simplicity all along. P0-b (prominence terrain) is closed as a
documented negative, not deferred. What the data *does* earn is the fallback
the rule anticipated: a **"simplicity" colour mode** on the cloud (per-node
depth-weighted q, computable at load, no eigensolves, no butterfly claim) —
the Farey-gravity thesis made visible with the numbers on its side.
Prominence remains a FareyView-only quantity, where it is §6-validated as
lattice annotation rather than a map claim.

---

## Prior art on the TRUNCATED butterfly — searched 2026-08-25

The horizon result (`COINCIDENCE-HORIZON.md`) suggests the object to compare the
map against is not the butterfly but the butterfly **truncated at a bounded
denominator**, since the instrument only resolves q ≤ A = 2·order_bound(I). A
literature scout was run on whether that object is known. Findings, with the
scout's own confidence markings preserved.

**1. Is "the butterfly at q ≤ Q" a named object?** No — but it is what every
plot already is. At flux p/q the spectrum is exactly q bands, and Hofstadter's
own 1976 figure (Phys. Rev. B 14, 2239) used q ≤ 50. The literature treats this
as a plotting device rather than a studied object. What exists instead is
*convergence theory* controlling how well a truncation approximates the whole:
Hölder-½ continuity of the spectrum in the flux (Avron–van Mouche–Simon, CMP 132,
1990), and Last's measure results (CMP 164, 1994). Confidence the ingredients
are right: high. Confidence no named truncated object exists: moderate — this is
absence of evidence after targeted searching.

**2. Which gaps survive a cutoff?** The literature's version of this question is
*gap width versus gap label*, and it is answered: gap widths decay
**exponentially in |label|** for non-critical coupling (Leguil–You–Zhao–Zhou,
arXiv:1712.04700; also arXiv:1311.0658). The consequence — at resolution ε only
labels with |k| ≲ log(1/ε) survive — is a quantitative truncation rule already
sitting in the theorems, though nobody phrases it as a Q-cutoff.

> **This is the closest structural analogue to our ε.** `order_bound(I)` is
> defined by an amplitude floor ε = 1e-3 on |J_n(I)|, and Bessel tails also decay
> faster than exponentially in n past the turning point. Both truncations are
> "keep what is above a floor", and both produce a cutoff that moves when the
> control moves. That the analogy is *structural* and not an identity is the
> point of the next item.

Also relevant: total bandwidth at denominator q scales as ~1/q (Thouless's
conjecture; Helffer–Kerdelhué, CMP 173, 335–356, 1995), so a q ≤ Q truncation
captures essentially all spectral weight above scale ~1/Q. And experiment is
itself a truncation — in moiré superlattices disorder broadening resolves only
low-q gaps (Dean et al., Nature 497, 598, 2013).

**3. Any Hofstadter ↔ FM synthesis connection?** **None found.** Three targeted
searches returned nothing linking almost-Mathieu structure to FM synthesis or
sideband coincidence. The neighbouring literatures are real and disjoint:
Stern-Brocot in tuning theory (well-formed scale theory, just-intonation
lattices, semiconvergents in Pythagorean tuning), mode-locking and
devil's-staircase accounts of consonance, and Chowning-FM sideband structure
which never invokes continued fractions. Confidence the connection is genuinely
absent: moderate-high.

**4. Arnold tongues — and a correction to an assumption we had queued.** There
*is* a rigorous bridge between the almost-Mathieu operator and circle-map
mode-locking: the projectivised AMO cocycle is a quasiperiodically forced circle
map, its fibered rotation number equals the integrated density of states, and
**spectral gaps correspond exactly to mode-locking plateaus** (Bjerklöv–Jäger,
J. Amer. Math. Soc., 2009; arXiv:math/0607598).

> **But the axes do not line up the way we assumed.** In that correspondence the
> mode-locking parameter is **energy**, and the resonance order is the **gap
> label** — not the flux. So a bounded denominator on the *flux* axis is **not**
> the tongue order, and the finite-coupling Arnold-tongue truncation is not
> formally the same object as a bounded-denominator butterfly. No theorem or
> explicit conjecture identifying them was found. The queue entry that
> speculated otherwise has been corrected.

The quantitative parallel that *does* line up is coupling ↔ gap label: an
order-p/q Arnold tongue has width ~K^q at small coupling K, and an AMO gap of
label k has width ~λ^|k| at small λ. No paper was found making even this
identification explicit.

**Scout caveats, preserved.** Several attributions (Johnson–Moser, van Mouche,
Choi–Elliott–Yui, Last–Wilkinson, and the authorship of arXiv:1005.1301 and
arXiv:1703.09634) are from recall of standard literature and were *not*
confirmed against a fetched source in this session. Anything above marked as
verified was checked; the rest should be re-verified before citation in
anything public.
