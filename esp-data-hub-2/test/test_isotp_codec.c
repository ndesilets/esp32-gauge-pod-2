#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "isotp_codec.h"

static void fill_payload(uint8_t* payload, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    payload[i] = (uint8_t)i;
  }
}

static void copy_tx_to_rx(const uint8_t tx[][8], can_rx_frame_t* rx, size_t frame_count) {
  for (size_t i = 0; i < frame_count; ++i) {
    memcpy(rx[i].data, tx[i], sizeof(rx[i].data));
    rx[i].data_len = 8;
  }
}

static void assert_round_trip(size_t payload_length) {
  uint8_t payload[128];
  uint8_t tx[20][8] = {{0}};
  can_rx_frame_t rx[20] = {{0}};
  uint8_t output[sizeof(payload)] = {0};
  size_t frame_count = 0;
  size_t output_length = 0;
  fill_payload(payload, payload_length);

  assert(isotp_wrap_payload(payload, (uint16_t)payload_length, tx, 20, &frame_count));
  copy_tx_to_rx(tx, rx, frame_count);
  assert(isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  assert(output_length == payload_length);
  assert(memcmp(output, payload, payload_length) == 0);
}

static void test_wraps_single_frame_boundaries(void) {
  uint8_t payload[7];
  uint8_t frames[1][8] = {{0}};
  size_t frame_count = 0;
  fill_payload(payload, sizeof(payload));

  assert(isotp_wrap_payload(payload, 0, frames, 1, &frame_count));
  assert(frame_count == 1);
  assert(frames[0][0] == 0x00);

  memset(frames, 0, sizeof(frames));
  assert(isotp_wrap_payload(payload, sizeof(payload), frames, 1, &frame_count));
  assert(frame_count == 1);
  assert(frames[0][0] == 0x07);
  assert(memcmp(&frames[0][1], payload, sizeof(payload)) == 0);
}

static void test_wraps_and_unwraps_multiple_frames(void) {
  uint8_t payload[20];
  uint8_t tx[3][8] = {{0}};
  can_rx_frame_t rx[3] = {{0}};
  uint8_t output[sizeof(payload)] = {0};
  size_t frame_count = 0;
  size_t output_length = 0;
  fill_payload(payload, sizeof(payload));

  assert(isotp_wrap_payload(payload, sizeof(payload), tx, 3, &frame_count));
  assert(frame_count == 3);
  assert(tx[0][0] == 0x10);
  assert(tx[0][1] == sizeof(payload));
  assert(memcmp(&tx[0][2], &payload[0], 6) == 0);
  assert(tx[1][0] == 0x21);
  assert(memcmp(&tx[1][1], &payload[6], 7) == 0);
  assert(tx[2][0] == 0x22);
  assert(memcmp(&tx[2][1], &payload[13], 7) == 0);

  copy_tx_to_rx(tx, rx, frame_count);
  assert(isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  assert(output_length == sizeof(payload));
  assert(memcmp(output, payload, sizeof(payload)) == 0);
}

static void test_round_trips_payload_length_boundaries(void) {
  assert_round_trip(0);
  assert_round_trip(1);
  assert_round_trip(7);
  assert_round_trip(8);
  assert_round_trip(13);
  assert_round_trip(64);
}

static void test_sequence_number_rolls_over(void) {
  uint8_t payload[118];
  uint8_t tx[17][8] = {{0}};
  can_rx_frame_t rx[17] = {{0}};
  uint8_t output[sizeof(payload)] = {0};
  size_t frame_count = 0;
  size_t output_length = 0;
  fill_payload(payload, sizeof(payload));

  assert(isotp_wrap_payload(payload, sizeof(payload), tx, 17, &frame_count));
  assert(frame_count == 17);
  assert(tx[15][0] == 0x2F);
  assert(tx[16][0] == 0x20);

  copy_tx_to_rx(tx, rx, frame_count);
  assert(isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  assert(output_length == sizeof(payload));
  assert(memcmp(output, payload, sizeof(payload)) == 0);
}

static void test_rejects_invalid_wrap_arguments_and_capacity(void) {
  uint8_t payload[20] = {0};
  uint8_t frames[2][8] = {{0}};
  size_t frame_count = 99;

  assert(!isotp_wrap_payload(NULL, sizeof(payload), frames, 2, &frame_count));
  assert(!isotp_wrap_payload(payload, sizeof(payload), NULL, 2, &frame_count));
  assert(!isotp_wrap_payload(payload, sizeof(payload), frames, 0, &frame_count));
  assert(!isotp_wrap_payload(payload, sizeof(payload), frames, 2, NULL));
  assert(!isotp_wrap_payload(payload, sizeof(payload), frames, 2, &frame_count));
  assert(frame_count == 2);
  assert(!isotp_wrap_payload(payload, 4096, frames, 2, &frame_count));
  assert(frame_count == 0);
}

static void test_rejects_malformed_frames(void) {
  uint8_t payload[20];
  uint8_t tx[3][8] = {{0}};
  can_rx_frame_t rx[3] = {{0}};
  uint8_t output[sizeof(payload)] = {0};
  size_t frame_count = 0;
  size_t output_length = 0;
  fill_payload(payload, sizeof(payload));
  assert(isotp_wrap_payload(payload, sizeof(payload), tx, 3, &frame_count));
  copy_tx_to_rx(tx, rx, frame_count);

  assert(!isotp_unwrap_frames(NULL, frame_count, output, sizeof(output), &output_length));
  assert(!isotp_unwrap_frames(rx, 0, output, sizeof(output), &output_length));
  assert(!isotp_unwrap_frames(rx, frame_count, NULL, sizeof(output), &output_length));
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output), NULL));
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output) - 1, &output_length));
  assert(!isotp_unwrap_frames(rx, frame_count - 1, output, sizeof(output), &output_length));

  rx[1].data[0] = 0x22;
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  copy_tx_to_rx(tx, rx, frame_count);
  rx[1].data[0] = ISOTP_FLOW_CONTROL_FRAME;
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  copy_tx_to_rx(tx, rx, frame_count);
  rx[0].data_len = 7;
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
  copy_tx_to_rx(tx, rx, frame_count);
  rx[2].data_len = 7;
  assert(!isotp_unwrap_frames(rx, frame_count, output, sizeof(output), &output_length));
}

static void test_unwraps_single_frame_and_checks_data_length(void) {
  can_rx_frame_t frame = {
      .data = {0x04, 0xDE, 0xAD, 0xBE, 0xEF},
      .data_len = 5,
  };
  uint8_t output[4] = {0};
  size_t output_length = 0;

  assert(isotp_unwrap_frames(&frame, 1, output, sizeof(output), &output_length));
  assert(output_length == sizeof(output));
  assert(memcmp(output, &frame.data[1], sizeof(output)) == 0);

  assert(!isotp_unwrap_frames(&frame, 1, output, sizeof(output) - 1, &output_length));
  frame.data_len = 4;
  assert(!isotp_unwrap_frames(&frame, 1, output, sizeof(output), &output_length));
  frame.data_len = 5;
  frame.data[0] = 0x80;
  assert(!isotp_unwrap_frames(&frame, 1, output, sizeof(output), &output_length));
}

int main(void) {
  test_wraps_single_frame_boundaries();
  test_wraps_and_unwraps_multiple_frames();
  test_round_trips_payload_length_boundaries();
  test_sequence_number_rolls_over();
  test_rejects_invalid_wrap_arguments_and_capacity();
  test_rejects_malformed_frames();
  test_unwraps_single_frame_and_checks_data_length();
  puts("ISO-TP codec tests passed");
  return 0;
}
