"""PN532 binary sensor platform for ESPHome.

ESPHome resolves `binary_sensor: - platform: pn532` to this file.
It MUST be named binary_sensor.py (not handled in __init__.py).
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, CONF_UID

from . import pn532_ns, PN532, CONF_PN532_ID

DEPENDENCIES = ["pn532"]

# Declare PN532BinarySensor with its proper base class here,
# where binary_sensor is safely imported without circular dependency risk.
PN532BinarySensor = pn532_ns.class_(
    "PN532BinarySensor", binary_sensor.BinarySensor
)


def validate_uid(value):
    """Accept 'xx-xx-xx-xx' or 'xx:xx:xx:xx' format UIDs."""
    value = cv.string(value).upper().replace(":", "-")
    parts = value.split("-")
    if not parts:
        raise cv.Invalid("UID must not be empty")
    for part in parts:
        if len(part) != 2:
            raise cv.Invalid(
                f"UID part '{part}' must be exactly 2 hex digits"
            )
        try:
            int(part, 16)
        except ValueError as e:
            raise cv.Invalid(
                f"UID part '{part}' is not valid hexadecimal"
            ) from e
    return value


CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(PN532BinarySensor).extend(
    {
        cv.GenerateID(): cv.declare_id(PN532BinarySensor),
        cv.GenerateID(CONF_PN532_ID): cv.use_id(PN532),
        cv.Required(CONF_UID): validate_uid,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)

    parent = await cg.get_variable(config[CONF_PN532_ID])
    cg.add(parent.register_tag_sensor(var))

    uid_str: str = config[CONF_UID]
    # Parse "74-10-37-94" → vector<uint8_t>{0x74, 0x10, 0x37, 0x94}
    uid_bytes = [cg.uint8(int(p, 16)) for p in uid_str.split("-")]
    cg.add(var.set_uid(uid_bytes))
