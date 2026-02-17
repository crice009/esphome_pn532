#include "pn532_spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532_spi {

static const char *const TAG = "pn532_spi";

// PN532 SPI control bytes (LSB-first on the wire)
static const uint8_t PN532_SPI_STATREAD  = 0x02;
static const uint8_t PN532_SPI_DATAWRITE = 0x01;
static const uint8_t PN532_SPI_DATAREAD  = 0x03;
static const uint8_t PN532_SPI_READY     = 0x01;

// ─── pn532_pre_setup_ ────────────────────────────────────────────────────────

void PN532Spi::pn532_pre_setup_() {
  this->spi_setup();
  delay(10);  // PN532 SPI startup time
}

// ─── setup / dump_config ─────────────────────────────────────────────────────

void PN532Spi::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PN532 (SPI)...");
  // Calls pn532_pre_setup_() then the full PN532 init sequence
  pn532::PN532::setup();
}

void PN532Spi::dump_config() {
  ESP_LOGCONFIG(TAG, "PN532 (SPI):");
  LOG_PIN("  CS Pin: ", this->cs_);
  pn532::PN532::dump_config();
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with PN532 failed!");
  }
}

// ─── is_read_ready ───────────────────────────────────────────────────────────

bool PN532Spi::is_read_ready() {
  this->enable();
  this->write_byte(PN532_SPI_STATREAD);
  bool ready = (this->read_byte() == PN532_SPI_READY);
  this->disable();
  return ready;
}

// ─── write_data ──────────────────────────────────────────────────────────────

bool PN532Spi::write_data(const std::vector<uint8_t> &data) {
  this->enable();
  delay(2);
  this->write_byte(PN532_SPI_DATAWRITE);
  for (auto b : data)
    this->write_byte(b);
  this->disable();

  ESP_LOGV(TAG, "Writing data (%d bytes)", data.size());
  return true;
}

// ─── read_data ───────────────────────────────────────────────────────────────

bool PN532Spi::read_data(std::vector<uint8_t> &data, uint8_t len) {
  // Poll for readiness before reading
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
  for (uint8_t i = 0; i < len; i++)
    data[i] = this->read_byte();
  this->disable();

  ESP_LOGV(TAG, "Reading data (%d bytes)", len);
  return true;
}

// ─── read_response ───────────────────────────────────────────────────────────

bool PN532Spi::read_response(uint8_t command, std::vector<uint8_t> &data) {
  // Read the 7-byte frame header first
  std::vector<uint8_t> header;
  if (!this->read_data(header, 7))
    return false;

  ESP_LOGV(TAG, "Header: %02X %02X %02X %02X %02X %02X %02X",
           header[0], header[1], header[2], header[3],
           header[4], header[5], header[6]);

  // Validate: 0x00 0x00 0xFF [LEN] [LCS] [0xD5] [CMD+1]
  if (header[0] != 0x00 || header[1] != 0x00 || header[2] != 0xFF) {
    ESP_LOGW(TAG, "Invalid frame preamble: %02X %02X %02X", header[0], header[1], header[2]);
    return false;
  }
  uint8_t len = header[3];
  uint8_t lcs = header[4];
  if ((uint8_t)(len + lcs) != 0x00) {
    ESP_LOGW(TAG, "LCS mismatch: len=0x%02X lcs=0x%02X", len, lcs);
    return false;
  }
  if (header[5] != 0xD5) {
    ESP_LOGW(TAG, "Unexpected TFI: 0x%02X", header[5]);
    return false;
  }
  if (header[6] != (command + 1)) {
    ESP_LOGW(TAG, "Response command mismatch: 0x%02X vs 0x%02X", header[6], command + 1);
    return false;
  }

  uint8_t payload_len = len - 2;  // subtract TFI and CMD bytes
  if (payload_len == 0) {
    data.clear();
    return true;
  }

  // Read payload + DCS + postamble
  std::vector<uint8_t> raw;
  if (!this->read_data(raw, payload_len + 2))
    return false;

  // Verify DCS
  uint8_t checksum = 0xD5 + (command + 1);
  for (uint8_t i = 0; i < payload_len; i++)
    checksum += raw[i];
  checksum = (~checksum + 1) & 0xFF;

  if (checksum != raw[payload_len]) {
    ESP_LOGW(TAG, "DCS mismatch: expected 0x%02X got 0x%02X", checksum, raw[payload_len]);
    return false;
  }

  data.assign(raw.begin(), raw.begin() + payload_len);
  return true;
}

}  // namespace pn532_spi
}  // namespace esphome
