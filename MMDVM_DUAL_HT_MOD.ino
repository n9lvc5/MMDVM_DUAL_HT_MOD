/*
 *   Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 by Colin Durbridge G4EML
 *   Copyright (C) 2016,2017 by Andy Uribe CA6JAU
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

 https://github.com/juribeparada/MMDVM_HS
 */

#include "Config.h"
#include "Globals.h"

// Global variables
MMDVM_STATE m_modemState = STATE_IDLE;
MMDVM_STATE m_calState = STATE_IDLE;
MMDVM_STATE m_modemState_prev = STATE_IDLE;

bool m_cwid_state = false;
bool m_pocsag_state = false;

uint8_t m_cwIdTXLevel = 30;

uint32_t m_modeTimerCnt;

bool m_dstarEnable  = true;
bool m_dmrEnable    = true;
bool m_ysfEnable    = true;
bool m_p25Enable    = true;
bool m_nxdnEnable   = true;
bool m_m17Enable    = true;
bool m_pocsagEnable = true;

bool m_duplex = false;

bool m_tx  = false;
bool m_dcd = false;


uint8_t    m_control;

#if defined(DUPLEX)
CDMRIdleRX dmrIdleRX;
CDMRRX     dmrRX;
CDMRTX     dmrTX;
#endif

CDMRDMORX  dmrDMORX;
CDMRDMOTX  dmrDMOTX;


CM17RX     m17RX;
CM17TX     m17TX;

CCalDMR    calDMR;

#if defined(SEND_RSSI_DATA)
CCalRSSI   calRSSI;
#endif

CCWIdTX    cwIdTX;

CSerialPort serial;
CIO io;

void setup()
{
  serial.start();
}

void loop()
{
  serial.process();
  io.process();

  // The following is for transmitting


  if (m_dmrEnable && m_modemState == STATE_DMR && m_calState == STATE_IDLE) {
#if defined(DUPLEX)
    if (m_duplex)
      dmrTX.process();
    else
      dmrDMOTX.process();
#else
    dmrDMOTX.process();
#endif
  }


  if (m_m17Enable && m_modemState == STATE_M17)
    m17TX.process();


  if (m_calState == STATE_DMRCAL || m_calState == STATE_DMRDMO1K || m_calState == STATE_INTCAL)
    calDMR.process();

#if defined(SEND_RSSI_DATA)
  if (m_calState == STATE_RSSICAL)
    calRSSI.process();
#endif

  if (m_modemState == STATE_IDLE)
    cwIdTX.process();
}
