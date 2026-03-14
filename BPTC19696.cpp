/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "BPTC19696.h"

#include <string.h>

// BPTC (196,96) de-interleave table.
// ETSI TS 102 361-1 Section B.3.9 / Formula B.1:
//   "Interleave Index = Index × 181 modulo 196"
// This means code bit at position Index is transmitted at position (181*Index) mod 196.
// Decoding: decoded[n] = received[(181*n) mod 196]
// INTERLEAVE_TABLE[k] = (181 * k) mod 196
const uint32_t INTERLEAVE_TABLE[196] = {
      0U, 181U, 166U, 151U, 136U, 121U, 106U,  91U,  76U,  61U,  46U,  31U,  16U,   1U, 182U, 167U,
    152U, 137U, 122U, 107U,  92U,  77U,  62U,  47U,  32U,  17U,   2U, 183U, 168U, 153U, 138U, 123U,
    108U,  93U,  78U,  63U,  48U,  33U,  18U,   3U, 184U, 169U, 154U, 139U, 124U, 109U,  94U,  79U,
     64U,  49U,  34U,  19U,   4U, 185U, 170U, 155U, 140U, 125U, 110U,  95U,  80U,  65U,  50U,  35U,
     20U,   5U, 186U, 171U, 156U, 141U, 126U, 111U,  96U,  81U,  66U,  51U,  36U,  21U,   6U, 187U,
    172U, 157U, 142U, 127U, 112U,  97U,  82U,  67U,  52U,  37U,  22U,   7U, 188U, 173U, 158U, 143U,
    128U, 113U,  98U,  83U,  68U,  53U,  38U,  23U,   8U, 189U, 174U, 159U, 144U, 129U, 114U,  99U,
     84U,  69U,  54U,  39U,  24U,   9U, 190U, 175U, 160U, 145U, 130U, 115U, 100U,  85U,  70U,  55U,
     40U,  25U,  10U, 191U, 176U, 161U, 146U, 131U, 116U, 101U,  86U,  71U,  56U,  41U,  26U,  11U,
    192U, 177U, 162U, 147U, 132U, 117U, 102U,  87U,  72U,  57U,  42U,  27U,  12U, 193U, 178U, 163U,
    148U, 133U, 118U, 103U,  88U,  73U,  58U,  43U,  28U,  13U, 194U, 179U, 164U, 149U, 134U, 119U,
    104U,  89U,  74U,  59U,  44U,  29U,  14U, 195U, 180U, 165U, 150U, 135U, 120U, 105U,  90U,  75U,
     60U,  45U,  30U,  15U
};

// Hamming(13,9,3) decode – used for the 15 BPTC columns.
static bool hammingDecode1393(bool* d)
{
  bool c0 = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6];
  bool c1 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7];
  bool c2 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
  bool c3 = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8];

  uint8_t n = 0x00U;
  n |= (c0 != d[9])  ? 0x01U : 0x00U;
  n |= (c1 != d[10]) ? 0x02U : 0x00U;
  n |= (c2 != d[11]) ? 0x04U : 0x00U;
  n |= (c3 != d[12]) ? 0x08U : 0x00U;

  switch (n) {
    case 0x01U: d[9]  = !d[9];  return true;
    case 0x02U: d[10] = !d[10]; return true;
    case 0x04U: d[11] = !d[11]; return true;
    case 0x08U: d[12] = !d[12]; return true;
    case 0x0FU: d[0]  = !d[0];  return true;
    case 0x07U: d[1]  = !d[1];  return true;
    case 0x0EU: d[2]  = !d[2];  return true;
    case 0x05U: d[3]  = !d[3];  return true;
    case 0x0AU: d[4]  = !d[4];  return true;
    case 0x0DU: d[5]  = !d[5];  return true;
    case 0x03U: d[6]  = !d[6];  return true;
    case 0x06U: d[7]  = !d[7];  return true;
    case 0x0CU: d[8]  = !d[8];  return true;
    default: return false;
  }
}

// Hamming(15,11,3) variant #2 decode – used for the 9 BPTC rows.
static bool hammingDecode15113_2(bool* d)
{
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];

  uint8_t n = 0x00U;
  n |= (c0 != d[11]) ? 0x01U : 0x00U;
  n |= (c1 != d[12]) ? 0x02U : 0x00U;
  n |= (c2 != d[13]) ? 0x04U : 0x00U;
  n |= (c3 != d[14]) ? 0x08U : 0x00U;

  switch (n) {
    case 0x01U: d[11] = !d[11]; return true;
    case 0x02U: d[12] = !d[12]; return true;
    case 0x04U: d[13] = !d[13]; return true;
    case 0x08U: d[14] = !d[14]; return true;
    case 0x09U: d[0]  = !d[0];  return true;
    case 0x0BU: d[1]  = !d[1];  return true;
    case 0x0FU: d[2]  = !d[2];  return true;
    case 0x07U: d[3]  = !d[3];  return true;
    case 0x0EU: d[4]  = !d[4];  return true;
    case 0x05U: d[5]  = !d[5];  return true;
    case 0x0AU: d[6]  = !d[6];  return true;
    case 0x0DU: d[7]  = !d[7];  return true;
    case 0x03U: d[8]  = !d[8];  return true;
    case 0x06U: d[9]  = !d[9];  return true;
    case 0x0CU: d[10] = !d[10]; return true;
    default: return false;
  }
}

