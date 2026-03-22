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

// CRC mask values per ETSI TS 102 361-1 Section 9.2.5 and Table 9.16
// ===================================================================
// Per ETSI TS 102 361-1 Section 9.2.5 (Link Control Data):
// "The 12-byte LC is protected by RS(12,9) Reed-Solomon code (3 check bytes).
//  Before transmission, the CRC check bytes (positions 9-11) are masked with
//  a 24-bit pattern that depends on the LC type (Voice Header vs Terminator)."
//
// CRC Masking is applied AFTER RS encoding and BEFORE transmission:
//   1. Encode 12 LC bytes with RS(12,9) → bytes 0-8 (info), bytes 9-11 (RS check)
//   2. XOR mask bytes 9-11 with type-specific mask value
//   3. Transmit masked LC over the air via BPTC(196,96) + interleaving
//
// Decoding (this direction) reverses the process:
//   1. Receive BPTC bits, de-interleave to get 12 bytes (with mask applied)
//   2. Remove mask by XOR'ing bytes 9-11 again (XOR is self-inverse)
//   3. Verify RS(12,9) check on unmasked data (bytes 9-11 must match RS check)
//   4. Extract LC fields (bytes 0-8) which are unmasked
//
// Mask values (Table 9.16):
const uint8_t VOICE_LC_HEADER_CRC_MASK[3]    = {0x96U, 0x96U, 0x96U};      // Voice LC Header: 0x969696
const uint8_t TERMINATOR_WITH_LC_CRC_MASK[3] = {0x99U, 0x99U, 0x99U};      // Terminator w/ LC: 0x999999

bool CDMRLC::decode(const uint8_t* data, uint8_t dataType, DMRLC_T* lc)
{
  // BPTC(196,96) decode from the full 33‑byte DMR burst payload
  // (data[0] is the control byte, payload starts at data[1]).
  // After BPTC decode, lc->rawData has the 12-byte LC with CRC mask ALREADY APPLIED
  CBPTC19696 bptc;
  bptc.decode(data + 1U, lc->rawData);

  // Remove CRC mask from bytes 9-11 (ETSI TS 102 361-1 Section 9.2.5)
  // The mask was applied at transmission; we remove it to compute RS check
  // Note: XOR is self-inverse; XOR'ing again removes the mask
  applyMask(lc->rawData, dataType);

  // Reed-Solomon(12,9) check on unmasked data (bytes 0-11, check uses bytes 9-11)
  // ETSI TS 102 361-1 Section 9.2.5: RS check is computed on the LC BEFORE mask application
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

  if (!rsOk) {
    DEBUG1("LC discarded - RS failed");
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

  // Sanity check for valid DMR ID range (1 to 16,777,215)
  if (lc->dstId == 0U || lc->dstId > 16777215U || lc->srcId == 0U || lc->srcId > 16777215U) {
    DEBUG1("LC discarded - Invalid ID range");
    return false;
  }

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
    uint32_t srcByte = i / 8U;
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
    uint32_t srcByte = i / 8U;
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
  // Apply (or remove) CRC mask for RS(12,9) check bytes (positions 9-11)
  // Per ETSI TS 102 361-1 Section 9.2.5, Table 9.16:
  //
  // Since XOR is self-inverse (A XOR M XOR M = A), this function is used both for:
  //   A) Decoding (receiver): Remove mask before RS check
  //   B) Re-encoding (transmitter): Re-apply mask for forwarding to host
  //
  // The mask pattern depends on LC type (Voice Header vs Terminator with LC)
  switch (dataType) {
    case DT_VOICE_LC_HEADER:
      // Voice LC Header mask: 0x96, 0x96, 0x96 (Table 9.16)
      data[9U]  ^= VOICE_LC_HEADER_CRC_MASK[0U];
      data[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
      data[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
      break;

    case DT_TERMINATOR_WITH_LC:
      // Terminator with LC mask: 0x99, 0x99, 0x99 (Table 9.16)
      data[9U]  ^= TERMINATOR_WITH_LC_CRC_MASK[0U];
      data[10U] ^= TERMINATOR_WITH_LC_CRC_MASK[1U];
      data[11U] ^= TERMINATOR_WITH_LC_CRC_MASK[2U];
      break;

    default:
      // Other data types have no LC, so no mask to apply
      break;
  }
}
