# esp32-data-hub

TODO

## Hardware

- Scamazon ESP32S3 Of Some Sort
- [Adafruit CAN Pal - CAN Bus Transceiver - TJA1051T/3 ](https://www.adafruit.com/product/5708) (for car)
- [HiLetgo MCP2515 CAN Module](https://www.amazon.com/HiLetgo-MCP2515-TJA1050-Receiver-Arduino/dp/B01D0WSEWU?th=1) (separate CAN network for display)

data
- water temp            16 bit int
- oil temp              16 bit int
- oil pressure          8 bit unsigned int
- afr                   32 bit float
- af correction         32 bit float
- af learning           32 bit float
- intake air temp       32 bit float
- feedback knock        32 bit float
- dam                   32 bit float
- boost                 32 bit float

- engine rpm            16 bit int
- accel pedal angle     32 bit float
- brake pressure        8 bit unsigned int
- steering angle        32 bit float

                        352 bits * ~16hz = 5632