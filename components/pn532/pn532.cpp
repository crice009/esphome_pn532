#include "pn532.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532 {

static const char *const TAG = "pn532";

// ─── PN532 frame constants ────────────────────────────────────────────────────
static const uint8_t PN532_PREAMBLE    = 0x00;
static const uint8_t PN532_STARTCODE2  = 0xFF;
static const uint8_t PN532_POSTAMBLE   = 0x00;
static const uint8_t PN532_HOSTTOPN532 = 0xD4;
static const uint8_t PN532_PN532TOHOST = 0xD5;

// ─── PN532 commands ───────────────────────────────────────────────────────────
static const uint8_t PN532_CMD_GETFIRMWAREVERSION   = 0x02;
static const uint8_t PN532_CMD_SAMCONFIGURATION     = 0x14;
static const uint8_t PN532_CMD_RFCONFIGURATION      = 0x32;
static const uint8_t PN532_CMD_INLISTPASSIVETARGET  = 0x4A;

static const uint8_t PN532_RF_FIELD_ITEM = 0x01;

// ─── ACK bytes ───────────────────────────────────────────────────────────────
static const uint8_t PN532_ACK_BYTES[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

// ─── Trigger constructors ─────────────────────────────────────────────────────

PN532Trigger::PN532Trigger(PN532 *parent) {
  parent->add_on_tag_callback([this](std::string uid) { this->trigger(uid); });
}

PN532TagRemovedTrigger::PN532TagRemovedTrigger(PN532 *parent) {
  parent->add_on_tag_removed_callback([this](std::string uid) { this->trigger(uid); });
}

// ─── Binary sensor ───────────────────────────────────────────────────────────

bool PN532BinarySensor::process(const std::vector<uint8_t> &uid) {
  if (uid.size() != this->uid_.size())
    return false;
  for (size_t i = 0; i < uid.size(); i++) {
    if (uid[i] != this->uid_[i])
      return false;
  }
  this->found_ = true;
  this->publish_state(true);
  return true;
}

// ─── Init sequence ────────────────────────────────────────────────────────────

bool PN532::init_pn532_() {
  // Retry firmware version check up to 3 times (fix for esphome/#3823)
  for (int attempt = 0; attempt < 3; attempt++) {
    if (this->get_firmware_version_())
      break;
    if (attempt == 2) {
      ESP_LOGE(TAG, "Could not get firmware version after 3 attempts");
      return false;
    }
    ESP_LOGW(TAG, "GetFirmwareVersion attempt %d failed, retrying...", attempt + 1);
    delay(50);
  }

  if (!this->setup_sam_()) {
    ESP_LOGE(TAG, "SAM configuration failed");
    return false;
  }

  return true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void PN532::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532...");

  // Transport-specific pre-setup (e.g. spi_setup())
  this->pn532_pre_setup_();

  if (!this->init_pn532_()) {
    this->mark_failed();
    return;
  }

  // RF field off to start — reduces WiFi interference between polls
  if (this->rf_field_off_when_idle_) {
    this->turn_off_rf_();
  }

  if (this->health_check_enabled_) {
    this->last_health_check_ms_ = millis();
  }

  ESP_LOGCONFIG(TAG, "PN532 setup complete");
}

// ─── Dump config ─────────────────────────────────────────────────────────────

void PN532::dump_config() {
  ESP_LOGCONFIG(TAG, "PN532:");
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Registered binary sensors: %d", this->binary_sensors_.size());
  ESP_LOGCONFIG(TAG, "  RF field off when idle: %s", this->rf_field_off_when_idle_ ? "yes" : "no");
  if (this->health_check_enabled_) {
    ESP_LOGCONFIG(TAG, "  Health Check: interval=%dms, max_failures=%d, auto_reset=%s",
                  this->health_check_interval_, this->max_failed_checks_,
                  this->auto_reset_on_failure_ ? "yes" : "no");
  } else {
    ESP_LOGCONFIG(TAG, "  Health Check: disabled");
  }
}

// ─── Loop (health check) ──────────────────────────────────────────────────────

void PN532::loop() {
  if (!this->health_check_enabled_)
    return;

  uint32_t now = millis();
  if (now - this->last_health_check_ms_ < this->health_check_interval_)
    return;
  this->last_health_check_ms_ = now;

  if (!this->perform_health_check_()) {
    this->consecutive_failures_++;
    ESP_LOGW(TAG, "Health check failed (%d/%d)", this->consecutive_failures_, this->max_failed_checks_);

    if (this->consecutive_failures_ >= this->max_failed_checks_) {
      if (this->is_healthy_) {
        this->is_healthy_ = false;
        this->status_set_error("PN532 health check failed");
        ESP_LOGE(TAG, "PN532 declared unhealthy after %d consecutive failures",
                 this->consecutive_failures_);
      }

      if (this->auto_reset_on_failure_) {
        ESP_LOGW(TAG, "Attempting automatic reset...");
        delay(50);
        if (this->init_pn532_()) {
          if (this->rf_field_off_when_idle_)
            this->turn_off_rf_();
          this->consecutive_failures_ = 0;
          this->is_healthy_ = true;
          this->retries_ = 0;
          this->throttle_ms_ = 0;
          this->status_clear_error();
          ESP_LOGI(TAG, "PN532 reset successful");
        } else {
          ESP_LOGE(TAG, "PN532 reset failed");
        }
      }
    }
  } else {
    if (this->consecutive_failures_ > 0)
      ESP_LOGI(TAG, "Health check recovered after %d failures", this->consecutive_failures_);
    this->consecutive_failures_ = 0;
    if (!this->is_healthy_) {
      this->is_healthy_ = true;
      this->status_clear_error();
      ESP_LOGI(TAG, "PN532 health restored");
    }
  }
}

// ─── Update (tag scanning) ────────────────────────────────────────────────────

void PN532::update() {
  if (this->health_check_enabled_ && !this->is_healthy_) {
    ESP_LOGD(TAG, "Skipping scan — PN532 unhealthy");
    return;
  }

  // Throttled backoff after bus failures
  if (this->throttle_ms_ > 0) {
    if (millis() - this->last_update_ms_ < this->throttle_ms_)
      return;
  }
  this->last_update_ms_ = millis();

  // Mark all binary sensors unseen at the start of this scan cycle
  for (auto *sensor : this->binary_sensors_)
    sensor->on_scan_end();

  // Send InListPassiveTarget (ISO14443A, 1 target max)
  if (!this->write_command_({PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00})) {
    ESP_LOGW(TAG, "Requesting tag read failed");
    this->status_set_warning();

    // Exponential backoff: 5s → 10s → 60s
    switch (this->retries_) {
      case 0:  this->retries_++; this->throttle_ms_ = std::max((uint32_t)5000u,  this->update_interval_); break;
      case 1:  this->retries_++; this->throttle_ms_ = std::max((uint32_t)10000u, this->update_interval_); break;
      default: this->throttle_ms_ = std::max((uint32_t)60000u, this->update_interval_); break;
    }
    return;
  }

  this->status_clear_warning();
  this->retries_ = 0;
  this->throttle_ms_ = 0;

  std::vector<uint8_t> uid;
  if (this->read_tag_(uid)) {
    this->process_tag_(uid);
  } else {
    if (this->tag_present_)
      this->tag_removed_();
  }

  if (this->rf_field_off_when_idle_) {
    ESP_LOGV(TAG, "Turning RF field OFF");
    this->turn_off_rf_();
  }
}

// ─── Tag processing ───────────────────────────────────────────────────────────

void PN532::process_tag_(const std::vector<uint8_t> &uid) {
  // BUG FIX for #9875: silently refresh when same tag still present
  if (uid == this->current_uid_ && this->tag_present_) {
    for (auto *sensor : this->binary_sensors_)
      sensor->process(uid);
    return;
  }

  this->current_uid_ = uid;
  this->tag_present_ = true;

  // Format UID as hyphen-separated hex: "74-10-37-94"
  std::string uid_str;
  for (size_t i = 0; i < uid.size(); i++) {
    if (i > 0) uid_str += '-';
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uid[i]);
    uid_str += buf;
  }

  ESP_LOGD(TAG, "Found new tag '%s'", uid_str.c_str());

  for (auto *sensor : this->binary_sensors_)
    sensor->process(uid);

  this->on_tag_callbacks_.call(uid_str);
}

