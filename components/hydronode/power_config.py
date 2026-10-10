"""The optional `power:` block: rules and the X-Device-Config value.

Plain Python without ESPHome imports, so tests/test_power_config.py runs it without ESPHome.
The header is fixed at compile time: an ESPHome device reports its values, HydroNode cannot
change them (no caps=settings).

    power:
      source: battery      # usb, battery or solar
      gauge: max17048      # optional, how the battery is measured
      cells: 1
      interval: 5min       # optional, default: update_interval
      save: 3.50V          # volts per cell, like the HydroNode sensor settings
      recovery: 3.30V
      standby: 3.20V
      resume: 3.60V

gives `X-Device-Config: v=1 int=300 save=3500 rec=3300 sby=3200 res=3600 src=bat gauge=max17048 cells=1`.
"""

from __future__ import annotations

import math
import re

# YAML value -> header token, as the backend reads src=.
SOURCES = {"usb": "usb", "battery": "bat", "bat": "bat", "solar": "solar"}
THRESHOLDS = ("save", "recovery", "standby", "resume")
GAUGE_RE = re.compile(r"^[a-z0-9_]{1,32}$")

# Rules shared with the backend, web, apps, firmware, library and station (Power-Sync).
# The range is per cell and covers LiPo, Li-ion (2.80 to 4.10 V) and LiFePO4 (2.50 to 3.40 V);
# HydroNode checks the chemistry's own range when it shows the values.
PER_CELL_MIN_V = 2.50
PER_CELL_MAX_V = 4.10
GAP_STANDBY_MV = 50
GAP_RECOVERY_MV = 50
GAP_RESUME_MV = 100
RESUME_ABOVE_SAVE_MV = 400
INTERVAL_MIN_S = 10
INTERVAL_MAX_S = 604800
CELLS_MAX = 16
HEADER_MAX = 256


def pack_mv(per_cell_volts: float, cells: int) -> int:
    """Volts per cell to pack millivolts, rounded half up like the backend."""
    return int(math.floor(per_cell_volts * 1000.0 * cells + 0.5))


def _gap(pack: int, cells: int) -> str:
    return f"{pack / 1000.0 / cells:.2f} V"


def check(power: dict, interval_s: int) -> list[str]:
    """Every broken rule as an English sentence; empty when the block fits."""
    errors: list[str] = []
    source = power.get("source", "battery")
    if source not in SOURCES:
        errors.append("source must be usb, battery or solar.")
    cells = power.get("cells", 1)
    if not 1 <= cells <= CELLS_MAX:
        errors.append(f"cells must be 1 to {CELLS_MAX}.")
        cells = 1
    gauge = power.get("gauge")
    if gauge is not None and not GAUGE_RE.match(gauge):
        errors.append("gauge must be lower case letters, digits or _ (at most 32), e.g. max17048.")
    if not INTERVAL_MIN_S <= interval_s <= INTERVAL_MAX_S:
        errors.append(f"Send interval must be {INTERVAL_MIN_S} to {INTERVAL_MAX_S} seconds.")

    given = [key for key in THRESHOLDS if power.get(key) is not None]
    if not given:
        return errors
    if SOURCES.get(source) == "usb":
        errors.append("Thresholds need source battery or solar.")
        return errors
    if len(given) != len(THRESHOLDS):
        missing = ", ".join(key for key in THRESHOLDS if key not in given)
        errors.append(f"Set all four thresholds or none (missing: {missing}).")
        return errors

    for key in THRESHOLDS:
        volts = power[key]
        if not PER_CELL_MIN_V <= volts <= PER_CELL_MAX_V:
            errors.append(
                f"{key.capitalize()} must be between {PER_CELL_MIN_V:.2f} V and {PER_CELL_MAX_V:.2f} V per cell."
            )
    save, recovery, standby, resume = (pack_mv(power[key], cells) for key in THRESHOLDS)
    if recovery + GAP_RECOVERY_MV > save:
        errors.append(f"Recovery must be at least {_gap(GAP_RECOVERY_MV, cells)} below Save.")
    if standby + GAP_STANDBY_MV > recovery:
        errors.append(f"Standby must be at least {_gap(GAP_STANDBY_MV, cells)} below Recovery.")
    if recovery + GAP_RESUME_MV > resume:
        errors.append(f"Resume must be at least {_gap(GAP_RESUME_MV, cells)} above Recovery.")
    if resume > save + RESUME_ABOVE_SAVE_MV:
        errors.append(f"Resume must be at most {_gap(RESUME_ABOVE_SAVE_MV, cells)} above Save.")
    return errors


def device_config_header(power: dict, interval_s: int) -> str:
    """The X-Device-Config value for a checked block. No rev: ESPHome values have no revision."""
    cells = power.get("cells", 1)
    parts = ["v=1", f"int={interval_s}"]
    if all(power.get(key) is not None for key in THRESHOLDS):
        for key, name in zip(THRESHOLDS, ("save", "rec", "sby", "res")):
            parts.append(f"{name}={pack_mv(power[key], cells)}")
    parts.append(f"src={SOURCES[power.get('source', 'battery')]}")
    if power.get("gauge"):
        parts.append(f"gauge={power['gauge']}")
    parts.append(f"cells={cells}")
    header = " ".join(parts)
    if len(header) > HEADER_MAX:
        raise ValueError("X-Device-Config is longer than 256 characters")
    return header
