# Architecture

## System Overview

Two ESP32 boards communicate over UART. The data hub polls the car's ECU and ABS
module over CAN bus, reads analog sensors via I2C, and emits periodic CBOR packets.
The display receives those packets, evaluates thresholds, renders an LVGL UI, and
logs data to an SD card.

```
┌────────────────────────────────────────────────────┐
│              esp-data-hub-2 (ESP32)                │
│                                                    │
│  task_canbus_data (prio+1)                         │
│    Send ECU poll (0x7E0) via ISO-TP                │
│    Receive ECU response (0x7E8) via ISO-TP         │
│    Parse SSM response → display_state              │
│    Send VDC poll (0x7B0) via ISO-TP                │
│    Parse VDC response → display_state              │
│                                                    │
│  task_analog_sensors (prio+1)                      │
│    Read ADS1115 (I2C) → oil temp + oil pressure    │
│    → display_state                                 │
│                                                    │
│  task_can_rx_dispatcher (prio+2)                   │
│    Route CAN frames → ecu_can_frames / vdc_can_frames │
│                                                    │
│  task_uart_emitter (prio+1)                        │
│    Copy display_state (under mutex)                │
│    CBOR encode → 14-field array                    │
│    TX over UART at 115200 baud                     │
└──────────────────────────┬─────────────────────────┘
                           │ UART (115200 baud, CBOR)
                           ▼
┌────────────────────────────────────────────────────┐
│           esp32-data-display-2 (ESP32-P4)          │
│                                                    │
│  uart_pipeline_task (prio+2)                       │
│    RX UART packet                                  │
│    CBOR decode → display_packet_t                  │
│    Update monitored_state (under mutex)            │
│    evaluate_statuses() → status per field          │
│    Detect alert transitions → play audio           │
│                                                    │
│  display_render_task (prio+1, 30 fps)              │
│    Snapshot monitored_state (under mutex)          │
│    dd_ui_controller_render() → LVGL widgets        │
│                                                    │
│  dd_logger (background)                            │
│    CSV rows to SD card (FATFS)                     │
│                                                    │
│  dd_usb_drive                                      │
│    SD card exposed as USB MSC (TinyUSB)            │
└────────────────────────────────────────────────────┘
```

## Component Roles

### esp-data-hub-2

Owns all vehicle data acquisition. Responsibilities:
- CAN bus communication via ESP-IDF TWAI driver at 500 kbps
- ISO-TP framing/deframing for multi-byte ECU/VDC responses
- SSM (Subaru Select Monitor) request construction and response parsing
- Analog sensor reading via ADS1115 over I2C (oil temp, oil pressure)
- Packing `display_packet_t` and emitting over UART as CBOR

Central state is `app_context_t` in `main/app_context.h`. All tasks receive a
pointer to this; shared fields are protected by their respective mutexes.

### esp32-data-display-2

Owns all UI and monitoring. Responsibilities:
- UART receive and CBOR decode
- Threshold evaluation and alert status per field
- LVGL rendering at 30 fps
- SD card logging via FATFS
- USB mass storage export via TinyUSB MSC

Central state is `monitored_state_t` in `main/monitoring.h`. Fields are
`numeric_monitor_t` structs containing current value, min/max seen, and status.

### esp32-shared

Single header `telemetry_types.h` defining the wire format structs. Both firmware
components include this. It is the canonical definition of what travels over UART.

## Task Priority Summary

| Task | Component | Priority | Stack |
|---|---|---|---|
| `task_can_rx_dispatcher` | hub | tskIDLE+2 | 8 KB |
| `uart_pipeline_task` | display | tskIDLE+2 | 4 KB |
| `task_canbus_data` | hub | tskIDLE+1 | 16 KB |
| `task_analog_sensors` | hub | tskIDLE+1 | 8 KB |
| `task_uart_emitter` | hub | tskIDLE+1 | 8 KB |
| `task_twai_monitor` | hub | tskIDLE+1 | 4 KB |
| `display_render_task` | display | tskIDLE+1 | 4 KB |

## Error Handling Convention

Transport and communication errors must not crash the application. Any function
that sends over CAN, UART, I2C, or other physical buses must handle errors by
logging a warning and returning a failure indicator — never by calling
`ESP_ERROR_CHECK` (which aborts on failure).

The reasoning: bus errors are transient. The ECU, VDC, or peripheral may not be
ready at power-on, or may miss a frame under load. An abort causes a hard reset,
which can compound the problem (e.g. repeated resets drive the CAN bus into
bus-off state, or leave the ECU's ISO-TP state machine stuck mid-transfer).

**Rule:** Use `ESP_ERROR_CHECK` only for one-time initialization that is
genuinely unrecoverable (e.g. driver install, mutex creation). For any runtime
communication path, check the return value and log + retry instead.

## Alert Thresholds

All thresholds are in `esp32-data-display-2/main/monitoring.c`. Summary:

| Parameter | NOT_READY | OK | WARN | CRITICAL |
|---|---|---|---|---|
| Water temp (°F) | < 160 | < 215 | < 220 | ≥ 220 |
| Oil temp (°F) | < 180 | < 240 | < 250 | ≥ 250 |
| Oil pressure | < 300 RPM | ≥ 10 PSI/1000 RPM | — | < threshold |
| DAM | — | ≥ 1.0 | — | < 1.0 |

Audio alert (`/storage/audio/tacobell.wav`) plays on any transition to WARN or
CRITICAL. Alert audio requires `CONFIG_DD_ENABLE_ALERT_AUDIO=y`.
