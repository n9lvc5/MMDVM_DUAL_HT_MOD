/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
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

#include "Globals.h"
#include "DMRRX.h"
#include "Debug.h"

static uint32_t bitCounter = 0;
static uint32_t syncCounter = 0;
static bool firstSync = true;

CDMRRX::CDMRRX() :
m_control_old(0U)
{
}

void CDMRRX::databit(bool bit, const uint8_t control)
{
#if defined(MS_MODE)
  // In MS_MODE, we don't have a hardware control signal to indicate slots
  // The slot will be determined by sync pattern detection in DMRSlotRX
  bitCounter++;
  if (bitCounter == 10000) {
    //DEBUG2I("DMRRX: Received bits", bitCounter);
    //DEBUG2I("DMRRX: Syncs detected", syncCounter);
    bitCounter = 0;
    syncCounter = 0;
  }
  



  
  bool decode = m_slotRX.databit(bit);
  if (decode) {
    syncCounter++;
    if (firstSync) {
      DEBUG1("DMRRX: First sync detected!");
      firstSync = false;
    }
  }
  
  io.setDecode(decode);
  io.resetWatchdog();
#else
  if (control != m_control_old) {
    m_control_old = control;
    if (control)
      m_slotRX.start(true);
    else
      m_slotRX.start(false);
  }

  io.setDecode(m_slotRX.databit(bit));
#endif
}

void CDMRRX::setColorCode(uint8_t colorCode)
{
  m_slotRX.setColorCode(colorCode);
  DEBUG2I("DMRRX: Color code set to", colorCode);
}

void CDMRRX::setDelay(uint8_t delay)
{
  m_slotRX.setDelay(delay);
  DEBUG2I("DMRRX: Delay set to", delay);
}

void CDMRRX::reset()
{
  m_slotRX.reset();
  DEBUG1("DMRRX: Reset");
}

#endif

