# esp32-data-display

TODO

painful lesson: espressif dropped support for platformio some time in 2024 supposedly because of money, so we're stuck with an older version of the core arduino framework libraries (v2) and a lot of the stuff (like https://github.com/moononournation/Arduino_GFX, which would be nice to use) for this display needs v3. so you either have to use the arduino ide (gross) or use freertos (more complicated).

notes:

- copy .pio\libdeps\esp32-s3-devkitc-1\lvgl\lv_conf_template.h into .pio\libdeps\esp32-s3-devkitc-1\lv_conf.h
    - set `#if 1` in file
    - or copy from https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49/blob/main/Arduino_Libraries/lvgl9/lv_conf.h ?
- AXS15231B is the display driver, has touch driver over i2c


## Hardware

- [Waveshare ESP32 3.5" 172×640 Display](https://www.waveshare.com/product/arduino/displays/lcd-spi-qspi/esp32-s3-touch-lcd-3.49.htm)
- [Adafruit NeoPixel Stick 8x RGBW](https://www.adafruit.com/product/2868)
- [Adafruit CAN Pal - CAN Bus Transceiver - TJA1051T/3 ](https://www.adafruit.com/product/5708)


```
primary screen:
    coolant temp, oil temp, oil pressure, AFR, knock

secondary screen:
    ethanol concentration, injector duty cycle, fuel trims, AFR, short term fuel, long term fuel
```