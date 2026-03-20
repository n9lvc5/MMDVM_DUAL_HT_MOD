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
#include "BPTC19696.h"
#include "Utils.h"
#include <string.h>
#include <stdio.h>

const uint8_t MAX_SYNC_BYTES_ERRS   = 3U;

const uint8_t MAX_SYNC_LOST_FRAMES  = 13U;

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
m_inverted(false),
m_delayPtr(0U),
m_colorCode(0U),
m_delay(0U)
#if defined(MS_MODE)
  ,m_currentSlot(1U),
  m_slotTimer(0U),
  m_syncLocked(false),
  m_slotHysteresis(0U)
#endif
{
  for (uint8_t i = 0U; i < 2U; i++) {
    m_syncCount[i] = 0U;
    m_state[i] = DMRRXS_NONE;
    m_n[i] = 0U;
    m_type[i] = 0U;
    m_callStartMs[i] = 0U;
    m_callActive[i]  = false;

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
  m_inverted  = false;
  m_startPtr  = 0U;
  m_endPtr    = NOENDPTR;
  
  for (uint8_t i = 0U; i < 2U; i++) {
    m_syncCount[i] = 0U;
    m_state[i]     = DMRRXS_NONE;
    m_n[i]         = 0U;
    m_type[i]      = 0U;
    m_callStartMs[i] = 0U;
    m_callActive[i]  = false;
#if defined(MS_MODE)
    m_lcValid[i] = false;
#endif
  }

#if defined(MS_MODE)
  m_currentSlot = 1U;
  m_slotTimer = 0U;
  m_syncLocked = false;
  m_slotHysteresis = 0U;
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
  // Slot timing logic for MS mode
  m_slotTimer++;
  if (m_slotTimer >= 288U)
    m_slotTimer = 0U;
#endif
  
#if defined(MS_MODE)
  uint8_t slot_idx = m_currentSlot - 1U;
#else
  uint8_t slot_idx = m_slot ? 1U : 0U;
#endif

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
  
    frame[0U] = m_control;

    bitsToBytes(m_startPtr, DMR_FRAME_LENGTH_BYTES, frame + 1U);

#if defined(MS_MODE)
    // Transpose BS sync to MS sync so MMDVMHost recognizes the traffic.
    // The 48-bit sync pattern starts at bit 108 of the 264-bit burst.
    // In our 34-byte frame buffer (frame[0]=control, frame[1..33]=payload), 
    // bit 108 corresponds to byte index 14 (108/8 = 13.5).
    if (m_control != CONTROL_NONE) {
      uint8_t msSync[DMR_SYNC_BYTES_LENGTH];
      if (m_control == CONTROL_VOICE) {
        memcpy(msSync, DMR_MS_VOICE_SYNC_BYTES, DMR_SYNC_BYTES_LENGTH);
      } else {
        memcpy(msSync, DMR_MS_DATA_SYNC_BYTES, DMR_SYNC_BYTES_LENGTH);
      }
      if (m_inverted) {
        for (uint8_t i = 0U; i < DMR_SYNC_BYTES_LENGTH; i++) {
          msSync[i] ^= DMR_SYNC_BYTES_MASK[i];
        }
      }
      // Sync starts at bit 108 of the 264-bit burst (bit 4 of byte 13). frame[14] is byte 13 of payload.
      frame[14U] = (frame[14U] & (0xFFU << 4U)) | (msSync[0U] & (0xFFU >> 4U));
      frame[15U] = msSync[1U];
      frame[16U] = msSync[2U];
      frame[17U] = msSync[3U];
      frame[18U] = msSync[4U];
      frame[19U] = msSync[5U];
      // Sync ends at bit 155 (bit 3 of byte 19). Bit 156 (bit 4 of byte 19) is next.
      // frame[20] is byte 19 of payload.
      frame[20U] = (msSync[6U] & (0xFFU << 4U)) | (frame[20U] & (0xFFU >> 4U));
    }
#endif

    if (m_control == CONTROL_DATA) {
      // Data sync
      uint8_t colorCode;
      uint8_t dataType;
      CDMRSlotType slotType;
      slotType.decode(frame + 1U, colorCode, dataType);
      // Re-encode the slot type using the MS codeword layout so the host
      // sees a clean uplink-style burst even when the raw burst was BS.
      slotType.encode(colorCode, dataType, frame + 1U);

      if (colorCode == m_colorCode || m_colorCode == 0U) {
#if defined(MS_MODE)
        // Reset both slots - flywheel alternates slot identity so only resetting
        // m_syncCount[slot] leaves the other slot accumulating false misses.
        m_syncCount[0U] = 0U;
        m_syncCount[1U] = 0U;
#else
        m_syncCount[slot] = 0U;
#endif
        m_n[slot]         = 0U;

        frame[0U] |= dataType;

        switch (dataType) {
          case DT_DATA_HEADER:
            writeRSSIData();
            m_state[slot] = DMRRXS_DATA;
            m_type[slot]  = 0x00U;
            break;
          case DT_RATE_12_DATA:
          case DT_RATE_34_DATA:
          case DT_RATE_1_DATA:
            if (m_state[slot] == DMRRXS_DATA) {
               writeRSSIData();
              m_type[slot] = dataType;
            }
            break;
          case DT_VOICE_LC_HEADER: {
            DEBUG2("DMRSlotRX: voice header found pos", m_syncPtr);
            // Treat this as a new call only when both slots are idle.
            // In MS_MODE the flywheel can transiently flip slot labels.
            const bool newVoiceCall = (m_state[0U] == DMRRXS_NONE && m_state[1U] == DMRRXS_NONE);
            m_state[slot] = DMRRXS_VOICE;
#if defined(MS_MODE)
            if (newVoiceCall)
              m_state[slot ^ 1U] = DMRRXS_NONE;  // Only one slot active at call start
#endif
              // Extract and embed Link Control (LC) data in the frame
              DMRLC_T lc;
              
              bool lcValid = CDMRLC::decode(frame, DT_VOICE_LC_HEADER, &lc);
              
#if defined(ENABLE_DEBUG)
              if (lcValid) {
                DEBUG2I("LC decoded - SrcID", lc.srcId);
                DEBUG2I("LC decoded - DstID", lc.dstId);
                DEBUG2I("CC ", m_colorCode);
                DEBUG2I("TS", slot + 1U);
              } else {
                DEBUG2("LC decode failed !!!!", slot);
              }
#endif

     if (lcValid && !m_callActive[slot]) {
                m_callActive[slot] = true;
                m_callStartMs[slot] = millis();
        #if defined(MS_MODE)
                m_callActive[slot ^ 1U] = false;
        #endif
              }

              
              // Store LC data for embedding in voice frames
#if defined(MS_MODE)
              if (lcValid) {
                memcpy(m_lcData, lc.rawData, 12);
                m_lcValid[slot] = true;
              } else {
                m_lcValid[slot] = false;
              }
#endif
              
              // Re-encode the error-corrected LC back into clean BPTC payload bits.
              // This replaces the raw (possibly erroneous) received bits with a
              // perfect codeword so MMDVMHost's CFullLC::decode() always succeeds.
#if defined(MS_MODE)
              if (lcValid) {
                uint8_t lcClean[12U];
                memcpy(lcClean, lc.rawData, 12U);
                // lc.rawData has had the CRC mask removed; re-apply it so
                // MMDVMHost's applyMask() step yields the correct RS parities.
                lcClean[9U]  ^= VOICE_LC_HEADER_CRC_MASK[0U];
                lcClean[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
                lcClean[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
                CBPTC19696 bptcEnc;
                bptcEnc.encode(lcClean, frame + 1U);
              }
#endif
#if defined(ENABLE_DEBUG)
              DEBUG2("DMRSlotRX: Sending voice header to MMDVMHost", slot);
              DEBUG2I("  Host frame control", frame[0U]);
              DEBUG2I("  Host frame payload[1-3]", (frame[1U] << 16) | (frame[2U] << 8) | frame[3U]);
#endif
              writeRSSIData();
            break;
          }

          case DT_VOICE_PI_HEADER:
            if (m_state[slot] == DMRRXS_VOICE) {
             
              writeRSSIData();
            }
            m_state[slot] = DMRRXS_VOICE;
            break;

          case DT_TERMINATOR_WITH_LC:
            if (m_state[slot] == DMRRXS_VOICE) {
              DEBUG2("DMRSlotRX: voice terminator found pos", m_syncPtr);
              {
                                // Extract and embed Link Control (LC) data in the terminator frame
                DMRLC_T lc;
                
                bool lcValid = CDMRLC::decode(frame, DT_TERMINATOR_WITH_LC, &lc);
                
#if defined(ENABLE_DEBUG)
                if (lcValid) {
                  DEBUG2I("Terminator LC decoded - SrcID", lc.srcId);
                  DEBUG2I("Terminator LC decoded - DstID", lc.dstId);
                } else {
                  DEBUG2("Terminator LC decode failed!!!!", slot);
                }
#endif
                
                // Re-encode error-corrected LC into clean BPTC payload bits.
#if defined(MS_MODE)
                if (lcValid) {
                  uint8_t lcClean[12U];
                  memcpy(lcClean, lc.rawData, 12U);
                  lcClean[9U]  ^= TERMINATOR_WITH_LC_CRC_MASK[0U];
                  lcClean[10U] ^= TERMINATOR_WITH_LC_CRC_MASK[1U];
                  lcClean[11U] ^= TERMINATOR_WITH_LC_CRC_MASK[2U];
                  CBPTC19696 bptcEnc;
                  bptcEnc.encode(lcClean, frame + 1U);
                }
#endif

             #if defined(ENABLE_DEBUG)
              DEBUG2("DMRSlotRX: Sending voice terminator to MMDVMHost", slot);
#endif
                writeRSSIData();


                if (m_callActive[slot]) {
                  m_callActive[slot] = false;
                }

              }
              m_state[slot]  = DMRRXS_NONE;
            }
            break;
          default:    // DT_CSBK
            
            writeRSSIData();
            m_state[slot]  = DMRRXS_NONE;
            break;
        }
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
      // In MS_MODE the flywheel alternates m_currentSlot every burst, so 'slot'
      // oscillates between 0 and 1. Resetting only syncCount[slot] leaves the
      // OTHER slot's counter accumulating, which triggers false sync-lost after
      // 13 bursts (~390ms). Reset BOTH to prevent false dropout during voice calls.
#if defined(MS_MODE)
      m_syncCount[0U] = 0U;
      m_syncCount[1U] = 0U;
#else
      m_syncCount[slot] = 0U;
#endif
      m_n[slot]         = 0U;
    } else {
#if defined(MS_MODE)
      if (m_syncLocked) {
        m_syncCount[slot]++;
        if (m_syncCount[slot] >= MAX_SYNC_LOST_FRAMES) {
         
#if defined(ENABLE_DEBUG)
          DEBUG2("DMRSlotRX: Sync lost in MS_MODE", m_syncCount[slot]);
    #endif      

          if (m_callActive[slot]) {
            uint32_t dtMs = millis() - m_callStartMs[slot];
            uint32_t sec10 = (dtMs + 50U) / 100U;
            uint32_t secI = sec10 / 10U;
            uint32_t secF = sec10 % 10U;
            char rfLostLine[128];
            snprintf(rfLostLine, sizeof(rfLostLine), "DMR Slot %u, RF voice transmission lost, %lu.%lu seconds, BER: 0.0%%", slot + 1U, (unsigned long)secI, (unsigned long)secF);
            DEBUG1(rfLostLine);
          }

          m_callActive[slot] = false;
          serial.writeDMRLost(slot);
          reset();
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
    m_inverted = false;

#if defined(MS_MODE)
    // Advance pointers for next slot (flywheel)
    m_syncPtr  = (m_syncPtr  + 288U) % DMR_BUFFER_LENGTH_BITS;
    m_startPtr = (m_startPtr + 288U) % DMR_BUFFER_LENGTH_BITS;
    m_endPtr   = (m_endPtr   + 288U) % DMR_BUFFER_LENGTH_BITS;
    // Toggle slot for the flywheel - decodeCACH will correct if needed.
    m_currentSlot = (m_currentSlot == 1U) ? 2U : 1U;
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

  if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_DATA_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_DATA;
    m_inverted = false;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_VOICE_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_VOICE;
    m_inverted = false;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_DATA_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_DATA;
    m_inverted = true;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_BS_VOICE_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
#if defined(DUPLEX)
    if (dmrTX.isWaitingForBSSync()) {
      dmrTX.confirmBSSync();
    }
#endif
    control = CONTROL_VOICE;
    m_inverted = true;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
    m_inverted = false;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
    m_inverted = false;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
    m_inverted = true;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
    m_inverted = true;
  }

  if (control != CONTROL_NONE) {
#if defined(MS_MODE)
    // Set sync lock when we find a BS sync pattern
    if (control != CONTROL_NONE) {
      // Determine the correct timeslot from this burst's own CACH.
      // The CACH for the current burst starts 179 bits before the sync end
      // (sync ends at bit 179 of the 288-bit slot; CACH is at bits 0-23).
      // Bit 1 of the CACH is the TC bit: TC=0 → TS1 (m_currentSlot=1),
      //                                  TC=1 → TS2 (m_currentSlot=2).
      // The TC bit in the CACH describes the identity of the current burst
      // (TC=0 → TS1, TC=1 → TS2).
      uint16_t tcCachStart = (m_dataPtr + DMR_BUFFER_LENGTH_BITS - 179U) % DMR_BUFFER_LENGTH_BITS;
      bool t[7];
      t[0] = READ_BIT1(m_buffer, (tcCachStart + 0U) % DMR_BUFFER_LENGTH_BITS); // AT
      t[1] = READ_BIT1(m_buffer, (tcCachStart + 1U) % DMR_BUFFER_LENGTH_BITS); // TC
      t[2] = READ_BIT1(m_buffer, (tcCachStart + 5U) % DMR_BUFFER_LENGTH_BITS); // LCSS1
      t[3] = READ_BIT1(m_buffer, (tcCachStart + 6U) % DMR_BUFFER_LENGTH_BITS); // LCSS0
      t[4] = READ_BIT1(m_buffer, (tcCachStart + 10U) % DMR_BUFFER_LENGTH_BITS); // H2
      t[5] = READ_BIT1(m_buffer, (tcCachStart + 11U) % DMR_BUFFER_LENGTH_BITS); // H1
      t[6] = READ_BIT1(m_buffer, (tcCachStart + 15U) % DMR_BUFFER_LENGTH_BITS); // H0

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
          default: break; // Multi-bit error - use raw bit
        }
      }

      // The TC bit indicates the identity of the burst that follows the CACH.
      // Since this CACH precedes the current burst, it identifies the current burst.
      // (TC=0 → TS1, TC=1 → TS2)
      uint8_t indicated_current_slot = t[1] ? 2U : 1U;
      if (!m_syncLocked) {
        m_currentSlot = indicated_current_slot;
        m_syncLocked = true;
        m_slotHysteresis = 0U;
      } else {
        if (m_currentSlot != indicated_current_slot) {
          if (++m_slotHysteresis >= 2U) {
            //DEBUG2("Slot changed at sync to", indicated_current_slot);
            m_currentSlot = indicated_current_slot;
            m_slotHysteresis = 0U;
          }
        } else {
          m_slotHysteresis = 0U;
        }
      }
      m_slotTimer = 0U;
    }
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
    
    if (m_state[slot_idx] == DMRRXS_NONE) {
      m_syncCount[slot_idx] = 0U; // If we are idle, reset the sync counter as well
    }
    // Only reset the state if we are not in the middle of a voice or data call
    if (m_state[slot_idx] != DMRRXS_VOICE && m_state[slot_idx] != DMRRXS_DATA) {
        m_state[slot_idx] = DMRRXS_NONE;
        // [debug removed - high frequency]
    }
    m_endPtr = endPtr;
  }
}

void CDMRSlotRX::decodeCACH()
{
  // decodeCACH is called 132 bits after sync detection/flywheel trigger.
  // m_syncPtr has already been advanced by 288 in procSlot2.
  // The CACH for the current burst is 179 bits before the advanced sync end position.
  uint16_t cachStartPtr = (m_syncPtr + DMR_BUFFER_LENGTH_BITS - 179U) % DMR_BUFFER_LENGTH_BITS;

  bool c[24];
  for (uint8_t i = 0; i < 24; i++) {
    c[i] = READ_BIT1(m_buffer, (cachStartPtr + i) % DMR_BUFFER_LENGTH_BITS);
  }

  // TACT bits
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
      default: return; // Multi-bit error
    }
  }

  // TC bit to logical timeslot mapping per DMR spec (ETSI TS 102 361-1):
  // TC=0 → TS1, TC=1 → TS2. This CACH was read from the burst that just finished
  // (Slot N) and describes the identity of the *following* burst (Slot N+1).
  // Since procSlot2 already toggled m_currentSlot to the expected Slot N+1,
  // we verify it matches the TC bit's indication with 2-burst hysteresis.
  uint8_t indicated_next_slot = t[1] ? 2U : 1U;
  if (m_currentSlot != indicated_next_slot) {
    if (++m_slotHysteresis >= 2U) {
      //DEBUG2("Slot changed at CACH to", indicated_next_slot);
      m_currentSlot = indicated_next_slot;
      m_slotHysteresis = 0U;
    }
  } else {
    m_slotHysteresis = 0U;
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
  // Forward every burst immediately in MS_MODE so Pi-Star/MMDVMHost
  // sees BS downlink traffic as if it were MS uplink. Rely on the host
  // to discard any invalid frames rather than suppressing here.
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);
#else
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);
#endif
#else
#if defined(MS_MODE)
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#else
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#endif
#endif
}

#endif
