import base64
import hashlib
import hmac
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
VECTORS = ROOT / "tests" / "fixtures" / "hmac_vectors.json"


def canonical_payload(sensor_id: str, sensor_type: str, value: float, timestamp: int):
    return (
        f'{{"sensorId":"{sensor_id}","type":"{sensor_type}",'
        f'"value":{value:.2f},"timestamp":{timestamp}}}'
    )


def signature(secret: str, payload: str, timestamp: int):
    digest = hmac.new(
        secret.encode(),
        (payload + str(timestamp)).encode(),
        hashlib.sha256,
    ).digest()
    return base64.b64encode(digest).decode()


class HydroNodeContractTest(unittest.TestCase):
    def test_hmac_vectors(self):
        vectors = json.loads(VECTORS.read_text())
        for vector in vectors:
            with self.subTest(vector=vector["name"]):
                payload = canonical_payload(
                    vector["sensor_id"],
                    vector["type"],
                    vector["value"],
                    vector["timestamp"],
                )
                self.assertEqual(vector["payload"], payload)
                self.assertEqual(
                    vector["signature"],
                    signature(vector["secret"], payload, vector["timestamp"]),
                )

    def test_payload_is_canonical(self):
        payload = canonical_payload(
            "550e8400-e29b-41d4-a716-446655440000",
            "WATER_PH",
            7.0,
            1784793600,
        )
        self.assertEqual(
            payload,
            '{"sensorId":"550e8400-e29b-41d4-a716-446655440000",'
            '"type":"WATER_PH","value":7.00,"timestamp":1784793600}',
        )
        self.assertNotIn(" ", payload)

    def test_cpp_contract_constants_match_backend_paths(self):
        source = (
            ROOT / "components" / "hydronode" / "hydronode.cpp"
        ).read_text()
        self.assertIn('"/api/webhook/sensor-value"', source)
        self.assertIn('"/api/webhook/sensor-command-ack"', source)
        self.assertRegex(source, re.compile(r'"X-Signature"'))

    def test_command_types_match_backend_and_library(self):
        source = (ROOT / "components" / "hydronode" / "__init__.py").read_text()
        cpp = (ROOT / "components" / "hydronode" / "hydronode.cpp").read_text()
        types = ["BOOL", "INT32", "UINT32", "INT64", "UINT64", "STRING"]
        self.assertIn(
            "COMMAND_VALUE_TYPES = " + json.dumps(types), source
        )
        for value_type in types:
            self.assertIn(f'"{value_type}"', cpp)
        for reason in ["NO_HANDLER", "TYPE_MISMATCH", "INVALID_VALUE"]:
            self.assertIn(f'"{reason}"', cpp)
        self.assertIn('root["declined"]', cpp)

    def test_fleet_headers_follow_the_wire_format(self):
        header = (ROOT / "components" / "hydronode" / "hydronode.h").read_text()
        cpp = (ROOT / "components" / "hydronode" / "hydronode.cpp").read_text()
        init = (ROOT / "components" / "hydronode" / "__init__.py").read_text()
        changelog = (ROOT / "CHANGELOG.md").read_text()

        version = re.search(r'COMPONENT_VERSION = "([0-9]+\.[0-9]+\.[0-9]+)"', init).group(1)
        self.assertIn(f'COMPONENT_VERSION = "{version}"', header)
        newest = re.search(r"^## \[([0-9]+\.[0-9]+\.[0-9]+)\]", changelog, re.MULTILINE).group(1)
        self.assertEqual(version, newest)

        self.assertIn('"X-Firmware"', cpp)
        self.assertIn('"X-Device-Status"', cpp)
        self.assertIn('"esphome-hydronode"', cpp)
        for family in ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]:
            self.assertIn(f'"{family}"', cpp)
        for reason in ["poweron", "software", "panic", "watchdog", "brownout", "deepsleep", "external", "unknown"]:
            self.assertIn(f'"{reason}"', cpp)
        # Key order and separators of X-Device-Status, as the backend parses it.
        self.assertRegex(cpp, r'"boot=".*";reset="')
        self.assertIn('";uptime="', cpp)
        self.assertIn('";rssi="', cpp)
        self.assertIn('";net="', cpp)
        # No update keys: ESPHome devices are never offered OTA through HydroNode.
        self.assertNotIn(" ota", cpp)
        self.assertNotIn("cfg=", cpp)

        example_firmware = f"esphome-hydronode/{version} esp32c3"
        self.assertRegex(example_firmware, r"^[a-z0-9-]+/[0-9.]+ (esp32|esp32s2|esp32s3|esp32c3|esp32c6)$")
        self.assertLessEqual(len(example_firmware), 128)

    def test_full_ca_bundle_is_required(self):
        source = (
            ROOT / "components" / "hydronode" / "__init__.py"
        ).read_text()
        self.assertIn("esp32.require_full_certificate_bundle()", source)


if __name__ == "__main__":
    unittest.main()
