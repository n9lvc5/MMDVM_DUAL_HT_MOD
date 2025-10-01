/*
 *   Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX
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
 */

#if !defined(DSTARRX_H)
#define  DSTARRX_H

#include "DStarDefines.h"

const uint16_t DSTAR_BUFFER_LENGTH_BITS = 800U;

enum DSRX_STATE {
  DSRXS_NONE,
  DSRXS_HEADER,
  DSRXS_DATA
};

class CDStarRX {
public:
  CDStarRX();

  void databit(bool bit);

  void reset();

private:
  DSRX_STATE   m_rxState;
  uint64_t     m_patternBuffer;
  uint8_t      m_rxBuffer[DSTAR_BUFFER_LENGTH_BITS / 8U];
  unsigned int m_rxBufferBits;
  unsigned int m_dataBits;
  unsigned int m_mar;
  int          m_pathMetric[4U];
  unsigned int m_pathMemory0[42U];
  unsigned int m_pathMemory1[42U];
  unsigned int m_pathMemory2[42U];
  unsigned int m_pathMemory3[42U];
  uint8_t      m_fecOutput[42U];

  void    processNone(bool bit);
  void    processHeader(bool bit);
  void    processData(bool bit);
  bool    rxHeader(uint8_t* in, uint8_t* out);
  void    acs(int* metric);
  void    viterbiDecode(int* data);
  void    traceBack();
  bool    checksum(const uint8_t* header) const;
  void    writeRSSIHeader(unsigned char* header);
  void    writeRSSIData(unsigned char* data);
};

#endif
