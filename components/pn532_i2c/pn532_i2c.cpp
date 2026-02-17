#include "pn532_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532_i2c {

static const char *const TAG = "pn532_i2c";

// The PN532 prepends a 1-byte ready indicator to every I2C read
static const uint8_t PN532_I2C_READY = 0x01;

// ─── setup / dump_config ─────────────────────────────────────────────────────

void PN532I2C::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532 (I2C)...");
  pn532::PN532::setup();
}

void PN532I2C::dump_config() {
  ESP_LOGCONFIG(TAG, "PN532 (I2C):");
  LOG_I2C_DEVICE(this);
  pn532::PN532::dump_config();
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with PN532 failed!");
  }
}

// ─── is_read_ready ───────────────────────────────────────────────────────────
//
// Fix for esphome/issues#4382:
// The native implementation busy-waited in a while(true) loop until the PN532
// returned 0x01, blocking the main loop for up to 100ms on every poll.
// This implementation does a single non-blocking check — the outer loop in the
// base class has its own 1-second wall-clock timeout.

bool PN532I2C::is_read_ready() {
  uint8_t ready_byte = 0;
  // Single-byte read; if the bus times out, i2c ERROR_OK won't be set.
  auto err = this->read_register(0, &ready_byte, 1, false);
  if (err != i2c::ERROR_OK)
    return false;
  return (ready_byte == PN532_I2C_READY);
}

// ─── write_data ──────────────────────────────────────────────────────────────

bool PN532I2C::write_data(const std::vector<uint8_t> &data) {
  auto err = this->write(data.data(), data.size());
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C write error: %d", err);
    return false;
  }
  return true;
}

// ─── read_data ───────────────────────────────────────────────────────────────

bool PN532I2C::read_data(std::vector<uint8_t> &data, uint8_t len) {
  // PN532 I2C prepends a 1-byte ready indicator — read len+1 and discard first
  std::vector<uint8_t> buf(len + 1);
  auto err = this->read(buf.data(), len + 1);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C read error: %d", err);
    return false;
  }
  // First byte is the ready indicator; actual data starts at index 1
  data.assign(buf.begin() + 1, buf.end());
  return true;
}

// ─── read_response ───────────────────────────────────────────────────────────

bool PN532I2C::read_response(uint8_t command, std::vector<uint8_t> &data) {
  // I2C frame header: [ready(1)] [0x00][0x00][0xFF][LEN][LCS][0xD5][CMD+1]
  // We call read_data which strips the ready byte, so we get 7 bytes.
  std::vector<uint8_t> header;
  if (!this->read_data(header, 7))
    return false;

  // Validate preamble
  if (header[0] != 0x00 || header[1] != 0x00 || header[2] != 0xFF) {
    ESP_LOGW(TAG, "I2C: Bad preamble: %02X %02X %02X", header[0], header[1], header[2]);
    return false;
  }

  uint8_t len = header[3];
  uint8_t lcs = header[4];
  if ((uint8_t)(len + lcs) != 0x00) {
    ESP_LOGW(TAG, "I2C: LCS mismatch: len=0x%02X lcs=0x%02X", len, lcs);
    return false;
  }
  if (header[5] != 0xD5) {
    ESP_LOGW(TAG, "I2C: Unexpected TFI: 0x%02X", header[5]);
    return false;
  }
  if (header[6] != (command + 1)) {
    ESP_LOGW(TAG, "I2C: Command mismatch: 0x%02X vs 0x%02X", header[6], command + 1);
    return false;
  }

  uint8_t payload_len = len - 2;
  if (payload_len == 0) {
    data.clear();
    return true;
  }

  std::vector<uint8_t> raw;
  if (!this->read_data(raw, payload_len + 2))
    return false;

  // Verify DCS
  uint8_t checksum = 0xD5 + (command + 1);
  for (uint8_t i = 0; i < payload_len; i++)
    checksum += raw[i];
  checksum = (~checksum + 1) & 0xFF;

  if (checksum != raw[payload_len]) {
    ESP_LOGW(TAG, "I2C: DCS mismatch: expected 0x%02X got 0x%02X", checksum, raw[payload_len]);
    return false;
  }

  data.assign(raw.begin(), raw.begin() + payload_len);
  return true;
}

}  // namespace pn532_i2c
}  // namespace esphome
