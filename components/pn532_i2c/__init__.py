"""PN532 NFC/RFID over I2C for ESPHome."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

from ..pn532 import PN532, PN532_SCHEMA, setup_pn532_core_

CODEOWNERS = ["@your-github-handle"]
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["pn532"]

pn532_i2c_ns = cg.esphome_ns.namespace("pn532_i2c")
PN532I2C = pn532_i2c_ns.class_("PN532I2C", PN532, i2c.I2CDevice)

CONFIG_SCHEMA = (
    PN532_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(PN532I2C),
        }
    )
    .extend(i2c.i2c_device_schema(0x24))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await setup_pn532_core_(var, config)
