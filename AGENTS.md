# ESP32 Gauge Pod 2 — Agent Guide

Real-time CAN bus telemetry system for a 2019 Subaru WRX STI. Two ESP32 firmware
components communicate over UART. One polls the ECU/VDC via CAN; the other drives
an LVGL display with threshold-based alerts and SD card logging.

## Repository Layout

| Path | Description |
|---|---|
| `esp-data-hub-2/` | Data hub firmware — polls ECU/VDC over CAN, reads analog sensors, emits UART/CBOR |
| `esp32-data-display-2/` | Display firmware — receives UART/CBOR, evaluates thresholds, renders LVGL UI |
| `esp32-shared/include/` | Shared packet types (`telemetry_types.h`) used by both components |
| `canhacker-usb-adapter/` | Utility firmware for USB CAN debugging |
| `scripts/subaru-decode/` | Python: decode CANHacker traces using SSM address map |
| `scripts/parse-foxwell-nt614/` | Python: parse Foxwell NT614 scan logs |

Deeper documentation lives in `docs/`:
- [`docs/architecture.md`](docs/architecture.md) — data flow and component roles
- [`docs/protocols.md`](docs/protocols.md) — CAN IDs, ISO-TP, SSM/UDS, CBOR format
- [`docs/build.md`](docs/build.md) — build, flash, monitor, and mock/fake data modes

## Build

Both components use ESP-IDF. All `idf.py` commands run from the component directory.

```sh
cd esp-data-hub-2        # or esp32-data-display-2
idf.py build
idf.py flash monitor
```

See [`docs/build.md`](docs/build.md) for full details, port flags, and mock configs.

## Key Conventions

**CBOR field order is a hard contract.** The hub encodes and the display decodes the
same ordered array. If you add, remove, or reorder fields, update both sides and
`telemetry_types.h` together. See [`docs/protocols.md`](docs/protocols.md).

**Shared types live in `esp32-shared/include/telemetry_types.h`.** Both components
reference this via their `EXTRA_COMPONENT_DIRS` or include path. Do not duplicate
struct definitions.

**Thread safety via mutex.** Both components protect shared state with
`SemaphoreHandle_t` mutexes. Always take the mutex before reading or writing
`display_state` / `bt_state` (hub) or `monitored_state_t` (display). Release
promptly — render and pipeline tasks run at different priorities.

**Mock and fake data.** Use these sdkconfig options to test without hardware:
- Hub: `CONFIG_DH_ANALOG_USE_MOCK=y` — replaces ADS1115 with stub readings
- Display: `CONFIG_DD_ENABLE_FAKE_DATA=y` — generates synthetic sine-wave packets

## Critical Files

| File | Why it matters |
|---|---|
| `esp32-shared/include/telemetry_types.h` | Defines `display_packet_t` and `bt_packet_t` — the wire format |
| `esp-data-hub-2/main/tasks/task_uart_emitter.c` | Encodes `display_packet_t` as CBOR; field order is the contract |
| `esp32-data-display-2/main/car_data.c` | Decodes CBOR back to `display_packet_t`; must match emitter exactly |
| `esp32-data-display-2/main/monitoring.c` | All alert thresholds live here |
| `esp-data-hub-2/main/data_canbus/request_ecu.c` | SSM poll payload and response parsing |
