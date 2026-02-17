#include "pn532.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace pn532 {

static const char *const TAG = "pn532";

// ─── PN532 frame constants ────────────────────────────────────────────────────
static const uint8_t PN532_PREAMBLE = 0x00;
static const uint8_t PN532_STARTCODE1 = 0x00;
static const uint8_t PN532_STARTCODE2 = 0xFF;
static const uint8_t PN532_POSTAMBLE = 0x00;
static const uint8_t PN532_HOSTTOPN532 = 0xD4;
static const uint8_t PN532_PN532TOHOST = 0xD5;

// ─── PN532 commands ───────────────────────────────────────────────────────────
static const uint8_t PN532_COMMAND_GETFIRMWAREVERSION = 0x02;
static const uint8_t PN532_COMMAND_SAMCONFIGURATION = 0x14;
static const uint8_t PN532_COMMAND_RFCONFIGURATION = 0x32;
static const uint8_t PN532_COMMAND_INLISTPASSIVETARGET = 0x4A;

// RFCONFIGURATION item for RF field control
static const uint8_t PN532_RF_FIELD_ITEM = 0x01;

// ─── ACK frame ───────────────────────────────────────────────────────────────
static const uint8_t PN532_ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static const uint8_t PN532_NACK[] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};

// ─── Trigger constructors ─────────────────────────────────────────────────────

PN532Trigger::PN532Trigger(PN532 *parent) {
#ifdef USE_NFC
  parent->add_on_tag_callback([this](std::string uid, nfc::NfcTag tag) {
    this->trigger(uid, tag);
  });
#else
  parent->add_on_tag_callback([this](std::string uid) {
    this->trigger(uid);
  });
#endif
}

PN532TagRemovedTrigger::PN532TagRemovedTrigger(PN532 *parent) {
  parent->add_on_tag_removed_callback([this](std::string uid) {
    this->trigger(uid);
  });
}

#ifdef USE_NFC
bool PN532IsWritingCondition::check() {
  return this->parent_->is_writing();
}
#endif

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

// ─── Setup ───────────────────────────────────────────────────────────────────

