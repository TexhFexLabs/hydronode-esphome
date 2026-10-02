#include "hydronode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::hydronode {

static const char *const TAG = "hydronode";
static const char *const VALUE_PATH = "/api/webhook/sensor-value";
static const char *const ACK_PATH = "/api/webhook/sensor-command-ack";
static const char *const HEADER_CONTENT_TYPE = "Content-Type";
static const char *const HEADER_SENSOR_ID = "X-Sensor-Id";
static const char *const HEADER_TIMESTAMP = "X-Timestamp";
static const char *const HEADER_SIGNATURE = "X-Signature";
static const char *const CONTENT_TYPE_JSON = "application/json";
static constexpr int HTTP_ACCEPTED = 202;
static constexpr int HTTP_OK = 200;
static constexpr int STATUS_NETWORK_UNAVAILABLE = -1;
static constexpr int STATUS_TIME_UNAVAILABLE = -2;
static constexpr int STATUS_TRANSPORT_ERROR = -3;
static constexpr int STATUS_SIGNING_ERROR = -4;
static constexpr int64_t MIN_VALID_EPOCH = 1600000000LL;
static constexpr size_t READ_CHUNK_SIZE = 512;

void HydroNodeComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HydroNode...");
}

void HydroNodeComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "HydroNode:");
  ESP_LOGCONFIG(TAG, "  Base URL: %s", this->base_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Sensor ID: %s", this->sensor_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Measurements: %u", static_cast<unsigned>(this->measurements_.size()));
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", static_cast<unsigned>(this->get_update_interval()));
  ESP_LOGCONFIG(TAG, "  Response buffer: %u bytes", static_cast<unsigned>(this->response_buffer_size_));
  for (const auto &measurement : this->measurements_) {
    ESP_LOGCONFIG(TAG, "    - %s", measurement.type.c_str());
  }
  if (this->commands_.empty()) {
    ESP_LOGCONFIG(TAG, "  Commands: none declared, every command is confirmed");
  } else {
    ESP_LOGCONFIG(TAG, "  Commands: %u declared, others are declined", static_cast<unsigned>(this->commands_.size()));
    for (const auto &[name, types] : this->commands_) {
      for (const auto &type : types) {
        ESP_LOGCONFIG(TAG, "    - %s (%s)", name.c_str(), type.c_str());
      }
    }
  }
}

void HydroNodeComponent::update() {
  for (const auto &measurement : this->measurements_) {
    if (measurement.source == nullptr || !measurement.source->has_state() ||
        !std::isfinite(measurement.source->state)) {
      ESP_LOGW(TAG, "Skipping %s: source has no finite state", measurement.type.c_str());
      continue;
    }
    this->send_value(measurement.type, measurement.source->state);
  }
}

int HydroNodeComponent::send_value(const std::string &type, float value) {
  if (!network::is_connected()) {
    ESP_LOGW(TAG, "Cannot send %s: network is unavailable", type.c_str());
    this->upload_error_trigger_.trigger(type, value, STATUS_NETWORK_UNAVAILABLE);
    return STATUS_NETWORK_UNAVAILABLE;
  }
  if (!std::isfinite(value)) {
    ESP_LOGW(TAG, "Cannot send %s: value is not finite", type.c_str());
    this->upload_error_trigger_.trigger(type, value, STATUS_TRANSPORT_ERROR);
    return STATUS_TRANSPORT_ERROR;
  }

  int64_t timestamp;
  if (!this->valid_time_(timestamp)) {
    ESP_LOGW(TAG, "Cannot send %s: clock is not synchronized", type.c_str());
    this->upload_error_trigger_.trigger(type, value, STATUS_TIME_UNAVAILABLE);
    return STATUS_TIME_UNAVAILABLE;
  }

  const std::string payload = this->build_value_payload_(type, value, timestamp);
  HydroNodeHttpResult result = this->post_signed_(VALUE_PATH, payload, timestamp);
  if (result.status == HTTP_ACCEPTED) {
    ESP_LOGI(TAG, "%s=%.2f accepted", type.c_str(), value);
    this->upload_success_trigger_.trigger(type, value);
    if (!result.body.empty()) {
      this->handle_commands_(result.body);
    }
  } else {
    ESP_LOGW(TAG, "%s=%.2f failed with status %d", type.c_str(), value, result.status);
    this->upload_error_trigger_.trigger(type, value, result.status);
  }
  return result.status;
}

std::string HydroNodeComponent::build_value_payload_(const std::string &type, float value, int64_t timestamp) const {
  // Largest finite float: max_exponent10 + 1 integer digits, sign, dot,
  // two fractional digits and the terminating null byte.
  char value_buffer[std::numeric_limits<float>::max_exponent10 + 6];
  std::snprintf(value_buffer, sizeof(value_buffer), "%.2f", static_cast<double>(value));

  return "{\"sensorId\":\"" + this->sensor_id_ + "\",\"type\":\"" + type + "\",\"value\":" + value_buffer +
         ",\"timestamp\":" + std::to_string(timestamp) + "}";
}

