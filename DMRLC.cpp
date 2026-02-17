/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "DMRLC.h"
#include "BPTC19696.h"
#include "RS129.h"
#include "DMRDefines.h"

#include <string.h>

bool CDMRLC::decode(const uint8_t* data, uint8_t dataType, DMRLC_T* lc)
{
  // Extract 196-bit encoded LC data from frame
  uint8_t encoded[25]; // 196 bits = 24.5 bytes, round up to 25
  extractData(data, encoded);

#if defined(ENABLE_DEBUG)
  // Debug: Show first few bytes of encoded LC
  DEBUG2I("Encoded LC [0-3]", (encoded[0] << 24) | (encoded[1] << 16) | (encoded[2] << 8) | encoded[3]);
#endif

  // BPTC(196,96) decode to get 12-byte LC
  CBPTC19696 bptc;
  bptc.decode(encoded, lc->rawData);

#if defined(ENABLE_DEBUG)
  // Debug: Show decoded LC before mask
  DEBUG2I("Decoded LC [0-3]", (lc->rawData[0] << 24) | (lc->rawData[1] << 16) | (lc->rawData[2] << 8) | lc->rawData[3]);
#endif

  // Apply CRC mask based on data type
  applyMask(lc->rawData, dataType);

  // Reed-Solomon check
  if (!CRS129::check(lc->rawData)) {
#if defined(ENABLE_DEBUG)
    DEBUG2("LC RS check failed", 0);
#endif
    return false;
  }

  // Extract LC fields
  lc->PF = (lc->rawData[0U] & 0x80U) != 0;
  lc->R  = (lc->rawData[0U] & 0x40U) != 0;
  lc->FLCO = lc->rawData[0U] & 0x3FU;
  
  lc->FID = lc->rawData[1U];
  lc->options = lc->rawData[2U];
  
  // Destination ID (Talkgroup) - 3 bytes, big-endian
  lc->dstId = ((uint32_t)lc->rawData[3U] << 16) |
              ((uint32_t)lc->rawData[4U] << 8) |
              ((uint32_t)lc->rawData[5U]);
  
  // Source ID (Caller DMR ID) - 3 bytes, big-endian
  lc->srcId = ((uint32_t)lc->rawData[6U] << 16) |
              ((uint32_t)lc->rawData[7U] << 8) |
              ((uint32_t)lc->rawData[8U]);

  return true;
}

void CDMRLC::extractData(const uint8_t* frame, uint8_t* lcData)
{
  // DMR frame structure: 33 bytes (264 bits)
  // frame[0] = control byte
  // frame[1-33] = DMR payload: [108 bits Info1][48 bits SYNC][108 bits Info2]
  // LC data (196 bits) is in the Info sections
  
  // Clear output
  memset(lcData, 0x00U, 25U);
  
  uint32_t bitPos = 0U;
  
  // Extract bits from Info1 (bytes 1-13 of frame, before SYNC at bytes 14-19)
  for (uint32_t i = 1U; i <= 13U; i++) {
    for (uint32_t j = 0U; j < 8U; j++) {
      bool bit = (frame[i] & (1U << (7U - j))) != 0;
      if (bitPos < 196U) {
        uint32_t byteIdx = bitPos / 8U;
        uint32_t bitIdx = 7U - (bitPos % 8U);
        if (bit)
          lcData[byteIdx] |= (1U << bitIdx);
        bitPos++;
      }
    }
  }
  
  // Skip SYNC (bytes 14-19, which is 48 bits)
  
  // Extract bits from Info2 (bytes 20-33 of frame)
  for (uint32_t i = 20U; i <= 33U; i++) {
    for (uint32_t j = 0U; j < 8U; j++) {
      bool bit = (frame[i] & (1U << (7U - j))) != 0;
      if (bitPos < 196U) {
        uint32_t byteIdx = bitPos / 8U;
        uint32_t bitIdx = 7U - (bitPos % 8U);
        if (bit)
          lcData[byteIdx] |= (1U << bitIdx);
        bitPos++;
      }
    }
  }
}

void CDMRLC::applyMask(uint8_t* data, uint8_t dataType)
{
  switch (dataType) {
    case DT_VOICE_LC_HEADER:
      data[9U]  ^= VOICE_LC_HEADER_CRC_MASK[0U];
      data[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
      data[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
      break;
      
    case DT_TERMINATOR_WITH_LC:
      data[9U]  ^= TERMINATOR_WITH_LC_CRC_MASK[0U];
      data[10U] ^= TERMINATOR_WITH_LC_CRC_MASK[1U];
      data[11U] ^= TERMINATOR_WITH_LC_CRC_MASK[2U];
      break;
      
    default:
      break;
  }
}
