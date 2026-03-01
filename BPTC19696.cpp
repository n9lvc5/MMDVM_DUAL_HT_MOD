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

// BPTC (196,96) de-interleave table
// ETSI TS 102 361-1 Section B.3.9 defines the FORWARD (encoding) permutation as:
//   e[k] = raw[(181 * k) mod 196]
// The INVERSE (decoding) permutation is therefore:
//   raw[j] = e[(13 * j) mod 196]   because 13 = 181^-1 mod 196  (181 * 13 ≡ 1 mod 196)
// This table implements the decoding direction: INTERLEAVE_TABLE[k] = (13 * k) mod 196
const uint32_t INTERLEAVE_TABLE[196] = {
    0U,  13U,  26U,  39U,  52U,  65U,  78U,  91U, 104U, 117U, 130U, 143U, 156U, 169U, 182U, 195U,  12U,  25U,  38U,  51U,
   64U,  77U,  90U, 103U, 116U, 129U, 142U, 155U, 168U, 181U, 194U,  11U,  24U,  37U,  50U,  63U,  76U,  89U, 102U, 115U,
  128U, 141U, 154U, 167U, 180U, 193U,  10U,  23U,  36U,  49U,  62U,  75U,  88U, 101U, 114U, 127U, 140U, 153U, 166U, 179U,
  192U,   9U,  22U,  35U,  48U,  61U,  74U,  87U, 100U, 113U, 126U, 139U, 152U, 165U, 178U, 191U,   8U,  21U,  34U,  47U,
   60U,  73U,  86U,  99U, 112U, 125U, 138U, 151U, 164U, 177U, 190U,   7U,  20U,  33U,  46U,  59U,  72U,  85U,  98U, 111U,
  124U, 137U, 150U, 163U, 176U, 189U,   6U,  19U,  32U,  45U,  58U,  71U,  84U,  97U, 110U, 123U, 136U, 149U, 162U, 175U,
  188U,   5U,  18U,  31U,  44U,  57U,  70U,  83U,  96U, 109U, 122U, 135U, 148U, 161U, 174U, 187U,   4U,  17U,  30U,  43U,
   56U,  69U,  82U,  95U, 108U, 121U, 134U, 147U, 160U, 173U, 186U,   3U,  16U,  29U,  42U,  55U,  68U,  81U,  94U, 107U,
  120U, 133U, 146U, 159U, 172U, 185U,   2U,  15U,  28U,  41U,  54U,  67U,  80U,  93U, 106U, 119U, 132U, 145U, 158U, 171U,
  184U,   1U,  14U,  27U,  40U,  53U,  66U,  79U,  92U, 105U, 118U, 131U, 144U, 157U, 170U, 183U
};

CBPTC19696::CBPTC19696()
{
}

void CBPTC19696::decode(const uint8_t* in, uint8_t* out)
{
  // Extract 196 bits from input bytes
  for (uint32_t i = 0U; i < 196U; i++) {
    uint32_t bytePos = i / 8U;
    uint32_t bitPos = 7U - (i % 8U);
    m_rawData[i] = (in[bytePos] & (1U << bitPos)) != 0;
  }

  // De-interleave
  deInterleave();

  // Error check and correction
  errorCheck();

  // Extract 96-bit data
  extractData(out);
}

void CBPTC19696::deInterleave()
{
  for (uint32_t i = 0U; i < 196U; i++)
    m_deInterData[i] = m_rawData[INTERLEAVE_TABLE[i]];
}