void PN532::tag_removed_() {
  std::string uid_str;
  for (size_t i = 0; i < this->current_uid_.size(); i++) {
    if (i > 0) uid_str += '-';
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", this->current_uid_[i]);
    uid_str += buf;
  }

  ESP_LOGD(TAG, "Tag removed: '%s'", uid_str.c_str());

  this->on_tag_removed_callbacks_.call(uid_str);

  for (auto *sensor : this->binary_sensors_) {
    if (sensor->state)
      sensor->publish_state(false);
  }

  this->tag_present_ = false;
  this->current_uid_.clear();
}

// ─── Read tag UID ─────────────────────────────────────────────────────────────

bool PN532::read_tag_(std::vector<uint8_t> &uid) {
  // Wait for PN532 readiness (with 1s wall-clock timeout)
  uint32_t start = millis();
  while (!this->is_read_ready()) {
    if (millis() - start > 1000) {
      ESP_LOGV(TAG, "Timed out waiting for readiness from PN532!");
      this->send_ack_();
      return false;
    }
    yield();
  }

  std::vector<uint8_t> response;
  if (!this->read_response(PN532_CMD_INLISTPASSIVETARGET, response))
    return false;

  // Response: [num_targets, target#, ATQA(2), SAK(1), nfcid_len, nfcid...]
  if (response.empty() || response[0] == 0x00)
    return false;

  if (response.size() < 6) {
    ESP_LOGW(TAG, "Malformed InListPassiveTarget response (%d bytes)", response.size());
    return false;
  }

  uint8_t nfcid_len = response[5];
  if (response.size() < (size_t)(6 + nfcid_len)) {
    ESP_LOGW(TAG, "NFCID length %d exceeds response buffer (%d bytes)", nfcid_len, response.size());
    return false;
  }

  uid.assign(response.begin() + 6, response.begin() + 6 + nfcid_len);
  return true;
}

