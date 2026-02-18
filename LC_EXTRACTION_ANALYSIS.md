# Critical Finding: Missing Link Control (LC) Data Extraction

## Problem Root Cause Identified

After analyzing OpenGD77 implementation and MMDVM firmware, we've identified the **critical missing component**:

### What We're Missing

Our firmware **does not extract Link Control (LC) data** from voice headers. We only decode:
- Color Code (4 bits)
- Data Type (4 bits)

But we **DO NOT** extract:
- **Source DMR ID** (24 bits) - Caller identification  
- **Destination Talkgroup** (24 bits) - Target group
- **Feature ID** (8 bits)
- **Service Options** (8 bits)

### Why Pi-Star Shows No Caller Info

Pi-Star requires a **DMR_START frame** before voice data:

```
Frame Format:
[0xE0][Length][MMDVM_DMR_START][Slot][CC][TG_Hi][TG_Mid][TG_Lo][ID_Hi][ID_Mid][ID_Lo]

Example:
0xE0 0x0A 0x1D 0x01 0x01 0x00 0x00 0x09 0x00 0x00 0x01
 |    |    |    |    |    |-----TG 9-----|  |------ID 1----|
 |    |    |    |    Color Code = 1
 |    |    |    Timeslot = 1
 |    |    DMR_START command
 |    Length = 10 bytes
 Start byte
```

**We're currently only sending DATA frames** (0x18/0x1A) without the initial START frame.

---

## Link Control (LC) Structure in DMR Voice Header

### Full LC Format (96 bits before encoding)

| Bits | Field | Description |
|------|-------|-------------|
| 1 | PF (Protect Flag) | Priority indicator |
| 1 | R (Reserved) | Reserved bit |
| 6 | FLCO | Frame Logical Channel Operator |
| 8 | FID | Feature set ID (usually 0x00) |
| 8 | Service Options | Additional options |
| 24 | Destination ID | Talkgroup or target DMR ID |
| 24 | Source ID | Caller's DMR ID |
| 24 | CRC/Checksum | Error detection |

### Encoding in Voice Header Burst

The 96-bit LC is encoded with error correction:
1. **Reed-Solomon RS(12,9)** - Adds 3 parity bytes
2. **BPTC(196,96)** - Hamming product code for bit-level protection
3. Result: **196 bits** in the frame

### Location in DMR Frame

Voice header (33 bytes = 264 bits):
```
[108 bits Audio/Info][48 bits SYNC][108 bits Audio/Info]
```

The LC data spans the Info sections (196 bits total):
- Bits 0-97: First half of LC (98 bits)
- Bits 98-195: Second half of LC (98 bits)

---

## Current Code Analysis

### What We Have ✅

1. **DMRSlotType.cpp** - Extracts Color Code and Data Type
   - Uses Golay(20,8) error correction
   - Decodes 8-bit value: `[4-bit CC][4-bit DataType]`

2. **Voice Header Detection** - Line 179-192 in DMRSlotRX.cpp
   ```cpp
   case DT_VOICE_LC_HEADER:
     DEBUG2("DMRSlot2RX: voice header found pos", m_syncPtr);
     writeRSSIData();
     m_state = DMRRXS_VOICE;
     // Now sends frame data
     serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
     break;
   ```

3. **Frame Data Available** - The `frame[]` buffer contains the full 33-byte payload

### What We're Missing ❌

1. **No LC Decoding Function** - No code to extract srcId/dstId from the 196-bit encoded LC
2. **No BPTC(196,96) Decoder** - Required to extract 96-bit LC from 196-bit encoded data
3. **No RS(12,9) Decoder** - Required to validate and correct LC errors
4. **No DMR_START Transmission** - SerialPort.cpp has no `writeDMRStart()` function

---

## OpenGD77 Implementation (Reference)

### LC Extraction Process

