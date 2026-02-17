# DMR Timeslot Identification - ETSI Standards Analysis

## Problem: How Does MS_MODE Identify Timeslot 1 vs Timeslot 2?

### Background
In duplex repeater mode, the firmware uses a **hardware control signal** derived from the CACH (Common Announcement Channel) TC bit to identify timeslots. But in **MS_MODE** (single-chip RX listening to repeater), we don't have this hardware signal.

## ETSI DMR Standard Findings

### 1. TDMA Frame Structure (ETSI TS 102 361-1, Section 4.2)
- **TDMA Frame**: 60ms = two 30ms timeslots
- **Timeslot duration**: 30ms
  - 27.5ms: 264 bits of data
  - 2.5ms: Guard time or CACH

### 2. Burst Structure (264 bits)
```
[108 bits Payload] [48 bits SYNC/EMB] [108 bits Payload]
```

The center 48-bit field contains either:
- **SYNC pattern** (for first burst of superframe)
- **EMB (Embedded signalling)** (for bursts 2-6 of voice superframe)

### 3. SYNC Patterns (ETSI TS 102 361-1, Table 9.2)

#### Repeater Mode (What we're receiving)
| Source | Type | Hex Pattern |
|--------|------|-------------|
| **BS (Base Station)** | Voice | `75 5F D7 DF 75 F7` |
| **BS** | Data | `DF F5 7D 75 DF 5D` |
| **MS (Mobile Station)** | Voice | `7F 7D 5D D5 7D FD` |
| **MS** | Data | `D5 D7 F7 7F D7 57` |

**KEY OBSERVATION**: In repeater mode, **both timeslots use the SAME sync patterns**!

#### TDMA Direct Mode (NOT used in repeater systems)
| Timeslot | Type | Hex Pattern |
|----------|------|-------------|
| **TS1** | Voice | `5D 57 7F 77 57 FF` |
| **TS1** | Data | `F7 FD D5 DD FD 55` |
| **TS2** | Voice | `7D FF D5 F5 5D 5F` |
| **TS2** | Data | `D7 55 7F 5F F7 F5` |

In TDMA Direct mode, different sync patterns identify the timeslot.

### 4. Timeslot Identification in Repeater Mode

**From ETSI TS 102 361-1, Section 9.1.4 - CACH/TACT:**

The **CACH (Common Announcement Channel)** precedes each outbound burst from the base station and contains:
- **TC (TDMA Channel) bit**: Indicates if following burst is Channel 1 (TS1) or Channel 2 (TS2)
  - `TC = 0`: Channel 1
  - `TC = 1`: Channel 2

**In hardware duplex mode**: The ADF7021 decodes the CACH and provides the TC bit as a **control signal** to the firmware.

**In MS_MODE (single RX chip)**: We don't have hardware CACH decoding, so **no control signal available**.

## Solution: Alternating Sequence

### Why Toggle Works

Since the ETSI standard specifies:
1. **TDMA frame = 60ms** with two 30ms timeslots
2. Repeater **always** transmits TS1, then TS2, then TS1, then TS2...
3. This is a **fixed alternating sequence**

We can simply **toggle the slot after each complete frame is sent**:
```cpp
// After sending frame
m_slot = !m_slot;  // Toggle to next slot
```

### Self-Correction Mechanism

If we start out-of-phase (wrong initial slot assignment):
1. Voice transmissions use **6-burst superframes** (360ms)
2. Only **Burst A** has SYNC pattern
3. Bursts B-F have **EMB (Embedded signalling)**
4. If out of phase, EMB decode will fail, triggering re-sync
5. System will naturally lock to correct phase within 1-2 superframes

### Why Position-Based Detection Failed

Earlier attempt used absolute sync positions (e.g., "position 150-350 = TS1"). This failed because:
1. Sync positions **drift** based on when we lock onto the signal
2. The 576-bit circular buffer **wraps around**, causing positions to jump
3. Observed positions: 26, 184, 196, 262, 280, 314, 352, 472, 484, 550, 568...
4. No consistent absolute position for TS1 vs TS2

## Implementation Status

### Current Code (DMRSlotRX.cpp)

**Removed**: Slot toggle from `correlateSync()` (before frame processing)

**Added**: Slot toggle at end of `procSlot2()` (after frame is sent):
```cpp
// End of this slot, reset some items for the next slot.
m_control = CONTROL_NONE;

#if defined(MS_MODE)
// In MS_MODE, timeslots alternate. Toggle to next slot AFTER sending this frame.
m_slot = !m_slot;
#if defined(ENABLE_DEBUG)
DEBUG2("DMRSlotRX: Next slot will be", m_slot ? 2 : 1);
#endif
#endif
```

### Expected Behavior

**Debug logs should show**:
```
M: Debug: DMRSlot2RX: voice header found pos 314
M: Debug: Next slot will be 2
M: Debug: DMRSlot2RX: voice found pos 568
M: Debug: Next slot will be 1
M: Debug: DMR: Slot1 frames 50  <- ~50/50 split
M: Debug: DMR: Slot2 frames 50
```

**Pi-Star dashboard should display**:
- Caller ID (Name + DMR ID)
- Talkgroup number
- Timeslot indicator (TS1/TS2)
- Active call duration

## Conclusion

The ETSI DMR standards confirm:
1. **No explicit timeslot field** in repeater mode frames
2. **CACH TC bit** identifies slots (hardware-decoded in duplex)
3. **Fixed 30ms alternating sequence** in TDMA structure
4. **Toggle-after-send** is the correct software solution for MS_MODE

Our implementation aligns with the DMR protocol specification.
