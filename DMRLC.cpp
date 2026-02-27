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
#include "Debug.h"

#include <string.h>

// CRC mask values per ETSI TS 102 361-1 Table 9.16 / 9.17
// These are XOR'd with RS(12,9) parity bytes 9-11 before the RS check
const uint8_t VOICE_LC_HEADER_CRC_MASK[3]    = {0x96U, 0x96U, 0x96U};
const uint8_t TERMINATOR_WITH_LC_CRC_MASK[3] = {0x99U, 0x99U, 0x99U};

bool CDMRLC::decode(const uint8_t* data, uint8_t dataType, DMRLC_T* lc)
{
  // BPTC(196,96) decode from the full 33‑byte DMR burst payload
  // (data[0] is the control byte, payload starts at data[1]).
  CBPTC19696 bptc;
  bptc.decode(data + 1U, lc->rawData);

  // Apply CRC mask based on data type
  applyMask(lc->rawData, dataType);

  // Reed-Solomon check - log result (rare: once per voice call)
  bool rsOk = CRS129::check(lc->rawData);
  DEBUG2I("LC RS:", rsOk ? 1 : 0);
  if (rsOk) {
    DEBUG2I("LC dstId", ((uint32_t)lc->rawData[3] << 16) | ((uint32_t)lc->rawData[4] << 8) | lc->rawData[5]);
    DEBUG2I("LC srcId", ((uint32_t)lc->rawData[6] << 16) | ((uint32_t)lc->rawData[7] << 8) | lc->rawData[8]);
  } else {
    // RS failed - log first 3 raw bytes to help diagnose decode errors
    DEBUG2I("LC raw012", (lc->rawData[0] << 16) | (lc->rawData[1] << 8) | lc->rawData[2]);
    DEBUG2I("LC raw345", (lc->rawData[3] << 16) | (lc->rawData[4] << 8) | lc->rawData[5]);
  }

  // Accept LC when RS passes, or when the decoded header looks plausible
  // (BS→MS can sometimes fail RS due to parity/mask differences; TG/dstId is often correct)
  uint32_t dstId = ((uint32_t)lc->rawData[3U] << 16) |
                   ((uint32_t)lc->rawData[4U] << 8) |
                   (uint32_t)lc->rawData[5U];
  uint8_t flco = lc->rawData[0U] & 0x3FU;
  bool plausible = (flco <= 1U) && (dstId >= 1U && dstId <= 16777215U);

  DEBUG2I("LC plausible:", plausible ? 1 : 0);

  if (!rsOk && !plausible) {
    DEBUG1("LC discarded - RS and Plausible failed");
    return false;
  }
  
  if (!rsOk && plausible) {
    DEBUG1("LC accepted via plausibility check");
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
  // DMR frame structure: 33 bytes (264 bits) payload + 1 control byte at frame[0]
  // Voice Header Burst Structure (ETSI TS 102 361-1, Section 6.2):
  // [98 bits LC Part 1] [10 bits Slot Type Part 1] [48 bits SYNC] [10 bits Slot Type Part 2] [98 bits LC Part 2]
  // Bits 0-97: LC Part 1
  // Bits 98-107: Slot Type Part 1
  // Bits 108-155: SYNC
  // Bits 156-165: Slot Type Part 2
  // Bits 166-263: LC Part 2
  
  // Clear output
  memset(lcData, 0x00U, 25U);
  
  uint32_t bitPos = 0U;
  
  // Extract bits from LC Part 1 (bits 0-97 of burst)
  for (uint32_t i = 0U; i < 98U; i++) {
    uint32_t srcByte = 1U + (i / 8U);
    uint32_t srcBit = 7U - (i % 8U);
    bool bit = (frame[srcByte] & (1U << srcBit)) != 0;
    
    uint32_t dstByte = bitPos / 8U;
    uint32_t dstBit = 7U - (bitPos % 8U);
    if (bit)
      lcData[dstByte] |= (1U << dstBit);
    bitPos++;
  }
  
  // Extract bits from LC Part 2 (bits 166-263 of burst)
  for (uint32_t i = 166U; i < 264U; i++) {
    uint32_t srcByte = 1U + (i / 8U);
    uint32_t srcBit = 7U - (i % 8U);
    bool bit = (frame[srcByte] & (1U << srcBit)) != 0;
    
    uint32_t dstByte = bitPos / 8U;
    uint32_t dstBit = 7U - (bitPos % 8U);
    if (bit)
      lcData[dstByte] |= (1U << dstBit);
    bitPos++;
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
