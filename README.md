# esp32-gauge-pod-2

not anywhere near complete. for now this is just a intermediate device to forward canbus messages to CANHacker (using SLCAN formatting) for analysis. intended to be a single display that combines data that'd you see from your accessport with custom analog sensors (oil temp, pressure, etc.) with alerting.

this is still in the "how do i make sense of this data" stage

hardware:
- ESP32S3 (using built-in CAN controller)
    - i think you need at least an S2 for fast serial USB that can keep up with 500kbit canbus
- Adafruit CAN transceiver TJA1051T/3
- Another microcontroller for UART serial debugging when necessary


https://github.com/ndesilets/esp32-gauge-pod is the first iteration that just shows oil temp/pressure.