std::string HydroNodeComponent::build_ack_payload_(
    const std::vector<std::string> &accepted, const std::vector<std::pair<std::string, std::string>> &declined) const {
  auto serialized = json::build_json([this, &accepted, &declined](JsonObject root) {
    root["sensorId"] = this->sensor_id_;
    JsonArray ids = root["commandIds"].to<JsonArray>();
    for (const auto &id : accepted) {
      ids.add(id);
    }
    if (!declined.empty()) {
      JsonArray rejected = root["declined"].to<JsonArray>();
      for (const auto &[id, reason] : declined) {
        JsonObject entry = rejected.add<JsonObject>();
        entry["id"] = id;
        entry["reason"] = reason;
      }
    }
  });
  return std::string(serialized.data(), serialized.size());
}

bool HydroNodeComponent::valid_time_(int64_t &timestamp) const {
  if (this->time_ == nullptr) {
    return false;
  }
  const ESPTime now = this->time_->utcnow();
  if (!now.is_valid() || now.timestamp < MIN_VALID_EPOCH) {
    return false;
  }
  timestamp = now.timestamp;
  return true;
}

std::string HydroNodeComponent::sign_(const std::string &message) const {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return {};
  }

  unsigned char digest[32];
  const int hmac_result =
      mbedtls_md_hmac(info, reinterpret_cast<const unsigned char *>(this->device_secret_.data()),
                      this->device_secret_.size(), reinterpret_cast<const unsigned char *>(message.data()),
                      message.size(), digest);
  if (hmac_result != 0) {
    return {};
  }

  unsigned char encoded[64];
  size_t encoded_length = 0;
  const int base64_result =
      mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_length, digest, sizeof(digest));
  if (base64_result != 0) {
    return {};
  }
  return std::string(reinterpret_cast<char *>(encoded), encoded_length);
}

HydroNodeHttpResult HydroNodeComponent::post_signed_(const std::string &path, const std::string &payload,
                                                     int64_t timestamp) {
  const std::string timestamp_string = std::to_string(timestamp);
  const std::string signature = this->sign_(payload + timestamp_string);
  if (signature.empty()) {
    ESP_LOGE(TAG, "HMAC-SHA256 signing failed");
    return {STATUS_SIGNING_ERROR, {}};
  }

  std::vector<http_request::Header> headers{
      {HEADER_CONTENT_TYPE, CONTENT_TYPE_JSON},
      {HEADER_SENSOR_ID, this->sensor_id_},
      {HEADER_TIMESTAMP, timestamp_string},
      {HEADER_SIGNATURE, signature},
  };

  auto container = this->http_request_->post(this->base_url_ + path, payload, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "HTTP request could not be started");
    return {STATUS_TRANSPORT_ERROR, {}};
  }

  const int status = container->status_code;
  std::string response = this->read_response_(container);
  container->end();
  return {status, std::move(response)};
}

std::string HydroNodeComponent::read_response_(
    const std::shared_ptr<http_request::HttpContainer> &container) const {
  std::string response;
  response.reserve(std::min(container->content_length, this->response_buffer_size_));
  std::vector<uint8_t> buffer(std::min(READ_CHUNK_SIZE, this->response_buffer_size_));
  uint32_t last_data_time = millis();

  while (response.size() < this->response_buffer_size_ && !container->is_read_complete()) {
    const size_t available = this->response_buffer_size_ - response.size();
    const size_t requested = std::min(buffer.size(), available);
    const int read_or_error = container->read(buffer.data(), requested);
    App.feed_wdt();
    yield();

    const auto result = http_request::http_read_loop_result(
        read_or_error, last_data_time, this->http_request_->get_timeout(), container->is_read_complete());
    if (result == http_request::HttpReadLoopResult::RETRY) {
      continue;
    }
    if (result == http_request::HttpReadLoopResult::DATA) {
      response.append(reinterpret_cast<const char *>(buffer.data()), read_or_error);
      continue;
    }
    if (result == http_request::HttpReadLoopResult::ERROR) {
      ESP_LOGW(TAG, "Error while reading HTTP response");
    } else if (result == http_request::HttpReadLoopResult::TIMEOUT) {
      ESP_LOGW(TAG, "Timed out while reading HTTP response");
    }
    break;
  }

  if (!container->is_read_complete() && response.size() >= this->response_buffer_size_) {
    ESP_LOGW(TAG, "HTTP response exceeded configured response_buffer_size");
  }
  return response;
}

