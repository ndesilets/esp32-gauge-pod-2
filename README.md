# esp32-gauge-pod-2

TODO: notes

not anywhere near complete. v2 of https://github.com/ndesilets/esp32-gauge-pod that incorporates canbus data for a single display for All The Things in a 2019 WRX STI

## Project Structure

| Directory        | Description                                |
| ---------------- | ------------------------------------------ |
| `canhacker-usb-adapter` | esp32s3 usb adapter for CANHacker |
| `esp32-data-display` | displays the data from esp32-data-hub while providing monitoring/alerts |
| `esp32-data-hub` | gets data from canbus and custom sensors. makes it available via canbus for esp32-data-display or BT for SoloStorm |
| `scripts/parse-foxwell-nt614` | parses data logs from foxwell nt614 into csv |
| `scripts/subaru-decode` | parses and decodes CANHacker Subaru traces into csv |

## Shout outs

https://www.youtube.com/@T-WRXMechanic/videos for showing how accessports/subaru select monitor works