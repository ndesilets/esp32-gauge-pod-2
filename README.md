# esp32-gauge-pod-2

TODO: notes

not anywhere near complete. v2 of https://github.com/ndesilets/esp32-gauge-pod that incorporates canbus data for a single display for All The Things in a 2019 WRX STI

hardware:
- ESP32S3 (using built-in CAN controller)
    - i think you need at least an S2 for fast serial USB that can keep up with 500kbit canbus
- Adafruit CAN transceiver TJA1051T/3
- Another microcontroller for UART serial debugging when necessary
