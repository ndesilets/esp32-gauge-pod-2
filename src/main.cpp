#define CAN_RX_PIN 4
#define CAN_TX_PIN 5

#define DEBUG_RX_PIN 11
#define DEBUG_TX_PIN 10

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include <Arduino.h>

#include <ESP32-TWAI-CAN.hpp>

enum { CH_CLOSED, CH_OPEN, CH_LISTEN } chState;
bool timestampEnabled = false;

HardwareSerial debugSerial(1);
char chRxBuffer[32] = {0};
char chTxBuffer[32] = {0};

static unsigned long timeElapsed = 0;
static unsigned long lastSent = 0;

// encode to SLCAN format
void encodeCanFrame(const CanFrame& frame, char* buffer, size_t bufferSize,
                    bool withTimestamp) {
  int offset = 0;

  // set frame type and identifier
  if (frame.extd) {
    offset += snprintf(buffer, bufferSize, "T%08X", frame.identifier);
  } else {
    offset += snprintf(buffer, bufferSize, "t%03X", frame.identifier);
  }

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
  int consumed = 0;

  // decode frame type and identifier
  frame.extd = buffer[0] == 'T';
  int offset = 1;  // skip type
  sscanf(buffer + offset, frame.extd ? "%08X%n" : "%03X%n", &frame.identifier,
         &consumed);
  offset += consumed;

  // get data length code
  sscanf(buffer + offset, "%1X%n", &frame.data_length_code, &consumed);
  offset += consumed;

  // get data bytes
  for (int i = 0; i < MIN(frame.data_length_code, 8); i++) {
    sscanf(buffer + offset, "%02X%n", &frame.data[i], &consumed);
    offset += consumed;
  }

  // ignore timestamp, remote frames for now
}

void setup() {
  Serial.begin(115200);
  debugSerial.begin(115200, SERIAL_8N1, DEBUG_RX_PIN, DEBUG_TX_PIN);
  debugSerial.println("hello mfer");

  ESP32Can.setPins(CAN_TX_PIN, CAN_RX_PIN);
  ESP32Can.setSpeed(TWAI_SPEED_500KBPS);
  ESP32Can.begin();
}

void loop() {
  timeElapsed = millis();
  CanFrame chFrame{};

  if (Serial.available()) {
    // TODO move global buffers out to here
    int bytesRead = Serial.readBytesUntil('\r', chRxBuffer, sizeof(chRxBuffer));
    chRxBuffer[bytesRead] = '\0';
    debugSerial.println(chRxBuffer);

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
      case 'S':  // set bit rate
        // don't care, it's gonna be 500kb for me
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
        debugSerial.printf("Frame to send: ID %03X DLC %d\n",
                           chFrame.identifier, chFrame.data_length_code);
        break;
      default:
        // it is a mystery
        Serial.write('\a');
        break;
    }
  }

  /*
   * if CANHacker frame is set then send the frame to ECU
   * then read can frame from ECU and forward to CANHacker
   */

  if (chState == CH_OPEN && (timeElapsed - lastSent) > 1000) {
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
}