CBPTC19696::CBPTC19696()
{
}

// Decode a BPTC(196,96) codeword from a full 34-byte DMR frame buffer.
// frame points to the start of the 33-byte burst payload (skipping the control byte).
// This function extracts the 196 BPTC bits by skipping the 48-bit SYNC
// and 20-bit slot-type fields, exactly like MMDVMHost CBPTC19696::decode().
void CBPTC19696::decode(const uint8_t* frame, uint8_t* out)
{
  // Extract 196 BPTC bits from the 264-bit burst, skipping sync and slot type.
  // DMR voice header burst layout (bit positions within frame[1..33]):
  //   bits 0-97:   LC info part 1     (98 bits) → BPTC bits  0-97
  //   bits 98-107: Slot Type part 1   (10 bits) → SKIP
  //   bits 108-155: SYNC              (48 bits) → SKIP
  //   bits 156-165: Slot Type part 2  (10 bits) → SKIP
  //   bits 166-263: LC info part 2    (98 bits) → BPTC bits 98-195
  uint32_t bptcPos = 0U;

  // LC part 1: burst bits 0-97
  for (uint32_t i = 0U; i < 98U; i++) {
    uint32_t srcByte = i / 8U;
    uint32_t srcBit  = 7U - (i % 8U);
    m_rawData[bptcPos++] = (frame[srcByte] >> srcBit) & 1U;
  }
  // LC part 2: burst bits 166-263
  for (uint32_t i = 166U; i < 264U; i++) {
    uint32_t srcByte = i / 8U;
    uint32_t srcBit  = 7U - (i % 8U);
    m_rawData[bptcPos++] = (frame[srcByte] >> srcBit) & 1U;
  }

  deInterleave();
  errorCheck();
  extractData(out);
}

void CBPTC19696::deInterleave()
{
  for (uint32_t i = 0U; i < 196U; i++)
    m_deInterData[i] = m_rawData[INTERLEAVE_TABLE[i]];
}

void CBPTC19696::errorCheck()
{
  // Iterative row/column Hamming error correction (up to 5 passes).
  // Mirrors MMDVMHost CBPTC19696::decodeErrorCheck().
  bool fixing;
  uint32_t count = 0U;
  do {
    fixing = false;

    // 15 columns of Hamming(13,9,3).
    // Column c: data at m_deInterData[c+1 + r*15] for r=0..8 (9 bits),
    // parity at m_deInterData[c+1 + r*15] for r=9..12 (4 bits).
    bool col[13U];
    for (uint32_t c = 0U; c < 15U; c++) {
      uint32_t pos = c + 1U;
      for (uint32_t a = 0U; a < 13U; a++) {
        col[a] = m_deInterData[pos];
        pos += 15U;
      }
      if (hammingDecode1393(col)) {
        uint32_t wpos = c + 1U;
        for (uint32_t a = 0U; a < 13U; a++) {
          m_deInterData[wpos] = col[a];
          wpos += 15U;
        }
        fixing = true;
      }
    }

    // 9 rows of Hamming(15,11,3) variant #2.
    // Row r: bits at m_deInterData[r*15+1 .. r*15+15].
    for (uint32_t r = 0U; r < 9U; r++) {
      uint32_t pos = (r * 15U) + 1U;
      if (hammingDecode15113_2(m_deInterData + pos))
        fixing = true;
    }

    count++;
  } while (fixing && count < 5U);
}