void PN532::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532...");

  // Allow transport subclass to run its own init (e.g. SPI/I2C setup)
  this->pn532_setup_();

  // Retry firmware version check up to 3 times (fixes esphome/issues#3823)
  for (int attempt = 0; attempt < 3; attempt++) {
    if (this->get_firmware_version_()) {
      break;
    }
    if (attempt == 2) {
      ESP_LOGE(TAG, "Could not get firmware version from PN532 after 3 attempts");
      this->mark_failed();
      return;
    }
    delay(50);
    ESP_LOGW(TAG, "Firmware version check attempt %d failed, retrying...", attempt + 1);
  }

  if (!this->setup_sam_()) {
    ESP_LOGE(TAG, "Failed to configure SAM on PN532");
    this->mark_failed();
    return;
  }

  // Ensure RF field starts off to avoid WiFi interference
  if (this->rf_field_off_when_idle_) {
    this->turn_off_rf_();
  }

  // Initial health check
  if (this->health_check_enabled_) {
    this->last_health_check_ = millis();
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
    ESP_LOGCONFIG(TAG, "  Health Check: enabled, interval=%dms, max_failures=%d, auto_reset=%s",
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
  if (now - this->last_health_check_ < this->health_check_interval_)
    return;

  this->last_health_check_ = now;

  if (!this->perform_health_check_()) {
    this->consecutive_failures_++;
    ESP_LOGW(TAG, "Health check failed (%d/%d)", this->consecutive_failures_, this->max_failed_checks_);

    if (this->consecutive_failures_ >= this->max_failed_checks_) {
      if (this->is_healthy_) {
        this->is_healthy_ = false;
        this->status_set_error("PN532 health check failed");
        ESP_LOGE(TAG, "PN532 declared unhealthy after %d consecutive failures", this->consecutive_failures_);
      }

      if (this->auto_reset_on_failure_) {
        ESP_LOGW(TAG, "Attempting automatic reset...");
        // Attempt re-initialization
        delay(50);
        if (this->get_firmware_version_() && this->setup_sam_()) {
          if (this->rf_field_off_when_idle_)
            this->turn_off_rf_();
          ESP_LOGI(TAG, "PN532 reset successful, resuming normal operation");
          this->consecutive_failures_ = 0;
          this->is_healthy_ = true;
          this->status_clear_error();
          this->retries_ = 0;
          this->throttle_ = 0;
          this->updates_enabled_ = true;
        } else {
          ESP_LOGE(TAG, "PN532 reset failed");
        }
      }
    }
  } else {
    if (this->consecutive_failures_ > 0) {
      ESP_LOGI(TAG, "Health check recovered after %d failures", this->consecutive_failures_);
    }
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
  if (!this->updates_enabled_) {
#ifdef USE_NFC
    // In NDEF write mode — handled differently, skip polling
#endif
    return;
  }

  // Skip if declared unhealthy and health checks are on
  if (this->health_check_enabled_ && !this->is_healthy_) {
    ESP_LOGD(TAG, "Skipping scan - PN532 unhealthy");
    return;
  }

  // Throttle backoff after bus failures (fixes blocking-operation warning)
  if (this->throttle_ > 0) {
    uint32_t now = millis();
    if (now - this->last_update_ < this->throttle_) {
      return;
    }
  }
  this->last_update_ = millis();

  // Mark all binary sensors as unseen at start of scan cycle
  for (auto *sensor : this->binary_sensors_) {
    sensor->on_scan_end();
  }

  // Send the passive target inventory command
  if (!this->write_command_({PN532_COMMAND_INLISTPASSIVETARGET,
                              0x01,   // max 1 card
                              0x00})) {  // baud rate: ISO14443A 106 kbit/s
    ESP_LOGW(TAG, "Requesting tag read failed. Throttling updates.");
    this->status_set_warning();

    // Exponential backoff: 5s → 10s → 60s (always at least the configured update_interval)
    uint32_t ui = this->update_interval_;
    switch (this->retries_) {
      case 0:
        this->retries_++;
        this->throttle_ = std::max((uint32_t)5000u, ui);
        break;
      case 1:
        this->retries_++;
        this->throttle_ = std::max((uint32_t)10000u, ui);
        break;
      default:
        this->throttle_ = std::max((uint32_t)60000u, ui);
    }
    return;
  }

  // Command sent successfully — clear any prior warning and reset backoff
  this->status_clear_warning();
  this->retries_ = 0;
  this->throttle_ = 0;
  this->requested_read_ = true;

  // Read the response (with readiness polling)
  std::vector<uint8_t> uid;
  if (this->read_tag_(uid)) {
    this->process_tag_(uid);
  } else {
    // No tag found; if one was previously present, fire tag_removed
    if (this->tag_present_) {
      this->tag_removed_();
    }
  }

  // If RF field should be off between polls, disable it now
  if (this->rf_field_off_when_idle_) {
    ESP_LOGV(TAG, "Turning RF field OFF");
    this->turn_off_rf_();
  }
}

// ─── Tag processing ───────────────────────────────────────────────────────────

void PN532::process_tag_(const std::vector<uint8_t> &uid) {
  // BUG FIX for #9875: detect same-tag re-read properly
  // The native code called turn_off_rf_() when it saw the same UID, causing
  // the tag to disappear and reappear on the next poll. We now just return
  // silently when it's the same tag still present.
  if (uid == this->current_uid_ && this->tag_present_) {
    // Same tag still present — nothing to do except refresh binary sensor state
    for (auto *sensor : this->binary_sensors_) {
      sensor->process(uid);
    }
    return;
  }

  // New tag (or first detection)
  this->current_uid_ = uid;
  this->tag_present_ = true;

  // Format UID string (hyphen-separated hex, e.g. "74-10-37-94")
  std::string uid_str;
  for (size_t i = 0; i < uid.size(); i++) {
    if (i > 0)
      uid_str += '-';
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uid[i]);
    uid_str += buf;
  }

  ESP_LOGD(TAG, "Found new tag '%s'", uid_str.c_str());

  // Update binary sensors
  for (auto *sensor : this->binary_sensors_) {
    sensor->process(uid);
  }

#ifdef USE_NFC
  // Read full NDEF tag data
  nfc::NfcTag tag(uid);
  // (Full NDEF reading is done by the derived class if it overrides read_tag_nfc_)
  this->on_tag_callbacks_.call(uid_str, tag);
#else
  this->on_tag_callbacks_.call(uid_str);
#endif
}

void PN532::tag_removed_() {
  // Format the removed UID for the trigger argument
  std::string uid_str;
  for (size_t i = 0; i < this->current_uid_.size(); i++) {
    if (i > 0)
      uid_str += '-';
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", this->current_uid_[i]);
    uid_str += buf;
  }

  ESP_LOGD(TAG, "Tag removed: '%s'", uid_str.c_str());

  this->on_tag_removed_callbacks_.call(uid_str);

  // Turn off all binary sensors for previously present tag
  for (auto *sensor : this->binary_sensors_) {
    if (sensor->state) {
      sensor->publish_state(false);
    }
  }

  this->tag_present_ = false;
  this->current_uid_.clear();
}

// ─── Low-level: read tag UID ─────────────────────────────────────────────────

bool PN532::read_tag_(std::vector<uint8_t> &uid) {
  // Wait for PN532 to be ready (with timeout)
  uint32_t start = millis();
  while (!this->is_read_ready()) {
    if (millis() - start > 1000) {
      ESP_LOGV(TAG, "Timed out waiting for readiness from PN532!");
      // Send ACK/abort to reset the command state on the PN532
      this->send_ack_();
      return false;
    }
    yield();
  }

  std::vector<uint8_t> response;
  if (!this->read_response(PN532_COMMAND_INLISTPASSIVETARGET, response)) {
    return false;
  }

  // Response format: [num_targets, target_num, ATQA(2), SAK(1), nfcid_len, nfcid...]
  if (response.empty() || response[0] == 0x00) {
    // No target found
    return false;
  }

  if (response.size() < 6) {
    ESP_LOGW(TAG, "Malformed InListPassiveTarget response (length %d)", response.size());
    return false;
  }

  uint8_t nfcid_len = response[5];
  if (response.size() < (size_t)(6 + nfcid_len)) {
    ESP_LOGW(TAG, "NFCID length %d exceeds response buffer", nfcid_len);
    return false;
  }

  uid.clear();
  for (uint8_t i = 0; i < nfcid_len; i++) {
    uid.push_back(response[6 + i]);
  }

  return true;
}

// ─── Low-level: RF field off ──────────────────────────────────────────────────

void PN532::turn_off_rf_() {
  // RFConfiguration: ConfigItem 0x01 (RF Field), AutoRFCA=0, RF=0
  this->write_command_({PN532_COMMAND_RFCONFIGURATION, PN532_RF_FIELD_ITEM, 0x00});
}

// ─── Low-level: SAM configuration ────────────────────────────────────────────

bool PN532::setup_sam_() {
  // SAM mode: Normal (0x01), timeout: 1000ms/50 = 20, use IRQ: 0
  if (!this->write_command_({PN532_COMMAND_SAMCONFIGURATION, 0x01, 0x14, 0x01})) {
    ESP_LOGE(TAG, "SAM configuration failed (write)");
    return false;
  }

  std::vector<uint8_t> response;
  if (!this->read_response(PN532_COMMAND_SAMCONFIGURATION, response)) {
    ESP_LOGE(TAG, "SAM configuration failed (read)");
    return false;
  }

  return true;
}

// ─── Low-level: Get firmware version ─────────────────────────────────────────

bool PN532::get_firmware_version_() {
  if (!this->write_command_({PN532_COMMAND_GETFIRMWAREVERSION})) {
    ESP_LOGW(TAG, "GetFirmwareVersion command failed");
    return false;
  }

  std::vector<uint8_t> response;
  if (!this->read_response(PN532_COMMAND_GETFIRMWAREVERSION, response)) {
    ESP_LOGW(TAG, "GetFirmwareVersion response failed");
    return false;
  }

  if (response.size() < 4) {
    ESP_LOGW(TAG, "GetFirmwareVersion response too short (%d bytes)", response.size());
    return false;
  }

  ESP_LOGCONFIG(TAG, "  IC: 0x%02X, Ver: %d.%d, Support: 0x%02X",
                response[0], response[1], response[2], response[3]);
  return true;
}

// ─── Low-level: Write command frame ──────────────────────────────────────────

bool PN532::write_command_(const std::vector<uint8_t> &data) {
  // Build TFI + data
  uint8_t tfi = PN532_HOSTTOPN532;
  uint8_t len = data.size() + 1;  // +1 for TFI byte
  uint8_t lcs = (~len + 1) & 0xFF;

  // Checksum over TFI + data
  uint8_t sum = tfi;
  for (auto b : data)
    sum += b;
  uint8_t dcs = (~sum + 1) & 0xFF;

  std::vector<uint8_t> frame;
  frame.push_back(PN532_PREAMBLE);
  frame.push_back(PN532_STARTCODE1);
  frame.push_back(PN532_STARTCODE2);
  frame.push_back(len);
  frame.push_back(lcs);
  frame.push_back(tfi);
  for (auto b : data)
    frame.push_back(b);
  frame.push_back(dcs);
  frame.push_back(PN532_POSTAMBLE);

  if (!this->write_data(frame)) {
    return false;
  }

  return this->read_ack_();
}

// ─── Low-level: Read ACK ──────────────────────────────────────────────────────

bool PN532::read_ack_() {
  ESP_LOGV(TAG, "Reading ACK");

  std::vector<uint8_t> ack_data;
  if (!this->read_data(ack_data, 6)) {
    ESP_LOGW(TAG, "Failed to read ACK");
    return false;
  }

  bool valid = (ack_data[0] == PN532_ACK[0] &&
                ack_data[1] == PN532_ACK[1] &&
                ack_data[2] == PN532_ACK[2] &&
                ack_data[3] == PN532_ACK[3] &&
                ack_data[4] == PN532_ACK[4] &&
                ack_data[5] == PN532_ACK[5]);

  ESP_LOGV(TAG, "ACK valid: %s", valid ? "YES" : "NO");
  return valid;
}

// ─── Low-level: Send ACK (abort) ─────────────────────────────────────────────

bool PN532::send_ack_() {
  ESP_LOGV(TAG, "Sending ACK for abort");
  std::vector<uint8_t> ack(PN532_ACK, PN532_ACK + sizeof(PN532_ACK));
  return this->write_data(ack);
}

// ─── Health check ─────────────────────────────────────────────────────────────

bool PN532::perform_health_check_() {
  // Re-issue firmware version check to verify communication is alive
  if (!this->write_command_({PN532_COMMAND_GETFIRMWAREVERSION})) {
    ESP_LOGD(TAG, "Health check: GetFirmwareVersion write failed");
    return false;
  }

  std::vector<uint8_t> response;
  if (!this->read_response(PN532_COMMAND_GETFIRMWAREVERSION, response)) {
    ESP_LOGD(TAG, "Health check: GetFirmwareVersion read failed");
    return false;
  }

  if (response.size() < 4) {
    ESP_LOGD(TAG, "Health check: Response too short");
    return false;
  }

  ESP_LOGV(TAG, "Health check passed");
  return true;
}

}  // namespace pn532
}  // namespace esphome
