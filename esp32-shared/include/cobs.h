#pragma once

// Consistent Overhead Byte Stuffing (COBS)
//
// Encodes arbitrary binary data so that 0x00 never appears in the output.
// A 0x00 byte can then be used as an unambiguous frame delimiter on a byte
// stream (e.g. UART), providing self-synchronisation: if alignment is lost,
// the receiver discards bytes until the next 0x00 and resumes cleanly.
//
// Wire format:  [encoded frame, no 0x00 bytes] [0x00 delimiter]
//
// Overhead: at most 1 extra byte per 254 bytes of input.
// For a 128-byte payload the encoded output is at most 129 bytes plus the
// 0x00 delimiter = 130 bytes on the wire.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Encode `in_len` bytes from `input` into `output` using COBS.
//
// `output` must be at least `in_len + in_len/254 + 1` bytes.
// Returns the number of bytes written to `output`, NOT including the 0x00
// delimiter. The caller is responsible for writing the trailing 0x00.
static inline size_t cobs_encode(const uint8_t *input, size_t in_len, uint8_t *output) {
  size_t ri = 0;   // read index into input
  size_t wi = 1;   // write index into output (slot 0 reserved for first code)
  size_t ci = 0;   // index of the current code byte in output
  uint8_t code = 1;  // distance to the next 0x00 (or end of block)

  while (ri < in_len) {
    if (input[ri] == 0x00) {
      // Terminate current block and start a new one
      output[ci] = code;
      ci = wi++;
      code = 1;
    } else {
      output[wi++] = input[ri];
      code++;
      if (code == 0xFF) {
        // Block is full (254 data bytes) — close it without a 0x00 in output
        output[ci] = code;
        ci = wi++;
        code = 1;
      }
    }
    ri++;
  }

  output[ci] = code;
  return wi;
}

// Decode `in_len` COBS-encoded bytes from `input` into `output`.
//
// `input` must NOT include the 0x00 delimiter.
// `out_cap` is the capacity of `output`; returns false if output would overflow.
// On success writes the decoded length to `*out_len` and returns true.
// Returns false on any framing error (0x00 found in encoded data, truncated
// block, or output overflow).
static inline bool cobs_decode(const uint8_t *input, size_t in_len,
                               uint8_t *output, size_t out_cap,
                               size_t *out_len) {
  size_t ri = 0;
  size_t wi = 0;

  while (ri < in_len) {
    uint8_t code = input[ri++];
    if (code == 0x00) {
      return false;  // 0x00 in encoded data is a framing error
    }

    // Copy (code - 1) literal data bytes
    for (uint8_t i = 1; i < code; i++) {
      if (ri >= in_len || wi >= out_cap) {
        return false;
      }
      output[wi++] = input[ri++];
    }

    // If this was not a full 254-byte block, the original had a 0x00 here —
    // restore it, unless we are at the very end of the input.
    if (code < 0xFF && ri < in_len) {
      if (wi >= out_cap) {
        return false;
      }
      output[wi++] = 0x00;
    }
  }

  *out_len = wi;
  return true;
}
