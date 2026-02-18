/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#if !defined(DMRLC_H)
#define DMRLC_H

#include <stdint.h>

// DMR Link Control structure
struct DMRLC_T {
  bool     PF;         // Priority Flag
  bool     R;          // Reserved
  uint8_t  FLCO;       // Full LC Opcode
  uint8_t  FID;        // Feature ID
  uint8_t  options;    // Service Options
  uint32_t dstId;      // Destination ID (Talkgroup)
  uint32_t srcId;      // Source ID (Caller DMR ID)
  uint8_t  rawData[12];// Raw 12-byte LC data
};

class CDMRLC
{
public:
  static bool decode(const uint8_t* data, uint8_t dataType, DMRLC_T* lc);
  static void extractData(const uint8_t* frame, uint8_t* lcData);

private:
  static void applyMask(uint8_t* data, uint8_t dataType);
};

// CRC masks for different data types
const uint8_t VOICE_LC_HEADER_CRC_MASK[3]    = {0x96U, 0x96U, 0x96U};
const uint8_t TERMINATOR_WITH_LC_CRC_MASK[3] = {0x99U, 0x99U, 0x99U};

#endif
