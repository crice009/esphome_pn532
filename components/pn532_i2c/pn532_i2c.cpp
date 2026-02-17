#include "pn532_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532_i2c {

static const char *const TAG = "pn532_i2c";

// PN532 I2C ready byte (first byte of every I2C response)
static const uint8_t PN532_I2C_READY = 0x01;

void PN532I2C::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532 over I2C...");
  // Delegate to base class setup
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

// Check if PN532 has data ready by reading a single byte.
// I2C: PN532 will return 0x01 when ready, 0x00 when not.
bool PN532I2C::is_read_ready() {
  uint8_t ready_byte;
  // BUG FIX for esphome/issues#4745:
  // Use a short I2C read with no blocking. If the bus times out we return false
  // rather than blocking. The outer loop in the base class has a 1s wall-clock
  // timeout anyway so this won't loop forever.
  auto err = this->read_register(0, &ready_byte, 1, false);
  if (err != i2c::ERROR_OK) {
    return false;
  }
  return (ready_byte == PN532_I2C_READY);
}

bool PN532I2C::write_data(const std::vector<uint8_t> &data) {
  auto err = this->write(data.data(), data.size());
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C write failed (error %d)", err);
    return false;
  }
  return true;
}

bool PN532I2C::read_data(std::vector<uint8_t> &data, uint8_t len) {
  // PN532 I2C prepends a 1-byte ready indicator before the actual data
  uint8_t total = len + 1;
  std::vector<uint8_t> buf(total);

  auto err = this->read(buf.data(), total);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C read failed (error %d)", err);
    return false;
  }

  // First byte is the ready indicator — discard it
  data.assign(buf.begin() + 1, buf.end());
  return true;
}

bool PN532I2C::read_response(uint8_t command, std::vector<uint8_t> &data) {
  // Read the response header: [ready] [0x00] [0x00] [0xFF] [LEN] [LCS] [TFI=0xD5] [CMD+1]
  // = 8 bytes total (1 ready + 7 header)
  std::vector<uint8_t> header;
  if (!this->read_data(header, 7)) {
    return false;
  }

  // Validate frame structure
  if (header[0] != 0x00 || header[1] != 0x00 || header[2] != 0xFF) {
    ESP_LOGW(TAG, "I2C: Invalid frame preamble (got %02X %02X %02X)",
             header[0], header[1], header[2]);
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
    ESP_LOGW(TAG, "I2C: Response command mismatch: 0x%02X vs 0x%02X",
             header[6], command + 1);
    return false;
  }

  uint8_t payload_len = len - 2;
  if (payload_len == 0) {
    data.clear();
    return true;
  }

  // Read remaining payload + DCS + postamble
  std::vector<uint8_t> payload_raw;
  if (!this->read_data(payload_raw, payload_len + 2)) {
    return false;
  }

  // Verify DCS
  uint8_t checksum = 0xD5 + (command + 1);
  for (uint8_t i = 0; i < payload_len; i++) {
    checksum += payload_raw[i];
  }
  checksum = (~checksum + 1) & 0xFF;

  if (checksum != payload_raw[payload_len]) {
    ESP_LOGW(TAG, "I2C: Response DCS mismatch: expected 0x%02X, got 0x%02X",
             checksum, payload_raw[payload_len]);
    return false;
  }

  data.assign(payload_raw.begin(), payload_raw.begin() + payload_len);
  return true;
}

}  // namespace pn532_i2c
}  // namespace esphome
