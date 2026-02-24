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
// Per ETSI TS 102 361-1 Section 7.2.6: a(k) = (181 * k) mod 196
// deInterData[k] = rawData[a(k)]
const uint32_t INTERLEAVE_TABLE[196] = {
    0U, 181U, 166U, 151U, 136U, 121U, 106U,  91U,  76U,  61U,  46U,  31U,  16U,   1U, 182U, 167U, 152U, 137U, 122U, 107U,
   92U,  77U,  62U,  47U,  32U,  17U,   2U, 183U, 168U, 153U, 138U, 123U, 108U,  93U,  78U,  63U,  48U,  33U,  18U,   3U,
  184U, 169U, 154U, 139U, 124U, 109U,  94U,  79U,  64U,  49U,  34U,  19U,   4U, 185U, 170U, 155U, 140U, 125U, 110U,  95U,
   80U,  65U,  50U,  35U,  20U,   5U, 186U, 171U, 156U, 141U, 126U, 111U,  96U,  81U,  66U,  51U,  36U,  21U,   6U, 187U,
  172U, 157U, 142U, 127U, 112U,  97U,  82U,  67U,  52U,  37U,  22U,   7U, 188U, 173U, 158U, 143U, 128U, 113U,  98U,  83U,
   68U,  53U,  38U,  23U,   8U, 189U, 174U, 159U, 144U, 129U, 114U,  99U,  84U,  69U,  54U,  39U,  24U,   9U, 190U, 175U,
  160U, 145U, 130U, 115U, 100U,  85U,  70U,  55U,  40U,  25U,  10U, 191U, 176U, 161U, 146U, 131U, 116U, 101U,  86U,  71U,
   56U,  41U,  26U,  11U, 192U, 177U, 162U, 147U, 132U, 117U, 102U,  87U,  72U,  57U,  42U,  27U,  12U, 193U, 178U, 163U,
  148U, 133U, 118U, 103U,  88U,  73U,  58U,  43U,  28U,  13U, 194U, 179U, 164U, 149U, 134U, 119U, 104U,  89U,  74U,  59U,
   44U,  29U,  14U, 195U, 180U, 165U, 150U, 135U, 120U, 105U,  90U,  75U,  60U,  45U,  30U,  15U
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
  // Column is a shortened Hamming(15,11) code -> Hamming(13,9)
  for (uint32_t c = 0U; c < 15U; c++) {
    bool data[15U];
    data[0] = false; // d0
    data[1] = false; // d1
    for (uint32_t i = 0U; i < 13U; i++)
      data[i + 2] = m_deInterData[c + i * 15U];

    if (!hamming1511(data)) {
      hamming1503(data);
      for (uint32_t i = 0U; i < 13U; i++)
        m_deInterData[c + i * 15U] = data[i + 2];
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
