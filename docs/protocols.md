# Protocols

## CAN Bus

- Standard: ISO 11898-1
- Bitrate: 500 kbps
- Interface: ESP-IDF TWAI driver (Two-Wire Automotive Interface)

| Node | Request ID | Response ID |
|---|---|---|
| ECU (Subaru SSM) | 0x7E0 | 0x7E8 |
| VDC / ABS module | 0x7B0 | 0x7B8 |

## ISO-TP (ISO 15765-2)

Multi-byte ECU/VDC payloads are transported via ISO-TP. Implementation lives in
`esp-data-hub-2/main/data_canbus/isotp.{c,h}`.

Frame type bytes:

| Constant | Value | Description |
|---|---|---|
| `ISOTP_SINGLE_FRAME` | 0x00 | Payload ≤7 bytes, single CAN frame |
| `ISOTP_FIRST_FRAME` | 0x10 | Start of multi-frame, contains total length |
| `ISOTP_CONSECUTIVE_FRAME` | 0x20 | Continuation frames |
| `ISOTP_FLOW_CONTROL_FRAME` | 0x30 | Receiver sends back to authorize more frames |

Flow control block size constants:

| Constant | Value | Meaning |
|---|---|---|
| `ISOTP_FC_BS_LET_ER_EAT_BUD` | 0x00 | Send all remaining frames without pause |
| `ISOTP_FC_BS_CONTINUE` | 0x01 | Continue sending |
| `ISOTP_FC_BS_WAIT` | 0x02 | Wait for another FC |
| `ISOTP_FC_BS_ABORT` | 0x03 | Abort transmission |

Key functions:
- `isotp_wrap_payload()` — segments a byte payload into CAN frames for TX
- `isotp_unwrap_frames()` — reassembles received CAN frames into a payload
- `isotp_wait_for_fc()` — blocks (with timeout) until flow control frame arrives
- `isotp_send_flow_control()` — sends FC frame to permit sender to continue

## SSM (Subaru Select Monitor) / UDS

ECU polling uses service 0xA8 (read memory by address list) defined in
`esp-data-hub-2/main/data_canbus/request_ecu.c`. The response service ID is 0xE8.

### Poll Payload (sent to 0x7E0)

```
0xA8 0x00             service + padding mode
0x00 0x00 0x08        coolant temp
0x00 0x00 0x09        AF correction #1
0x00 0x00 0x0A        AF learning #1
0x00 0x00 0x0E        engine RPM (high byte)
0x00 0x00 0x0F        engine RPM (low byte)
0x00 0x00 0x12        intake air temp
0x00 0x00 0x20        fuel injector #1 pulse width
0x00 0x00 0x46        AFR
0xFF 0x6B 0x49        DAM
0xFF 0x84 0x80        feedback knock correction (byte 0)
0xFF 0x84 0x81        feedback knock correction (byte 1)
0xFF 0x84 0x82        feedback knock correction (byte 2)
0xFF 0x84 0x83        feedback knock correction (byte 3)
0xFF 0x1E 0xE4        ethanol concentration (byte 0)
0xFF 0x1E 0xE5        ethanol concentration (byte 1)
0x00 0x00 0x29        accelerator pedal
```

Injector duty cycle is derived from injector #1 pulse width with
`IDC = injector_pw_ms * RPM / 1200`.

### Response Parsing (from 0x7E8)

Response payload begins with service ID 0xE8. Bytes after that:

| Offset | Field | Conversion |
|---|---|---|
| data[0] | water_temp | `32 + 9 * (value - 40) / 5` → °F |
| data[1] | af_correct | `(value - 128) * 100 / 128` → % |
| data[2] | af_learned | `(value - 128) * 100 / 128` → % |
| data[3:4] | engine_rpm | `(high << 8 | low) / 4` → RPM |
| data[5] | int_temp | `32 + 9 * (value - 40) / 5` → °F |
| data[6] | inj_duty | `(value * 0.256 ms) * RPM / 1200` → % |
| data[7] | af_ratio | `value * 14.7 / 128` → λ ratio |
| data[8] | dam | `value * 0.0625` → 0..1.049 |
| data[9:12] | fb_knock | `memcpy float` from 4 bytes big-endian |
| data[13:14] | eth_conc | `(high << 8 | low) * 100 / 65536` → % |
| data[15] | throttle_pos | `value * 100 / 255` → % |

