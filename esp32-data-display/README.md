# esp32-data-display

TODO

notes:

- copy .pio\libdeps\esp32-s3-devkitc-1\lvgl\lv_conf_template.h into .pio\libdeps\esp32-s3-devkitc-1\lv_conf.h
    - set `#if 1` in file
    - or copy from https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/blob/main/Arduino_Libraries/lvgl9/lv_conf.h ?
- AXS15231B is the display driver, has touch driver over i2c


## Hardware

- [Waveshare ESP32 3.5" 172×640 Display](https://www.waveshare.com/product/arduino/displays/lcd-spi-qspi/esp32-s3-touch-lcd-3.49.htm)
- [Adafruit NeoPixel Stick 8x RGBW](https://www.adafruit.com/product/2868)
- [Adafruit CAN Pal - CAN Bus Transceiver - TJA1051T/3 ](https://www.adafruit.com/product/5708)