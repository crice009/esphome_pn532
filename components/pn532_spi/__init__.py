"""PN532 NFC/RFID over SPI for ESPHome."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.const import CONF_ID

from ..pn532 import PN532, PN532_SCHEMA, setup_pn532_core_

CODEOWNERS = ["@your-github-handle"]
DEPENDENCIES = ["spi"]
AUTO_LOAD = ["pn532"]

pn532_spi_ns = cg.esphome_ns.namespace("pn532_spi")
PN532Spi = pn532_spi_ns.class_("PN532Spi", PN532, spi.SPIDevice)

CONFIG_SCHEMA = (
    PN532_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(PN532Spi),
        }
    )
    .extend(spi.spi_device_schema())
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    await setup_pn532_core_(var, config)
