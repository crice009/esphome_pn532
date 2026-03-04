# AGENTS.md — Contributor & AI Agent Guide

This file documents how to work in this repository, intended for both human contributors and AI coding agents.

---

## Project Overview

This is an **enhanced external ESPHome component** for the PN532 NFC/RFID reader. It provides drop-in replacements for the native `pn532`, `pn532_spi`, and `pn532_i2c` ESPHome components with critical bug fixes (tag flapping, I2C freeze, blocking-operation warnings) and new reliability features (health check, exponential backoff, RF field control).

**Component locations:**
- `components/pn532/` — shared base (C++ logic, Python config schema, binary sensor)
- `components/pn532_spi/` — SPI transport variant
- `components/pn532_i2c/` — I2C transport variant

---

## Repository Structure

```
components/          ESPHome external component source
  pn532/             Base component (pn532.h/cpp, binary_sensor.py, NDEF helpers)
  pn532_spi/         SPI hub
  pn532_i2c/         I2C hub
.github/workflows/
  compile.yml        Compiles ci-test-spi.yaml and ci-test-i2c.yaml via ESPHome Docker
  lint.yml           Runs pre-commit (ruff, clang-format, yamllint, whitespace)
ci-test-spi.yaml     CI firmware config for SPI
ci-test-i2c.yaml     CI firmware config for I2C
local-test.yaml      Local dev firmware config (not used in CI)
pyproject.toml       Ruff / pylint / pytest config
.pre-commit-config.yaml  Pre-commit hooks definition
```

---

## Development Conventions

### C++ (component code)
- Follow the [ESPHome C++ style guide](https://esphome.io/guides/contributing.html).
- Formatting is enforced by **clang-format** (`.clang-format` at repo root). Run before committing.
- Static analysis config is in `.clang-tidy`.
- Use `esphome::` namespace conventions; all component classes live in `namespace esphome { namespace pn532 { ... } }`.
- Do not use `delay()` — all polling must be non-blocking (use `millis()`-based state machines inside `loop()`).
- Prefer `ESP_LOGD` / `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE` for logging.

### Python (ESPHome config schema)
- Schema files are `__init__.py` and component `*.py` files under `components/`.
- Follow **PEP 8** style; formatting and import order are enforced by **ruff**.
- Target Python 3.11+.

### YAML (firmware configs)
- **yamllint** enforces YAML style (see `.pre-commit-config.yaml` for exclusions).
- `secrets.yaml` is excluded from linting.

---

## Linting

All linting is run via **pre-commit**:

```bash
pip install pre-commit
pre-commit run --all-files
```

Individual hooks:

| Hook | What it checks |
|---|---|
| `ruff` | Python style, imports, formatting |
| `clang-format` | C/C++ formatting |
| `yamllint` | YAML validity and style |
| `end-of-file-fixer` | Files must end with a newline |
| `trailing-whitespace` | No trailing spaces |

---

## Building & Testing

### Compile check (CI method, requires Docker)

```bash
docker run --rm \
  -v "$PWD":/config \
  ghcr.io/esphome/esphome:dev \
  compile ci-test-spi.yaml

docker run --rm \
  -v "$PWD":/config \
  ghcr.io/esphome/esphome:dev \
  compile ci-test-i2c.yaml
```

Both configs must compile without errors before merging.

### Local ESPHome compile (if ESPHome is installed locally)

```bash
esphome compile ci-test-spi.yaml
esphome compile ci-test-i2c.yaml
```

### Hardware testing

See [`TESTING.md`](TESTING.md) for the full hardware validation procedure. Key scenarios:
- Dual-bus operation (I2C + SPI simultaneously)
- Tag flapping prevention (leave tag on reader for 60 s — `on_tag` fires once)
- Health check / auto-reset under bus fault injection
- Multi-tag detection (two tags on one reader)

---

## CI

Two GitHub Actions workflows run on pushes and PRs to `main`/`dev`:

| Workflow | Trigger | What it does |
|---|---|---|
| `compile.yml` | push/PR to `main`, `dev` | Compiles both SPI and I2C CI configs with ESPHome Docker |
| `lint.yml` | push/PR | Runs `pre-commit --all-files` |

Both must pass before a PR can be merged.

---

## Making Changes

### Adding a new feature or bug fix
1. Edit C++ sources under `components/pn532/` (and/or `pn532_spi/`, `pn532_i2c/`).
2. Update the Python schema (`__init__.py`) if new YAML config options are added.
3. Update `README.md` documentation for any new config variables.
4. If behaviour changes, update `TESTING.md` test cases accordingly.
5. Run `pre-commit run --all-files` and ensure it passes.
6. Compile both CI YAML configs and confirm zero errors.

### Changing Python schema
- Config options are validated in the `CONFIG_SCHEMA` / `to_code` functions in the component's `__init__.py`.
- New options should have sensible defaults so existing YAML configs continue to work unchanged.

### Modifying CI configs (`ci-test-*.yaml`)
- These are minimal firmware configs used only to verify the component compiles.
- Do not add secrets or real pin assignments that break in a headless CI environment.
- Real pin assignments live in `local-test.yaml` (not CI-tested).

---

## Known Limitations / Open Issues

- **NTAG216 write stability** — NDEF writes time out on some NTAG216 modules.
- **Mifare Classic authentication** — intermittent failures with non-default keys.
- **Counterfeit module detection** — automated silicon-level detection not yet implemented.
- **Multi-type polling** — ISO14443B and FeliCa not yet supported.

See the TODO section in [`README.md`](README.md) for the full list.

---

## Useful References

- [ESPHome external components guide](https://esphome.io/components/external_components.html)
- [ESPHome contributing guide](https://esphome.io/guides/contributing.html)
- [PN532 User Manual (NXP)](https://www.nxp.com/docs/en/user-guide/141520.pdf)
- [ESPHome issue #9875](https://github.com/esphome/esphome/issues/9875) — tag flapping
- [ESPHome issue #3281](https://github.com/esphome/issues/issues/3281) — I2C freeze
