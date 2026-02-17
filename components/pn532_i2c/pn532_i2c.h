#pragma once

#include "../pn532/pn532.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace pn532_i2c {

static const uint8_t PN532_I2C_DEFAULT_ADDRESS = 0x24;

class PN532I2C : public pn532::PN532, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  bool write_data(const std::vector<uint8_t> &data) override;
  bool read_data(std::vector<uint8_t> &data, uint8_t len) override;
  bool read_response(uint8_t command, std::vector<uint8_t> &data) override;
  bool is_read_ready() override;
};

}  // namespace pn532_i2c
}  // namespace esphome
