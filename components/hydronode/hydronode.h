#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::hydronode {

struct HydroNodeMeasurement {
  sensor::Sensor *source;
  std::string type;
};

struct HydroNodeHttpResult {
  int status;
  std::string body;
};

class HydroNodeComponent final : public PollingComponent {
 public:
  void set_http_request(http_request::HttpRequestComponent *http_request) { this->http_request_ = http_request; }
  void set_time(time::RealTimeClock *rtc) { this->time_ = rtc; }
  void set_base_url(const std::string &base_url) { this->base_url_ = base_url; }
  void set_sensor_id(const std::string &sensor_id) { this->sensor_id_ = sensor_id; }
  void set_device_secret(const std::string &device_secret) { this->device_secret_ = device_secret; }
  void set_response_buffer_size(size_t size) { this->response_buffer_size_ = size; }
  void add_measurement(sensor::Sensor *source, const std::string &type) {
    this->measurements_.push_back({source, type});
  }
  /// Declares a command the device handles. Once any command is declared,
  /// undeclared names and wrong value types are declined instead of confirmed.
  void add_command(const std::string &name, const std::string &type) { this->commands_[name] = type; }

  Trigger<std::string, std::string, std::string> *get_command_trigger() { return &this->command_trigger_; }
  Trigger<std::string, float> *get_upload_success_trigger() { return &this->upload_success_trigger_; }
  Trigger<std::string, float, int> *get_upload_error_trigger() { return &this->upload_error_trigger_; }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  int send_value(const std::string &type, float value);

 protected:
  std::string build_value_payload_(const std::string &type, float value, int64_t timestamp) const;
  std::string build_ack_payload_(const std::vector<std::string> &accepted,
                                 const std::vector<std::pair<std::string, std::string>> &declined) const;
  std::string sign_(const std::string &message) const;
  HydroNodeHttpResult post_signed_(const std::string &path, const std::string &payload, int64_t timestamp);
  std::string read_response_(const std::shared_ptr<http_request::HttpContainer> &container) const;
  void handle_commands_(const std::string &response);
  bool send_ack_(const std::vector<std::string> &accepted,
                 const std::vector<std::pair<std::string, std::string>> &declined);
  bool valid_time_(int64_t &timestamp) const;

  http_request::HttpRequestComponent *http_request_{nullptr};
  time::RealTimeClock *time_{nullptr};
  std::string base_url_;
  std::string sensor_id_;
  std::string device_secret_;
  size_t response_buffer_size_{2048};
  std::vector<HydroNodeMeasurement> measurements_;
  std::map<std::string, std::string> commands_;

  Trigger<std::string, std::string, std::string> command_trigger_;
  Trigger<std::string, float> upload_success_trigger_;
  Trigger<std::string, float, int> upload_error_trigger_;
};

template<typename... Ts> class HydroNodeSendAction final : public Action<Ts...> {
 public:
  explicit HydroNodeSendAction(HydroNodeComponent *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, type)
  TEMPLATABLE_VALUE(float, value)

  void play(const Ts &...x) override { this->parent_->send_value(this->type_.value(x...), this->value_.value(x...)); }

 protected:
  HydroNodeComponent *parent_;
};

}  // namespace esphome::hydronode
