# DMR Slot Routing Fix - Implementation Log

## Problem Summary
Pi-Star was not showing caller information (name, DMR ID, talkgroup) despite receiving frames from both timeslots. Logs showed voice headers being detected but no user data appearing on dashboard.

## Root Cause Analysis

### Issue 1: Slot Toggling Timing
**Original bug**: Slot toggle happened BEFORE frame processing in `correlateSync()`, causing frames to be sent with the NEXT slot number instead of CURRENT.

**Fix**: Moved toggle to end of `procSlot2()` AFTER frame transmission (line 259):
```cpp
#if defined(MS_MODE)
// In MS_MODE, timeslots alternate. Toggle to next slot AFTER sending this frame.
m_slot = !m_slot;
#endif
```

**Result**: Perfect 50/50 slot distribution confirmed in logs.

### Issue 2: Voice Headers Not Transmitted
**Critical bug**: Voice headers (DT_VOICE_LC_HEADER) were detected and logged (line 179-183), but the frame data was **never sent to Pi-Star**.

The code flow was:
1. Voice header detected → `writeRSSIData()` called
2. State set to `DMRRXS_VOICE`
3. But **no `serial.writeDMRData()` call** to send the header frame!

**Why this breaks Pi-Star**:
- Voice headers contain the **Link Control (LC)** data
- LC includes: Source DMR ID, Destination TG, Call type (group/private)
- Without the header, Pi-Star has **no caller information** to display
- Subsequent voice frames (Bursts B-F with EMB) only contain audio

**Fix Applied** (DMRSlotRX.cpp line 183-191):
```cpp
case DT_VOICE_LC_HEADER:
  DEBUG2("DMRSlot2RX: voice header found pos", m_syncPtr);
  writeRSSIData();
  m_state = DMRRXS_VOICE;
  {
    uint8_t slot = m_slot ? 1U : 0U;
#if defined(MS_MODE)
    io.DSTAR_pin(slot == 0U);
    io.P25_pin(slot == 1U);
#endif
    serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
  }
  break;
```

**Also fixed**: Voice terminator (DT_TERMINATOR_WITH_LC) now also sent (line 200-211)

### Issue 3: Frame Data Verification
**Added debug logging** in `SerialPort.cpp` to dump first bytes of each frame:
```cpp
// Debug: Dump first 10 bytes of frame to verify structure
static uint32_t dumpCounter = 0;
dumpCounter++;
if (dumpCounter <= 5) {
  DEBUG2I("DMR Frame dump - Slot", slot ? 2 : 1);
  DEBUG2I("  Control byte [0]", data[0]);
  if (length >= 4) {
    DEBUG2I("  Payload [1-3]", (data[1] << 16) | (data[2] << 8) | data[3]);
  }
}
```

## Expected Debug Output After Fix

### Voice Call Start
```
M: Debug: DMRSlot2RX: voice header found pos 314
M: Debug: DMR Frame dump - Slot 2
M: Debug:   Control byte [0] 65    <- 0x41 = CONTROL_DATA (0x40) | DT_VOICE_LC_HEADER (1)
M: Debug:   Payload [1-3] <LC data with DMR ID and TG>
M: Debug: DMRSlotRX: Next slot will be 1
```

### Voice Frames
```
M: Debug: DMR Frame dump - Slot 1
M: Debug:   Control byte [0] 32    <- 0x20 = CONTROL_VOICE (Burst A re-sync)
M: Debug:   Payload [1-3] <voice audio + AMBE>
M: Debug: DMRSlotRX: Next slot will be 2

M: Debug: DMR Frame dump - Slot 2
M: Debug:   Control byte [0] 1     <- Sequence 1 (Burst B with EMB)
M: Debug:   Payload [1-3] <voice audio + EMB>
M: Debug: DMRSlotRX: Next slot will be 1
```

### Voice Call End
```
M: Debug: DMRSlot2RX: voice terminator found pos 550
M: Debug: DMR Frame dump - Slot 2
M: Debug:   Control byte [0] 66    <- 0x42 = CONTROL_DATA (0x40) | DT_TERMINATOR_WITH_LC (2)
M: Debug:   Payload [1-3] <LC terminator data>
M: Debug: DMRSlotRX: Next slot will be 1
```

## Pi-Star Dashboard Expected Behavior

After these fixes, the dashboard should show:
- **Caller Name**: (e.g., "N9LVC 5")
- **Source ID**: 7-digit DMR ID
- **Destination**: Talkgroup number
- **Timeslot**: TS1 or TS2 indicator
- **Duration**: Active call timer
- **RSSI/BER**: Signal quality

## ETSI DMR Standards Reference

### Frame Structure (ETSI TS 102 361-1)
- **Voice Superframe**: 6 bursts (360ms)
  - **Burst A**: Contains SYNC pattern
  - **Bursts B-F**: Contain EMB (Embedded signalling)
  
- **Burst structure**: 264 bits
  - 108 bits: Payload (voice/data)
  - 48 bits: SYNC (Burst A) or EMB (Bursts B-F)
  - 108 bits: Payload (voice/data)

### Link Control (LC) in Voice Headers
- **12 bytes** (96 bits) of LC data
- **FLC (Full LC)**: Sent in voice header (Burst A)
- Contains:
  - **FLCO** (Full LC Opcode): Call type (group, private, etc.)
  - **FID** (Feature set ID): 0x00 for standard
  - **Source ID**: 24 bits (DMR ID of caller)
  - **Destination ID**: 24 bits (Talkgroup or target ID)

Pi-Star decodes this LC data from the voice header to populate the dashboard.

## Files Modified

1. **DMRSlotRX.cpp**:
   - Line 259: Moved slot toggle to after frame send
   - Line 183-191: Added `serial.writeDMRData()` for voice headers
   - Line 204-211: Added `serial.writeDMRData()` for voice terminators

2. **SerialPort.cpp**:
   - Line 1132-1141: Added frame dump debug logging

## Testing Instructions

1. Compile and flash firmware in Arduino IDE
2. Connect to Pi-Star and start monitoring MMDVM logs:
   ```bash
   sudo tail -f /var/log/pi-star/MMDVM-*.log
   ```
3. Initiate DMR call on repeater
4. Verify debug output shows:
   - Voice header with control byte 0x41 (65)
   - Voice frames with control byte 0x20 (32) or 1-5
   - Voice terminator with control byte 0x42 (66)
   - Slot distribution remains 50/50
5. Check Pi-Star dashboard shows caller information

## Success Criteria

- [x] Slot routing correct (50/50 distribution)
- [ ] Voice headers transmitted to Pi-Star
- [ ] Caller information appears on dashboard
- [ ] Talkgroup displayed correctly
- [ ] Timeslot indicator shows TS1/TS2
- [ ] Call duration timer updates

## Next Steps

After user compiles and tests:
1. Verify frame dumps show correct control bytes
2. Check Pi-Star logs for LC decode messages
3. Confirm dashboard displays caller information
4. If still no caller info, compare frame structure with stock MMDVM
