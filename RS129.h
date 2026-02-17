/*
 *   Copyright (C) 2015,2016 by Jonathan Naylor G4KLX
 *   Adapted from OpenGD77 for MMDVM_HS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#if !defined(RS129_H)
#define RS129_H

#include <stdint.h>

class CRS129
{
public:
  static bool check(const uint8_t* in);

private:
  static uint8_t gf6Mult(uint8_t a, uint8_t b);
  static uint8_t gf6Add(uint8_t a, uint8_t b);
};

#endif