// Encode 12 clean LC bytes into a BPTC(196,96) codeword and write the corrected
// payload bits back into the DMR burst frame (frame points to the 33-byte burst,
// i.e. frame[0..32], NOT the control byte).
// Only bits 0-97 (LC part 1) and 166-263 (LC part 2) are written; sync and slot
// type bits (98-165) are untouched.
void CBPTC19696::encode(const uint8_t* data, uint8_t* frame)
{
  // Unpack 96 info bits from the 12 input bytes
  bool bData[96U];
  for (uint32_t i = 0U; i < 96U; i++)
    bData[i] = (data[i / 8U] >> (7U - (i % 8U))) & 1U;

  // Clear the 196-bit de-interleaved matrix
  for (uint32_t i = 0U; i < 196U; i++)
    m_deInterData[i] = false;

  // Place info bits at their positions (reverse of extractData)
  uint32_t pos = 0U;
  for (uint32_t a = 4U;   a <= 11U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 16U;  a <= 26U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 31U;  a <= 41U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 46U;  a <= 56U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 61U;  a <= 71U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 76U;  a <= 86U;  a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 91U;  a <= 101U; a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 106U; a <= 116U; a++) m_deInterData[a] = bData[pos++];
  for (uint32_t a = 121U; a <= 131U; a++) m_deInterData[a] = bData[pos++];

  // Compute row Hamming(15,11,3) parities for rows 0-8
  // Row r occupies m_deInterData[r*15+1 .. r*15+15] (indices into row: d[0..14])
  // Parity equations from hammingDecode15113_2: d[11..14] = f(d[0..10])
  for (uint32_t r = 0U; r < 9U; r++) {
    bool* row = m_deInterData + (r * 15U + 1U);
    row[11] = row[0] ^ row[1] ^ row[2] ^ row[3] ^ row[5] ^ row[7] ^ row[8];
    row[12] = row[1] ^ row[2] ^ row[3] ^ row[4] ^ row[6] ^ row[8] ^ row[9];
    row[13] = row[2] ^ row[3] ^ row[4] ^ row[5] ^ row[7] ^ row[9] ^ row[10];
    row[14] = row[0] ^ row[1] ^ row[2] ^ row[4] ^ row[6] ^ row[7] ^ row[10];
  }

  // Compute column Hamming(13,9,3) parities for columns 0-14
  // Column c data bits (9 bits) are at m_deInterData[c+1 + r*15] for r=0..8
  // Column c parity bits (4 bits) are at m_deInterData[c+1 + r*15] for r=9..12
  // (i.e. at indices c+136, c+151, c+166, c+181)
  // Parity equations from hammingDecode1393: d[9..12] = f(d[0..8])
  for (uint32_t c = 0U; c < 15U; c++) {
    uint32_t base = c + 1U;
    bool d0 = m_deInterData[base];
    bool d1 = m_deInterData[base + 15U];
    bool d2 = m_deInterData[base + 30U];
    bool d3 = m_deInterData[base + 45U];
    bool d4 = m_deInterData[base + 60U];
    bool d5 = m_deInterData[base + 75U];
    bool d6 = m_deInterData[base + 90U];
    bool d7 = m_deInterData[base + 105U];
    bool d8 = m_deInterData[base + 120U];
    m_deInterData[base + 135U] = d0 ^ d1 ^ d3 ^ d5 ^ d6;
    m_deInterData[base + 150U] = d0 ^ d1 ^ d2 ^ d4 ^ d6 ^ d7;
    m_deInterData[base + 165U] = d0 ^ d1 ^ d2 ^ d3 ^ d5 ^ d7 ^ d8;
    m_deInterData[base + 180U] = d0 ^ d2 ^ d4 ^ d5 ^ d8;
  }

  // Re-interleave: m_rawData[INTERLEAVE_TABLE[i]] = m_deInterData[i]
  for (uint32_t i = 0U; i < 196U; i++)
    m_rawData[INTERLEAVE_TABLE[i]] = m_deInterData[i];

  // Write corrected BPTC bits into the burst frame.
  // LC part 1: burst bits 0-97  → frame[0..12] (bit positions 0-97)
  // LC part 2: burst bits 166-263 → frame[20..32] (bit positions 166-263)
  // Sync (108-155) and slot type (98-107, 156-165) are NOT touched.
  uint32_t bptcPos = 0U;
  for (uint32_t i = 0U; i < 98U; i++) {
    uint32_t dstByte = i / 8U;
    uint32_t dstBit  = 7U - (i % 8U);
    if (m_rawData[bptcPos++])
      frame[dstByte] |= (1U << dstBit);
    else
      frame[dstByte] &= ~(uint8_t)(1U << dstBit);
  }
  for (uint32_t i = 166U; i < 264U; i++) {
    uint32_t dstByte = i / 8U;
    uint32_t dstBit  = 7U - (i % 8U);
    if (m_rawData[bptcPos++])
      frame[dstByte] |= (1U << dstBit);
    else
      frame[dstByte] &= ~(uint8_t)(1U << dstBit);
  }
}

void CBPTC19696::extractData(uint8_t* data) const
{
  // Extract the 96 information bits from the de-interleaved 196-bit matrix.
  // Bit layout per ETSI TS 102 361-1 Table B.2:
  //   Position 0:    R(3) – excluded
  //   Positions 1-3: R(2..0) – reserved (excluded from info data)
  //   Row 0 info: positions 4-11 (8 bits)
  //   Rows 1-8 info: positions 16-26, 31-41, ..., 121-131 (11 bits each)
  bool bData[96U];
  uint32_t pos = 0U;

  for (uint32_t a = 4U;   a <= 11U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 16U;  a <= 26U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 31U;  a <= 41U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 46U;  a <= 56U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 61U;  a <= 71U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 76U;  a <= 86U;  a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 91U;  a <= 101U; a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 106U; a <= 116U; a++, pos++) bData[pos] = m_deInterData[a];
  for (uint32_t a = 121U; a <= 131U; a++, pos++) bData[pos] = m_deInterData[a];

  // Pack 96 bits into 12 bytes (big-endian)
  for (uint32_t i = 0U; i < 12U; i++) {
    data[i] = 0U;
    for (uint32_t j = 0U; j < 8U; j++) {
      if (bData[i * 8U + j])
        data[i] |= (1U << (7U - j));
    }
  }
}
