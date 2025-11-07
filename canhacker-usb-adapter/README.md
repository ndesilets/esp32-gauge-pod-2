# canhacker-usb-adapter

TODO: notes

uses esp32s3 with Adafruit CAN Pal (TJA1051T/3) to work with CANHacker (www.canhack.de) using Lawicel SLCAN format over USB. doesn't implement SLCAN entirely but enough for my purposes.

hardware:
- ESP32S3 (using built-in CAN controller)
    - i think you need at least an S2 for fast serial USB that can keep up with 500kbit canbus
- Adafruit CAN transceiver TJA1051T/3
- Another microcontroller for UART serial debugging when necessary
