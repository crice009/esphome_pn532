"""PN532 NFC/RFID component base for ESPHome.

Exports:
  - Shared C++ class declarations (PN532, PN532BinarySensor, triggers)
  - PN532_SCHEMA: base hub config schema extended by pn532_spi / pn532_i2c
  - setup_pn532_core_(): async helper called by both transport to_code()

The binary sensor platform lives in binary_sensor.py.
ESPHome resolves `binary_sensor: - platform: pn532` to that file.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import binary_sensor
from esphome.const import (
    CONF_TRIGGER_ID,
    CONF_RST_PIN,
)

CODEOWNERS = ["@your-github-handle"]
AUTO_LOAD = ["binary_sensor"]

pn532_ns = cg.esphome_ns.namespace("pn532")

# ── C++ class declarations ────────────────────────────────────────────────────

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

# ── Shared config keys ────────────────────────────────────────────────────────

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

# ── Shared hub schema (extended by pn532_spi / pn532_i2c) ────────────────────

PN532_SCHEMA = cv.Schema(
    {
        # Optional hardware reset pin — connects to RSTPD_N on the PN532 board
        # (fixes intermittent IC hardware failure bug, see esphome/#10968)
        cv.Optional(CONF_RST_PIN): pins.gpio_output_pin_schema,
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
        # RF field off between polls by default — reduces WiFi interference
        cv.Optional(CONF_RF_FIELD_ENABLED, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema(DEFAULT_UPDATE_INTERVAL))


# ── Shared hub code-gen helper ────────────────────────────────────────────────

async def setup_pn532_core_(var, config):
    """Wire up automation triggers, pins, and health-check settings."""
    if CONF_RST_PIN in config:
        rst = await cg.gpio_pin_expression(config[CONF_RST_PIN])
        cg.add(var.set_rst_pin(rst))

    cg.add(var.set_health_check_enabled(config[CONF_HEALTH_CHECK_ENABLED]))
    cg.add(var.set_health_check_interval(config[CONF_HEALTH_CHECK_INTERVAL]))
    cg.add(var.set_auto_reset_on_failure(config[CONF_AUTO_RESET_ON_FAILURE]))
    cg.add(var.set_max_failed_checks(config[CONF_MAX_FAILED_CHECKS]))
    cg.add(var.set_rf_field_enabled(config[CONF_RF_FIELD_ENABLED]))

    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
