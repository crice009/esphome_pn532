"""PN532 NFC/RFID component base for ESPHome.

This module serves two roles:
  1. Shared base — PN532_SCHEMA and setup_pn532_core_() are imported by
     pn532_spi and pn532_i2c to build their hub components.
  2. Binary sensor platform — CONFIG_SCHEMA / to_code() are used when the
     user writes `binary_sensor: - platform: pn532 ...`
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    CONF_TRIGGER_ID,
    CONF_UID,
)
import logging

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@your-github-handle"]
# pn532_spi and pn532_i2c are loaded on demand by the user
AUTO_LOAD = ["binary_sensor"]

pn532_ns = cg.esphome_ns.namespace("pn532")

PN532 = pn532_ns.class_("PN532", cg.PollingComponent)

PN532BinarySensor = pn532_ns.class_(
    "PN532BinarySensor", binary_sensor.BinarySensor
)
PN532Trigger = pn532_ns.class_(
    "PN532Trigger", automation.Trigger.template(cg.std_string)
)
PN532TagRemovedTrigger = pn532_ns.class_(
    "PN532TagRemovedTrigger", automation.Trigger.template(cg.std_string)
)
PN532IsWritingCondition = pn532_ns.class_(
    "PN532IsWritingCondition", automation.Condition
)

# Config keys
CONF_ON_TAG = "on_tag"
CONF_ON_TAG_REMOVED = "on_tag_removed"
CONF_PN532_ID = "pn532_id"
CONF_HEALTH_CHECK_ENABLED = "health_check_enabled"
CONF_HEALTH_CHECK_INTERVAL = "health_check_interval"
CONF_AUTO_RESET_ON_FAILURE = "auto_reset_on_failure"
CONF_MAX_FAILED_CHECKS = "max_failed_checks"
CONF_RF_FIELD_ENABLED = "rf_field_enabled"

DEFAULT_UPDATE_INTERVAL = "1s"
DEFAULT_HEALTH_CHECK_INTERVAL = "60s"
DEFAULT_MAX_FAILED_CHECKS = 3

# Shared base schema — extended by pn532_spi and pn532_i2c
PN532_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ON_TAG): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PN532Trigger)}
        ),
        cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PN532TagRemovedTrigger)}
        ),
        cv.Optional(CONF_HEALTH_CHECK_ENABLED, default=True): cv.boolean,
        cv.Optional(
            CONF_HEALTH_CHECK_INTERVAL, default=DEFAULT_HEALTH_CHECK_INTERVAL
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_AUTO_RESET_ON_FAILURE, default=True): cv.boolean,
        cv.Optional(
            CONF_MAX_FAILED_CHECKS, default=DEFAULT_MAX_FAILED_CHECKS
        ): cv.int_range(min=1, max=10),
        # RF field: disabled between polls by default to reduce WiFi interference
        cv.Optional(CONF_RF_FIELD_ENABLED, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema(DEFAULT_UPDATE_INTERVAL))


async def setup_pn532_core_(var, config):
    """Register automation triggers and health check settings for any PN532 variant."""

    cg.add(var.set_health_check_enabled(config[CONF_HEALTH_CHECK_ENABLED]))
    cg.add(
        var.set_health_check_interval(config[CONF_HEALTH_CHECK_INTERVAL])
    )
    cg.add(var.set_auto_reset_on_failure(config[CONF_AUTO_RESET_ON_FAILURE]))
    cg.add(var.set_max_failed_checks(config[CONF_MAX_FAILED_CHECKS]))
    cg.add(var.set_rf_field_enabled(config[CONF_RF_FIELD_ENABLED]))

    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)


# ─── Binary Sensor platform schema ───────────────────────────────────────────

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(PN532BinarySensor).extend(
    {
        cv.GenerateID(): cv.declare_id(PN532BinarySensor),
        cv.GenerateID(CONF_PN532_ID): cv.use_id(PN532),
        cv.Required(CONF_UID): cv.templatable(
            cv.All(
                cv.string,
                # Accept both "74-10-37-94" (native format) and "74:10:37:94"
                lambda s: s.upper().replace(":", "-"),
            )
        ),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)

    parent = await cg.get_variable(config[CONF_PN532_ID])
    cg.add(parent.register_tag_sensor(var))

    uid_str: str = config[CONF_UID]
    # Parse "74-10-37-94" → [0x74, 0x10, 0x37, 0x94]
    uid_parts = uid_str.split("-")
    uid_bytes = [cg.uint8(int(p, 16)) for p in uid_parts]
    cg.add(var.set_uid(uid_bytes))
