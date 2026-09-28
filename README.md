<p align="center">
  <img src="assets/hydronode-logo.svg" alt="HydroNode" width="140">
</p>

<h1 align="center">HydroNode ESPHome</h1>

<p align="center">
  Native ESPHome external component for the <strong>HydroNode</strong> IoT platform by TexhFexLabs.<br>
  Map ESPHome sensors to HydroNode, sign every upload, and handle remote commands, all in YAML.
</p>

<p align="center">
  <a href="https://github.com/TexhFexLabs/hydronode-esphome/actions/workflows/validate.yml"><img src="https://github.com/TexhFexLabs/hydronode-esphome/actions/workflows/validate.yml/badge.svg" alt="Validate"></a>
  <img src="https://img.shields.io/badge/ESPHome-2026.7.1%2B-000000.svg" alt="ESPHome 2026.7.1 or newer">
  <img src="https://img.shields.io/badge/platform-ESP32-e7352c.svg" alt="ESP32">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT license"></a>
</p>

<p align="center">
  <a href="https://hydronode.texhfexlabs.de/"><strong>Website</strong></a> ·
  <a href="https://hydronode.texhfexlabs.de/docs/guide/sensor-types/">Sensor Types</a> ·
  <a href="https://hydronode.texhfexlabs.de/docs/faq/">FAQ</a> ·
  <a href="https://github.com/TexhFexLabs/HydroNode-Library">Arduino Library</a>
</p>

---

```yaml
hydronode:
  sensor_id: !secret hydronode_sensor_id
  device_secret: !secret hydronode_device_secret
  measurements:
    - source: air_temperature
      type: TEMPERATURE
    - source: air_humidity
      type: HUMIDITY
```

