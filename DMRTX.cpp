/*
 *   Copyright (C) 2015,2016,2017 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 by Colin Durbridge G4EML
 *   Copyright (C) 2017 by Andy Uribe CA6JAU
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

#include "DMRTX.h"
#include "Globals.h"
#include "DMRDefines.h"
#include "Utils.h"

#define REQUEST_TIMEOUT 200U
#define MAX_RETRIES 3U
#define BACKOFF_MIN 100U
#define BACKOFF_MAX 500U

CDMRTX::CDMRTX() :
m_state(DMRTXSTATE_IDLE),
m_cachPtr(0U),
m_poLen(0U),
m_poPtr(0U),
m_frameCount(0U),
m_control_old(0U),
m_bs_sync_confirmed(false),
m_wait_timestamp(0U),
m_request_retries(0U),
m_backoff_timer(0U)
{
  m_fifo[0].reset();
  m_fifo[1].reset();

  for (unsigned int i = 0U; i < 2U; i++)
    m_abort[i] = false;

  memset(m_idle, 0U, DMR_FRAME_LENGTH_BYTES);
  m_idle[0] = (DT_IDLE << 0);
}

uint8_t CDMRTX::writeData1(const uint8_t* data, uint8_t length)
{
  return 4U; // Disabled for MS mode TS1
}

uint8_t CDMRTX::writeData2(const uint8_t* data, uint8_t length)
{
  if (length != (DMR_FRAME_LENGTH_BYTES + 1U))
    return 4U;

  if (m_fifo[1].getSpace() < (DMR_FRAME_LENGTH_BYTES + 1U))
    return 5U;

  for (uint8_t i = 0U; i < (DMR_FRAME_LENGTH_BYTES + 1U); i++)
    m_fifo[1].put(data[i]);

  return 0U;
}

uint8_t CDMRTX::writeShortLC(const uint8_t* data, uint8_t length)
{
  return 0U;
}

uint8_t CDMRTX::writeAbort(const uint8_t* data, uint8_t length)
{
  if (length != 1U)
    return 4U;

  if (data[0] == 0U)
    m_abort[0] = true;
  else
    m_abort[1] = true;

  return 0U;
}

void CDMRTX::reset()
{
  m_fifo[0].reset();
  m_fifo[1].reset();
  m_state = DMRTXSTATE_IDLE;
  m_bs_sync_confirmed = false;
  io.setRX();
}

void CDMRTX::process()
{
  switch (m_state) {
  case DMRTXSTATE_IDLE:
    if (m_fifo[1].getData() >= (DMR_FRAME_LENGTH_BYTES + 1U)) {
      m_state = DMRTXSTATE_REQUEST_CHANNEL;
      m_request_retries = 0U;
    }
    break;

  case DMRTXSTATE_REQUEST_CHANNEL:
    m_wait_timestamp = millis();
    m_bs_sync_confirmed = false;
    m_state = DMRTXSTATE_WAIT_BS_CONFIRM;
    // Potentially send a preamble/request here if needed
    break;

  case DMRTXSTATE_WAIT_BS_CONFIRM:
    if (m_bs_sync_confirmed) {
      m_state = DMRTXSTATE_SLOT1;
      m_frameCount = 0U;
      io.setTX();
    } else if (millis() - m_wait_timestamp > REQUEST_TIMEOUT) {
      if (m_request_retries < MAX_RETRIES) {
        m_request_retries++;
        m_backoff_timer = millis() + random(BACKOFF_MIN, BACKOFF_MAX);
        m_state = DMRTXSTATE_BACKOFF;
      } else {
        m_fifo[1].reset();
        m_state = DMRTXSTATE_IDLE;
      }
    }
    break;

  case DMRTXSTATE_BACKOFF:
    if (millis() > m_backoff_timer) {
      m_state = DMRTXSTATE_REQUEST_CHANNEL;
    }
    break;

  case DMRTXSTATE_SLOT1:
    createData(0, true);
    m_state = DMRTXSTATE_CACH1;
    break;

  case DMRTXSTATE_CACH1:
    createCACH(0, 1);
    m_state = DMRTXSTATE_SLOT2;
    break;

  case DMRTXSTATE_SLOT2:
    if (m_fifo[1].getData() >= (DMR_FRAME_LENGTH_BYTES + 1U)) {
      createData(1, false);
      m_frameCount = 0U;
    } else {
      createData(1, true);
      m_frameCount++;
      if (m_frameCount > 20U) {
        m_state = DMRTXSTATE_IDLE;
        io.setRX();
        return;
      }
    }
    m_state = DMRTXSTATE_CACH2;
    break;

  case DMRTXSTATE_CACH2:
    createCACH(1, 0);
    m_state = DMRTXSTATE_SLOT1;
    break;

  default:
    break;
  }

  if (m_poLen > 0U) {
    uint16_t space = io.getSpace();
    while (space > 8U) {
      writeByte(m_poBuffer[m_poPtr++], 0x00U);
      space -= 8U;

      if (m_poPtr >= m_poLen) {
        m_poPtr = 0U;
        m_poLen = 0U;
        return;
      }
    }
  }
}

uint8_t CDMRTX::getSpace1() const
{
  return 0U;
}

uint8_t CDMRTX::getSpace2() const
{
  return m_fifo[1].getSpace() / (DMR_FRAME_LENGTH_BYTES + 1U);
}

void CDMRTX::setColorCode(uint8_t colorCode)
{
}

void CDMRTX::confirmBSSync()
{
  m_bs_sync_confirmed = true;
}

bool CDMRTX::isWaitingForBSSync() const
{
  return m_state == DMRTXSTATE_WAIT_BS_CONFIRM;
}

void CDMRTX::createData(uint8_t slotIndex, bool forceIdle)
{
  uint8_t frame[DMR_FRAME_LENGTH_BYTES + 1];
  if (forceIdle || m_fifo[slotIndex].getData() < (DMR_FRAME_LENGTH_BYTES + 1U)) {
    frame[0] = DT_IDLE;
    memcpy(frame + 1, m_idle, DMR_FRAME_LENGTH_BYTES);
  } else {
    for (unsigned int i = 0U; i < (DMR_FRAME_LENGTH_BYTES + 1U); i++)
      frame[i] = m_fifo[slotIndex].get();
  }

  uint8_t dataType = frame[0] & 0x0FU;
  const uint8_t* sync;
  if (dataType == DT_VOICE_LC_HEADER || dataType == DT_VOICE_PI_HEADER || (dataType >= 1 && dataType <= 5))
    sync = DMR_MS_VOICE_SYNC_BYTES;
  else
    sync = DMR_MS_DATA_SYNC_BYTES;

  memcpy(m_poBuffer, frame + 1, 33);

  // Replacement of sync bits with MS sync
  m_poBuffer[13] = (m_poBuffer[13] & 0xF0U) | (sync[0] & 0x0FU);
  m_poBuffer[14] = sync[1];
  m_poBuffer[15] = sync[2];
  m_poBuffer[16] = sync[3];
  m_poBuffer[17] = sync[4];
  m_poBuffer[18] = sync[5];
  m_poBuffer[19] = (sync[6] & 0xF0U) | (m_poBuffer[19] & 0x0FU);

  m_poLen = 33U;
  m_poPtr = 0U;
}

void CDMRTX::createCACH(uint8_t txSlotIndex, uint8_t rxSlotIndex)
{
  m_poLen = 3U;
  memset(m_poBuffer, 0, 3);
  m_poPtr = 0U;
}

void CDMRTX::writeByte(uint8_t c, uint8_t control)
{
  uint8_t bit;
  uint8_t mask = 0x80U;
  for (uint8_t i = 0U; i < 8U; i++, c <<= 1) {
    bit = (c & mask) ? 1U : 0U;
    io.write(&bit, 1);
  }
}

#endif
