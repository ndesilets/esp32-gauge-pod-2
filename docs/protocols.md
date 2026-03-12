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
0x00 0x00 0x46        AFR
0xFF 0x6B 0x49        DAM
0xFF 0x88 0x10        feedback knock correction (byte 0)
0xFF 0x88 0x11        feedback knock correction (byte 1)
0xFF 0x88 0x12        feedback knock correction (byte 2)
0xFF 0x88 0x13        feedback knock correction (byte 3)
0x00 0x00 0x29        accelerator pedal angle
```

Note: injector duty cycle (TODO) and ethanol concentration (TODO) are not yet polled.

### Response Parsing (from 0x7E8)

Response payload begins with service ID 0xE8. Bytes after that:

| Offset | Field | Conversion |
|---|---|---|
| data[0] | water_temp | `32 + 9 * (value - 40) / 5` → °F |
| data[1] | af_correct | `(value - 128) * 100 / 128` → % |
| data[2] | af_learned | `(value - 128) * 100 / 128` → % |
| data[3:4] | engine_rpm | `(high << 8 | low) / 4` → RPM |
| data[5] | int_temp | `32 + 9 * (value - 40) / 5` → °F |
| data[6] | af_ratio | `value * 14.7 / 128` → λ ratio |
| data[7] | dam | `value * 0.0625` → 0..1.049 |
| data[8:11] | fb_knock | `memcpy float` from 4 bytes big-endian → dB |
| data[12] | throttle_pos | `value * 100 / 255` → % (bt_packet only) |

VDC parsing: `esp-data-hub-2/main/data_canbus/request_vdc.c` — produces
`brake_pressure_bar` and `steering_angle_deg`.

## UART Telemetry (Hub → Display)

- Baud: 115200, 8N1
- Encoding: CBOR binary (tinycbor library)
- Packet size: ~56 bytes typical

### CBOR Field Order (MUST stay in sync)

Encoder: `esp-data-hub-2/main/tasks/task_uart_emitter.c::encode_display_packet()`
Decoder: `esp32-data-display-2/main/car_data.c::decode_telemetry_packet()`

```
Index  Type    Field
  0    uint    sequence
  1    uint    timestamp_ms
  2    float   water_temp      (°F)
  3    float   oil_temp        (°F)
  4    float   oil_pressure    (PSI)
  5    float   dam             (0..1.049)
  6    float   af_learned      (%)
  7    float   af_ratio        (λ, e.g. 14.7)
  8    float   int_temp        (°F)
  9    float   fb_knock        (dB)
 10    float   af_correct      (%)
 11    float   inj_duty        (%, currently 0 — not yet polled)
 12    float   eth_conc        (%, currently 0 — not yet polled)
 13    float   engine_rpm      (RPM)
```

**Adding a new field:** add it to `display_packet_t`, bump the array count from 14
to N in both encoder and decoder, add the field at the end of both encode/decode
sequences, and update this table.
