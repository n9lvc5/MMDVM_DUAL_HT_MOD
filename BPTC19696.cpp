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

// BPTC (196,96) interleaver pattern
const uint32_t INTERLEAVE_TABLE[196] = {
    0U,   1U,   8U,   9U,  16U,  17U,  24U,  25U,  32U,  33U,  40U,  41U,  48U,  49U,  56U,  57U,  64U,  65U,  72U,  73U,
   80U,  81U,  88U,  89U,  96U,  97U, 104U, 105U, 112U, 113U, 120U, 121U, 128U, 129U, 136U, 137U, 144U, 145U, 152U, 153U,
  160U, 161U, 168U, 169U, 176U, 177U, 184U, 185U, 192U, 193U,   2U,   3U,  10U,  11U,  18U,  19U,  26U,  27U,  34U,  35U,
   42U,  43U,  50U,  51U,  58U,  59U,  66U,  67U,  74U,  75U,  82U,  83U,  90U,  91U,  98U,  99U, 106U, 107U, 114U, 115U,
  122U, 123U, 130U, 131U, 138U, 139U, 146U, 147U, 154U, 155U, 162U, 163U, 170U, 171U, 178U, 179U, 186U, 187U, 194U, 195U,
    4U,   5U,  12U,  13U,  20U,  21U,  28U,  29U,  36U,  37U,  44U,  45U,  52U,  53U,  60U,  61U,  68U,  69U,  76U,  77U,
   84U,  85U,  92U,  93U, 100U, 101U, 108U, 109U, 116U, 117U, 124U, 125U, 132U, 133U, 140U, 141U, 148U, 149U, 156U, 157U,
  164U, 165U, 172U, 173U, 180U, 181U, 188U, 189U,   6U,   7U,  14U,  15U,  22U,  23U,  30U,  31U,  38U,  39U,  46U,  47U,
   54U,  55U,  62U,  63U,  70U,  71U,  78U,  79U,  86U,  87U,  94U,  95U, 102U, 103U, 110U, 111U, 118U, 119U, 126U, 127U,
  134U, 135U, 142U, 143U, 150U, 151U, 158U, 159U, 166U, 167U, 174U, 175U, 182U, 183U, 190U, 191U
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
  for (uint32_t c = 0U; c < 15U; c++) {
    bool data[13U];
    for (uint32_t i = 0U; i < 13U; i++)
      data[i] = m_deInterData[c + i * 15U];

    if (!hamming1511(data)) {
      hamming1503(data);
      for (uint32_t i = 0U; i < 13U; i++)
        m_deInterData[c + i * 15U] = data[i];
    }
  }
}

bool CBPTC19696::hamming1511(bool* d) const
{
  // Check a Hamming (15,11,3) block
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];

  return !c0 && !c1 && !c2 && !c3;
}

void CBPTC19696::hamming1503(bool* d) const
{
  // Calculate the Hamming (15,11,3) error syndrome
  bool c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
  bool c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
  bool c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
  bool c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];

  uint8_t n = 0U;
  n |= (c0 ? 0x01U : 0x00U);
  n |= (c1 ? 0x02U : 0x00U);
  n |= (c2 ? 0x04U : 0x00U);
  n |= (c3 ? 0x08U : 0x00U);

  // Correct single-bit errors
  switch (n) {
    case 0x01U: d[0] = !d[0]; break;
    case 0x02U: d[1] = !d[1]; break;
    case 0x03U: d[2] = !d[2]; break;
    case 0x04U: d[3] = !d[3]; break;
    case 0x05U: d[4] = !d[4]; break;
    case 0x06U: d[5] = !d[5]; break;
    case 0x07U: d[6] = !d[6]; break;
    case 0x08U: d[7] = !d[7]; break;
    case 0x09U: d[8] = !d[8]; break;
    case 0x0AU: d[9] = !d[9]; break;
    case 0x0BU: d[10] = !d[10]; break;
    default: break; // No correction needed or uncorrectable
  }
}

void CBPTC19696::extractData(uint8_t* data) const
{
  // Extract the 96 bits of data from the de-interleaved and error-corrected data
  bool bData[96U];
  uint32_t pos = 0U;
  
  for (uint32_t a = 0U; a < 9U; a++) {
    for (uint32_t b = 0U; b < 11U; b++) {
      bData[pos++] = m_deInterData[a * 15U + b];
    }
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
