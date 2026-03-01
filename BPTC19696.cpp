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
// ETSI TS 102 361-1 Section B.3.9 defines the FORWARD (encoding) permutation as:
//   e[k] = raw[(181 * k) mod 196]
// The INVERSE (decoding) permutation is:
//   raw[j] = e[(13 * j) mod 196]   because 13 * 181 ≡ 1 (mod 196)
// This table implements the decoding direction: INTERLEAVE_TABLE[k] = (13 * k) mod 196
const uint32_t INTERLEAVE_TABLE[196] = {
      0U,  13U,  26U,  39U,  52U,  65U,  78U,  91U, 104U, 117U, 130U, 143U, 156U, 169U, 182U, 195U,
     12U,  25U,  38U,  51U,  64U,  77U,  90U, 103U, 116U, 129U, 142U, 155U, 168U, 181U, 194U,  11U,
     24U,  37U,  50U,  63U,  76U,  89U, 102U, 115U, 128U, 141U, 154U, 167U, 180U, 193U,  10U,  23U,
     36U,  49U,  62U,  75U,  88U, 101U, 114U, 127U, 140U, 153U, 166U, 179U, 192U,   9U,  22U,  35U,
     48U,  61U,  74U,  87U, 100U, 113U, 126U, 139U, 152U, 165U, 178U, 191U,   8U,  21U,  34U,  47U,
     60U,  73U,  86U,  99U, 112U, 125U, 138U, 151U, 164U, 177U, 190U,   7U,  20U,  33U,  46U,  59U,
     72U,  85U,  98U, 111U, 124U, 137U, 150U, 163U, 176U, 189U,   6U,  19U,  32U,  45U,  58U,  71U,
     84U,  97U, 110U, 123U, 136U, 149U, 162U, 175U, 188U,   5U,  18U,  31U,  44U,  57U,  70U,  83U,
     96U, 109U, 122U, 135U, 148U, 161U, 174U, 187U,   4U,  17U,  30U,  43U,  56U,  69U,  82U,  95U,
    108U, 121U, 134U, 147U, 160U, 173U, 186U,   3U,  16U,  29U,  42U,  55U,  68U,  81U,  94U, 107U,
    120U, 133U, 146U, 159U, 172U, 185U,   2U,  15U,  28U,  41U,  54U,  67U,  80U,  93U, 106U, 119U,
    132U, 145U, 158U, 171U, 184U,   1U,  14U,  27U,  40U,  53U,  66U,  79U,  92U, 105U, 118U, 131U,
    144U, 157U, 170U, 183U
};

CBPTC19696::CBPTC19696()
{
}

// Decode a BPTC(196,96) codeword from a full 34-byte DMR frame buffer.
// frame[0] is the control byte; frame[1..33] are the 33 burst bytes.
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
  bool fixing;
  uint32_t count = 0U;
  do {
    fixing = false;

    // 9 rows of Hamming(15,11,3)
    for (uint32_t r = 0U; r < 9U; r++) {
      uint32_t offset = r * 15U;
      if (!hamming1511(m_deInterData + offset)) {
        hamming1503(m_deInterData + offset);
        fixing = true;
      }
    }

    // 15 columns of shortened Hamming(13,9,3) derived from Hamming(15,11,3)
    // by fixing d[0]=d[1]=0 (shortened code).
    // Column c has data bits at m_deInterData[c + r*15] for r=0..8,
    // and column parity bits at m_deInterData[135 + k*15 + c] for k=0..3.
    for (uint32_t c = 0U; c < 15U; c++) {
      bool data[15U];
      data[0U] = false;  // shortened
      data[1U] = false;  // shortened
      for (uint32_t r = 0U; r < 9U; r++)
        data[r + 2U] = m_deInterData[c + r * 15U];
      data[11U] = m_deInterData[135U + 0U * 15U + c];
      data[12U] = m_deInterData[135U + 1U * 15U + c];
      data[13U] = m_deInterData[135U + 2U * 15U + c];
      data[14U] = m_deInterData[135U + 3U * 15U + c];

      if (!hamming1511(data)) {
        hamming1503(data);
        fixing = true;
        for (uint32_t r = 0U; r < 9U; r++)
          m_deInterData[c + r * 15U] = data[r + 2U];
        m_deInterData[135U + 0U * 15U + c] = data[11U];
        m_deInterData[135U + 1U * 15U + c] = data[12U];
        m_deInterData[135U + 2U * 15U + c] = data[13U];
        m_deInterData[135U + 3U * 15U + c] = data[14U];
      }
    }

    count++;
  } while (fixing && count < 5U);
}

bool CBPTC19696::hamming1511(bool* d) const
{
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8] ^ d[11];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9] ^ d[12];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10] ^ d[13];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10] ^ d[14];
  return !c0 && !c1 && !c2 && !c3;
}

void CBPTC19696::hamming1503(bool* d) const
{
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8] ^ d[11];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9] ^ d[12];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10] ^ d[13];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10] ^ d[14];

  uint8_t n = 0U;
  n |= c0 ? 0x01U : 0x00U;
  n |= c1 ? 0x02U : 0x00U;
  n |= c2 ? 0x04U : 0x00U;
  n |= c3 ? 0x08U : 0x00U;

  switch (n) {
    case 0x09U: d[0]  = !d[0];  break;
    case 0x0BU: d[1]  = !d[1];  break;
    case 0x0FU: d[2]  = !d[2];  break;
    case 0x07U: d[3]  = !d[3];  break;
    case 0x0EU: d[4]  = !d[4];  break;
    case 0x05U: d[5]  = !d[5];  break;
    case 0x0AU: d[6]  = !d[6];  break;
    case 0x0DU: d[7]  = !d[7];  break;
    case 0x03U: d[8]  = !d[8];  break;
    case 0x06U: d[9]  = !d[9];  break;
    case 0x0CU: d[10] = !d[10]; break;
    case 0x01U: d[11] = !d[11]; break;
    case 0x02U: d[12] = !d[12]; break;
    case 0x04U: d[13] = !d[13]; break;
    case 0x08U: d[14] = !d[14]; break;
    default: break;
  }
}

void CBPTC19696::extractData(uint8_t* data) const
{
  // Extract the 96 information bits from the 9×15 de-interleaved matrix.
  // Each of the 9 rows has 11 data bits (positions 0-10) and 4 parity bits (11-14).
  // Row 8 carries only 8 information bits (positions 0-7).
  bool bData[96U];
  uint32_t pos = 0U;

  for (uint32_t r = 0U; r < 8U; r++) {
    for (uint32_t b = 0U; b < 11U; b++)
      bData[pos++] = m_deInterData[r * 15U + b];
  }
  for (uint32_t b = 0U; b < 8U; b++)
    bData[pos++] = m_deInterData[8U * 15U + b];

  // Pack 96 bits into 12 bytes (big-endian)
  for (uint32_t i = 0U; i < 12U; i++) {
    data[i] = 0U;
    for (uint32_t j = 0U; j < 8U; j++) {
      if (bData[i * 8U + j])
        data[i] |= (1U << (7U - j));
    }
  }
}
