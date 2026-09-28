"""Regression tests for wire payload limits; run the actual C++ formatter."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PayloadLimitsTest(unittest.TestCase):
    def test_default_buffer_fits_full_delivery_in_utf8(self):
        header = (ROOT / "components/hydronode/hydronode.h").read_text()
        schema = (ROOT / "components/hydronode/__init__.py").read_text()
        cpp_limit = int(re.search(r"response_buffer_size_\{(\d+)\}", header)[1])
        yaml_limit = int(re.search(r"CONF_RESPONSE_BUFFER_SIZE, default=(\d+)", schema)[1])
        self.assertEqual(cpp_limit, yaml_limit)
        # Backend permits 8 commands, 64-character names and 512-character
        # serialized scalar values. BMP characters can consume 3 UTF-8 bytes
        # per Java character, including in names. Quotes use 2 of the 512 chars.
        for letter in ["X", "漢"]:
            with self.subTest(letter=letter):
                commands = [{
                    "id": f"00000000-0000-4000-8000-{i:012d}",
                    "command": letter * 64,
                    "type": "STRING",
                    "value": letter * 510,
                } for i in range(8)]
                payload = json.dumps({"commands": commands}, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")
                self.assertLessEqual(len(payload), cpp_limit)

    def test_actual_cpp_formatter_preserves_finite_float_extremes(self):
        source = (ROOT / "components/hydronode/hydronode.cpp").read_text()
        start = source.index("std::string HydroNodeComponent::build_value_payload_(")
        end = source.index("\n}", start) + 2
        method = source[start:end]
        harness = r'''
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
class HydroNodeComponent {
 public:
  std::string sensor_id_ = "550e8400-e29b-41d4-a716-446655440000";
  std::string build_value_payload_(const std::string &, float, int64_t) const;
};
''' + method + r'''
int main() {
  HydroNodeComponent component;
  for (float value : {std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::lowest(), 21.5f, -0.0f}) {
    std::cout << component.build_value_payload_("TEMPERATURE", value, 1784793600)
              << '\n';
  }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(path / "test.cpp"), "-o", str(path / "test")], check=True)
            lines = subprocess.check_output([str(path / "test")], text=True).splitlines()
        self.assertEqual(len(lines), 4)
        max_float = (2 - 2 ** -23) * 2 ** 127
        for line, expected in zip(lines, [max_float, -max_float, 21.5, -0.0]):
            self.assertEqual(json.loads(line)["value"], expected)
            self.assertRegex(line, r'"value":-?\d+\.\d{2},"timestamp":1784793600}')
        self.assertIn('"value":-0.00,', lines[-1])


if __name__ == "__main__":
    unittest.main()
