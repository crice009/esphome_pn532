"""PN532 binary sensor platform for ESPHome.

ESPHome's platform loader resolves `binary_sensor: - platform: pn532` to
this file. It MUST be named binary_sensor.py, not handled in __init__.py.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, CONF_UID

from . import (
    PN532,
    PN532BinarySensor,
    CONF_PN532_ID,
)

# pn532_spi and pn532_i2c both AUTO_LOAD pn532, so this dependency is met
# regardless of which transport the user configured.
DEPENDENCIES = ["pn532"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(PN532BinarySensor).extend(
    {
        cv.GenerateID(): cv.declare_id(PN532BinarySensor),
        # Link to the hub (pn532_spi or pn532_i2c instance).
        # ESPHome's use_id resolves subclasses of PN532 correctly.
        cv.GenerateID(CONF_PN532_ID): cv.use_id(PN532),
        # Accept "74-10-37-94" (native) or "74:10:37:94" (colon-separated)
        cv.Required(CONF_UID): cv.All(
            cv.string,
            lambda s: s.upper().replace(":", "-"),
        ),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)

    parent = await cg.get_variable(config[CONF_PN532_ID])
    cg.add(parent.register_tag_sensor(var))

    uid_str: str = config[CONF_UID]
    # Parse "74-10-37-94" → vector<uint8_t>{0x74, 0x10, 0x37, 0x94}
    uid_parts = uid_str.split("-")
    uid_bytes = [cg.uint8(int(p, 16)) for p in uid_parts]
    cg.add(var.set_uid(uid_bytes))