VDC parsing: `esp-data-hub-2/main/data_canbus/request_vdc.c` — produces
`brake_pressure_bar` and `steering_angle_deg`.

## RaceChrono DIY BLE Telemetry

The data hub exposes RaceChrono's DIY BLE CAN-Bus service when
`CONFIG_DH_RACECHRONO_BLE_ENABLED=y`. This is a BLE-only interface; it does not
transmit the synthetic packet on the vehicle CAN bus and does not use ISO-TP.

- Service UUID: `0x1FF8`
- Main packet characteristic: `0x0001` (`READ`, `NOTIFY`)
- Filter characteristic: `0x0002` (`WRITE`)
- Synthetic packet ID: `0x00000500`

RaceChrono writes its packet filter and requested notification interval to the
filter characteristic. The hub sends notifications only after the packet is
enabled and the central has subscribed to the main characteristic.

The `0x500` packet ID is little-endian in the BLE packet header, as required by
the RaceChrono DIY API. The four-byte payload is big-endian where applicable:

| Payload byte(s) | Type | Vehicle state field | Units / conversion |
|---|---|---|---|
| `0` | `uint8` | `throttle_pos` | whole %, rounded and clamped to `0..100` |
| `1` | `uint8` | `brake_pressure_bar` | whole bar, rounded and clamped to `0..120` |
| `2:3` | signed `int16` | `steering_angle_deg` | whole degrees, rounded |

In RaceChrono, enable **Expert settings → Experimental devices**, add a
RaceChrono DIY BLE CAN-Bus device, then create three CAN-Bus channels for
packet ID `0x500` that decode the listed byte ranges. The packet layout is implemented by
`esp-data-hub-2/main/racechrono/racechrono_packet.c`.

## UART Telemetry (Hub → Display)

- Baud: 115200, 8N1
- Payload: MessagePack fixed array (MPack v1.1.1)
- Integrity: CRC-16/CCITT-FALSE over the MessagePack payload
- Framing: COBS with a trailing `0x00` delimiter
- Maximum wire frame: 93 bytes, including delimiter

### Wire framing

```text
MessagePack payload
→ append CRC16 high byte, then low byte
→ COBS encode payload + CRC
→ append 0x00
```

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, final XOR
`0x0000`, no input/output reflection. The check value for ASCII `123456789` is
`0x29B1`. The receiver validates COBS and CRC before parsing MessagePack.

The shared implementation is `esp32-shared/src/telemetry_protocol.c`; the hub
and display do not maintain separate codecs.

### MessagePack Field Order (MUST stay in sync)

```
Index  Type      Field
  0    uint      schema_version (currently 3)
  1    uint32    sequence
  2    uint32    timestamp_ms
  3    float32   water_temp      (°F)
  4    float32   oil_temp        (°F)
  5    float32   oil_pressure    (PSI)
  6    float32   dam             (0..1.049)
  7    float32   af_learned      (%)
  8    float32   af_ratio        (λ, e.g. 14.7)
  9    float32   int_temp        (°F)
 10    float32   fb_knock        (dB)
 11    float32   af_correct      (%)
 12    float32   inj_duty        (%)
 13    float32   eth_conc        (%)
 14    float32   engine_rpm      (RPM)
 15    float32   throttle_pos    (%)
 16    float32   brake_pressure_bar (bar)
 17    float32   steering_angle_deg (degrees)
 18    float32   oil_pressure_raw (unfiltered PSI)
```

`oil_pressure` is the filtered value used by the display and alert monitoring.
`oil_pressure_raw` is the calibrated but unsmoothed value retained for data
logging and electrical-noise diagnosis.

The decoder requires exactly 19 items, exact `float32` telemetry values, unsigned
integers fitting `uint32_t`, the supported schema version, and no trailing data.

**Adding a new field:** add it to `vehicle_state_t`, append it to both sequences
in the shared codec, update the item count and maximum sizes, bump the schema
version, update the golden test vector, and update this table. Both devices must
be flashed together when the schema changes.
