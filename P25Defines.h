/*
 *   Copyright (C) 2016 by Jonathan Naylor G4KLX
 *   Copyright (C) 2018 by Andy Uribe CA6JAU
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

#if !defined(P25DEFINES_H)
#define  P25DEFINES_H

const unsigned int P25_HDR_FRAME_LENGTH_BYTES      = 99U;
const unsigned int P25_HDR_FRAME_LENGTH_BITS       = P25_HDR_FRAME_LENGTH_BYTES * 8U;

const unsigned int P25_LDU_FRAME_LENGTH_BYTES      = 216U;
const unsigned int P25_LDU_FRAME_LENGTH_BITS       = P25_LDU_FRAME_LENGTH_BYTES * 8U;

const unsigned int P25_TERMLC_FRAME_LENGTH_BYTES   = 54U;
const unsigned int P25_TERMLC_FRAME_LENGTH_BITS    = P25_TERMLC_FRAME_LENGTH_BYTES * 8U;

const unsigned int P25_TERM_FRAME_LENGTH_BYTES     = 18U;
const unsigned int P25_TERM_FRAME_LENGTH_BITS      = P25_TERM_FRAME_LENGTH_BYTES * 8U;

const unsigned int P25_TSDU_FRAME_LENGTH_BYTES     = 45U;
const unsigned int P25_TSDU_FRAME_LENGTH_BITS      = P25_TSDU_FRAME_LENGTH_BYTES * 8U; 

const unsigned int P25_PDU_HDR_FRAME_LENGTH_BYTES  = 45U;
const unsigned int P25_PDU_HDR_FRAME_LENGTH_BITS   = P25_PDU_HDR_FRAME_LENGTH_BYTES * 8U;

const unsigned int P25_SYNC_LENGTH_BYTES = 6U;
const unsigned int P25_SYNC_LENGTH_BITS  = P25_SYNC_LENGTH_BYTES * 8U;

const unsigned int P25_NID_LENGTH_BYTES  = 8U;
const unsigned int P25_NID_LENGTH_BITS   = P25_NID_LENGTH_BYTES * 8U;

const uint8_t P25_SYNC_BYTES[] = {0x55U, 0x75U, 0xF5U, 0xFFU, 0x77U, 0xFFU};
const uint8_t P25_SYNC_BYTES_LENGTH  = 6U;

const uint64_t P25_SYNC_BITS      = 0x00005575F5FF77FFU;
const uint64_t P25_SYNC_BITS_MASK = 0x0000FFFFFFFFFFFFU;

const uint8_t P25_DUID_HDU   = 0x00U;            // Header Data Unit
const uint8_t P25_DUID_TDU   = 0x03U;            // Simple Terminator Data Unit
const uint8_t P25_DUID_LDU1  = 0x05U;            // Logical Link Data Unit 1
const uint8_t P25_DUID_TSDU  = 0x07U;            // Trunking System Data Unit
const uint8_t P25_DUID_LDU2  = 0x0AU;            // Logical Link Data Unit 2
const uint8_t P25_DUID_PDU   = 0x0CU;            // Packet Data Unit 
const uint8_t P25_DUID_TDULC = 0x0FU;            // Terminator Data Unit with Link Control

#endif
