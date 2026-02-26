/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
 *   Adapted from OpenGD77 and MMDVMHost for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "BPTC19696.h"

#include <string.h>
#include <assert.h>

// Local helpers for bit/byte conversion (big‑endian within a byte)
static void byteToBitsBE(uint8_t byte, bool* bits)
{
  for (uint8_t i = 0U; i < 8U; i++)
    bits[i] = (byte & (1U << (7U - i))) != 0U;
}

static void bitsToByteBE(const bool* bits, uint8_t& byte)
{
  byte = 0U;
  for (uint8_t i = 0U; i < 8U; i++) {
    if (bits[i])
      byte |= (1U << (7U - i));
  }
}

// Minimal Hamming helpers copied from MMDVMHost's CHamming,
// restricted to the variants used by the BPTC(196,96) decoder.

// Hamming (13,9,3) decode
static bool hammingDecode1393(bool* d)
{
  assert(d != nullptr);

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
    // Parity bit errors
    case 0x01U: d[9]  = !d[9];  return true;
    case 0x02U: d[10] = !d[10]; return true;
    case 0x04U: d[11] = !d[11]; return true;
    case 0x08U: d[12] = !d[12]; return true;

    // Data bit errors
    case 0x0FU: d[0] = !d[0]; return true;
    case 0x07U: d[1] = !d[1]; return true;
    case 0x0EU: d[2] = !d[2]; return true;
    case 0x05U: d[3] = !d[3]; return true;
    case 0x0AU: d[4] = !d[4]; return true;
    case 0x0DU: d[5] = !d[5]; return true;
    case 0x03U: d[6] = !d[6]; return true;
    case 0x06U: d[7] = !d[7]; return true;
    case 0x0CU: d[8] = !d[8]; return true;

    // No bit errors
    default: return false;
  }
}

// Hamming (15,11,3) decode variant #2 used by BPTC rows
static bool hammingDecode15113_2(bool* d)
{
  assert(d != nullptr);

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
    // Parity bit errors
    case 0x01U: d[11] = !d[11]; return true;
    case 0x02U: d[12] = !d[12]; return true;
    case 0x04U: d[13] = !d[13]; return true;
    case 0x08U: d[14] = !d[14]; return true;

    // Data bit errors
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

    // No bit errors
    default: return false;
  }
}

CBPTC19696::CBPTC19696()
{
}

// Decode a BPTC(196,96) codeword from a 33‑byte DMR burst payload.
// This follows the same bit mapping and error‑correction strategy
// as MMDVMHost's CBPTC19696::decode().
void CBPTC19696::decode(const uint8_t* in, uint8_t* out)
{
  assert(in  != nullptr);
  assert(out != nullptr);

  decodeExtractBinary(in);
  decodeDeInterleave();
  decodeErrorCheck();
  decodeExtractData(out);
}

// Map the 33‑byte DMR burst payload into the 196‑bit raw BPTC matrix.
// Layout taken directly from MMDVMHost BPTC19696::decodeExtractBinary().
void CBPTC19696::decodeExtractBinary(const uint8_t* in)
{
  assert(in != nullptr);

  // First 12 bytes -> 96 bits
  byteToBitsBE(in[0U],  m_rawData + 0U);
  byteToBitsBE(in[1U],  m_rawData + 8U);
  byteToBitsBE(in[2U],  m_rawData + 16U);
  byteToBitsBE(in[3U],  m_rawData + 24U);
  byteToBitsBE(in[4U],  m_rawData + 32U);
  byteToBitsBE(in[5U],  m_rawData + 40U);
  byteToBitsBE(in[6U],  m_rawData + 48U);
  byteToBitsBE(in[7U],  m_rawData + 56U);
  byteToBitsBE(in[8U],  m_rawData + 64U);
  byteToBitsBE(in[9U],  m_rawData + 72U);
  byteToBitsBE(in[10U], m_rawData + 80U);
  byteToBitsBE(in[11U], m_rawData + 88U);
  byteToBitsBE(in[12U], m_rawData + 96U);

  // Two LC bits carried in the SYNC area (byte 20)
  bool bits[8U];
  byteToBitsBE(in[20U], bits);
  m_rawData[98U] = bits[6U];
  m_rawData[99U] = bits[7U];

  // Remaining bytes 21–32 map to bits 100–195
  byteToBitsBE(in[21U], m_rawData + 100U);
  byteToBitsBE(in[22U], m_rawData + 108U);
  byteToBitsBE(in[23U], m_rawData + 116U);
  byteToBitsBE(in[24U], m_rawData + 124U);
  byteToBitsBE(in[25U], m_rawData + 132U);
  byteToBitsBE(in[26U], m_rawData + 140U);
  byteToBitsBE(in[27U], m_rawData + 148U);
  byteToBitsBE(in[28U], m_rawData + 156U);
  byteToBitsBE(in[29U], m_rawData + 164U);
  byteToBitsBE(in[30U], m_rawData + 172U);
  byteToBitsBE(in[31U], m_rawData + 180U);
  byteToBitsBE(in[32U], m_rawData + 188U);
}

// De‑interleave using the ETSI permutation e[k] = raw[(181*k) mod 196]
void CBPTC19696::decodeDeInterleave()
{
  for (uint32_t i = 0U; i < 196U; i++)
    m_deInterData[i] = false;

  for (uint32_t a = 0U; a < 196U; a++) {
    uint32_t interleaveSequence = (a * 181U) % 196U;
    m_deInterData[a] = m_rawData[interleaveSequence];
  }
}

// Row/column Hamming error‑correction; mirrors MMDVMHost.
void CBPTC19696::decodeErrorCheck()
{
  bool fixing;
  uint32_t count = 0U;

  do {
    fixing = false;

    // Columns: 15 columns of Hamming(13,9,3)
    bool col[13U];
    for (uint32_t c = 0U; c < 15U; c++) {
      uint32_t pos = c + 1U;
      for (uint32_t a = 0U; a < 13U; a++) {
        col[a] = m_deInterData[pos];
        pos   += 15U;
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

    // Rows: 9 rows of Hamming(15,11,3) variant #2
    for (uint32_t r = 0U; r < 9U; r++) {
      uint32_t pos = (r * 15U) + 1U;
      if (hammingDecode15113_2(m_deInterData + pos))
        fixing = true;
    }

    count++;
  } while (fixing && count < 5U);
}

// Extract 96 information bits as 12 LC bytes, as in MMDVMHost.
void CBPTC19696::decodeExtractData(uint8_t* data) const
{
  assert(data != nullptr);

  bool bData[96U];
  uint32_t pos = 0U;

  // The information bits are spread over specific positions in the
  // 196‑bit matrix. This mapping is taken from MMDVMHost.
  for (uint32_t a = 4U; a <= 11U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 16U; a <= 26U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 31U; a <= 41U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 46U; a <= 56U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 61U; a <= 71U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 76U; a <= 86U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 91U; a <= 101U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 106U; a <= 116U; a++, pos++)
    bData[pos] = m_deInterData[a];

  for (uint32_t a = 121U; a <= 131U; a++, pos++)
    bData[pos] = m_deInterData[a];

  // Pack into 12 bytes
  for (uint32_t i = 0U; i < 12U; i++)
    bitsToByteBE(bData + (i * 8U), data[i]);
}