[HydroNode](https://hydronode.texhfexlabs.de/) is a secure IoT platform for hydroponics, weather stations and environmental monitoring. This component connects normal ESPHome sensor entities to the existing HydroNode API. It uses the same signed wire protocol as [HydroNode-Library](https://github.com/TexhFexLabs/HydroNode-Library), while ESPHome continues to manage WiFi, OTA updates, sensor drivers and local automations.

Create a sensor on the HydroNode Website or in the iOS App, add its credentials to ESPHome secrets, and flash the ESP32.

## Features

- **YAML-native sensor mapping:** Connect any ESPHome numeric sensor to a HydroNode measurement type.
- **Several values per HydroNode sensor:** Temperature, humidity, pH and other measurements can share one sensor UUID.
- **Secure by default:** Uploads and command acknowledgements use HMAC-SHA256 signatures over HTTPS.
- **Replay protection:** ESPHome time is checked before transmission. An unsynchronized device will not send an invalid request.
- **Remote commands:** Receive HydroNode commands and drive ESPHome switches, outputs, scripts or other automations.
- **Manual uploads:** Use the `hydronode.send` action from buttons, intervals or automations.
- **Useful diagnostics:** Success and error triggers expose the measurement type, value and HTTP status without logging the secret.
- **Both ESP32 frameworks:** Continuously validated with ESP-IDF and Arduino.

## Compatibility

| Item | Support |
|---|---|
| ESPHome | 2026.7.1 or newer |
| Boards | ESP32 family |
| Frameworks | ESP-IDF and Arduino |
| Network | Any ESPHome network supported by `http_request` |
| HydroNode | Sensor UUID and device secret from the HydroNode Website or iOS App |

ESP8266 is not currently supported. The implementation relies on the ESP32 mbedTLS stack and is compiled in CI against both supported ESP32 frameworks.

## Installation

### 1. Obtain HydroNode credentials

Create one sensor on the [HydroNode Website](https://hydronode.texhfexlabs.de/) or in the iOS App. Copy:

- the sensor UUID;
- the device secret.

One HydroNode sensor can receive multiple measurement types. You therefore normally need only one UUID and secret for one physical ESPHome node.

### 2. Add secrets

Add the credentials to the `secrets.yaml` next to your device YAML:

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"

hydronode_sensor_id: "550e8400-e29b-41d4-a716-446655440000"
hydronode_device_secret: "YOUR_HYDRONODE_DEVICE_SECRET"
```

Do not commit `secrets.yaml`. A safe template is provided in [`examples/secrets.example.yaml`](examples/secrets.example.yaml).

### 3. Load the external component

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/TexhFexLabs/hydronode-esphome
      ref: v0.1.0
    components: [hydronode]
    refresh: 1d
```

`ref` is pinned to a release tag so a device build never changes underneath you. Use `ref: main` only to test unreleased changes.

### 4. Configure HTTP and time

Signatures include the current Unix timestamp, so both components are required:

```yaml
http_request:
  id: hydronode_http
  timeout: 10s
  verify_ssl: true

time:
  - platform: sntp
    id: hydronode_time
    timezone: UTC
```

### 5. Map ESPHome sensors

Complete example with two template sensors:

```yaml
esphome:
  name: hydronode-demo

esp32:
  board: esp32dev
  framework:
    type: esp-idf

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:

external_components:
  - source:
      type: git
      url: https://github.com/TexhFexLabs/hydronode-esphome
      ref: v0.1.0
    components: [hydronode]

http_request:
  id: hydronode_http
  timeout: 10s
  verify_ssl: true

time:
  - platform: sntp
    id: hydronode_time
    timezone: UTC

sensor:
  - platform: template
    id: air_temperature
    name: Air Temperature
    update_interval: 30s
    lambda: return 21.5f;

  - platform: template
    id: air_humidity
    name: Air Humidity
    update_interval: 30s
    lambda: return 55.0f;

hydronode:
  id: hydronode_cloud
  http_request_id: hydronode_http
  time_id: hydronode_time
  sensor_id: !secret hydronode_sensor_id
  device_secret: !secret hydronode_device_secret
  update_interval: 60s
  measurements:
    - source: air_temperature
      type: TEMPERATURE
    - source: air_humidity
      type: HUMIDITY
```

ESPHome reads each source independently. Every `update_interval`, HydroNode sends the latest finite state of every mapping as one signed request per measurement. HydroNode automatically adds a measurement type to the sensor when it first arrives.

## Examples

All examples are complete, commented ESPHome configurations:

| Example | What it demonstrates |
|---|---|
| [`basic-dht22.yaml`](examples/basic-dht22.yaml) | A physical DHT22 with temperature and humidity |
| [`hydroponics.yaml`](examples/hydroponics.yaml) | DS18B20 water temperature plus a calibrated analog pH input |
| [`commands-and-actuators.yaml`](examples/commands-and-actuators.yaml) | A remote pump command, GPIO output and manual upload button |
| [`template-sensors.yaml`](examples/template-sensors.yaml) | Small hardware-free example for a first configuration test |

Copy `examples/secrets.example.yaml` to `secrets.yaml`, fill in your own values, adjust pins and calibration, then run:

```bash
esphome config examples/basic-dht22.yaml
esphome run examples/basic-dht22.yaml
```

## Configuration reference

### `hydronode`

| Option | Required | Default | Description |
|---|---:|---|---|
| `id` | no | generated | ESPHome component ID; required when referring to this instance from `hydronode.send` |
| `sensor_id` | yes | n/a | Canonical HydroNode sensor UUID |
| `device_secret` | yes | n/a | HydroNode device secret; use `!secret` |
| `measurements` | yes | n/a | 1–25 source/type mappings |
| `http_request_id` | usually no | auto-resolved | ID of the ESPHome `http_request` component |
| `time_id` | usually no | auto-resolved | ID of the ESPHome real-time clock |
| `base_url` | no | `https://hydronode.texhfexlabs.de` | HydroNode origin without a path |
| `update_interval` | no | `60s` | Upload cycle; minimum `10s` |
| `response_buffer_size` | no | `2048` | Maximum command-response body in bytes; range 256–16384 |
| `allow_insecure` | no | `false` | Explicit opt-in to HTTP for local development only |
| `commands` | no | n/a | 1–64 `name`/`type` pairs the device handles; others are declined. Without it every command is confirmed |
| `on_command` | no | n/a | Automation invoked for each confirmed command |
| `on_upload_success` | no | n/a | Automation invoked after an HTTP `202` |
| `on_upload_error` | no | n/a | Automation invoked after local or HTTP errors |

### `measurements`

| Option | Required | Description |
|---|---:|---|
| `source` | yes | ID of any ESPHome numeric sensor |
| `type` | yes | HydroNode type: uppercase `A-Z`, digits and `_`, maximum 64 characters |

Each type must be unique within one `hydronode` block. HydroNode supports up to 25 measurement types per sensor. Keep the component interval at 10 seconds or more to respect the per-sensor/type rate limit.

Common types include `TEMPERATURE`, `HUMIDITY`, `PRESSURE`, `CO2`, `PM25`, `SOIL_MOISTURE`, `WATER_TEMPERATURE`, `WATER_PH`, `WATER_EC` and `BATTERY_VOLTAGE`. See the [HydroNode sensor type reference](https://hydronode.texhfexlabs.de/docs/guide/sensor-types/) for the complete list and expected units.

## Manual uploads

The `hydronode.send` action sends a value immediately and supports templated type and value fields:

```yaml
button:
  - platform: template
    name: Send pH Now
    on_press:
      - hydronode.send:
          id: hydronode_cloud
          type: WATER_PH
          value: !lambda return id(water_ph).state;
```

Manual sends share the HydroNode rate limit with scheduled sends. The action performs the signed HTTPS request synchronously, just like ESPHome's `http_request` action.

## Remote commands

Commands already queued for the HydroNode sensor are returned with an accepted measurement. In the HydroNode app every command has a name, a value type (`BOOL`, `INT32`, `UINT32`, `INT64`, `UINT64`, `STRING`) and a value.

Declare the commands your device handles under `commands:`. The component then answers the backend before any automation runs:

- a declared name with the matching type is **confirmed** and passed to `on_command`;
- an unknown name is **declined** with reason `NO_HANDLER`;
- a different type is **declined** with `TYPE_MISMATCH`;
- a value that does not fit the type (for example `-1` for `UINT32`) is **declined** with `INVALID_VALUE`.

The app shows declined commands with the reason, and they never reach your automation. Without a `commands:` block every command is confirmed and passed on, as in earlier versions.

```yaml
hydronode:
  # ...credentials and measurements...
  commands:
    - name: pump
      type: BOOL
    - name: co2_calibration
      type: UINT32
```

`on_command` exposes three variables:

- `command`: command name as a C++ `std::string`;
- `value_json`: exact JSON scalar, for example `true`, `4000` or `"auto"`. Hex entered in the app (`0x20124`) arrives as the decimal number `131364`;
- `type`: the value type, for example `BOOL`. Empty only for untyped commands without a declaration.

```yaml
hydronode:
  # ...credentials and measurements...
  on_command:
    then:
      - if:
          condition:
            lambda: return command == "pump";
          then:
            - if:
                condition:
                  lambda: return value_json == "true";
                then:
                  - switch.turn_on: nutrient_pump
                else:
                  - switch.turn_off: nutrient_pump
```

Confirmed means **received by the ESP32 and handed to your automation**, not necessarily that a physical action completed. Add your own safety conditions, duration limits, interlocks and local fallback behavior before controlling pumps, heaters or dosing equipment.

## Upload status automations

```yaml
hydronode:
  # ...credentials and measurements...
  on_upload_success:
    then:
      - logger.log:
          format: "Uploaded %s = %.2f"
          args: [type.c_str(), value]

  on_upload_error:
    then:
      - logger.log:
          level: WARN
          format: "Upload failed: %s = %.2f, status %d"
          args: [type.c_str(), value, status]
```

Status codes:

| Status | Meaning | Suggested action |
|---:|---|---|
| `202` | Measurement accepted | No action required |
| `-1` | Network unavailable | Wait for reconnect |
| `-2` | Clock not synchronized | Check NTP and internet access |
| `-3` | Transport/read error or invalid value | Inspect ESPHome logs; retry with backoff |
| `-4` | Local HMAC signing failure | Rebuild/reflash and report if reproducible |
| `401` | Invalid credentials, signature or timestamp | Check UUID, secret and time |
| `429` | Backend rate limit | Increase intervals and avoid overlapping manual sends |
| `5xx` | HydroNode service error | Retry later with backoff |

## Security model

- Every body is serialized deterministically and signed as `Base64(HMAC-SHA256(payload + timestamp))`.
- `X-Sensor-Id`, `X-Timestamp` and `X-Signature` are sent using the existing HydroNode protocol.
- HydroNode rejects requests outside its short timestamp window, reducing replay risk.
- The full ESPHome CA certificate bundle is enabled automatically so Cloudflare certificate-chain rotations remain trusted.
- `verify_ssl: true` must remain enabled for production. Plain HTTP requires both an HTTP `base_url` and `allow_insecure: true`.
- The component never prints the device secret, but ESPHome embeds it in the firmware. Protect configuration files, build artifacts, backups and physical access to the device.
- Rotate the HydroNode device secret if a configuration or firmware image is exposed.

## Current limitations

- Measurements are sent live; there is no on-device offline history queue.
- Each measurement is uploaded separately.
- Commands are delivered after a successful measurement upload rather than through independent polling.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `clock is not synchronized` | NTP has not completed; confirm DNS/internet access and wait for time sync |
| HTTP `401` | Sensor UUID/secret mismatch or clock outside the accepted time window |
| HTTP `429` | The same type was sent too frequently |
| HTTP/TLS failure | DNS, firewall, certificate validation or network instability |
| Sensor is skipped | The source has not published a state yet, or its state is `NaN`/infinite |
| Commands do not arrive | They are returned only after an accepted measurement upload |
| YAML says duplicate type | Map each HydroNode type only once in a component instance |
| ESP8266 compile failure | This release supports ESP32 only |

Set `logger:` to `DEBUG` or `VERY_VERBOSE` while diagnosing. Never paste secrets into an issue or log excerpt.

## Development

Clone the repository and install the validated ESPHome release:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install "esphome==2026.7.1"

python -m unittest discover -s tests -v
esphome config tests/configs/minimal-idf.yaml
esphome compile tests/configs/minimal-idf.yaml
esphome compile tests/configs/minimal-arduino.yaml
```

CI validates HMAC contract vectors and compiles complete firmware for ESP-IDF and Arduino. See [CONTRIBUTING.md](CONTRIBUTING.md) for project contributions.

## Related

- [HydroNode website and documentation](https://hydronode.texhfexlabs.de/)
- [HydroNode Arduino/ESP32 library](https://github.com/TexhFexLabs/HydroNode-Library)
- [ESPHome external component documentation](https://esphome.io/components/external_components/)

## License

MIT. See [LICENSE](LICENSE).

HydroNode ESPHome is developed and maintained by **TexhFexLabs**.
Support, feature requests and business inquiries: contact@knollfelix.de
