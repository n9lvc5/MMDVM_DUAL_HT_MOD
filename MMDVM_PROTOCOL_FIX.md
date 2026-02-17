# CRITICAL FINDING: MMDVMHost Protocol Issue

## Problem: "Unknown message, type: 1D"

Pi-Star's MMDVMHost shows:
```
M: Unknown message, type: 1D
M: Buffer dump
M: 0000: E0 0B 1D 00 01 00 00 00 00 00 00
```

**Type 0x1D = MMDVM_DMR_START**

## Root Cause

After reviewing the MMDVM protocol, **DMR_START (0x1D) is for TX mode only**!

### MMDVM Protocol Commands

**TX (Host → Modem)**:
- `0x1D` MMDVM_DMR_START - Start DMR transmission with metadata
- `0x18` MMDVM_DMR_DATA1 - Timeslot 1 data to transmit
- `0x1A` MMDVM_DMR_DATA2 - Timeslot 2 data to transmit

**RX (Modem → Host)**:
- `0x18` MMDVM_DMR_DATA1 - Received Timeslot 1 data (with LC embedded)
- `0x1A` MMDVM_DMR_DATA2 - Received Timeslot 2 data (with LC embedded)

**There is NO separate START frame in RX direction!**

## Solution

The LC data (caller ID, talkgroup) must be **embedded in the control byte and first bytes of the DMR_DATA frame**, not sent separately.

### Standard MMDVM RX DMR Frame Format

```
[0xE0][Length][0x18 or 0x1A][SeqNo][Control_Bits][33_bytes_payload]

Example for voice header:
0xE0 0x26 0x18 0x00 0x41 [33 bytes of DMR frame data]
                    |
                    Control byte: 0x41 = CONTROL_DATA | DT_VOICE_LC_HEADER
```

The **33-byte payload contains the raw DMR frame** with LC already encoded inside.

MMDVMHost then:
1. Receives DMR_DATA frame
2. Sees control byte 0x41 (voice header)
3. Extracts and decodes LC from the 33-byte payload
4. Displays caller info

## What We Got Wrong

We added a separate `writeDMRStart()` function that sends `0x1D` command in RX mode, but:
- MMDVMHost doesn't expect this in RX
- LC data should be in the payload, not a separate message
- MMDVMHost has its own LC decoder

## Correct Implementation

### Option A: MMDVMHost Decodes LC (Recommended)

**What to do**: Nothing! Just send the DMR_DATA frame as-is.

MMDVMHost already has:
- BPTC decoder
- RS validator  
- LC parser

It will decode the LC from the 33-byte payload automatically.

**Why our current code fails**:
- We're sending LC data correctly in the DMR_DATA payload
- But we're ALSO sending a DMR_START frame that MMDVMHost doesn't recognize
- This confuses MMDVMHost

**Fix**: Remove the `writeDMRStart()` call from voice header handling.

### Option B: Pre-decode and Insert (Not Standard)

Some non-standard implementations pre-decode LC and insert it differently, but this breaks compatibility with standard MMDVMHost.

## Testing Current Implementation

The logs show:
```
M: Debug: DMRSlot2RX: voice header found pos 420
M: Debug: LC decode failed - using placeholder IDs 0
```

Two issues:
1. Our LC decoder is failing (extraction bug)
2. Even if it worked, MMDVMHost doesn't want DMR_START in RX

## Recommended Fix

### Step 1: Fix LC Extraction (Done)
Fixed byte indexing in `DMRLC::extractData()` - was accessing `frame[i+1U]` instead of `frame[i]`.

### Step 2: Remove DMR_START Call

Remove the `serial.writeDMRStart()` call from DMRSlotRX.cpp. MMDVMHost will decode the LC itself from the DMR_DATA payload.

The voice header frame already contains all the LC data in the 33-byte payload. MMDVMHost just needs to receive the frame and it will decode it automatically.

### Step 3: Verify Payload Content

Add debug to dump the raw 33-byte payload to confirm it contains valid LC data.

## Action Items

1. Remove `writeDMRStart()` call from DT_VOICE_LC_HEADER case
2. Keep the `writeDMRData()` call - this is correct
3. Test and verify MMDVMHost decodes LC from payload
4. If MMDVMHost still doesn't show caller info, check:
   - Is the 33-byte payload correct?
   - Is MMDVMHost configured for this mode?
   - Are we in the right DMR mode (repeater vs simplex)?
