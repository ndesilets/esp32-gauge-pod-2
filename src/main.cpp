#include <Arduino.h>

#include <ESP32-TWAI-CAN.hpp>

#define CAN_RX_PIN 4
#define CAN_TX_PIN 5
#define DEBUG_RX_PIN 11
#define DEBUG_TX_PIN 10

#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum { CH_CLOSED, CH_OPEN, CH_LISTEN } chState;
bool timestampEnabled = false;

#ifdef ENABLE_DEBUG_SERIAL
HardwareSerial debugSerial(1);
#endif

static unsigned long timeElapsed = 0;
static unsigned long lastSent = 0;

// encode to SLCAN format
void encodeCanFrame(const CanFrame& frame, char* buffer, size_t bufferSize,
                    bool withTimestamp) {
  int offset = 0;

  // set frame type and identifier
  offset += snprintf(buffer, bufferSize, frame.extd ? "T%08X" : "t%03X",
                     frame.identifier);

  // set data length code
  offset += snprintf(buffer + offset, bufferSize - offset, "%1X",
                     frame.data_length_code);

  // encode frame data
  for (int i = 0; i < MIN(frame.data_length_code, 8); i++) {
    offset +=
        snprintf(buffer + offset, bufferSize - offset, "%02X", frame.data[i]);
  }

  // handle timestamp
  if (withTimestamp) {
    // assume milliseconds, varies by version but in this case i don't care
    uint16_t now = (uint16_t)(millis() % 0xFFFF);
    offset += snprintf(buffer + offset, bufferSize - offset, "%04X", now);
  }

  // terminate message
  buffer[MIN(offset, bufferSize - 2)] = '\r';
  offset += 1;
  buffer[MIN(offset, bufferSize - 1)] = '\0';
}

// assumes that sender implements SLCAN correctly, if not well fuck me
void decodeCanFrame(const char* buffer, CanFrame& frame) {
  int n = 0;

  // decode frame type and identifier
  frame.extd = buffer[0] == 'T';
  int offset = 1;  // skip type
  sscanf(buffer + offset, frame.extd ? "%08X%n" : "%03X%n", &frame.identifier,
         &n);
  offset += n;

  // get data length code
  sscanf(buffer + offset, "%1X%n", &frame.data_length_code, &n);
  offset += n;

  // get data bytes
  for (int i = 0; i < MIN(frame.data_length_code, 8); i++) {
    sscanf(buffer + offset, "%02X%n", &frame.data[i], &n);
    offset += n;
  }

  // ignore timestamp, remote frames for now
}

void setup() {
  Serial.begin(115200);
#ifdef ENABLE_DEBUG_SERIAL
  debugSerial.begin(115200, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
#endif

  ESP32Can.setPins(CAN_TX_PIN, CAN_RX_PIN);
}

void loop() {
  timeElapsed = millis();
  char chRxBuffer[64] = {0};
  CanFrame chFrame{};

  // handle SLCAN commands

  if (Serial.available()) {
    int bytesRead = Serial.readBytesUntil('\r', chRxBuffer, sizeof(chRxBuffer));
    chRxBuffer[MIN(bytesRead, sizeof(chRxBuffer) - 1)] = '\0';
#ifdef ENABLE_DEBUG_SERIAL
    debugSerial.println(chRxBuffer);
#endif

    switch (chRxBuffer[0]) {
      case 'C':
        chState = CH_CLOSED;
        Serial.write('\r');
        break;
      case 'O':
        chState = CH_OPEN;
        Serial.write('\r');
        break;
      case 'L':
        chState = CH_LISTEN;
        Serial.write('\r');
        break;
      case 'Z':
        // should be either 0 or 1
        timestampEnabled = chRxBuffer[1] == '1';
        Serial.write('\r');
        break;
      case 'S':
        switch (chRxBuffer[1]) {
          case '0':
            ESP32Can.setSpeed(TWAI_SPEED_10KBPS);
            break;
          case '1':
            ESP32Can.setSpeed(TWAI_SPEED_20KBPS);
            break;
          case '2':
            // ESP32-TWAI-CAN doesn't have 50kbps
            Serial.write('\a');
            return;
          case '3':
            ESP32Can.setSpeed(TWAI_SPEED_100KBPS);
            break;
          case '4':
            ESP32Can.setSpeed(TWAI_SPEED_125KBPS);
            break;
          case '5':
            ESP32Can.setSpeed(TWAI_SPEED_250KBPS);
            break;
          case '6':
            ESP32Can.setSpeed(TWAI_SPEED_500KBPS);
            break;
          case '7':
            ESP32Can.setSpeed(TWAI_SPEED_800KBPS);
            break;
          case '8':
            ESP32Can.setSpeed(TWAI_SPEED_1000KBPS);
            break;
          default:
            // unsupported speed
            Serial.write('\a');
            return;
        }

        ESP32Can.begin();
        Serial.write('\r');
        break;
      case 'M':  // set acceptance mask
      case 'm':  // set acceptance filter
      case 's':  // set custom bit timing
      case 'F':  // get status flags
        // aw hell naw
        Serial.write('\a');
        break;
      case 'V':  // get firmware version
        Serial.write("V69\r");
        break;
      case 'N':  // get serial number
        Serial.write("N69\r");
        break;
      case 'T':
      case 't':
        decodeCanFrame(chRxBuffer, chFrame);
#ifdef ENABLE_DEBUG_SERIAL
        debugSerial.printf("Frame to send: ID %03X DLC %d\n",
                           chFrame.identifier, chFrame.data_length_code);
#endif
        // TODO send over canbus to ECU
        break;
      default:
        // it is a mystery
        Serial.write('\a');
        break;
    }
  }

#ifdef SEND_TEST_MESSAGE_ONLY

  if (chState == CH_OPEN && (timeElapsed - lastSent) > 1000) {
    char chTxBuffer[64] = {0};
    CanFrame test{};

    test.identifier = 0x123;
    test.data_length_code = 8;
    test.data[0] = 0x11;
    test.data[1] = 0x22;
    test.data[2] = 0x33;
    test.data[3] = 0x44;
    test.data[4] = 0x55;
    test.data[5] = 0x66;
    test.data[6] = 0x77;
    test.data[7] = 0x88;
    test.extd = false;
    test.rtr = false;

    encodeCanFrame(test, chTxBuffer, sizeof(chTxBuffer), timestampEnabled);
    Serial.write(chTxBuffer);

    lastSent = timeElapsed;
  }

#else

  // handle CAN to/from ECU

  if (chState == CH_OPEN && chFrame.identifier != 0) {
    ESP32Can.writeFrame(&chFrame);
  }

  CanFrame ecuFrame{};
  if (chState == CH_OPEN && ESP32Can.readFrame(&ecuFrame)) {
    char chTxBuffer[64] = {0};
    encodeCanFrame(ecuFrame, chTxBuffer, sizeof(chTxBuffer), timestampEnabled);
    Serial.write(chTxBuffer);
  }

#endif
}