"""Brocot Phase 3 — ARS-driven preset generation.

Pipeline:
  partial_prediction  → families · family_distance → search → brocot_writer
                                                                    ↓
                                            resources/presets/factory/discovered/

The synth has zero runtime dependency on this tool — Phase 3 is strictly
an offline workflow that produces `.brocot` v2 JSON files Brocot loads
through its normal factory-preset path.
"""

__version__ = "0.1.0"