```c
// 1. Extract 196-bit BPTC-encoded LC from frame
bool DMRFullLC_decode(const unsigned char *data, unsigned char type, DMRLC_T *lc)
{
    // BPTC(196,96) decode to get 96-bit LC
    BPTC19696_decode(data, lc->rawData);  // Returns 12 bytes (96 bits)
    
    // Apply CRC mask based on type
    if (type == DT_VOICE_LC_HEADER) {
        lc->rawData[9U] ^= VOICE_LC_HEADER_CRC_MASK[0U];
        lc->rawData[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
        lc->rawData[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
    }
    
    // Reed-Solomon validation
    if (!RS129_check(lc->rawData))
        return false;
    
    // Parse LC fields
    DMRLCfromBytes(lc->rawData, lc);
    return true;
}

// 2. Parse LC fields
void DMRLCfromBytes(const unsigned char* bytes, DMRLC_T* lc)
{
    lc->FLCO = bytes[0U] & 0x3FU;
    lc->FID = bytes[1U];
    lc->options = bytes[2U];
    
    // Extract destination ID (3 bytes)
    lc->dstId = (((uint32_t)bytes[3U]) << 16) + 
                (((uint32_t)bytes[4U]) << 8) + 
                ((uint32_t)bytes[5U]);
    
    // Extract source ID (3 bytes)
    lc->srcId = (((uint32_t)bytes[6U]) << 16) + 
                (((uint32_t)bytes[7U]) << 8) + 
                ((uint32_t)bytes[8U]);
}
```

---

## Proposed Solution

### Option 1: Port OpenGD77 LC Decoder (Recommended)

**Pros:**
- Complete, tested implementation
- Includes all error correction (BPTC + RS)
- Handles CRC masking correctly

**Cons:**
- Need to port ~500 lines of code
- BPTC decoder is complex (interleaving matrices)

**Files to port:**
- `BPTC19696.c` (~200 lines) - BPTC encoder/decoder
- `RS129.c` (~100 lines) - Reed-Solomon checker
- `DMRLC.c` (~150 lines) - LC parser
- `DMRFullLC.c` (~50 lines) - High-level LC decode

### Option 2: Add DMR_START with Partial Data

**Pros:**
- Minimal code changes
- Can work immediately

**Cons:**
- Won't have source/dest IDs
- Pi-Star might still not display caller info

**Implementation:**
```cpp
void CSerialPort::writeDMRStart(bool slot, uint8_t colorCode) {
  uint8_t reply[10U];
  
  reply[0U] = MMDVM_FRAME_START;
  reply[1U] = 10U;
  reply[2U] = MMDVM_DMR_START;
  reply[3U] = slot ? 0x01U : 0x00U;  // Timeslot
  reply[4U] = colorCode;
  reply[5U] = 0x00U;  // TG unknown (Hi byte)
  reply[6U] = 0x00U;  // TG unknown (Mid byte)
  reply[7U] = 0x00U;  // TG unknown (Lo byte)
  reply[8U] = 0x00U;  // ID unknown (Hi byte)
  reply[9U] = 0x00U;  // ID unknown (Mid byte)
  reply[10U] = 0x00U; // ID unknown (Lo byte)
  
  writeInt(1U, reply, 10U);
}
```

### Option 3: Simplified LC Extraction (Pragmatic)

Extract LC **without** error correction, assuming clean reception:

```cpp
void CDMRSlotRX::extractLC(const uint8_t* frame, uint32_t& srcId, uint32_t& dstId)
{
  // Voice header LC is in Info bits (bytes 1-12 and 14-19, 21-32)
  // This is a SIMPLIFIED approach - no BPTC decode
  // May work if signal is clean enough
  
  uint8_t lcData[12];
  // Extract raw bytes (skipping sync at bytes 13, 20)
  // WARNING: This won't work - need proper BPTC deinterleaving
  
  // For now, just set to 0 and add TODO
  srcId = 0;
  dstId = 0;
}
```

---

## Recommended Implementation Plan

### Phase 1: Add DMR_START Frame (Immediate)