namespace {

struct ReceivedCommand {
  std::string id;
  std::string command;
  std::string value_json;
  std::string type;
};

// Same rule as the Arduino library: a typed command must name the declared
// type exactly; an untyped one (older apps) runs when the value fits.
const char *check_command(const std::string &declared, const std::string &wire_type, JsonVariantConst value) {
  if (!wire_type.empty() && wire_type != declared) {
    return "TYPE_MISMATCH";
  }
  bool fits = false;
  if (declared == "BOOL") {
    fits = value.is<bool>();
  } else if (declared == "INT32") {
    fits = value.is<int32_t>();
  } else if (declared == "UINT32") {
    fits = value.is<uint32_t>();
  } else if (declared == "INT64") {
    fits = value.is<int64_t>();
  } else if (declared == "UINT64") {
    fits = value.is<uint64_t>();
  } else if (declared == "STRING") {
    fits = value.is<const char *>();
  }
  if (fits) {
    return nullptr;
  }
  return wire_type.empty() ? "TYPE_MISMATCH" : "INVALID_VALUE";
}

// A typed command needs that type declared for its name; an untyped one (older app versions)
// takes the first declared type its value fits. Returns the decline reason, or nullptr and
// the matched type.
const char *match_command(const std::map<std::string, std::vector<std::string>> &commands, const std::string &name,
                          const std::string &wire_type, JsonVariantConst value, std::string &matched) {
  auto declared = commands.find(name);
  if (declared == commands.end() || declared->second.empty()) {
    return "NO_HANDLER";
  }
  const auto &types = declared->second;
  if (!wire_type.empty()) {
    if (std::find(types.begin(), types.end(), wire_type) == types.end()) {
      return "TYPE_MISMATCH";
    }
    matched = wire_type;
    return check_command(wire_type, wire_type, value);
  }
  const char *first = nullptr;
  for (const auto &type : types) {
    const char *reason = check_command(type, wire_type, value);
    if (reason == nullptr) {
      matched = type;
      return nullptr;
    }
    if (first == nullptr) {
      first = reason;
    }
  }
  return first;
}

}  // namespace

void HydroNodeComponent::handle_commands_(const std::string &response) {
  std::vector<ReceivedCommand> accepted;
  std::vector<std::string> accepted_ids;
  std::vector<std::pair<std::string, std::string>> declined;

  const bool valid = json::parse_json(response, [this, &accepted, &accepted_ids, &declined](JsonObject root) -> bool {
    if (!root["commands"].is<JsonArray>()) {
      return false;
    }
    JsonArray command_array = root["commands"].as<JsonArray>();
    for (JsonObject entry : command_array) {
      if (!entry["id"].is<const char *>() || !entry["command"].is<const char *>()) {
        continue;
      }
      ReceivedCommand command;
      command.id = entry["id"].as<const char *>();
      command.command = entry["command"].as<const char *>();
      command.type = entry["type"].is<const char *>() ? entry["type"].as<const char *>() : "";
      serializeJson(entry["value"], command.value_json);

      // Without declared commands every command is confirmed, as before.
      if (!this->commands_.empty()) {
        std::string matched;
        const char *reason = match_command(this->commands_, command.command, command.type, entry["value"], matched);
        if (reason != nullptr) {
          ESP_LOGW(TAG, "Declined command %s: %s", command.command.c_str(), reason);
          declined.emplace_back(command.id, reason);
          continue;
        }
        command.type = matched;
      }
      accepted_ids.push_back(command.id);
      accepted.push_back(std::move(command));
    }
    return true;
  });

  if (!valid) {
    ESP_LOGW(TAG, "Ignoring malformed command response");
    return;
  }
  if (accepted.empty() && declined.empty()) {
    return;
  }

  // Answer before user automations run, so a long-running pump automation
  // cannot push the signed ACK outside the backend's replay window.
  this->send_ack_(accepted_ids, declined);

  for (const auto &command : accepted) {
    ESP_LOGI(TAG, "Received command %s with value %s", command.command.c_str(), command.value_json.c_str());
    this->command_trigger_.trigger(command.command, command.value_json, command.type);
  }
}

bool HydroNodeComponent::send_ack_(const std::vector<std::string> &accepted,
                                   const std::vector<std::pair<std::string, std::string>> &declined) {
  int64_t timestamp;
  if (!this->valid_time_(timestamp)) {
    ESP_LOGW(TAG, "Cannot acknowledge commands: clock is not synchronized");
    return false;
  }
  const std::string payload = this->build_ack_payload_(accepted, declined);
  const HydroNodeHttpResult result = this->post_signed_(ACK_PATH, payload, timestamp);
  if (result.status != HTTP_OK) {
    ESP_LOGW(TAG, "Command ACK failed with status %d", result.status);
    return false;
  }
  ESP_LOGI(TAG, "Confirmed %u and declined %u command(s)", static_cast<unsigned>(accepted.size()),
           static_cast<unsigned>(declined.size()));
  return true;
}

}  // namespace esphome::hydronode
