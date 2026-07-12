# ESP32 Gauge Pod 2 — Agent Guide

Real-time CAN bus telemetry system for a 2019 Subaru WRX STI. Two ESP32 firmware
components communicate over UART. One polls the ECU/VDC via CAN; the other drives
an LVGL display with threshold-based alerts and SD card logging.

## Repository Layout

| Path | Description |
|---|---|
| `esp-data-hub-2/` | Data hub firmware — polls ECU/VDC over CAN, reads analog sensors, emits UART telemetry |
| `esp32-data-display-2/` | Display firmware — receives UART telemetry, evaluates thresholds, renders LVGL UI |
| `esp32-shared/` | Shared packet types and MessagePack/CRC16/COBS protocol codec |
| `canhacker-usb-adapter/` | Utility firmware for USB CAN debugging |
| `scripts/subaru-decode/` | Python: decode CANHacker traces using SSM address map |
| `scripts/parse-foxwell-nt614/` | Python: parse Foxwell NT614 scan logs |

Deeper documentation lives in `docs/`:
- [`docs/architecture.md`](docs/architecture.md) — data flow and component roles
- [`docs/protocols.md`](docs/protocols.md) — CAN IDs, ISO-TP, SSM/UDS, UART wire format
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

**MessagePack field order is a hard contract.** The shared codec encodes and
decodes one ordered array. If you add, remove, or reorder fields, update
`telemetry_types.h`, the shared codec, schema version, size constants, tests, and
protocol documentation together. See [`docs/protocols.md`](docs/protocols.md).

**Shared types and codec live in `esp32-shared`.** Both projects load it through
`EXTRA_COMPONENT_DIRS`. Do not duplicate packet structs or serialization logic.

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
| `esp32-shared/src/telemetry_protocol.c` | Owns MessagePack, CRC16, and COBS encoding/decoding |
| `esp-data-hub-2/main/tasks/task_uart_emitter.c` | Snapshots state and emits shared-codec UART frames |
| `esp32-data-display-2/main/car_data.c` | Accumulates UART bytes and passes complete frames to the shared codec |
| `esp32-data-display-2/main/monitoring.c` | All alert thresholds live here |
| `esp-data-hub-2/main/data_canbus/request_ecu.c` | SSM poll payload and response parsing |