void CBPTC19696::errorCheck()
{
  // Run through each of the 9 rows containing data
  for (uint32_t r = 0U; r < 9U; r++) {
    uint32_t offset = r * 15U;
    if (!hamming1511(m_deInterData + offset))
      hamming1503(m_deInterData + offset);
  }

  // Run through each of the 15 columns containing data
  // BPTC(196,96) layout:
  //   Bits 0-134:   9 rows x 15 bits = row matrix (data + row Hamming parities)
  //   Bits 135-194: 15 columns x 4 parity bits = column parity section
  //   Bit 195:      reserved
  // Column c uses shortened Hamming(13,9,3) from H(15,11,3) with d[0]=d[1]=0.
  // Data bits come from rows 0-8 (m_deInterData[c + r*15] for r=0..8).
  // Parity bits come from m_deInterData[135 + c*4 + 0..3].
  for (uint32_t c = 0U; c < 15U; c++) {
    bool data[15U];
    data[0U] = false;  // shortened - always 0
    data[1U] = false;  // shortened - always 0
    // 9 data bits: one from each row, column c
    for (uint32_t r = 0U; r < 9U; r++)
      data[r + 2U] = m_deInterData[c + r * 15U];
    // 4 column parity bits from the dedicated parity section
    data[11U] = m_deInterData[135U + c * 4U + 0U];
    data[12U] = m_deInterData[135U + c * 4U + 1U];
    data[13U] = m_deInterData[135U + c * 4U + 2U];
    data[14U] = m_deInterData[135U + c * 4U + 3U];

    if (!hamming1511(data)) {
      hamming1503(data);
      // Write back corrected column data
      for (uint32_t r = 0U; r < 9U; r++)
        m_deInterData[c + r * 15U] = data[r + 2U];
      // Write back corrected column parities
      m_deInterData[135U + c * 4U + 0U] = data[11U];
      m_deInterData[135U + c * 4U + 1U] = data[12U];
      m_deInterData[135U + c * 4U + 2U] = data[13U];
      m_deInterData[135U + c * 4U + 3U] = data[14U];
    }
  }
}

bool CBPTC19696::hamming1511(bool* d) const
{
  // Check a Hamming (15,11,3) block with systematic parity
  // Codeword: [d0 d1 d2 d3 d4 d5 d6 d7 d8 d9 d10 p0 p1 p2 p3]
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8] ^ d[11];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9] ^ d[12];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10] ^ d[13];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10] ^ d[14];

  return !c0 && !c1 && !c2 && !c3;
}

void CBPTC19696::hamming1503(bool* d) const
{
  // Calculate the Hamming (15,11,3) error syndrome
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8] ^ d[11];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9] ^ d[12];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10] ^ d[13];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10] ^ d[14];

  uint8_t n = 0U;
  n |= (c0 ? 0x01U : 0x00U);
  n |= (c1 ? 0x02U : 0x00U);
  n |= (c2 ? 0x04U : 0x00U);
  n |= (c3 ? 0x08U : 0x00U);

  // Correct single-bit errors using syndrome mapping for the DMR systematic matrix
  switch (n) {
    case 0x09U: d[0] = !d[0]; break;
    case 0x0BU: d[1] = !d[1]; break;
    case 0x0FU: d[2] = !d[2]; break;
    case 0x07U: d[3] = !d[3]; break;
    case 0x0EU: d[4] = !d[4]; break;
    case 0x05U: d[5] = !d[5]; break;
    case 0x0AU: d[6] = !d[6]; break;
    case 0x0DU: d[7] = !d[7]; break;
    case 0x03U: d[8] = !d[8]; break;
    case 0x06U: d[9] = !d[9]; break;
    case 0x0CU: d[10] = !d[10]; break;
    case 0x01U: d[11] = !d[11]; break;
    case 0x02U: d[12] = !d[12]; break;
    case 0x04U: d[13] = !d[13]; break;
    case 0x08U: d[14] = !d[14]; break;
    default: break; // No correction needed or uncorrectable
  }
}

void CBPTC19696::extractData(uint8_t* data) const
{
  // Extract the 96 bits of data from the de-interleaved and error-corrected data
  // BPTC (196,96) has 96 information bits.
  // According to ETSI TS 102 361-1, these are in 8 rows of 11 bits and 1 row of 8 bits.
  bool bData[96U];
  uint32_t pos = 0U;
  
  for (uint32_t a = 0U; a < 8U; a++) {
    for (uint32_t b = 0U; b < 11U; b++) {
      if (pos < 96U)
        bData[pos++] = m_deInterData[a * 15U + b];
    }
  }
  
  // Last row (row 8) has only 8 bits of information
  for (uint32_t b = 0U; b < 8U; b++) {
    if (pos < 96U)
      bData[pos++] = m_deInterData[8U * 15U + b];
  }

  // Convert 96 bits to 12 bytes
  for (uint32_t i = 0U; i < 12U; i++) {
    data[i] = 0U;
    for (uint32_t j = 0U; j < 8U; j++) {
      if (bData[i * 8U + j])
        data[i] |= (1U << (7U - j));
    }
  }
}
