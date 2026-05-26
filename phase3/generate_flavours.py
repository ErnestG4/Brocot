"""Generate the 'Flavours' factory preset pack from the extra families.

Uses the coverage-bank warm-started search (fast) to find FM configs that
approximate each instrument/waveform flavour, tags each preset with its
spectral-feature adjectives, and writes a .brocot pack. A fun, discoverable
menu for the toy — vowels, bell, brass, pluck, reed, glass…

Run: python -m phase3.generate_flavours [--per-family 2] [--out DIR]
"""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

os.environ.setdefault("OMP_NUM_THREADS", "1")

from phase3.brocot_writer import _slug, result_to_brocot_json  # noqa: E402
from phase3.coverage_bank import bank_seeded_search, load_bank  # noqa: E402
from phase3.families_extra import extra_families  # noqa: E402
from phase3.spectral_features import spectral_features  # noqa: E402
from phase3.family_distance import quantize_to_integer_grid  # noqa: E402

# Menu-friendly display name + category per family.
DISPLAY = {
    "pluck_0_18": ("Pluck", "Plucked"),
    "brass": ("Brass", "Brass"),
    "reed_1_2": ("Reed", "Reed"),
    "hollow": ("Hollow", "Hollow"),
    "vowel_a": ("Vowel · Ah", "Vowel"),
    "vowel_e": ("Vowel · Eh", "Vowel"),
    "vowel_i": ("Vowel · Ee", "Vowel"),
    "vowel_o": ("Vowel · Oh", "Vowel"),
    "vowel_u": ("Vowel · Oo", "Vowel"),
    "bell": ("Bell · Tubular", "Bell"),
    "glass_1_4": ("Glass", "Glass"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-family", type=int, default=2)
    ap.add_argument("--out", type=Path,
                    default=Path("resources/presets/factory/flavours"))
    ap.add_argument("--budget", type=float, default=2.5)
    args = ap.parse_args()

    amps, configs = load_bank()
    args.out.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).isoformat()
    n_written = 0

    for fam in extra_families():
        disp, category = DISPLAY.get(fam.name, (fam.name.capitalize(), "Flavour"))
        results = bank_seeded_search(
            fam, amps=amps, configs=configs, k_seeds=24, q_max=16,
            time_budget_per_candidate_s=args.budget)
        for rank, res in enumerate(results[:args.per_family], start=1):
            # Spectral adjectives of the matched config's predicted spectrum.
            sp = res.config.predict()
            feats = spectral_features(quantize_to_integer_grid(sp, 220.0, 64))
            tags = feats.describe()
            name = f"{disp}" if rank == 1 else f"{disp} {rank}"

            doc = result_to_brocot_json(res, name=name, q_max=16, timestamp=ts)
            doc["provenance"]["category"] = category
            doc["provenance"]["kind"] = "flavour-match"
            doc["provenance"]["description"] = (
                f"{disp}: FM approximation of the '{fam.name}' target "
                f"(objective {res.objective:.4f}). Spectral character: {tags}.")
            doc["provenance"]["spectral_tags"] = tags

            path = args.out / f"{_slug(disp)}_{rank}.brocot"
            path.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n")
            n_written += 1
            print(f"  {name:<16} obj={res.objective:.4f}  [{tags}]  -> {path.name}")

    print(f"\nwrote {n_written} flavour presets to {args.out}")


if __name__ == "__main__":
    main()
