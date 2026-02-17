#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace pn532 {

// ─── Forward declarations ────────────────────────────────────────────────────

class PN532;

// ─── Binary Sensor ───────────────────────────────────────────────────────────

class PN532BinarySensor : public binary_sensor::BinarySensor {
 public:
  void set_uid(const std::vector<uint8_t> &uid) { this->uid_ = uid; }

  bool process(const std::vector<uint8_t> &uid);

  void on_scan_end() {
    if (!this->found_) {
      this->publish_state(false);
    }
    this->found_ = false;
  }

 protected:
  std::vector<uint8_t> uid_;
  bool found_{false};
};

// ─── Triggers ────────────────────────────────────────────────────────────────

class PN532Trigger : public Trigger<std::string> {
 public:
  explicit PN532Trigger(PN532 *parent);
};

class PN532TagRemovedTrigger : public Trigger<std::string> {
 public:
  explicit PN532TagRemovedTrigger(PN532 *parent);
};

// ─── Main Base Component ─────────────────────────────────────────────────────

class PN532 : public PollingComponent {
 public:
  // ── Lifecycle ──
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ── Pin configuration ──
  void set_rst_pin(GPIOPin *pin) { this->rst_pin_ = pin; }

  // ── Sensor registration ──
  void register_tag_sensor(PN532BinarySensor *sensor) {
    this->binary_sensors_.push_back(sensor);
  }

  // ── Callbacks ──
  void add_on_tag_callback(std::function<void(std::string)> &&cb) {
    this->on_tag_callbacks_.add(std::move(cb));
  }
  void add_on_tag_removed_callback(std::function<void(std::string)> &&cb) {
    this->on_tag_removed_callbacks_.add(std::move(cb));
  }

  // ── Health check configuration ──
  void set_health_check_enabled(bool enabled) { this->health_check_enabled_ = enabled; }
  void set_health_check_interval(uint32_t ms) { this->health_check_interval_ = ms; }
  void set_auto_reset_on_failure(bool v) { this->auto_reset_on_failure_ = v; }
  void set_max_failed_checks(uint8_t v) { this->max_failed_checks_ = v; }

  // ── RF field management ──
  // When true: RF field stays on between polls (old native behaviour).
  // When false (default): RF off between polls — reduces WiFi interference.
  void set_rf_field_enabled(bool v) { this->rf_field_off_when_idle_ = !v; }

 protected:
  // ── Abstract transport interface (implemented by SPI/I2C subclasses) ──
  virtual bool write_data(const std::vector<uint8_t> &data) = 0;
  virtual bool read_data(std::vector<uint8_t> &data, uint8_t len) = 0;
  virtual bool read_response(uint8_t command, std::vector<uint8_t> &data) = 0;
  // Returns true when the PN532 has a response ready to read.
  // Default returns true (polling / SPI status register check is optional).
  virtual bool is_read_ready() { return true; }
  // Called once during setup() before the base init sequence.
  // Transport subclasses use this for their own bus setup (e.g. spi_setup()).
  virtual void pn532_pre_setup_() {}

  // ── Protocol helpers ──
  bool write_command_(const std::vector<uint8_t> &data);
  bool read_ack_();
  bool send_ack_();

  // ── Tag detection ──
  void process_tag_(const std::vector<uint8_t> &uid);
  void tag_removed_();
  bool read_tag_(std::vector<uint8_t> &uid);

  // ── RF field ──
  void turn_off_rf_();

  // ── Init helpers ──
  bool init_pn532_();
  bool setup_sam_();
  bool get_firmware_version_();

  // ── Hardware reset via RSTPD_N pin (fix for #10968) ──
  void hardware_reset_();

  // ── Health check ──
  bool perform_health_check_();

  // ── Hardware ──
  GPIOPin *rst_pin_{nullptr};

  // ── RF field state ──
  // true = turn off between polls (default, reduces WiFi interference)
  bool rf_field_off_when_idle_{true};

  // ── Tag state ──
  std::vector<uint8_t> current_uid_{};
  bool tag_present_{false};

  // ── Binary sensors ──
  std::vector<PN532BinarySensor *> binary_sensors_;

  // ── Backoff state (fix for blocking-operation warnings) ──
  uint8_t retries_{0};
  uint32_t last_update_ms_{0};
  uint32_t throttle_ms_{0};

  // ── Health check state ──
  bool health_check_enabled_{true};
  uint32_t health_check_interval_{60000};
  uint32_t last_health_check_ms_{0};
  uint8_t consecutive_failures_{0};
  uint8_t max_failed_checks_{3};
  bool auto_reset_on_failure_{true};
  bool is_healthy_{true};

  // ── Callbacks ──
  CallbackManager<void(std::string)> on_tag_callbacks_;
  CallbackManager<void(std::string)> on_tag_removed_callbacks_;
};

}  // namespace pn532
}  // namespace esphome
