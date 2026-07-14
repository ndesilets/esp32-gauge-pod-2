# Build & Flash

## Prerequisites

- ESP-IDF v5+ installed and sourced (`get_idf` or `. $IDF_PATH/export.sh`)
- Each component is a standalone ESP-IDF project — build from its own directory

## Data Hub (`esp-data-hub-2`)

```sh
cd esp-data-hub-2

idf.py build
idf.py -p PORT flash monitor    # replace PORT with e.g. COM3 or /dev/ttyUSB0
```

**Target board:** ESP32-S3

Default UART emit period: 33ms (~30 Hz)
Default CAN poll period: 63ms (~16 Hz)

## Display (`esp32-data-display-2`)

```sh
cd esp32-data-display-2

idf.py build
idf.py -p PORT flash monitor
```

**Target board:** ESP32-P4

Assets (audio, config) are bundled into a LittleFS partition image automatically
at build time from the `assets/` directory.

## Mock & Fake Data Modes

Use these to develop or test without physical hardware.

### Hub — mock analog sensors

Replaces ADS1115 I2C reads with a stub that returns fixed values.

```sh
idf.py menuconfig
# Component config → Data Hub → Use mock analog sensor backend
```

Or set directly in `sdkconfig`:
```
CONFIG_DH_ANALOG_USE_MOCK=y
```

### Display — fake data generator

Bypasses UART entirely. `get_data()` returns synthetic data (sine waves / counters).
Useful for testing UI layout and threshold logic on the bench.

```sh
idf.py menuconfig
# Component config → Data Display → Enable fake data
```

Or set directly in `sdkconfig.defaults` or `sdkconfig`:
```
CONFIG_DD_ENABLE_FAKE_DATA=y
```

Currently active fake fields (others are commented out in `car_data.c`):
- `oil_temp`: ramps 200–300°F
- `engine_rpm`: fixed at 2500

Uncomment other fields in `esp32-data-display-2/main/car_data.c` to exercise
additional gauges.

## Key sdkconfig Options

### Data Hub

| Key | Default | Description |
|---|---|---|
| `CONFIG_DH_TWAI_TX_GPIO` | 6 | CAN TX GPIO |
| `CONFIG_DH_TWAI_RX_GPIO` | 7 | CAN RX GPIO |
| `CONFIG_DH_UART_PORT` | 1 | UART port number |
| `CONFIG_DH_UART_TX_GPIO` | 17 | UART TX GPIO |
| `CONFIG_DH_UART_RX_GPIO` | 18 | UART RX GPIO |
| `CONFIG_DH_UART_EMIT_PERIOD_MS` | 33 | Packet emit interval (ms) |
| `CONFIG_DH_RACECHRONO_BLE_ENABLED` | y | Advertise the RaceChrono DIY BLE telemetry service |
| `CONFIG_DH_RACECHRONO_BLE_DEVICE_NAME` | `Gauge Pod 2` | BLE advertising name shown to RaceChrono |
| `CONFIG_DH_RACECHRONO_BLE_EMIT_PERIOD_MS` | 20 | Maximum BLE telemetry packet cadence (ms) |
| `CONFIG_DH_ECU_POLL_PERIOD_MS` | 63 | ECU SSM poll interval (ms) |
| `CONFIG_DH_VDC_POLL_PERIOD_MS` | 63 | VDC UDS poll interval (ms) |
| `CONFIG_DH_ANALOG_POLL_PERIOD_MS` | 20 | Analog sensor poll interval (ms) |
| `CONFIG_DH_ANALOG_USE_MOCK` | n | Enable mock analog backend |
| `CONFIG_DH_ANALOG_I2C_SDA_GPIO` | 4 | ADS1115 SDA |
| `CONFIG_DH_ANALOG_I2C_SCL_GPIO` | 5 | ADS1115 SCL |
| `CONFIG_DH_OIL_PRESSURE_FILTER_NORMAL_TAU_MS` | 180 | Normal pressure smoothing time constant |
| `CONFIG_DH_OIL_PRESSURE_FILTER_FAST_TAU_MS` | 22 | Fast pressure response time constant |
| `CONFIG_DH_OIL_PRESSURE_FILTER_FAST_STEP_PSI` | 8 | Pressure step that activates fast response |
| `CONFIG_DH_OIL_PRESSURE_FILTER_FAST_HOLD_MS` | 60 | Fast response duration after a large step |
| `CONFIG_DH_OIL_PRESSURE_FILTER_IMMEDIATE_LOW_PSI` | 3 | Median pressure that bypasses EMA smoothing |

### Data Display

| Key | Default | Description |
|---|---|---|
| `CONFIG_DD_ENABLE_FAKE_DATA` | n | Bypass UART, use synthetic data |
| `CONFIG_DD_ENABLE_ALERT_AUDIO` | — | Play audio on threshold transitions |
| `CONFIG_DD_ENABLE_INTRO_SOUND` | y | Play sound at startup |
| `CONFIG_DD_ENABLE_INTRO_SPLASH` | y | Show splash screen at startup |
| `CONFIG_DD_UART_BUFFER_SIZE` | 512 | UART RX buffer size (bytes) |
