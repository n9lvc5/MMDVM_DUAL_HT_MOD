/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Copyright (C) 2018,2019 by Andy Uribe CA6JAU
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#if !defined(BPTC19696_H)
#define BPTC19696_H

#include <stdint.h>

class CBPTC19696
{
public:
  CBPTC19696();

  // Decode a BPTC(196,96) codeword from a full 33‑byte
  // DMR burst payload (voice header/terminator).
  // The layout and error‑correction behaviour matches
  // MMDVMHost's CBPTC19696::decode().
  void decode(const uint8_t* in, uint8_t* out);

private:
  bool m_rawData[196];
  bool m_deInterData[196];
  
  void decodeExtractBinary(const uint8_t* in);
  void decodeDeInterleave();
  void decodeErrorCheck();
  void decodeExtractData(uint8_t* data) const;
};

#endif
