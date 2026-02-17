#include "pn532_spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532_spi {

static const char *const TAG = "pn532_spi";

// PN532 SPI command bytes
static const uint8_t PN532_SPI_STATREAD = 0x02;
static const uint8_t PN532_SPI_DATAWRITE = 0x01;
static const uint8_t PN532_SPI_DATAREAD = 0x03;
static const uint8_t PN532_SPI_READY = 0x01;

void PN532Spi::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532 over SPI...");
  this->spi_setup();
  // Delegate to base class setup (which calls pn532_setup_() via virtual)
  pn532::PN532::setup();
}

void PN532Spi::dump_config() {
  ESP_LOGCONFIG(TAG, "PN532 (SPI):");
  LOG_PIN("  CS Pin: ", this->cs_);
  pn532::PN532::dump_config();
}

void PN532Spi::pn532_setup_() {
  // The PN532 SPI needs a brief startup time
  delay(10);
}

bool PN532Spi::is_read_ready() {
  this->enable();
  this->write_byte(PN532_SPI_STATREAD);
  bool ready = (this->read_byte() == PN532_SPI_READY);
  this->disable();
  return ready;
}

bool PN532Spi::write_data(const std::vector<uint8_t> &data) {
  this->enable();
  delay(2);
  this->write_byte(PN532_SPI_DATAWRITE);
  for (auto b : data) {
    this->write_byte(b);
  }
  this->disable();

  ESP_LOGV(TAG, "Wrote %d bytes", data.size());
  return true;
}

bool PN532Spi::read_data(std::vector<uint8_t> &data, uint8_t len) {
  // Wait until ready
  uint32_t start = millis();
  while (!this->is_read_ready()) {
    if (millis() - start > 1000) {
      ESP_LOGW(TAG, "Timed out waiting for SPI read ready");
      return false;
    }
    delay(1);
  }

  this->enable();
  delay(2);
  this->write_byte(PN532_SPI_DATAREAD);
  data.resize(len);
  for (uint8_t i = 0; i < len; i++) {
    data[i] = this->read_byte();
  }
  this->disable();

  ESP_LOGV(TAG, "Read data (%d bytes)", len);
  return true;
}

bool PN532Spi::read_response(uint8_t command, std::vector<uint8_t> &data) {
  // Read the response header to find length
  std::vector<uint8_t> header;
  if (!this->read_data(header, 7)) {
    return false;
  }

  ESP_LOGV(TAG, "Header data: %02X.%02X.%02X.%02X.%02X.%02X.%02X",
           header[0], header[1], header[2], header[3],
           header[4], header[5], header[6]);

  // Validate frame structure: 0x00 0x00 0xFF [LEN] [LCS] [TFI=0xD5] [CMD+1]
  if (header[0] != 0x00 || header[1] != 0x00 || header[2] != 0xFF) {
    ESP_LOGW(TAG, "Invalid frame preamble");
    return false;
  }

  uint8_t len = header[3];
  uint8_t lcs = header[4];

  if ((uint8_t)(len + lcs) != 0x00) {
    ESP_LOGW(TAG, "LCS checksum mismatch: len=0x%02X lcs=0x%02X", len, lcs);
    return false;
  }

  if (header[5] != 0xD5) {
    ESP_LOGW(TAG, "Unexpected TFI: 0x%02X (expected 0xD5)", header[5]);
    return false;
  }

  if (header[6] != (command + 1)) {
    ESP_LOGW(TAG, "Response command mismatch: got 0x%02X, expected 0x%02X",
             header[6], command + 1);
    return false;
  }

  // Read the payload (len - 2 because TFI and CMD already consumed)
  uint8_t payload_len = len - 2;
  if (payload_len == 0) {
    data.clear();
    return true;
  }

  ESP_LOGV(TAG, "Reading response of length %d", payload_len);

  // Read payload + DCS + postamble (payload_len + 2 bytes)
  std::vector<uint8_t> payload_raw;
  if (!this->read_data(payload_raw, payload_len + 2)) {
    return false;
  }

  ESP_LOGV(TAG, "Response data (%d bytes)", payload_len);

  // Verify DCS
  uint8_t checksum = 0xD5 + (command + 1);
  for (uint8_t i = 0; i < payload_len; i++) {
    checksum += payload_raw[i];
  }
  checksum = (~checksum + 1) & 0xFF;

  if (checksum != payload_raw[payload_len]) {
    ESP_LOGW(TAG, "Response DCS mismatch: computed 0x%02X, got 0x%02X",
             checksum, payload_raw[payload_len]);
    return false;
  }

  data.assign(payload_raw.begin(), payload_raw.begin() + payload_len);
  return true;
}

}  // namespace pn532_spi
}  // namespace esphome
