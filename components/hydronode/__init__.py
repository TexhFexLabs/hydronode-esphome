"""ESPHome external component for the HydroNode IoT backend."""

from __future__ import annotations

import re
from urllib.parse import urlparse

from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32, http_request, sensor, time
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

CODEOWNERS = ["@TexhFexLabs"]
# Sent as X-Firmware: esphome-hydronode/<version> <chip>. Same as COMPONENT_VERSION in hydronode.h.
COMPONENT_VERSION = "0.4.0"
DEPENDENCIES = ["esp32", "http_request", "network", "sensor", "time"]
AUTO_LOAD = ["json"]
MULTI_CONF = True

CONF_ALLOW_INSECURE = "allow_insecure"
CONF_BASE_URL = "base_url"
CONF_COMMANDS = "commands"
CONF_DEVICE_SECRET = "device_secret"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_MEASUREMENTS = "measurements"
CONF_NAME = "name"
CONF_ON_COMMAND = "on_command"
CONF_ON_UPLOAD_ERROR = "on_upload_error"
CONF_ON_UPLOAD_SUCCESS = "on_upload_success"
CONF_RESPONSE_BUFFER_SIZE = "response_buffer_size"
CONF_SENSOR_ID = "sensor_id"
CONF_SOURCE = "source"
CONF_TIME_ID = "time_id"
CONF_TYPE = "type"
CONF_VALUE = "value"

DEFAULT_BASE_URL = "https://hydronode.tech"
MIN_SEND_INTERVAL_MS = 10_000

hydronode_ns = cg.esphome_ns.namespace("hydronode")
HydroNodeComponent = hydronode_ns.class_("HydroNodeComponent", cg.PollingComponent)
HydroNodeSendAction = hydronode_ns.class_("HydroNodeSendAction", automation.Action)

UUID_RE = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)
TYPE_RE = re.compile(r"^[A-Z][A-Z0-9_]{0,63}$")

# Value types a HydroNode command can carry; the same set as the backend and
# the Arduino library (onBool, onInt32, ...). Floats are deliberately absent.
COMMAND_VALUE_TYPES = ["BOOL", "INT32", "UINT32", "INT64", "UINT64", "STRING"]


def _validate_uuid(value: str) -> str:
    value = cv.string_strict(value)
    if not UUID_RE.fullmatch(value):
        raise cv.Invalid("sensor_id must be a canonical UUID")
    return value.lower()


def _validate_sensor_type(value: str) -> str:
    value = cv.string_strict(value)
    if not TYPE_RE.fullmatch(value):
        raise cv.Invalid(
            "type must start with A-Z and contain only A-Z, 0-9 or _ "
            "(maximum 64 characters)"
        )
    return value


def _validate_secret(value: str) -> str:
    value = cv.string_strict(value)
    if len(value) < 16:
        raise cv.Invalid("device_secret must contain at least 16 characters")
    return value


def _validate_send_interval(value):
    value = cv.positive_time_period_milliseconds(value)
    if value.total_milliseconds < MIN_SEND_INTERVAL_MS:
        raise cv.Invalid(
            "update_interval must be at least 10s because HydroNode rate-limits "
            "each sensor/type pair"
        )
    return value


def _validate_config(config):
    parsed = urlparse(config[CONF_BASE_URL])
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise cv.Invalid("base_url must be an absolute http:// or https:// URL")
    if parsed.query or parsed.fragment:
        raise cv.Invalid("base_url must not contain a query string or fragment")
    if parsed.path not in ("", "/"):
        raise cv.Invalid("base_url must not contain a path")
    if parsed.scheme != "https" and not config[CONF_ALLOW_INSECURE]:
        raise cv.Invalid(
            "HTTP exposes the device secret and measurements. Use HTTPS or set "
            "allow_insecure: true explicitly for local development."
        )

    seen_types: set[str] = set()
    for measurement in config[CONF_MEASUREMENTS]:
        sensor_type = measurement[CONF_TYPE]
        if sensor_type in seen_types:
            raise cv.Invalid(
                f"duplicate measurement type {sensor_type!r}; each type may only "
                "be mapped once per HydroNode component"
            )
        seen_types.add(sensor_type)

    # Cloudflare may rotate HydroNode between certificate chains rooted at
    # Google Trust Services, Let's Encrypt, or SSL.com. ESPHome's smaller
    # common-CA bundle does not include every one of those roots.
    esp32.require_full_certificate_bundle()

    config[CONF_BASE_URL] = config[CONF_BASE_URL].rstrip("/")
    return config


def _validate_command_name(value: str) -> str:
    value = cv.string_strict(value).strip()
    if not 1 <= len(value) <= 64:
        raise cv.Invalid("command name must be 1 to 64 characters")
    if any(ord(char) < 0x20 or char in '"\\' for char in value):
        raise cv.Invalid("command name must not contain quotes, backslashes or control characters")
    return value


