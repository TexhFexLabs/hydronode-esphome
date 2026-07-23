#include "hydronode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
  char value_buffer[32];
  std::snprintf(value_buffer, sizeof(value_buffer), "%.2f", static_cast<double>(value));

  return "{\"sensorId\":\"" + this->sensor_id_ + "\",\"type\":\"" + type + "\",\"value\":" + value_buffer +
         ",\"timestamp\":" + std::to_string(timestamp) + "}";
}

std::string HydroNodeComponent::build_ack_payload_(const std::vector<std::string> &command_ids) const {
  auto serialized = json::build_json([this, &command_ids](JsonObject root) {
    root["sensorId"] = this->sensor_id_;
    JsonArray ids = root["commandIds"].to<JsonArray>();
    for (const auto &id : command_ids) {
      ids.add(id);
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

void HydroNodeComponent::handle_commands_(const std::string &response) {
  std::vector<std::pair<std::string, std::string>> commands;
  std::vector<std::string> command_ids;

  const bool valid = json::parse_json(response, [&commands, &command_ids](JsonObject root) -> bool {
    if (!root["commands"].is<JsonArray>()) {
      return false;
    }
    JsonArray command_array = root["commands"].as<JsonArray>();
    for (JsonObject entry : command_array) {
      if (!entry["id"].is<const char *>() || !entry["command"].is<const char *>()) {
        continue;
      }
      std::string value_json;
      serializeJson(entry["value"], value_json);
      command_ids.emplace_back(entry["id"].as<const char *>());
      commands.emplace_back(entry["command"].as<const char *>(), std::move(value_json));
    }
    return true;
  });

  if (!valid) {
    ESP_LOGW(TAG, "Ignoring malformed command response");
    return;
  }
  if (commands.empty()) {
    return;
  }

  // HydroNode ACKs mean "received by the device", so acknowledge before user
  // automations run. A long-running pump automation cannot push the signed ACK
  // outside the backend's replay window.
  this->send_ack_(command_ids);

  for (const auto &[command, value_json] : commands) {
    ESP_LOGI(TAG, "Received command %s with value %s", command.c_str(), value_json.c_str());
    this->command_trigger_.trigger(command, value_json);
  }
}

bool HydroNodeComponent::send_ack_(const std::vector<std::string> &command_ids) {
  int64_t timestamp;
  if (!this->valid_time_(timestamp)) {
    ESP_LOGW(TAG, "Cannot acknowledge commands: clock is not synchronized");
    return false;
  }
  const std::string payload = this->build_ack_payload_(command_ids);
  const HydroNodeHttpResult result = this->post_signed_(ACK_PATH, payload, timestamp);
  if (result.status != HTTP_OK) {
    ESP_LOGW(TAG, "Command ACK failed with status %d", result.status);
    return false;
  }
  ESP_LOGI(TAG, "Acknowledged %u command(s)", static_cast<unsigned>(command_ids.size()));
  return true;
}

}  // namespace esphome::hydronode
