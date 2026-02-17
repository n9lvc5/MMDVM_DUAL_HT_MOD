/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "RS129.h"

// GF(2^6) multiplication table
const uint8_t MULT_TABLE[64][64] = {
  // This is a simplified implementation
  // Full table would be 4096 bytes - using algorithmic approach instead
};

bool CRS129::check(const uint8_t* in)
{
  // Reed-Solomon (12,9) check
  // 12 bytes total: 9 data bytes + 3 parity bytes
  
  uint8_t syndromes[3];
  syndromes[0] = 0U;
  syndromes[1] = 0U;
  syndromes[2] = 0U;

  // Calculate syndromes
  for (uint32_t i = 0U; i < 12U; i++) {
    uint8_t val = in[i];
    
    // Syndrome 0: Sum of all bytes (alpha^0)
    syndromes[0] = gf6Add(syndromes[0], val);
    
    // Syndrome 1: Weighted sum (alpha^1)
    uint8_t weight1 = 1U;
    for (uint32_t j = 0U; j < i; j++)
      weight1 = gf6Mult(weight1, 2U); // alpha = 2 in GF(2^6)
    syndromes[1] = gf6Add(syndromes[1], gf6Mult(val, weight1));
    
    // Syndrome 2: Weighted sum (alpha^2)
    uint8_t weight2 = 1U;
    for (uint32_t j = 0U; j < i; j++)
      weight2 = gf6Mult(weight2, 4U); // alpha^2 = 4 in GF(2^6)
    syndromes[2] = gf6Add(syndromes[2], gf6Mult(val, weight2));
  }

  // If all syndromes are zero, no errors detected
  return (syndromes[0] == 0U) && (syndromes[1] == 0U) && (syndromes[2] == 0U);
}

uint8_t CRS129::gf6Mult(uint8_t a, uint8_t b)
{
  // Galois Field GF(2^6) multiplication
  // Primitive polynomial: x^6 + x^4 + x^3 + x + 1 (0x4B)
  
  uint8_t result = 0U;
  
  for (uint32_t i = 0U; i < 6U; i++) {
    if ((b & 1U) != 0)
      result ^= a;
    
    bool highBit = (a & 0x20U) != 0;
    a <<= 1;
    if (highBit)
      a ^= 0x4BU; // Apply primitive polynomial
    
    b >>= 1;
  }
  
  return result & 0x3FU;
}

uint8_t CRS129::gf6Add(uint8_t a, uint8_t b)
{
  // Addition in GF(2^6) is XOR
  return (a ^ b) & 0x3FU;
}