def _validate_unique_commands(commands: list) -> list:
    # One name may carry several types (a relay switched with BOOL and pulsed with UINT32),
    # each declared once.
    seen = set()
    for command in commands:
        key = (command[CONF_NAME], command[CONF_TYPE])
        if key in seen:
            raise cv.Invalid(f"duplicate command {key[0]!r} with type {key[1]}; declare each name and type once")
        seen.add(key)
    return commands


COMMAND_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): _validate_command_name,
        cv.Required(CONF_TYPE): cv.one_of(*COMMAND_VALUE_TYPES, upper=True),
    }
)

MEASUREMENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SOURCE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TYPE): _validate_sensor_type,
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HydroNodeComponent),
            cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(
                http_request.HttpRequestComponent
            ),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_BASE_URL, default=DEFAULT_BASE_URL): cv.url,
            cv.Required(CONF_SENSOR_ID): _validate_uuid,
            cv.Required(CONF_DEVICE_SECRET): cv.sensitive(_validate_secret),
            cv.Required(CONF_MEASUREMENTS): cv.All(
                cv.ensure_list(MEASUREMENT_SCHEMA), cv.Length(min=1, max=25)
            ),
            cv.Optional(CONF_RESPONSE_BUFFER_SIZE, default=16384): cv.int_range(
                min=256, max=16384
            ),
            cv.Optional(CONF_ALLOW_INSECURE, default=False): cv.boolean,
            cv.Optional(CONF_COMMANDS): cv.All(
                cv.ensure_list(COMMAND_SCHEMA),
                cv.Length(min=1, max=64),
                _validate_unique_commands,
            ),
            cv.Optional(CONF_ON_COMMAND): automation.validate_automation(
                {
                    cv.GenerateID(): cv.declare_id(
                        automation.Trigger.template(
                            cg.std_string, cg.std_string, cg.std_string
                        )
                    )
                }
            ),
            cv.Optional(CONF_ON_UPLOAD_SUCCESS): automation.validate_automation(
                {
                    cv.GenerateID(): cv.declare_id(
                        automation.Trigger.template(cg.std_string, cg.float_)
                    )
                }
            ),
            cv.Optional(CONF_ON_UPLOAD_ERROR): automation.validate_automation(
                {
                    cv.GenerateID(): cv.declare_id(
                        automation.Trigger.template(
                            cg.std_string, cg.float_, cg.int_
                        )
                    )
                }
            ),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="60s"
            ): _validate_send_interval,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    http = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    rtc = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_http_request(http))
    cg.add(var.set_time(rtc))
    cg.add(var.set_base_url(config[CONF_BASE_URL]))
    cg.add(var.set_sensor_id(config[CONF_SENSOR_ID]))
    cg.add(var.set_device_secret(config[CONF_DEVICE_SECRET]))
    cg.add(var.set_response_buffer_size(config[CONF_RESPONSE_BUFFER_SIZE]))

    for measurement in config[CONF_MEASUREMENTS]:
        source = await cg.get_variable(measurement[CONF_SOURCE])
        cg.add(var.add_measurement(source, measurement[CONF_TYPE]))

    for command in config.get(CONF_COMMANDS, []):
        cg.add(var.add_command(command[CONF_NAME], command[CONF_TYPE]))

    for conf in config.get(CONF_ON_COMMAND, []):
        await automation.build_automation(
            var.get_command_trigger(),
            [
                (cg.std_string, "command"),
                (cg.std_string, "value_json"),
                (cg.std_string, "type"),
            ],
            conf,
        )

    for conf in config.get(CONF_ON_UPLOAD_SUCCESS, []):
        await automation.build_automation(
            var.get_upload_success_trigger(),
            [
                (cg.std_string, "type"),
                (cg.float_, "value"),
            ],
            conf,
        )

    for conf in config.get(CONF_ON_UPLOAD_ERROR, []):
        await automation.build_automation(
            var.get_upload_error_trigger(),
            [
                (cg.std_string, "type"),
                (cg.float_, "value"),
                (cg.int_, "status"),
            ],
            conf,
        )


HYDRONODE_SEND_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(HydroNodeComponent),
        cv.Required(CONF_TYPE): cv.templatable(_validate_sensor_type),
        cv.Required(CONF_VALUE): cv.templatable(cv.float_),
    }
)


@automation.register_action(
    "hydronode.send",
    HydroNodeSendAction,
    HYDRONODE_SEND_SCHEMA,
    synchronous=True,
)
async def hydronode_send_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    sensor_type = await cg.templatable(config[CONF_TYPE], args, cg.std_string)
    value = await cg.templatable(config[CONF_VALUE], args, cg.float_)
    cg.add(var.set_type(sensor_type))
    cg.add(var.set_value(value))
    return var
