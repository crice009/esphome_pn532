#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_NFC
#include "esphome/components/nfc/nfc.h"
#include "esphome/components/nfc/ndef_message.h"
#endif

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

#ifdef USE_NFC
class PN532Trigger : public Trigger<std::string, nfc::NfcTag> {
#else
class PN532Trigger : public Trigger<std::string> {
#endif
 public:
  explicit PN532Trigger(PN532 *parent);
};

class PN532TagRemovedTrigger : public Trigger<std::string> {
 public:
  explicit PN532TagRemovedTrigger(PN532 *parent);
};

// ─── NDEF Write Actions ──────────────────────────────────────────────────────

#ifdef USE_NFC
class PN532IsWritingCondition : public Condition<> {
 public:
  explicit PN532IsWritingCondition(PN532 *parent) : parent_(parent) {}
  bool check() override;

 protected:
  PN532 *parent_;
};
#endif

// ─── Main Base Component ─────────────────────────────────────────────────────

class PN532 : public PollingComponent {
 public:
  // ── Lifecycle ──
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ── Sensor registration ──
  void register_tag_sensor(PN532BinarySensor *sensor) {
    this->binary_sensors_.push_back(sensor);
  }

  // ── Callbacks ──
#ifdef USE_NFC
  void add_on_tag_callback(std::function<void(std::string, nfc::NfcTag)> &&cb) {
    this->on_tag_callbacks_.add(std::move(cb));
  }
#else
  void add_on_tag_callback(std::function<void(std::string)> &&cb) {
    this->on_tag_callbacks_.add(std::move(cb));
  }
#endif
  void add_on_tag_removed_callback(std::function<void(std::string)> &&cb) {
    this->on_tag_removed_callbacks_.add(std::move(cb));
  }

  // ── Health check configuration ──
  void set_health_check_enabled(bool enabled) { this->health_check_enabled_ = enabled; }
  void set_health_check_interval(uint32_t ms) { this->health_check_interval_ = ms; }
  void set_auto_reset_on_failure(bool v) { this->auto_reset_on_failure_ = v; }
  void set_max_failed_checks(uint8_t v) { this->max_failed_checks_ = v; }

  // ── RF field management ──
  void set_rf_field_enabled(bool v) { this->rf_field_off_when_idle_ = !v; }

  // ── NDEF write mode ──
#ifdef USE_NFC
  void write_mode(nfc::NdefMessage *message) {
    this->next_write_message_ = message;
    this->updates_enabled_ = false;
  }
  bool is_writing() { return !this->updates_enabled_; }
#endif

  // ── Called by transport subclasses ──
  void enable_updates() { this->updates_enabled_ = true; }

 protected:
  // ── Abstract transport interface ──
  virtual bool write_data(const std::vector<uint8_t> &data) = 0;
  virtual bool read_data(std::vector<uint8_t> &data, uint8_t len) = 0;
  virtual bool read_response(uint8_t command, std::vector<uint8_t> &data) = 0;
  virtual bool is_read_ready() { return true; }

  // ── Protocol helpers ──
  bool write_command_(const std::vector<uint8_t> &data);
  bool read_ack_();
  bool send_ack_();

  // ── Tag detection pipeline ──
  void process_tag_(const std::vector<uint8_t> &uid);
  void tag_removed_();
  bool read_tag_(std::vector<uint8_t> &uid);

  // ── RF field control ──
  void turn_off_rf_();

  // ── Initialization helpers ──
  bool setup_sam_();
  bool get_firmware_version_();
  virtual void pn532_setup_() {}  // subclass can add transport-specific init

  // ── Health check ──
  bool perform_health_check_();

  // ── State ──
  bool updates_enabled_{true};
  bool requested_read_{false};

  // RF field management
  bool rf_field_off_when_idle_{true};  // default: RF off between polls (reduces WiFi interference)

  // Tag state
  std::vector<uint8_t> current_uid_{};
  bool tag_present_{false};

  // Binary sensors for known tags
  std::vector<PN532BinarySensor *> binary_sensors_;

  // Error / throttle tracking (bug fix: prevent tight failure loops)
  uint8_t retries_{0};
  uint32_t last_update_{0};
  uint32_t throttle_{0};

  // Health check
  bool health_check_enabled_{true};
  uint32_t health_check_interval_{60000};
  uint32_t last_health_check_{0};
  uint8_t consecutive_failures_{0};
  uint8_t max_failed_checks_{3};
  bool auto_reset_on_failure_{true};
  bool is_healthy_{true};

  // NDEF write support
#ifdef USE_NFC
  nfc::NdefMessage *next_write_message_{nullptr};
#endif

  // Callbacks
#ifdef USE_NFC
  CallbackManager<void(std::string, nfc::NfcTag)> on_tag_callbacks_;
#else
  CallbackManager<void(std::string)> on_tag_callbacks_;
#endif
  CallbackManager<void(std::string)> on_tag_removed_callbacks_;
};

}  // namespace pn532
}  // namespace esphome
