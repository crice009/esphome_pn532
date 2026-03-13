# PN532 I2C Latch-up Diagnostic Plan

Follow these steps when the PN532 enters the "flapping" state (I2C hardware timeouts followed by brief re-initialization).

## 1. Baseline Stability Test (with Ring/Tag)
*   **Action:** Place a known good NFC ring/tag on the I2C reader.
*   **Observation:** Confirm if the 'NFC Connected' binary sensor stays ON and the logs are quiet (no `ready=1` timeouts).
*   **Goal:** Determine if the 250ms polling interval is stable under normal conditions on the bench.

## 2. Forced I2C Failure (Bus Recovery Test)
*   **Action:** While the tag is present, **briefly disconnect the SDA wire** (GPIO 23) for 3 seconds, then reconnect it.
*   **Observation:**
    *   Look for `I2C hardware timeout detected`.
    *   Wait for 3 consecutive failures until `Attempting PN532 re-initialisation...` appears.
*   **Success Criteria:** The reader should log `PN532 re-initialised successfully!` and resume reading the tag **without** an ESP32 restart.

## 3. Hardware Reset Validation (RSTPD)
*   **Action:** If Step 2 results in "flapping" (re-init succeeds but communication immediately fails again), **ground the RSTPD pin** on the PN532 for 100ms.
*   **Observation:** Check if the I2C bus stabilizes immediately after the hardware reset.
*   **Goal:** If this works but Step 2 failed, we must implement `reset_pin` support in the software to perform a hard reset during the `reinit_()` cycle.

## 4. Signal Integrity Check
*   **Action:** If the above fails, lower the I2C frequency in the YAML to `10kHz`.
*   **Goal:** If `10kHz` is stable but `100kHz` is not, the issue is physical (capacitance/interference) rather than logic.
