"""Convert SearchResult → `.brocot` v2 JSON.

Maps the Phase-3 search output onto the schema PresetSchema.{h,cpp}
parses on the C++ side. The runtime synth doesn't change; this just
fills in the file format with the discovery's lineage in
`provenance`.

The legacy v1 blocks (filter, envelope, lfo, master) are populated
with sensible defaults — they round-trip through PresetSchema and
keep the synth state predictable. Per V2.8 they're inert at runtime;
the V2 mod-bank state isn't touched by preset load, so the user's
current mod-bank configuration stays put when they audition a
discovery.
"""

from __future__ import annotations

import datetime as _dt
import json
import re
import subprocess
from pathlib import Path
from typing import Any

from phase3.config import Config
from phase3.search import SearchResult


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Top-level field order — matches the in-file ordering the loader doesn't care
# about but humans reading the file do.
_TOP_KEYS = ("name", "version", "provenance", "engine",
             "filter", "envelope", "lfo", "master")

_SCHEMA_VERSION = "2.0"


def _ars_version_or_none(ars_dir: Path | None = None) -> str | None:
    """Look up the ARS submodule's current commit SHA for provenance.
    Returns None if not in a git repo or git isn't available."""
    target = ars_dir or (Path(__file__).parent / "ars")
    if not target.exists():
        return None
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=target, stderr=subprocess.DEVNULL,
        )
        return out.decode().strip() or None
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def _human_family_category(family_name: str) -> str:
    """Turn 'prime_7' / 'pulse_pw_4' / 'organ_1_3_5' into menu-friendly text."""
    if family_name.startswith("prime_"):
        return f"Prime {family_name.split('_', 1)[1]}"
    if family_name.startswith("pulse_pw_"):
        return f"Pulse-PW {family_name.split('_')[-1]}"
    if family_name.startswith("organ_"):
        stops = family_name.split("_")[1:]
        return f"Organ {'/'.join(stops)}"
    if family_name.startswith("bell_beta_"):
        return "Bell (stretched)"
    return family_name.capitalize()


def _slug(s: str) -> str:
    return re.sub(r"[^a-z0-9_-]+", "_", s.lower()).strip("_")


# ---------------------------------------------------------------------------
# Engine block
# ---------------------------------------------------------------------------

def _engine_block(cfg: Config) -> dict[str, Any]:
    """Free-mode engine block — each op carries its own L/R path string."""
    operators: list[dict[str, Any]] = []
    for m in cfg.mods:
        op: dict[str, Any] = {
            "enabled": bool(m.enabled),
            "path":    m.path,
            "depth":   float(m.depth),
        }
        # Per-op fine detune in cents (offset from the exact SB ratio).
        # Omitted when zero to keep existing presets byte-identical.
        if getattr(m, "detune_cents", 0.0):
            op["detune"] = float(m.detune_cents)
        operators.append(op)
    return {
        "polyphony":   "mono",
        "octaveShift": 0,
        "pathMode":    "free",
        "operators":   operators,
    }


# ---------------------------------------------------------------------------
# Provenance block
# ---------------------------------------------------------------------------

def _provenance_block(
    result: SearchResult,
    *,
    n_phase1: int,
    k_top: int,
    q_max: int,
    ars_version: str | None,
    timestamp: str | None,
) -> dict[str, Any]:
    fam_human = _human_family_category(result.family)
    description = (
        f"{fam_human} family discovery via Phase-3 ARS search "
        f"(objective {result.objective:.4f}, seed {result.seed})."
    )
    search: dict[str, Any] = {
        "family":    result.family,
        "seed":      int(result.seed),
        "objective": float(result.objective),
        "params":    {
            "n_phase1": int(n_phase1),
            "k_top":    int(k_top),
            "q_max":    int(q_max),
        },
        "timestamp": timestamp or _dt.datetime.now(_dt.timezone.utc).isoformat(),
    }
    # P3.7 adds the separated family_distance + criticality numbers so
    # the audit can show how much of the score is family-fit vs
    # liveness bonus. Older P3.6 results don't carry these fields;
    # writing them only when present keeps the schema stable.
    if getattr(result, "family_distance", 0.0) or getattr(result, "criticality", 0.0):
        search["family_distance"] = float(result.family_distance)
        search["criticality"]     = float(result.criticality)

    block: dict[str, Any] = {
        "kind":        "family-search",
        "author":      "ARS Phase 3",
        "description": description,
        "category":    fam_human,
        "search":      search,
    }
    if ars_version is not None:
        block["ars_version"] = ars_version
    return block


# ---------------------------------------------------------------------------
# Legacy default blocks (inert at V2 runtime, retained for schema round-trip)
# ---------------------------------------------------------------------------

def _default_filter() -> dict[str, Any]:
    return {"type": "lp", "cutoff": 0.85, "resonance": 0.10}


def _default_envelope() -> dict[str, Any]:
    # ADSR was retired in V2.8 — values here just round-trip the v1 layout.
    return {"attack": 0.01, "decay": 0.15, "sustain": 0.80, "release": 0.40}


def _default_lfo() -> dict[str, Any]:
    return {"shape": "sine", "rate": 0.5, "depth": 0.0, "keySync": False}


def _default_master() -> dict[str, Any]:
    return {"level": 0.5}


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def result_to_brocot_json(
    result: SearchResult,
    *,
    name: str | None = None,
    n_phase1: int = 1000,
    k_top: int = 20,
    q_max: int = 16,
    ars_version: str | None = None,
    timestamp: str | None = None,
) -> dict[str, Any]:
    """Pure function — turns a SearchResult into a v2 .brocot JSON dict.

    Pass `ars_version` to record which ARS submodule SHA the search ran
    under; defaults to None (the writer fills it in via git when used).
    """
    if name is None:
        name = f"{_human_family_category(result.family)} (seed {result.seed})"

    doc: dict[str, Any] = {
        "name":       name,
        "version":    _SCHEMA_VERSION,
        "provenance": _provenance_block(
            result,
            n_phase1=n_phase1, k_top=k_top, q_max=q_max,
            ars_version=ars_version, timestamp=timestamp,
        ),
        "engine":   _engine_block(result.config),
        "filter":   _default_filter(),
        "envelope": _default_envelope(),
        "lfo":      _default_lfo(),
        "master":   _default_master(),
    }
    return {k: doc[k] for k in _TOP_KEYS}


def write_brocot(
    result: SearchResult,
    output_dir: str | Path,
    *,
    name: str | None = None,
    n_phase1: int = 1000,
    k_top: int = 20,
    q_max: int = 16,
    ars_version: str | None = None,
    file_stem: str | None = None,
) -> Path:
    """Write a SearchResult to disk as `<output_dir>/<stem>.brocot`.

    `file_stem` defaults to `<family>_<seed>_<index>` if not given.
    Returns the written path.
    """
    if ars_version is None:
        ars_version = _ars_version_or_none()
    if file_stem is None:
        file_stem = _slug(name or f"{result.family}_seed{result.seed}")

    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{file_stem}.brocot"

    doc = result_to_brocot_json(
        result,
        name=name,
        n_phase1=n_phase1, k_top=k_top, q_max=q_max,
        ars_version=ars_version,
    )
    out_path.write_text(json.dumps(doc, indent=2) + "\n")
    return out_path
