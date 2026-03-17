#include <Arduino.h>

#include <ESP32-TWAI-CAN.hpp>
#include <optional>

#define CAN_RX_PIN 4
#define CAN_TX_PIN 5

#define ISOTP_MAX_SESSIONS 16
// protocol max is 4095, but in practice this is using way less
#define ISOTP_MAX_PAYLOAD 128

struct ISOTPAssembledMessage {
  uint32_t id;
  size_t length;
  uint8_t data[ISOTP_MAX_PAYLOAD];
};

enum ISOTPFrameType : uint8_t {
  SINGLE_FRAME = 0x0,
  FIRST_FRAME = 0x1,
  CONSECUTIVE_FRAME = 0x2,
  FLOW_CONTROL_FRAME = 0x3
};

class ISOTPHandler {
 public:
  ISOTPHandler() {}

  std::optional<ISOTPAssembledMessage> handleFrame(const CanFrame& frame) {
    if (frame.data_length_code == 0) {
      return std::nullopt;
    }

    const uint8_t pci = frame.data[0];
    const uint8_t pciFrameType = (pci & 0xF0) >> 4;
    const uint8_t pciData = pci & 0x0F;

    switch (pciFrameType) {
      case ISOTPFrameType::SINGLE_FRAME:
        return handleSingleFrame(frame, pciData);
      case ISOTPFrameType::FIRST_FRAME:
        return handleFirstFrame(frame, pciData);
      case ISOTPFrameType::CONSECUTIVE_FRAME:
        return handleConsecutiveFrame(frame, pciData);
      case ISOTPFrameType::FLOW_CONTROL_FRAME:
        return handleFlowControlFrame(frame, pciData);
      default:
        return std::nullopt;
    }
  }

 private:
  struct Session {
    double start_ts{0.0};
    uint16_t targetLength{0};  // total ISO-TP payload length
    uint16_t haveLength{0};    // bytes buffered so far
    uint8_t expectedSeq{1};    // next CF SN (mod 16), starts at 1 after FF
    uint8_t buf[ISOTP_MAX_PAYLOAD];  // grows up to target_len
  };
  struct SessionEntry {
    uint32_t can_id;
    bool used;
    Session session;
  };
  SessionEntry sessions[ISOTP_MAX_SESSIONS];

  Session* findSession(uint32_t id) {
    for (auto& session : sessions)
      if (session.used && session.can_id == id) return &session.session;

    return nullptr;
  }

  Session* newSession(uint32_t id) {
    for (auto& session : sessions)
      if (!session.used) {
        session.used = true;
        session.can_id = id;
        return &session.session;
      }

    return nullptr;
  }

  void endSession(uint32_t id) {
    for (auto& session : sessions)
      if (!session.can_id == id) {
        memset(&session, 0, sizeof(session));
      }
  }

  //

  ISOTPAssembledMessage handleSingleFrame(const CanFrame& frame,
                                          uint8_t dataLength) {
    ISOTPAssembledMessage msg{
        .id = frame.identifier, .length = dataLength, .data = {0}};
    memcpy(msg.data, &frame.data[1], dataLength);

    return msg;
  }

  std::nullopt_t handleFirstFrame(const CanFrame& frame, uint8_t pciLow) {
    uint8_t upper_len_bits = pciLow;
    uint8_t lower_len_bits = frame.data[1];
    uint16_t data_length = (upper_len_bits << 8) | lower_len_bits;

    if (data_length > ISOTP_MAX_PAYLOAD) {
      // TODO
      return std::nullopt;
    }

    // create new session

    Session* session = newSession(frame.identifier);
    if (session == nullptr) {
      // TODO
      return std::nullopt;
    }

    return std::nullopt;
  }

  std::optional<ISOTPAssembledMessage> handleConsecutiveFrame(
      const CanFrame& frame, uint8_t low) {
    ISOTPAssembledMessage message{};
    return std::nullopt;
  }

  std::optional<ISOTPAssembledMessage> handleFlowControlFrame(
      const CanFrame& frame, uint8_t low) {
    ISOTPAssembledMessage message{};
    return std::nullopt;
  }
};

void setup() {
  // init bluetooth

  // init canbus
  ESP32Can.setPins(CAN_TX_PIN, CAN_RX_PIN);
}

void loop() {
  char chRxBuffer[64] = {0};

  // handle ssm response, if any

  CanFrame canFrame{};
  if (ESP32Can.readFrame(&canFrame)) {
  }

  // read analog sensors

  // send ssm requests

  // send data over bluetooth

  // send data to display
}
