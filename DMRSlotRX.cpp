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
#include "Utils.h"

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
m_syncCount(0U),
m_state(DMRRXS_NONE),
m_n(0U),
m_type(0U),
m_delayPtr(0U),
m_colorCode(0U),
m_delay(0U)
{
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
  m_syncCount = 0U;
  m_state     = DMRRXS_NONE;
  m_startPtr  = 0U;
  m_endPtr    = NOENDPTR;
  m_type      = 0U;
  m_n         = 0U;
}

bool CDMRSlotRX::databit(bool bit)
{
  uint16_t min, max;

  m_delayPtr++;
  if (m_delayPtr < m_delay)
    return (m_state != DMRRXS_NONE || m_control != CONTROL_NONE);

  WRITE_BIT1(m_buffer, m_dataPtr, bit);

  m_patternBuffer <<= 1;
  if (bit)
    m_patternBuffer |= 0x01U;
    
  if (m_state == DMRRXS_NONE) {
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

  m_dataPtr++;

  if (m_dataPtr >= DMR_BUFFER_LENGTH_BITS)
    m_dataPtr = 0U;

  return (m_state != DMRRXS_NONE || m_control != CONTROL_NONE);
}

void CDMRSlotRX::procSlot2()
{
  if (m_dataPtr == m_endPtr) {
    frame[0U] = m_control;

    bitsToBytes(m_startPtr, DMR_FRAME_LENGTH_BYTES, frame + 1U);

    if (m_control == CONTROL_DATA) {
      // Data sync
      uint8_t colorCode;
      uint8_t dataType;
      CDMRSlotType slotType;
      slotType.decode(frame + 1U, colorCode, dataType);

#if defined(ENABLE_DEBUG)
      static uint8_t lastColorCode = 0xFF;
      if (colorCode != lastColorCode) {
        DEBUG2I("DMRSlotRX: RX ColorCode", colorCode);
        DEBUG2I("DMRSlotRX: Expected ColorCode", m_colorCode);
        lastColorCode = colorCode;
      }
#endif

#if defined(MS_MODE)
      // MS_MODE: Accept all color codes (promiscuous mode for listening)
      if (true) {
#else
      if (colorCode == m_colorCode) {
#endif
        m_syncCount = 0U;
        m_n         = 0U;

        frame[0U] |= dataType;

        switch (dataType) {
          case DT_DATA_HEADER:
            DEBUG2("DMRSlot2RX: data header found pos", m_syncPtr);
            writeRSSIData();
            m_state = DMRRXS_DATA;
            m_type  = 0x00U;
            break;
          case DT_RATE_12_DATA:
          case DT_RATE_34_DATA:
          case DT_RATE_1_DATA:
            if (m_state == DMRRXS_DATA) {
              DEBUG2("DMRSlot2RX: data payload found pos", m_syncPtr);
              writeRSSIData();
              m_type = dataType;
            }
            break;
          case DT_VOICE_LC_HEADER:
            DEBUG2("DMRSlot2RX: voice header found pos", m_syncPtr);
            writeRSSIData();
            m_state = DMRRXS_VOICE;
            {
              uint8_t slot = m_slot ? 1U : 0U;
#if defined(MS_MODE)
              // In MS_MODE, repurpose mode LEDs as timeslot indicators
              io.DSTAR_pin(slot == 0U);
              io.P25_pin(slot == 1U);
#endif
              serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
            }
            break;
          case DT_VOICE_PI_HEADER:
            if (m_state == DMRRXS_VOICE) {
              DEBUG2("DMRSlot2RX: voice pi header found pos", m_syncPtr);
              writeRSSIData();
            }
            m_state = DMRRXS_VOICE;
            break;
          case DT_TERMINATOR_WITH_LC:
            if (m_state == DMRRXS_VOICE) {
              DEBUG2("DMRSlot2RX: voice terminator found pos", m_syncPtr);
              writeRSSIData();
              {
                uint8_t slot = m_slot ? 1U : 0U;
#if defined(MS_MODE)
                io.DSTAR_pin(slot == 0U);
                io.P25_pin(slot == 1U);
#endif
                serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
              }
              m_state  = DMRRXS_NONE;
              m_endPtr = NOENDPTR;
            }
            break;
          default:    // DT_CSBK
            DEBUG2("DMRSlot2RX: csbk found pos", m_syncPtr);
            writeRSSIData();
            m_state  = DMRRXS_NONE;
            m_endPtr = NOENDPTR;
            break;
        }
      } else {
#if defined(ENABLE_DEBUG)
        static uint32_t ccMismatchCount = 0;
        ccMismatchCount++;
        if (ccMismatchCount % 10 == 1) {
          DEBUG2I("DMRSlotRX: ColorCode MISMATCH! Count", ccMismatchCount);
        }
#endif
      }
    } else if (m_control == CONTROL_VOICE) {
      // Voice sync found
      writeRSSIData();
      m_state     = DMRRXS_VOICE;
      m_syncCount = 0U;
      m_n         = 0U;
    } else {
      if (m_state != DMRRXS_NONE) {
        m_syncCount++;
        if (m_syncCount >= MAX_SYNC_LOST_FRAMES) {
          serial.writeDMRLost(1U);
          reset();
        }
      }

      if (m_state == DMRRXS_VOICE) {
        if (m_n >= 5U) {
          frame[0U] = CONTROL_VOICE;
          m_n = 0U;
        } else {
          frame[0U] = ++m_n;
        }

        uint8_t slot = m_slot ? 1U : 0U;
#if defined(MS_MODE)
        // In MS_MODE, repurpose mode LEDs as timeslot indicators
        // D-Star LED = TS1 (slot 0), P25 LED = TS2 (slot 1)
        io.DSTAR_pin(slot == 0U);
        io.P25_pin(slot == 1U);
#endif
        serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
      } else if (m_state == DMRRXS_DATA) {
        if (m_type != 0x00U) {
          frame[0U] = CONTROL_DATA | m_type;
          writeRSSIData();
        }
      }
    }

    // End of this slot, reset some items for the next slot.
    m_control = CONTROL_NONE;
    
#if defined(MS_MODE)
    // In MS_MODE, timeslots alternate. Toggle to next slot AFTER sending this frame.
    m_slot = !m_slot;
#if defined(ENABLE_DEBUG)
    DEBUG2("DMRSlotRX: Next slot will be", m_slot ? 2 : 1);
#endif
#endif
  }
}

void CDMRSlotRX::correlateSync()
{
  uint16_t syncPtr;
  uint16_t startPtr;
  uint16_t endPtr;
  uint8_t  control = CONTROL_NONE;

#if defined(ENABLE_DEBUG)
  static uint32_t debugCounter = 0;
  static uint32_t totalSyncs = 0;
  debugCounter++;
  if (debugCounter == 50000) {
    DEBUG2("DMRSlotRX: Pattern Hi", (uint16_t)(m_patternBuffer >> 32));
    DEBUG2("DMRSlotRX: Pattern Lo", (uint16_t)(m_patternBuffer & 0xFFFF));
    DEBUG2I("DMRSlotRX: Total syncs found", totalSyncs);
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
#if !defined(MS_MODE)
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_DATA_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_DATA;
  } else if (countBits64((m_patternBuffer & DMR_SYNC_BITS_MASK) ^ DMR_MS_VOICE_SYNC_BITS_INV) <= MAX_SYNC_BYTES_ERRS) {
    control = CONTROL_VOICE;
#endif
  }

  if (control != CONTROL_NONE) {
#if defined(ENABLE_DEBUG)
    totalSyncs++;
    static uint32_t syncDebugCounter = 0;
    syncDebugCounter++;
    if (syncDebugCounter % 100 == 1) {
      DEBUG2("DMRSlotRX: SYNC Type", control);
      DEBUG2("DMRSlotRX: SYNC Pos", m_dataPtr);
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
    m_modeTimerCnt = 0U;
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
  uint8_t slot = m_slot ? 1U : 0U;
  
#if defined(SEND_RSSI_DATA)
  uint16_t rssi = io.readRSSI();

  frame[34U] = (rssi >> 8) & 0xFFU;
  frame[35U] = (rssi >> 0) & 0xFFU;

  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);
#else
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
#endif
}

#endif