1. Add `writeDMRStart()` to SerialPort.cpp
2. Call it when voice header detected
3. Use Color Code, hardcode TG/ID as 0

**Result:** Pi-Star gets START frame, might display "Unknown" caller

### Phase 2: Port BPTC Decoder (Short-term)

1. Port `BPTC19696.c` from OpenGD77
2. Add `BPTC_decode()` function
3. Extract raw 96-bit LC from voice header

**Result:** Can decode LC even with bit errors

### Phase 3: Port RS Checker (Short-term)

1. Port `RS129.c` from OpenGD77
2. Validate extracted LC

**Result:** Reliable LC detection

### Phase 4: Parse LC Fields (Short-term)

1. Extract srcId, dstId from validated LC
2. Send in DMR_START frame

**Result:** Pi-Star displays caller ID and talkgroup!

---

## Code Skeleton for Phase 1

### DMRSlotRX.cpp - Detect Voice Header

```cpp
case DT_VOICE_LC_HEADER:
  DEBUG2("DMRSlot2RX: voice header found pos", m_syncPtr);
  writeRSSIData();
  m_state = DMRRXS_VOICE;
  {
    uint8_t slot = m_slot ? 1U : 0U;
    uint8_t colorCode = m_colorCode;  // From decode
    
    // TODO: Extract srcId and dstId from frame LC
    uint32_t srcId = 0;  // Placeholder
    uint32_t dstId = 0;  // Placeholder
    
    // Send DMR_START with metadata
    serial.writeDMRStart(slot, colorCode, srcId, dstId);
    
    // Then send the voice header frame
    serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
  }
  break;
```

### SerialPort.h - Add Declaration

```cpp
void writeDMRStart(bool slot, uint8_t colorCode, uint32_t srcId, uint32_t dstId);
```

### SerialPort.cpp - Implement Function

```cpp
void CSerialPort::writeDMRStart(bool slot, uint8_t colorCode, uint32_t srcId, uint32_t dstId)
{
#if !defined(MS_MODE)
  if (m_modemState != STATE_DMR && m_modemState != STATE_IDLE)
    return;
  if (!m_dmrEnable)
    return;
#endif

  uint8_t reply[11U];
  
  reply[0U] = MMDVM_FRAME_START;
  reply[1U] = 11U;
  reply[2U] = MMDVM_DMR_START;
  reply[3U] = slot ? 0x01U : 0x00U;
  reply[4U] = colorCode;
  
  // Talkgroup (destination ID) - 3 bytes, big-endian
  reply[5U] = (dstId >> 16) & 0xFFU;
  reply[6U] = (dstId >> 8) & 0xFFU;
  reply[7U] = (dstId >> 0) & 0xFFU;
  
  // Source DMR ID - 3 bytes, big-endian
  reply[8U] = (srcId >> 16) & 0xFFU;
  reply[9U] = (srcId >> 8) & 0xFFU;
  reply[10U] = (srcId >> 0) & 0xFFU;
  
#if defined(ENABLE_DEBUG)
  DEBUG2I("DMR_START: Slot", slot ? 2 : 1);
  DEBUG2I("  ColorCode", colorCode);
  DEBUG2I("  Talkgroup", dstId);
  DEBUG2I("  Source ID", srcId);
#endif
  
  writeInt(1U, reply, 11U);
}
```

---

## Next Steps

1. **Implement Phase 1** - Add DMR_START frame with placeholder IDs
2. **Test with Pi-Star** - See if dashboard changes
3. **Port BPTC decoder** - Get real LC extraction working
4. **Verify caller ID display** - Confirm full functionality

## References

- OpenGD77 GitHub: https://github.com/open-ham/OpenGD77
- ETSI TS 102 361-1: DMR Air Interface Protocol
- ETSI TS 102 361-2: DMR Voice Services
- MMDVM Protocol: https://github.com/g4klx/MMDVM/blob/master/README.md
