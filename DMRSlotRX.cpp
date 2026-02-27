/*
 *   Copyright (C) 2009-2017 by Jonathan Naylor G4KLX
 *   Copyright (C) 2017,2018 by Andy Uribe CA6JAU
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "Config.h"

#if defined(DUPLEX)

#include "Globals.h"
#include "DMRSlotRX.h"
#include "DMRSlotType.h"
#include "DMRLC.h"
#include "Utils.h"
#include <string.h>

const uint8_t MAX_SYNC_BYTES_ERRS   = 3U;

const uint8_t MAX_SYNC_LOST_FRAMES  = 30U;

const uint16_t NOENDPTR = 9999U;

const uint8_t CONTROL_NONE  = 0x00U;
const uint8_t CONTROL_VOICE = 0x20U;
const uint8_t CONTROL_DATA  = 0x40U;

const uint8_t BIT_MASK_TABLE[] = {0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U};

#define WRITE_BIT1(p,i,b) p[(i)>>3] = (b) ? (p[(i)>>3] | BIT_MASK_TABLE[(i)&7]) : (p[(i)>>3] & ~BIT_MASK_TABLE[(i)&7])
#define READ_BIT1(p,i)    ((p[(i)>>3] & BIT_MASK_TABLE[(i)&7]) >> (7 - ((i)&7)))

CDMRSlotRX::CDMRSlotRX() :
m_slot(false),
m_patternBuffer(0x00U),
m_buffer(),
m_dataPtr(0U),
m_syncPtr(0U),
m_startPtr(0U),
m_endPtr(NOENDPTR),
m_control(CONTROL_NONE),
m_delayPtr(0U),
m_colorCode(0U),
m_delay(0U)
#if defined(MS_MODE)
  ,m_currentSlot(1U),
  m_slotTimer(0U),
  m_syncLocked(false),
  m_foundSyncInWindow(false),
  m_phaseInconsistentCount(0U)
#endif
{
  for (uint8_t i = 0U; i < 2U; i++) {
    m_syncCount[i] = 0U;
    m_state[i] = DMRRXS_NONE;
    m_n[i] = 0U;
    m_type[i] = 0U;
#if defined(MS_MODE)
    m_lcValid[i] = false;
#endif
  }
}

void CDMRSlotRX::start(bool slot)
{
  m_slot = slot;
  m_delayPtr = 0U;
}

void CDMRSlotRX::reset()
{
  m_dataPtr   = 0U;
  m_delayPtr  = 0U;
  m_patternBuffer = 0U;

  m_syncPtr   = 0U;
  m_control   = CONTROL_NONE;
  m_startPtr  = 0U;
  m_endPtr    = NOENDPTR;
  
  for (uint8_t i = 0U; i < 2U; i++) {
    m_syncCount[i] = 0U;
    m_state[i]     = DMRRXS_NONE;
    m_n[i]         = 0U;
    m_type[i]      = 0U;
#if defined(MS_MODE)
    m_lcValid[i] = false;
#endif
  }

#if defined(MS_MODE)
  m_currentSlot = 1U;
  m_slotTimer = 0U;
  m_syncLocked = false;
  m_foundSyncInWindow = false;
  m_phaseInconsistentCount = 0U;
  memset(m_lcData, 0, sizeof(m_lcData));
#endif
}

bool CDMRSlotRX::databit(bool bit)
{
  uint16_t min, max;

  m_delayPtr++;
  if (m_delayPtr < m_delay) {
#if defined(MS_MODE)
    uint8_t slot = m_currentSlot - 1U;
#else
    uint8_t slot = m_slot ? 1U : 0U;
#endif
    return (m_state[slot] != DMRRXS_NONE || m_control != CONTROL_NONE);
  }

  WRITE_BIT1(m_buffer, m_dataPtr, bit);

  m_patternBuffer <<= 1;
  if (bit)
    m_patternBuffer |= 0x01U;
    
#if defined(MS_MODE)
  // Slot timing logic for MS mode - wrap every 30ms (288 bits)
  m_slotTimer++;
  if (m_slotTimer >= 288U)
    m_slotTimer = 0U;
#endif
  
#if defined(MS_MODE)
  uint8_t slot_idx = m_currentSlot - 1U;
#else
  uint8_t slot_idx = m_slot ? 1U : 0U;
#endif

#if defined(MS_MODE)
  bool inWindow = false;
  if (m_syncLocked) {
    // Window for Slot 1 sync or Slot 2 sync (288 bits apart)
    uint16_t expected1 = m_syncPtr;
    uint16_t expected2 = (m_syncPtr + 288U) % DMR_BUFFER_LENGTH_BITS;

    uint16_t diff1 = (m_dataPtr >= expected1) ? (m_dataPtr - expected1) : (expected1 - m_dataPtr);
    if (diff1 > DMR_BUFFER_LENGTH_BITS / 2) diff1 = DMR_BUFFER_LENGTH_BITS - diff1;

    uint16_t diff2 = (m_dataPtr >= expected2) ? (m_dataPtr - expected2) : (expected2 - m_dataPtr);
    if (diff2 > DMR_BUFFER_LENGTH_BITS / 2) diff2 = DMR_BUFFER_LENGTH_BITS - diff2;

    // Check if we are inside a +/- 5 bit window around expected sync
    if (diff1 <= 5 || diff2 <= 5) {
       inWindow = true;
    } else {
       // Just exited a window, reset the flag for the next one
       m_foundSyncInWindow = false;
    }
  }

  // Only correlate if not locked, or if in window and we haven't found a sync yet in THIS window
  if (!m_syncLocked || (inWindow && !m_foundSyncInWindow)) {
    correlateSync();
  }
#else
  if (m_state[slot_idx] == DMRRXS_NONE) {
    correlateSync();
  } else {
    min = m_syncPtr + DMR_BUFFER_LENGTH_BITS - 2;
    max = m_syncPtr + 2;

    if (min >= DMR_BUFFER_LENGTH_BITS)
      min -= DMR_BUFFER_LENGTH_BITS;
    if (max >= DMR_BUFFER_LENGTH_BITS)
      max -= DMR_BUFFER_LENGTH_BITS;

    if (min < max) {
      if (m_dataPtr >= min && m_dataPtr <= max)
        correlateSync();
    } else {
      if (m_dataPtr >= min || m_dataPtr <= max)
        correlateSync();
    }
  }
#endif

  procSlot2();

#if defined(MS_MODE)
  if (m_syncLocked && m_slotTimer == 132U) {
    decodeCACH();
  }
#endif

  m_dataPtr++;

  if (m_dataPtr >= DMR_BUFFER_LENGTH_BITS)
    m_dataPtr = 0U;

#if defined(MS_MODE)
  if (m_syncLocked)
    return true;
#endif

  return (m_state[slot_idx] != DMRRXS_NONE || m_control != CONTROL_NONE);
}

void CDMRSlotRX::procSlot2()
{
#if defined(MS_MODE)
  uint8_t slot = m_currentSlot - 1U;
#else
  uint8_t slot = m_slot ? 1U : 0U;
#endif

  if (m_dataPtr == m_endPtr) {
    // [debug removed - high frequency]
    // [debug removed - high frequency]
    frame[0U] = m_control;

    bitsToBytes(m_startPtr, DMR_FRAME_LENGTH_BYTES, frame + 1U);

#if defined(MS_MODE)
    // MS_MODE: Transpose BS sync to MS sync so Pi-Star/MMDVMHost recognizes it
    if (m_control != CONTROL_NONE) {
      const uint8_t* msSync = (m_control == CONTROL_VOICE) ? DMR_MS_VOICE_SYNC_BYTES : DMR_MS_DATA_SYNC_BYTES;
      frame[14] = (frame[14] & 0xF0U) | (msSync[0] & 0x0FU);
      frame[15] = msSync[1];
      frame[16] = msSync[2];
      frame[17] = msSync[3];
      frame[18] = msSync[4];
      frame[19] = msSync[5];
      frame[20] = (msSync[6] & 0xF0U) | (frame[20] & 0x0FU);
      // [debug removed - high frequency]
    }
#endif

    if (m_control == CONTROL_DATA) {
      // Data sync
      uint8_t colorCode;
      uint8_t dataType;
      CDMRSlotType slotType;
      slotType.decode(frame + 1U, colorCode, dataType);

      // [debug removed - high frequency]
      // [debug removed - high frequency]

#if defined(ENABLE_DEBUG)
      static uint8_t lastColorCode = 0xFF;
      if (colorCode != lastColorCode) {
        DEBUG2I("DMR CC detected:", colorCode);
        lastColorCode = colorCode;
      }
#endif

      if (colorCode == m_colorCode || m_colorCode == 0U) {
        m_syncCount[slot] = 0U;
        m_n[slot]         = 0U;

        frame[0U] |= dataType;

        switch (dataType) {
          case DT_DATA_HEADER:
            // [debug removed - high frequency]
#if !defined(MS_MODE)
            writeRSSIData();
            m_state[slot] = DMRRXS_DATA;
            m_type[slot]  = 0x00U;
#endif
            break;
          case DT_RATE_12_DATA:
          case DT_RATE_34_DATA:
          case DT_RATE_1_DATA:
#if !defined(MS_MODE)
            if (m_state[slot] == DMRRXS_DATA) {
              // [debug removed - high frequency]
              writeRSSIData();
              m_type[slot] = dataType;
            }
#endif
            break;
          case DT_VOICE_LC_HEADER:
            DEBUG3("DMRSlotRX: voice header found CC/Slot", colorCode, slot + 1);
            m_state[slot] = DMRRXS_VOICE;
#if defined(MS_MODE)
            m_state[slot ^ 1U] = DMRRXS_NONE;  // Only one slot active at a time
#endif
            {
              // [debug removed - high frequency]
              
              // Extract and embed Link Control (LC) data in the frame
              DMRLC_T lc;
              
              bool lcValid = CDMRLC::decode(frame, DT_VOICE_LC_HEADER, &lc);
              // Note: writeDMRStart removed - MMDVMHost decodes LC itself from the voice header burst
              
#if defined(ENABLE_DEBUG)
              if (lcValid) {
                DEBUG2I("LC decoded - SrcID", lc.srcId);
                DEBUG2I("LC decoded - DstID", lc.dstId);
              } else {
                DEBUG2("LC decode failed", slot);
              }
#endif
              
              // Store LC data for embedding in voice frames
#if defined(MS_MODE)
              if (lcValid) {
                memcpy(m_lcData, lc.rawData, 12);
                m_lcValid[slot] = true;
                // [debug removed - high frequency]
              } else {
                m_lcValid[slot] = false;
              }
#endif
              
              // MMDVMHost decodes the LC itself from the encoded burst.
              // We do NOT overwrite the payload bits here.
                
              DEBUG2("DMRSlotRX: Sending voice header to MMDVMHost", slot);
              writeRSSIData();
            }
            break;
          case DT_VOICE_PI_HEADER:
            if (m_state[slot] == DMRRXS_VOICE) {
              // [debug removed - high frequency]
              writeRSSIData();
            }
            m_state[slot] = DMRRXS_VOICE;
            break;
          case DT_TERMINATOR_WITH_LC:
            if (m_state[slot] == DMRRXS_VOICE) {
              DEBUG2("DMRSlotRX: voice terminator found pos", m_syncPtr);
              {
                // [debug removed - high frequency]
                // Extract and embed Link Control (LC) data in the terminator frame
                DMRLC_T lc;
                
                bool lcValid = CDMRLC::decode(frame, DT_TERMINATOR_WITH_LC, &lc);
                
#if defined(ENABLE_DEBUG)
                if (lcValid) {
                  DEBUG2I("Terminator LC decoded - SrcID", lc.srcId);
                  DEBUG2I("Terminator LC decoded - DstID", lc.dstId);
                } else {
                  DEBUG2("Terminator LC decode failed", slot);
                }
#endif
                
                // Note: writeDMRStart removed - MMDVMHost doesn't implement message type 0x1D
                
                DEBUG2("DMRSlotRX: Sending voice terminator to MMDVMHost", slot);
                writeRSSIData();
              }
              m_state[slot]  = DMRRXS_NONE;
#if !defined(MS_MODE)
              m_endPtr = NOENDPTR;
#endif
            }
            break;
          default:    // DT_CSBK
            // [debug removed - high frequency]
#if !defined(MS_MODE)
            writeRSSIData();
#endif
            m_state[slot]  = DMRRXS_NONE;
#if !defined(MS_MODE)
            m_endPtr = NOENDPTR;
#endif
            break;
        }
      } else {
#if defined(ENABLE_DEBUG)
        static uint8_t lastFailedCC = 0xFF;
        if (colorCode != lastFailedCC) {
          DEBUG3("DMRSlotRX: CC mismatch - expected/got", m_colorCode, colorCode);
          lastFailedCC = colorCode;
        }
#endif
      }
    } else if (m_control == CONTROL_VOICE) {
      // Voice sync found (frames B/C/D/E/F in a superframe have CONTROL_VOICE with no slot type)
      // In MS_MODE: only emit to MMDVMHost if we already have a valid voice header
      // (i.e. m_state is already DMRRXS_VOICE). Never set DMRRXS_VOICE here directly;
      // that is done exclusively when DT_VOICE_LC_HEADER is decoded, so MMDVMHost
      // receives the header BEFORE any voice payload frames.
#if defined(MS_MODE)
      if (m_state[slot] == DMRRXS_VOICE) {
        // Already in a call - this is a mid-superframe voice sync, send it
        writeRSSIData();
      }
      // else: discard - MMDVMHost hasn't seen the header yet
#else
      writeRSSIData();
      m_state[slot] = DMRRXS_VOICE;
#endif
      m_syncCount[slot] = 0U;
      m_n[slot]         = 0U;
    } else {
#if defined(MS_MODE)
      if (m_syncLocked) {
        m_syncCount[slot]++;
        if (m_syncCount[slot] >= MAX_SYNC_LOST_FRAMES) {
          DEBUG2("DMRSlotRX: TS timeout in MS_MODE", m_syncCount[slot]);
          serial.writeDMRLost(slot);
          m_state[slot] = DMRRXS_NONE;
          m_syncCount[slot] = 0;
          // Do NOT call reset() here, keep the flywheel lock!
        }
      }
#else
      if (m_state[slot] != DMRRXS_NONE) {
        m_syncCount[slot]++;
        if (m_syncCount[slot] >= MAX_SYNC_LOST_FRAMES) {
          serial.writeDMRLost(slot);
          reset();
        }
      }
#endif

      if (m_state[slot] == DMRRXS_VOICE) {
        if (m_n[slot] >= 5U) {
          frame[0U] = CONTROL_VOICE;
          m_n[slot] = 0U;
        } else {
          frame[0U] = ++m_n[slot];
        }

        // [debug removed - high frequency]
        // [debug removed - high frequency]
        
        // Do NOT overwrite vocoder data with LC data.
        
        // [debug removed - high frequency]
#if defined(MS_MODE)
        if (m_syncLocked)
          serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#else
        serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#endif
      } else if (m_state[slot] == DMRRXS_DATA) {
        if (m_type[slot] != 0x00U) {
          frame[0U] = CONTROL_DATA | m_type[slot];
          writeRSSIData();
        }
      }
    }

    // End of this slot, reset some items for the next slot.
    m_control = CONTROL_NONE;

#if defined(MS_MODE)
    // Advance end pointer for next slot (flywheel)
    m_endPtr = (m_endPtr + 288U) % DMR_BUFFER_LENGTH_BITS;
    // Toggle slot
    m_currentSlot = (m_currentSlot == 1U) ? 2U : 1U;
    // [debug removed - high frequency]
#endif
  }
}

void CDMRSlotRX::correlateSync()
{
#if defined(MS_MODE)
  uint8_t slot_idx = m_currentSlot - 1U;
#else
  uint8_t slot_idx = m_slot ? 1U : 0U;
#endif

  uint16_t syncPtr;
  uint16_t startPtr;
  uint16_t endPtr;
  uint8_t  control = CONTROL_NONE;

#if defined(ENABLE_DEBUG)
  static uint32_t debugCounter = 0;
  static uint32_t totalSyncs = 0;
  debugCounter++;
  if (debugCounter == 50000) {
    // [debug removed - high frequency]
    // [debug removed - high frequency]
    // [debug removed - high frequency]
    debugCounter = 0;
  }
#endif

  if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_DATA_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_VOICE_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_VOICE;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_DATA_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_VOICE_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_VOICE;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
  }

  if (control != CONTROL_NONE) {
#if defined(ENABLE_DEBUG)
    totalSyncs++;
#endif
#if defined(MS_MODE)
    m_foundSyncInWindow = true;
    // Set sync lock when we find a BS sync pattern
    if (!m_syncLocked) {
      DEBUG2("DMRSlotRX: Initial lock at pos", m_dataPtr);
      m_currentSlot = 1U; // Initial lock, assume slot 1
      m_syncLocked = true;
    }
    // Update m_slotTimer relative to the burst we just saw
    m_slotTimer = 0U;
#endif
    syncPtr = m_dataPtr;

    startPtr = m_dataPtr + DMR_BUFFER_LENGTH_BITS - DMR_SLOT_TYPE_LENGTH_BITS / 2U - DMR_INFO_LENGTH_BITS / 2U - DMR_SYNC_LENGTH_BITS + 1;
    if (startPtr >= DMR_BUFFER_LENGTH_BITS)
      startPtr -= DMR_BUFFER_LENGTH_BITS;

    endPtr = m_dataPtr + DMR_SLOT_TYPE_LENGTH_BITS / 2U + DMR_INFO_LENGTH_BITS / 2U;
    if (endPtr >= DMR_BUFFER_LENGTH_BITS)
      endPtr -= DMR_BUFFER_LENGTH_BITS;

    m_syncPtr = syncPtr;
    m_startPtr = startPtr;
    m_endPtr = endPtr;
    m_control = control;
    
    // [debug removed - high frequency]
    
#if defined(MS_MODE)
    // In MS_MODE, any valid sync from the BS resets the watchdog for both slots
    m_syncCount[0U] = 0U;
    m_syncCount[1U] = 0U;
#else
    if (m_state[slot_idx] == DMRRXS_NONE) {
      m_syncCount[slot_idx] = 0U; // If we are idle, reset the sync counter as well
    }
#endif
    // Only reset the state if we are not in the middle of a voice or data call
    if (m_state[slot_idx] != DMRRXS_VOICE && m_state[slot_idx] != DMRRXS_DATA) {
        m_state[slot_idx] = DMRRXS_NONE;
    }
    m_endPtr = endPtr;
  }
}

void CDMRSlotRX::decodeCACH()
{
  uint16_t cachStartPtr = (m_syncPtr + 109U) % DMR_BUFFER_LENGTH_BITS;

  bool c[24];
  for (uint8_t i = 0; i < 24; i++) {
    c[i] = READ_BIT1(m_buffer, (cachStartPtr + i) % DMR_BUFFER_LENGTH_BITS);
  }

  // TACT bits - interleaved in first 16 bits of CACH
  bool t[7];
  t[0] = c[0];  // AT
  t[1] = c[1];  // TC
  t[2] = c[5];  // LCSS1
  t[3] = c[6];  // LCSS0
  t[4] = c[10]; // H2
  t[5] = c[11]; // H1
  t[6] = c[15]; // H0

  // Hamming(7,4) check
  bool s0 = t[0] ^ t[1] ^ t[2] ^ t[4];
  bool s1 = t[1] ^ t[2] ^ t[3] ^ t[5];
  bool s2 = t[0] ^ t[1] ^ t[3] ^ t[6];

  uint8_t s = (s2 << 2) | (s1 << 1) | s0;
  if (s != 0) {
    // Single bit error correction
    switch (s) {
      case 5: t[0] = !t[0]; break;
      case 7: t[1] = !t[1]; break;
      case 3: t[2] = !t[2]; break;
      case 6: t[3] = !t[3]; break;
      case 1: t[4] = !t[4]; break;
      case 2: t[5] = !t[5]; break;
      case 4: t[6] = !t[6]; break;
      default:
        DEBUG2I("DMRSlotRX: CACH Multi-bit error syndrome", s);
        return;
    }
  }

  uint8_t tc = t[1] ? 2U : 1U;
  if (m_currentSlot != tc) {
    m_phaseInconsistentCount++;
    if (m_phaseInconsistentCount >= 2U) {
       DEBUG2I("DMRSlotRX: Phase corrected to Slot", tc);
       m_currentSlot = tc;
       m_phaseInconsistentCount = 0U;
    }
  } else {
    m_phaseInconsistentCount = 0U;
  }
}

void CDMRSlotRX::bitsToBytes(uint16_t start, uint8_t count, uint8_t* buffer)
{
  for (uint8_t i = 0U; i < count; i++) {
    buffer[i]  = 0U;
    buffer[i] |= READ_BIT1(m_buffer, start) << 7;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 6;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 5;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 4;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 3;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 2;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 1;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
    buffer[i] |= READ_BIT1(m_buffer, start) << 0;
    start++;
    if (start >= DMR_BUFFER_LENGTH_BITS)
      start -= DMR_BUFFER_LENGTH_BITS;
  }
}

void CDMRSlotRX::setColorCode(uint8_t colorCode)
{
  m_colorCode = colorCode;
}

void CDMRSlotRX::setDelay(uint8_t delay)
{
  m_delay = delay / 5;
}

void CDMRSlotRX::writeRSSIData()
{
#if defined(MS_MODE)
  // In MS_MODE, use our slot timing instead of m_slot
  uint8_t slot = m_currentSlot - 1U;
#else
  uint8_t slot = m_slot ? 1U : 0U;
#endif
  
#if defined(SEND_RSSI_DATA)
  uint16_t rssi = io.readRSSI();

  frame[34U] = (rssi >> 8) & 0xFFU;
  frame[35U] = (rssi >> 0) & 0xFFU;

#if defined(MS_MODE)
  // [debug removed - high frequency]
  if (m_syncLocked) {
    // [debug removed - high frequency]
    serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);
  } else {
    // [debug removed - high frequency]
  }
#else
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);
#endif
#else
#if defined(MS_MODE)
  // [debug removed - high frequency]
  if (m_syncLocked) {
    // [debug removed - high frequency]
    serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
  } else {
    // [debug removed - high frequency]
  }
#else
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#endif
#endif
}

#endif