// ─── RF field off ─────────────────────────────────────────────────────────────

void PN532::turn_off_rf_() {
  // RFConfiguration item 0x01: AutoRFCA=0, RF=0
  this->write_command_({PN532_CMD_RFCONFIGURATION, PN532_RF_FIELD_ITEM, 0x00});
}

// ─── SAM configuration ───────────────────────────────────────────────────────

bool PN532::setup_sam_() {
  // Normal mode (0x01), timeout 0x14, use IRQ=1
  if (!this->write_command_({PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01})) {
    ESP_LOGE(TAG, "SAMConfiguration write failed");
    return false;
  }
  std::vector<uint8_t> response;
  if (!this->read_response(PN532_CMD_SAMCONFIGURATION, response)) {
    ESP_LOGE(TAG, "SAMConfiguration read failed");
    return false;
  }
  return true;
}

// ─── Firmware version ────────────────────────────────────────────────────────

bool PN532::get_firmware_version_() {
  if (!this->write_command_({PN532_CMD_GETFIRMWAREVERSION})) {
    ESP_LOGW(TAG, "GetFirmwareVersion write failed");
    return false;
  }
  std::vector<uint8_t> response;
  if (!this->read_response(PN532_CMD_GETFIRMWAREVERSION, response)) {
    ESP_LOGW(TAG, "GetFirmwareVersion read failed");
    return false;
  }
  if (response.size() < 4) {
    ESP_LOGW(TAG, "GetFirmwareVersion response too short (%d bytes)", response.size());
    return false;
  }
  ESP_LOGCONFIG(TAG, "  IC: 0x%02X  Ver: %d.%d  Support: 0x%02X",
                response[0], response[1], response[2], response[3]);
  return true;
}

// ─── Write command frame ──────────────────────────────────────────────────────

bool PN532::write_command_(const std::vector<uint8_t> &data) {
  uint8_t len = (uint8_t)(data.size() + 1);
  uint8_t lcs = (~len + 1) & 0xFF;

  uint8_t sum = PN532_HOSTTOPN532;
  for (auto b : data) sum += b;
  uint8_t dcs = (~sum + 1) & 0xFF;

  std::vector<uint8_t> frame;
  frame.push_back(PN532_PREAMBLE);
  frame.push_back(PN532_PREAMBLE);
  frame.push_back(PN532_STARTCODE2);
  frame.push_back(len);
  frame.push_back(lcs);
  frame.push_back(PN532_HOSTTOPN532);
  for (auto b : data) frame.push_back(b);
  frame.push_back(dcs);
  frame.push_back(PN532_POSTAMBLE);

  if (!this->write_data(frame))
    return false;

  return this->read_ack_();
}

// ─── Read ACK ────────────────────────────────────────────────────────────────

bool PN532::read_ack_() {
  ESP_LOGV(TAG, "Reading ACK");
  std::vector<uint8_t> ack;
  if (!this->read_data(ack, 6)) {
    ESP_LOGW(TAG, "Failed to read ACK");
    return false;
  }
  bool valid = (ack.size() == 6 &&
                ack[0] == PN532_ACK_BYTES[0] &&
                ack[1] == PN532_ACK_BYTES[1] &&
                ack[2] == PN532_ACK_BYTES[2] &&
                ack[3] == PN532_ACK_BYTES[3] &&
                ack[4] == PN532_ACK_BYTES[4] &&
                ack[5] == PN532_ACK_BYTES[5]);
  ESP_LOGV(TAG, "ACK valid: %s", valid ? "YES" : "NO");
  return valid;
}

// ─── Send ACK (abort) ────────────────────────────────────────────────────────

bool PN532::send_ack_() {
  ESP_LOGV(TAG, "Sending ACK for abort");
  std::vector<uint8_t> ack(PN532_ACK_BYTES, PN532_ACK_BYTES + 6);
  return this->write_data(ack);
}

// ─── Health check ─────────────────────────────────────────────────────────────

bool PN532::perform_health_check_() {
  if (!this->write_command_({PN532_CMD_GETFIRMWAREVERSION})) {
    ESP_LOGD(TAG, "Health check: GetFirmwareVersion write failed");
    return false;
  }
  std::vector<uint8_t> response;
  if (!this->read_response(PN532_CMD_GETFIRMWAREVERSION, response)) {
    ESP_LOGD(TAG, "Health check: GetFirmwareVersion read failed");
    return false;
  }
  if (response.size() < 4) {
    ESP_LOGD(TAG, "Health check: response too short");
    return false;
  }
  ESP_LOGV(TAG, "Health check passed");
  return true;
}

}  // namespace pn532
}  // namespace esphome
