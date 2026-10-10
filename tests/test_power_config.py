"""Tests for the optional `power:` block: rules and the X-Device-Config value."""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "power_config", ROOT / "components" / "hydronode" / "power_config.py"
)
power_config = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(power_config)

LIPO = {"source": "battery", "cells": 1, "save": 3.50, "recovery": 3.30, "standby": 3.20, "resume": 3.60}


def block(**changes):
    power = dict(LIPO)
    power.update(changes)
    return {key: value for key, value in power.items() if value is not None}


class PowerConfigTest(unittest.TestCase):
    def test_header_follows_the_wire_format(self):
        power = block(gauge="max17048")
        self.assertEqual(power_config.check(power, 300), [])
        self.assertEqual(
            power_config.device_config_header(power, 300),
            "v=1 int=300 save=3500 rec=3300 sby=3200 res=3600 src=bat gauge=max17048 cells=1",
        )

    def test_header_is_what_the_backend_parses(self):
        header = power_config.device_config_header(block(), 60)
        self.assertLessEqual(len(header), 256)
        self.assertRegex(header, r"^v=1( [a-z]+=[a-z0-9_]+)+$")
        # ESPHome cannot take values back: never caps=settings, no revision.
        self.assertNotIn("caps=", header)
        self.assertNotIn("rev=", header)

    def test_volts_per_cell_become_pack_millivolts(self):
        power = block(cells=2, save=3.5, recovery=3.3, standby=3.2, resume=3.6)
        self.assertEqual(power_config.check(power, 300), [])
        self.assertIn("save=7000 rec=6600 sby=6400 res=7200", power_config.device_config_header(power, 300))
        self.assertEqual(power_config.pack_mv(3.335, 1), 3335)
        self.assertEqual(power_config.pack_mv(3.2005, 2), 6401)

    def test_usb_reports_the_interval_only(self):
        power = {"source": "usb", "cells": 1}
        self.assertEqual(power_config.check(power, 120), [])
        self.assertEqual(power_config.device_config_header(power, 120), "v=1 int=120 src=usb cells=1")
        self.assertEqual(
            power_config.check({"source": "usb", "cells": 1, **{k: LIPO[k] for k in power_config.THRESHOLDS}}, 120),
            ["Thresholds need source battery or solar."],
        )

    def test_solar(self):
        self.assertIn("src=solar", power_config.device_config_header(block(source="solar"), 300))

    def test_all_four_or_none(self):
        errors = power_config.check(block(resume=None, standby=None), 300)
        self.assertEqual(errors, ["Set all four thresholds or none (missing: standby, resume)."])

    def test_rules_match_the_shared_table(self):
        cases = [
            ("smallest gaps", dict(save=3.35, recovery=3.30, standby=3.25, resume=3.40), []),
            ("standby too close", dict(standby=3.251), ["Standby must be at least 0.05 V below Recovery."]),
            ("recovery too close", dict(save=3.349), ["Recovery must be at least 0.05 V below Save."]),
            ("resume too close", dict(resume=3.399), ["Resume must be at least 0.10 V above Recovery."]),
            ("resume far above save", dict(resume=3.901), ["Resume must be at most 0.40 V above Save."]),
            ("resume exactly save + 400", dict(resume=3.90), []),
            ("LiFePO4 preset", dict(save=3.10, recovery=3.00, standby=2.80, resume=3.20), []),
            ("below every chemistry", dict(standby=2.49), ["Standby must be between 2.50 V and 4.10 V per cell."]),
            ("above every chemistry", dict(save=4.11, resume=3.80), ["Save must be between 2.50 V and 4.10 V per cell."]),
            ("2S gap is pack mV", dict(cells=2, save=3.5, recovery=3.3, standby=3.2755, resume=3.6),
             ["Standby must be at least 0.03 V below Recovery."]),
        ]
        for name, changes, expected in cases:
            with self.subTest(name):
                self.assertEqual(power_config.check(block(**changes), 300), expected)

    def test_interval_limits(self):
        self.assertEqual(power_config.check(block(), 10), [])
        self.assertEqual(power_config.check(block(), 604800), [])
        self.assertEqual(power_config.check(block(), 9), ["Send interval must be 10 to 604800 seconds."])
        self.assertEqual(power_config.check(block(), 604801), ["Send interval must be 10 to 604800 seconds."])

    def test_cells_and_gauge(self):
        self.assertEqual(power_config.check(block(cells=0), 300)[0], "cells must be 1 to 16.")
        self.assertTrue(power_config.check(block(gauge="MAX 17048"), 300)[0].startswith("gauge must be"))

    def test_schema_and_cpp_use_the_module(self):
        init = (ROOT / "components" / "hydronode" / "__init__.py").read_text()
        header = (ROOT / "components" / "hydronode" / "hydronode.h").read_text()
        cpp = (ROOT / "components" / "hydronode" / "hydronode.cpp").read_text()
        self.assertIn("power_config.check(", init)
        self.assertIn("power_config.device_config_header(", init)
        self.assertIn("set_device_config", header)
        self.assertIn('"X-Device-Config"', cpp)
        self.assertNotIn("caps=settings", cpp)
        self.assertRegex(cpp, re.compile(r"device_config_sent_ = true"))


if __name__ == "__main__":
    unittest.main()
