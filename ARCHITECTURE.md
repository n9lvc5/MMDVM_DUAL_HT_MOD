# MMDVM_DUAL_HT_MOD Architecture Guide

**Purpose**: Educational reference for understanding how this firmware receives BS (Base Station) downlink transmissions and re-presents them to MMDVMHost/Pi-Star as MS (Mobile Station) uplinks.

**Target Audience**: Developers, testers, and anyone wanting to understand the signal flow and design decisions.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Signal Flow: BS→MS Forwarding](#signal-flow-bsms-forwarding)
3. [RX Pipeline Deep Dive](#rx-pipeline-deep-dive)
4. [Key Data Structures & Timing](#key-data-structures--timing)
5. [Critical Design Decisions](#critical-design-decisions)
6. [TX Path (MS Transmission)](#tx-path-ms-transmission)
7. [Troubleshooting & Debug Guide](#troubleshooting--debug-guide)

---

## System Overview

### Hardware Platform
- **MCU**: STM32F103C8T6 (64KB flash, 20KB SRAM)
- **Radio Board**: MMDVM_HS_Dual_Hat Rev 1.0 (dual ADF7021 + 14.7456 MHz TCXO)
- **Build Target**: Arduino IDE 2.3.6 with stm32duino board manager

### Firmware Purpose

Standard MMDVM_HS serves as a **duplex DMR hotspot**: two independent ADF7021 radios (RX + TX) service two time slots simultaneously.

This **MS_MODE variant** uses a **single `CDMRSlotRX` instance** to monitor **one ADF7021 (RX only)** for BS downlink traffic. Every burst received from the base station is processed and forwarded to MMDVMHost as if it originated from a mobile station. This allows Pi-Star dashboard to monitor live repeater traffic in real time.

### Key Invariant

All bits received from ADF7021 flow through **one RX processing engine**:

```
ADF7021 (449.9875 MHz) 
  ↓ (bit stream)
IO.cpp:process() [IO::process()]
  ↓ (routes to single RX instance)
DMRRX.cpp:databit() [CDMRRX::databit()]
  ↓ (forwards to single slot RX)
DMRSlotRX.cpp:databit() [CDMRSlotRX::databit()]
  ↓ (correlates sync, decodes CACH, extracts LC, re-encodes BPTC)
SerialPort.cpp:writeDMRData() [CSerialPort::writeDMRData()]
  ↓ (MMDVM packet over USB/serial)
MMDVMHost (Pi-Star)
  ↓ (dashboard display)
```

---

## Signal Flow: BS→MS Forwarding

### Phase 1: Synchronization & Slot Assignment

**File**: `DMRSlotRX.cpp:572-670` (`correlateSync()` method)

The receiver scans a 576-bit circular buffer for TDMA sync patterns. DMR sync is 48 bits; the firmware checks for both BS and MS patterns:

| Pattern | Bytes (DMRDefines.h) | Purpose |
|---------|---|---------|
| `DMR_BS_VOICE_SYNC_BYTES` | DMRDefines.h:55 | Base station voice downlink |
| `DMR_BS_DATA_SYNC_BYTES` | DMRDefines.h:54 | Base station data downlink |
| `DMR_MS_VOICE_SYNC_BYTES` | DMRDefines.h:53 | Mobile station voice (fallback) |
| `DMR_MS_DATA_SYNC_BYTES` | DMRDefines.h:52 | Mobile station data (fallback) |

**Why check both?** In standard duplex mode, this receiver services both directions (BS and MS traffic). For MS_MODE, we're listening to BS, so BS patterns are the primary match. MS patterns are checked as fallback for robustness.

**Sync Lock Mechanism** (DMRSlotRX.cpp:602-620):
- When a sync is first detected (`m_control != CONTROL_NONE`), the receiver sets `m_syncLocked = true`
- Initiates "flywheel" timing: subsequent bursts expected at predictable 288-bit intervals
- Resets on sync loss after `MAX_SYNC_LOST_FRAMES = 13` bursts without sync

**Sync Window Behavior** (DMRSlotRX.cpp:585-595):
- Normally: ±2 bits around expected sync position (tight window)
- When `m_state[slot] == DMRRXS_VOICE` (voice call in progress): **widened to ±5 bits**
- **Why widen?** Over a 10-second voice call (~167 bursts), even small crystal-oscillator drift (±50 ppm) accumulates. The terminator burst can drift beyond ±2 bits. Widening the window catches it while Golay slot-type validation still rejects false matches. (Session 6 fix)

### Phase 2: Time Slot Assignment (MS_MODE Feature)

**File**: `DMRSlotRX.cpp:703-765` (`decodeCACH()` method)

Once sync is locked, at burst offset 132 bits, the receiver decodes the **CACH** (Common Additional Channel) to determine which time slot (TS1 or TS2) the current burst belongs to.

**CACH Structure** (ETSI TS 102 361-1 Section 6.2):
- 24 bits at the start of each 288-bit TDMA slot
- Protected by Hamming(7,4) error correction
- Contains TC (Time Slot Control) bit that indicates the **next** slot

**TC Bit Mapping** (DMRSlotRX.cpp:755):
```cpp
uint8_t indicated_next_slot = t[1] ? 2U : 1U;  // TC=0 → TS1, TC=1 → TS2
```

**Why Hamming(7,4)?** CACH is critical for slot identification. A single-bit error could misroute frames to the wrong time slot, breaking the call. Hamming(7,4) can detect and correct single-bit errors. (ETSI TS 102 361-1 Section 6.2.4.2)

**Slot Hysteresis** (DMRSlotRX.cpp:756-764):
```cpp
if (m_currentSlot != indicated_next_slot) {
  if (++m_slotHysteresis >= 2U) {  // Require 2 consecutive matches
    m_currentSlot = indicated_next_slot;
    m_slotHysteresis = 0U;
  }
} else {
  m_slotHysteresis = 0U;
}
```

**Why hysteresis?** Prevents slot jitter if a single CACH frame is corrupted (e.g., by interference). Requires 2 consecutive identical TC values before changing slot. This keeps the receiver "locked" to the correct TS even if one burst is hit by noise.

---

### Phase 3: Frame Processing & LC Extraction

**File**: `DMRSlotRX.cpp:211-570` (`procSlot2()` method)

At burst offset 108 bits, the receiver begins extracting payload and call metadata. This is where BS downlink is transformed into MS uplink presentation.

#### Step 3a: Sync Byte Replacement (BS→MS Translation)

**Location**: DMRSlotRX.cpp:225-252

The 48-bit sync at frame positions bits 108-155 (bytes 14-20 with partial overlaps) is replaced:

```cpp
if (m_control == CONTROL_VOICE)
  memcpy(msSync, DMR_MS_VOICE_SYNC_BYTES, 7U);
else
  memcpy(msSync, DMR_MS_DATA_SYNC_BYTES, 7U);

// Apply inversion if BS sync was inverted (TDMA polarity flip)
if (m_inverted) {
  for (uint8_t i = 0U; i < 7U; i++)
    msSync[i] ^= DMR_SYNC_BYTES_MASK[i];
}

// Bit-level placement into frame (bytes 14-20)
frame[14U] = (frame[14U] & (0xFFU << 4U)) | (msSync[0U] & (0xFFU >> 4U));  // Lower nibble of byte 14
frame[15U] = msSync[1U];
// ... full bytes 16-18 ...
frame[20U] = (msSync[6U] & (0xFFU << 4U)) | (frame[20U] & (0xFFU >> 4U));  // Upper nibble of byte 20
```

**Why this matters**: MMDVMHost decodes the sync bytes to determine whether the burst came from an MS or BS. By replacing BS sync with MS sync, we convince the host that this frame originated from a mobile station, not a base station.

**Why bit-level manipulation?** The sync is 48 bits = 6 bytes. Frame bytes 14-20 contain 56 bits, so sync bits straddle byte boundaries. We must preserve the surrounding bits (slot type and LC fragments).

#### Step 3b: Slot Type Decoding & Re-encoding

**Location**: DMRSlotRX.cpp:259-283

```cpp
slotType.decode(frame + 1U, colorCode, dataType);
```

The slot type field (10 bits at positions 98-107 and 156-165) is **Golay(20,8) protected** (ETSI TS 102 361-1 Section 7.2.5). It contains:
- **4 bits**: Color Code (0-15) — user-configurable access control
- **4 bits**: Data Type — frame type (voice header, terminator, data, idle, etc.)

**After decoding**, the firmware re-encodes using a perfect Golay codeword:

```cpp
slotType.encode(colorCode, dataType, frame + 1U);
```

**Why re-encode?** The received Golay codeword may have had single-bit errors, which Golay corrects. Re-encoding produces a mathematically perfect codeword. MMDVMHost's Golay decoder always succeeds on the re-encoded version, avoiding any corruption.

#### Step 3c: Link Control (LC) Extraction & BPTC Re-encoding

**Files**: `DMRLC.cpp:41-97` (decode), `DMRSlotRX.cpp:363-374` (re-encode)

The **LC (Link Control)** is the 12-byte metadata field carrying source ID, destination ID, FLCO (Features & Link Control Opcode), and other call parameters. It is:
1. **BPTC(196,96) protected** with Hamming error correction (ETSI TS 102 361-1 Section B.3.9)
2. **CRC masked** with pattern depending on frame type (Voice Header vs Terminator)
3. **Reed-Solomon(12,9) protected** with 3 check bytes (ETSI TS 102 361-1 Section 9.2.5)

**Decode Flow** (DMRLC.cpp:41-97, called from DMRSlotRX.cpp:328):

```
1. Extract BPTC(196,96) payload from frame bits 0-97, 166-263
2. Call CBPTC19696::decode() → outputs 12 bytes (CRC mask still applied)
3. Call applyMask(data, dataType) → XOR removes CRC mask
   - DT_VOICE_LC_HEADER: mask = 0x96, 0x96, 0x96 (Table 9.16)
   - DT_TERMINATOR_WITH_LC: mask = 0x99, 0x99, 0x99 (Table 9.17)
4. Call CRS129::check() → validates Reed-Solomon check bytes (positions 9-11)
5. Extract fields:
   - srcId (bytes 6-8, 24-bit big-endian)
   - dstId (bytes 3-5, 24-bit big-endian)
   - FLCO (byte 0, bits 5-0)
   - FID (byte 1)
   - Options (byte 2)
6. Validate ID range: reject if srcId==0, dstId==0, or either > 16,777,215
```

**BPTC Interleave Formula** (BPTC19696.cpp:16-36):

ETSI TS 102 361-1 Formula B.1 specifies: **"Interleave Index = Index × 181 modulo 196"**

This means during transmission, the code bit at position `k` is placed at transmitted position `(181*k) mod 196`. During reception (decoding), we reverse it:

```cpp
INTERLEAVE_TABLE[k] = (181 * k) % 196
decoded[n] = received[INTERLEAVE_TABLE[n]]
```

**Why NOT 13?** A common mistake is using `(13*k) mod 196`, which is the **ENCODING** formula (inverse permutation). Decoding requires the inverse, which is `181*k mod 196`. (Session 10 fix)

**Hamming Error Correction** (BPTC19696.cpp:39-100):
- Hamming(13,9,3) for 15 columns: detects/corrects single-bit errors column-wise
- Hamming(15,11,3) for 9 rows: detects/corrects single-bit errors row-wise
- Together: correct any single bit error in the 96 information bits

**Re-encoding for MS_MODE** (DMRSlotRX.cpp:363-374):

After successful CDMRLC::decode(), the firmware re-encodes the LC:

```cpp
if (lcValid) {
  uint8_t lcClean[12U];
  memcpy(lcClean, lc.rawData, 12U);  // lc.rawData has mask removed
  
  // Re-apply mask so MMDVMHost's applyMask() step gets correct parities
  lcClean[9U]  ^= VOICE_LC_HEADER_CRC_MASK[0U];
  lcClean[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
  lcClean[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
  
  CBPTC19696 bptcEnc;
  bptcEnc.encode(lcClean, frame + 1U);  // Replace LC bits in frame
}
```

**Why re-apply mask before re-encoding?** 
- During decode: mask is removed so RS(12,9) check validates unmasked data
- During re-encode: mask must be reapplied because the Golay check bytes (bytes 9-11) include the mask in their computation
- MMDVMHost's decoder expects the masked version, so it can remove the mask and validate RS

**Why this complexity?** The firmware is **error-correcting**: raw received BPTC likely has single-bit errors (FEC didn't catch them). By re-encoding, we send a perfect codeword to MMDVMHost, making call metadata reliable even when RF conditions are poor.

#### Step 3d: Voice/Data Frame Routing

**Location**: DMRSlotRX.cpp:297-570 (switch statement on dataType)

After LC extraction, the frame type determines routing:

| Data Type | Hex | Name | Action | File Reference |
|-----------|-----|------|--------|---|
| 0 | `0x00` | Voice LC Header / PI Header | Extract LC, re-encode BPTC, send with RSSI | DMRSlotRX.cpp:311-376 |
| 1 | `0x01` | Voice Frame A | Send with RSSI (superframe seq 0) | DMRSlotRX.cpp:454-465 |
| 2 | `0x02` | Terminator with LC | Extract LC, signal call end, send lost notification | DMRSlotRX.cpp:472-525 |
| 6 | `0x06` | Data Header | Extract LC, re-encode BPTC, send with RSSI | DMRSlotRX.cpp:298-302 |
| 7, 8 | `0x07`, `0x08` | Rate 1/2 & 3/4 Data | Send with RSSI | DMRSlotRX.cpp:303-309 |
| 9 | `0x09` | Idle / Slot Sign | Idle pattern (no LC) | DMRSlotRX.cpp:540-548 |

**Voice Superframe Sequencing** (DMRSlotRX.cpp:454-470, Session 8 fix):

Voice calls transmit 27 bytes of AMBE audio per TDMA cycle, split across 6 frames:
- **Frame A** (9 bytes): `frame[0] = 0x20` (CONTROL_VOICE), with RSSI data appended
- **Frames B-F** (5 frames × 9 bytes): `frame[0] = 1, 2, 3, 4, 5` (sequence counter), no RSSI

```cpp
if (m_n[slot] == 0U) {
  frame[0U] = CONTROL_VOICE;  // 0x20
  writeRSSIData();             // Append RSSI, send frame (36 bytes)
  m_n[slot] = 1U;
} else {
  frame[0U] = m_n[slot];       // 1-5
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES);  // 33 bytes
  if (m_n[slot] >= 5U)
    m_n[slot] = 0U;            // Reset for next superframe
}
```

**Why frame-by-frame?** AMBE voice is streamed in real time. Each frame is sent immediately; the host buffers 6 frames to reconstruct a 27-byte AMBE packet, then decodes audio.

**Why RSSI on A-frame only?** (DMRSlotRX.cpp:816-846)
- RSSI (Received Signal Strength Indicator) is read once per call cycle
- Appending RSSI to every frame would be redundant and waste bandwidth
- Appending to the A-frame (superframe start) is conventional; host knows to apply to whole superframe

#### Step 3e: Terminator Detection & Call Cleanup

**Location**: DMRSlotRX.cpp:472-525 (Session 10 fix)

When a **DT_TERMINATOR_WITH_LC** frame is detected, the call has ended. The firmware:

```cpp
case DT_TERMINATOR_WITH_LC:
  // Extract LC one final time (call metadata)
  DMRLC_T lc = {0};
  bool lcValid = CDMRLC::decode(frame + 1U, dataType, &lc);
  
  // Signal both time slots as lost (cleans up MMDVMHost state)
  serial.writeDMRLost(slot);
#if defined(MS_MODE)
  // In duplex mode, also notify other slot
  serial.writeDMRLost(slot ^ 1U);
#endif
  
  // Reset all state (clear buffers, sync counters, etc.)
  reset();
  break;
```

**Why signal both slots?** (Session 8 fix)
- MMDVMHost tracks RX state per slot: `RS_RF_IDLE`, `RS_RF_AUDIO`, etc.
- If only one slot is notified, the other can remain stuck in `RS_RF_AUDIO`
- On the next voice header, that slot ignores the new header (already "active")
- Result: calls appear to skip or have no audio
- Notifying both slots ensures both return to `RS_RF_IDLE`, ready for next call

**Why reset()?** (DMRSlotRX.cpp:90-140)
- Clears circular bit buffer (`memset(m_buffer, 0, sizeof(m_buffer))`)
- Resets sync counters, state machine, LC flags
- Prevents stray bits from being processed as the start of next call
- Prevents LC data from prior call leaking into next call

---

## RX Pipeline Deep Dive

### The Circular Bit Buffer

**File**: DMRSlotRX.h:55, DMRSlotRX.cpp databit() method

The receiver maintains a **576-bit circular buffer** (`m_buffer[72]` = 72 bytes × 8 bits):

```
m_buffer[0..71]  ← 576 bits of history
  ↓
Each new bit shifts in; oldest bit drops out
  ↓
Two 288-bit "slots" per cycle (TS1 and TS2)
```

**Why circular?** The receiver doesn't know burst boundaries ahead of time. Sync patterns can occur anywhere in the bit stream. A circular buffer lets us scan continuously without reallocating.

**Data Flow** (DMRSlotRX.cpp:126-160):

```cpp
bool CDMRSlotRX::databit(bool bit)
{
  // 1. Shift new bit into buffer
  m_buffer[m_dataPtr >> 3U] = (m_buffer[m_dataPtr >> 3U] << 1) | (bit ? 1 : 0);
  m_dataPtr = (m_dataPtr + 1U) % DMR_BUFFER_LENGTH_BITS;
  
  // 2. Update pattern buffer (for sync detection)
  m_patternBuffer = (m_patternBuffer << 1) | (bit ? 1 : 0);
  
  // 3. Call correlateSync() to check for sync patterns
  correlateSync();
  
  // 4. If sync locked, call procSlot2() at offset +108 bits, decodeCACH() at offset +132
  if (m_syncLocked) {
    m_slotTimer++;
    if (m_slotTimer == 108U) procSlot2();
    if (m_slotTimer == 132U) decodeCACH();
    if (m_slotTimer >= 288U) {
      m_slotTimer = 0U;  // Reset for next slot
      m_endPtr = (m_endPtr + 288U) % DMR_BUFFER_LENGTH_BITS;  // Advance flywheel
    }
  }
  
  return m_syncLocked;
}
```

**Timing**: At 4,800 baud (DMR rate), 108 bits ≈ 22.5 ms; 288 bits ≈ 60 ms (one TDMA cycle).

---

### Color Code Validation

**Files**: DMRSlotRX.cpp:284, DMRSlotType.cpp:353-371

After decoding the slot type (which includes a 4-bit color code), the firmware validates:

```cpp
if (colorCode == m_colorCode || m_colorCode == 0U) {
  // Frame accepted; process normally
} else {
  // Frame silently dropped (switch statement only executes inside if block)
}
```

**Color Code Ranges**: 0-15 (4 bits per ETSI TS 102 361-1 Section 7.2.5)

| CC Value | Meaning | Notes |
|----------|---------|-------|
| 1-15 | Normal access control | Must match repeater CC |
| 0 | Bypass (test mode) | Accepts any CC; useful for diagnostics |

**Why CC=0 bypass in MS_MODE?** Allows testing without knowing the exact repeater CC. Can be disabled by setting `m_colorCode` to a specific value via SET_CONFIG from MMDVMHost.

---

### RSSI Data Handling

**File**: DMRSlotRX.cpp:816-846 (writeRSSIData method)

RSSI (Received Signal Strength Indicator) is a 16-bit value read from the ADF7021 via analog input:

```cpp
#if defined(SEND_RSSI_DATA)
  uint16_t rssi = io.readRSSI();
  frame[34U] = (rssi >> 8) & 0xFFU;  // High byte
  frame[35U] = (rssi >> 0) & 0xFFU;  // Low byte
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 3U);  // 36 bytes
#else
  serial.writeDMRData(slot, &frame[1], DMR_FRAME_LENGTH_BYTES);  // 33 bytes
#endif
```

**MMDVM Packet Format** (with RSSI):
```
[0xE0]           ← Frame start
[length=35]      ← 34 payload bytes + 1 for length byte itself = 35
[0x18 or 0x1A]   ← Command (DATA1 or DATA2)
[frame[0..35]]   ← Data (36 bytes: flags + 33-byte payload + 2-byte RSSI)
```

**Why conditional?** RSSI adds ~3% overhead. If signal conditions are stable, omitting RSSI saves bandwidth (Makefile config: `SEND_RSSI_DATA`).

---

## Key Data Structures & Timing

### Frame Layout (33 Bytes Payload)

**File**: DMRDefines.h, DMRSlotRX.cpp

```
Byte Index   Bits       Content
──────────────────────────────────────────────────
[0]          7:0        Flags/Sequence (0x20, 1-5, 0x40|dataType, etc.)
[1..13]      104:0      Slot Type + BPTC LC Part 1 (bits 0-97)
[14..20]     355:108    Sync bytes (bits 108-155) + overlaps
[21..33]     263:166    BPTC LC Part 2 (bits 166-263)
```

### TDMA Timing (288-bit slot = 60 ms at 4.8 kbaud)

```
Offset (bits)  Content              Timing (ms)    Handler
──────────────────────────────────────────────────────
0-23           CACH (24 bits)       0 ms           (sync detected here)
24-107         Slot Type Pt1 + LC   20 ms
108-155        Sync (48 bits)       22.5-32 ms     procSlot2() @ +108
156-179        Slot Type Pt2 + LC   32-37 ms
180-287        Voice/Data Payload   37-60 ms
─ End of Slot, next 288 bits begin ─
  (+132 total)                       27.5 ms        decodeCACH() @ +132 (reads next burst's TC)
```

**Why decodeCACH at +132?** It's 132 bits into the **current** burst. By this point, the next burst's CACH has just arrived (bursts are tightly spaced in TDMA). Reading the TC bit at this offset ensures we have the correct slot assignment before processing the **next** burst.

---

## Critical Design Decisions

### 1. BS→MS Sync Translation (Lines 225-252)

**Decision**: Replace all 6 sync bytes before forwarding to MMDVMHost.

**Why**: MMDVMHost's slot state machine (`CDMRSlot`) uses sync patterns to distinguish BS from MS traffic. Replacing BS sync with MS sync convinces the host that frames originated from a mobile station. This is the core of the MS impersonation.

**Alternative Considered**: Pass raw BS sync. **Rejected** because:
- MMDVMHost would treat all traffic as BS (not useful for monitoring)
- Would require modifying MMDVMHost code
- MS_MODE firmware would be non-standard

---

### 2. BPTC Re-encoding (Lines 363-374)

**Decision**: After CDMRLC::decode() succeeds, re-encode the cleaned LC back into BPTC(196,96) codeword.

**Why**: The received BPTC payload likely contains single-bit errors (over-the-air corruption). After Hamming correction, we have the correct 96 information bits. Re-encoding produces a mathematically perfect codeword. When MMDVMHost decodes it, no errors are found → LC decode always succeeds → talker alias and location data always extracted.

**Alternative Considered**: Send raw received BPTC bits. **Rejected** because:
- MMDVMHost's Hamming decoder may fail on 2+ bit errors
- LC would be discarded → no talker alias shown on dashboard
- MMDVMHost can't validate if RS check fails

**Trade-off**: We're adding ~5ms of CPU per LC frame (BPTC encoding), but gaining 100% reliability on LC decode.

---

### 3. Slot Hysteresis (Lines 756-764)

**Decision**: Require 2 consecutive identical TC bits before changing `m_currentSlot`.

**Why**: Prevents jitter if a single CACH frame is corrupted. If a burst has a bit flip in the TC bit, accepting it immediately could flip the slot. The next burst (with correct TC) would then flip it back, causing frame misrouting.

**Trade-off**: Introduces ~60ms latency before adapting to a slot change (2 burst cycles). Acceptable because slot changes are rare (only at call boundaries or dynamic TS allocation).

---

### 4. Sync Window Widening (Lines 585-595)

**Decision**: When receiving voice (`m_state == DMRRXS_VOICE`), widen sync search window from ±2 to ±5 bits.

**Why**: Long voice calls (10+ seconds = 167+ bursts) accumulate clock drift. A 50 ppm oscillator error × 167 × 60 ms ≈ 500 ms cumulative drift ≈ 2.4 bits drift at 4.8 kbaud. Without widening, the terminator burst (last frame) drifts beyond the ±2 window and is missed. The call times out instead of ending cleanly.

**Trade-off**: Wider window has ~0.1% false-positive rate for sync detection. Mitigated by Golay slot-type validation inside `correlateSync()`, which rejects non-DMR patterns.

---

### 5. Terminator Handling (Lines 472-525)

**Decision**: On DT_TERMINATOR_WITH_LC, immediately signal call end and reset state.

**Why**: ETSI DMR standard specifies terminator as the authoritative call-end marker. Waiting for sync loss (the prior behavior) caused visible delay on dashboard and could leave MMDVMHost in a stuck state between calls.

**Trade-off**: If a terminator is received by error (corruption), the call ends immediately. Mitigated by Reed-Solomon check on LC (only terminators with valid LC are accepted as call-end).

---

### 6. MS_MODE Compilation Flag

**Decision**: Use `#if defined(MS_MODE)` to fork behavior.

**Why**: Allows the same firmware codebase to support both standard duplex mode and MS monitoring mode. Keeps changes isolated and reversible.

**Affected Sections**:
- IO.cpp:228-242 — RX gating (always active in MS_MODE)
- SerialPort.cpp — writeDMRData/writeDMRLost state guards (bypassed)
- DMRSlotRX.cpp:61-280 — MS-specific slot tracking and LC re-encoding
- DMRTX.cpp — TX always returns error (TX not supported in MS_MODE)

---

## TX Path (MS Transmission)

**File**: DMRTX.cpp/h, DMRTX.h:32-40 (state machine)

The TX path is **simplified for MS behavior** (Session 6 removal of BS handshake logic):

### State Machine (No BS States)

```
         ┌─ REQUEST_CHANNEL ─ SLOT1 ─ CACH1 ─┐
         │                                     │
    IDLE ┤                                     └─ SLOT2 ─ CACH2 ─┐
         │                                                        │
         └──────────────────────────────────────────────────────┘
                    (idle timeout = 600 ms)
```

**Key Points**:
- No PREAMBLE state (MS doesn't transmit preamble)
- No WAIT_BS_CONFIRM state (MS doesn't wait for BS to grant channel)
- No BACKOFF state (MS doesn't have backoff logic)
- **Direct loop**: REQUEST_CHANNEL → TS1/CACH1/TS2/CACH2 → loop

### Sync Byte Selection (Lines 198-232)

```cpp
void CDMRTX::createData(uint8_t slotIndex, bool forceIdle) {
  // Always use MS sync bytes (not BS)
  if (dataType == DT_VOICE_LC_HEADER || dataType == DT_VOICE_PI_HEADER)
    memcpy(txSync, DMR_MS_VOICE_SYNC_BYTES, 7U);
  else
    memcpy(txSync, DMR_MS_DATA_SYNC_BYTES, 7U);
  
  // Bit-level sync placement (same as RX, but in reverse)
  // ... sync at bits 108-155 ...
}
```

**Why always MS sync?** This firmware **impersonates an MS**. MS always transmits MS sync, never BS sync.

---

## Troubleshooting & Debug Guide

### Blank Dashboard (No Calls Appearing)

**Symptom**: RF signal is present, but Pi-Star dashboard shows no calls.

**Diagnosis Steps**:

1. **Check COS LED** (Carrier Operated Squelch)
   - Should light when RF is received
   - If dark: RF not reaching firmware (antenna, frequency mismatch, color code)
   
2. **Force Color Code=0** (test mode)
   - Edit `DMRSlotRX.cpp:806`, change `setColorCode()` to set `m_colorCode = 0U`
   - Recompile, flash
   - If calls then appear: color code mismatch with repeater CC
   
3. **Enable MMDVMHost Debug**
   - Add `Debug=1` under `[Modem]` in mmdvmhost config
   - Restart MMDVMHost
   - Monitor log for "DMRRX: First sync detected!" → confirms firmware is receiving

4. **Check Firmware Version**
   - Verify `version.h` is up-to-date (compare date with last build)
   - If stale, file sync may have failed → rebuild

5. **Verify MMDVM Packet Structure**
   - Enable serial sniffer on USB/UART
   - Should see `0xE0 [length] [cmd] [data...]` packets
   - Check that length is correct (0x22 = 34 bytes = "35" packet length)

### Single Burst on Dashboard (No Superframes)

**Symptom**: Dashboard shows "RF Header" but no voice frames follow.

**Root Cause**: Voice B-F frames not being forwarded (likely length mismatch or sequence byte error).

**Verification** (DMRSlotRX.cpp:454-470):
- Voice A-frame: `frame[0] = 0x20`, sent via `writeRSSIData()` (36 bytes with RSSI)
- Voice B-F frames: `frame[0] = 1-5`, sent via `writeDMRData()` (33 bytes)
- If B-F frames use wrong length or sequence, MMDVMHost silently drops them

### No RSSI Values Reported

**Symptom**: Dashboard shows calls but RSSI column is blank.

**Check**: Is `SEND_RSSI_DATA` enabled in `Config.h`?

**Alternative**: If RSSI is intentionally disabled to save bandwidth, this is normal behavior.

### Calls End with "Sync Lost" Instead of Terminator

**Symptom**: Dashboard shows call duration as ~13-14 frames (≈800 ms) regardless of actual call length.

**Root Cause**: Sync window is ±2 bits; terminator burst drifts beyond window over long calls.

**Solution**: Already implemented (Session 6 fix). Verify `DMRSlotRX.cpp:588-595` has:

```cpp
if (m_state[0U] == DMRRXS_VOICE || m_state[1U] == DMRRXS_VOICE)
  searchWinEnd = m_syncPtr + 5U;  // ±5 bits during voice
else
  searchWinEnd = m_syncPtr + 2U;  // ±2 bits normally
```

If not present, cherry-pick Session 6 fixes from git history.

---

## Key Files & Line References (Quick Lookup)

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| **RX Engine** | DMRSlotRX.cpp | 126-160 | `databit()` — bit-by-bit processing |
| | DMRSlotRX.cpp | 572-620 | `correlateSync()` — sync detection |
| | DMRSlotRX.cpp | 211-570 | `procSlot2()` — frame processing |
| | DMRSlotRX.cpp | 703-765 | `decodeCACH()` — TC bit reading |
| **LC Extraction** | DMRLC.cpp | 41-97 | `decode()` — BPTC, mask, RS check |
| | DMRLC.cpp | 142-171 | `applyMask()` — CRC mask removal |
| | BPTC19696.cpp | 112-140 | `decode()` — BPTC(196,96) |
| | BPTC19696.cpp | 208-300 | `encode()` — BPTC re-encoding |
| **Sync Translation** | DMRSlotRX.cpp | 225-252 | BS→MS sync replacement |
| **RSSI Handling** | DMRSlotRX.cpp | 816-846 | `writeRSSIData()` |
| **Terminator** | DMRSlotRX.cpp | 472-525 | Call end detection & cleanup |
| **TX Path** | DMRTX.cpp | 49-79 | `writeData1/2()` — frame queuing |
| | DMRTX.cpp | 198-232 | `createData()` — MS sync selection |
| **Serial/MMDVM** | SerialPort.cpp | 973-1005 | `writeDMRData()` — packet formatting |
| **Config** | Config.h | 1-100 | Feature flags (MS_MODE, SEND_RSSI_DATA, etc.) |
| **Constants** | DMRDefines.h | 40-96 | Sync bytes, data types, frame lengths |

---

## Glossary

| Term | Definition |
|------|-----------|
| **BS** | Base Station — the radio repeater |
| **MS** | Mobile Station — a handheld radio or hotspot |
| **TDMA** | Time Division Multiple Access — 2 slots per 60 ms cycle |
| **CACH** | Common Additional Channel — 24-bit field with slot control |
| **TC** | Time Slot Control bit — indicates next time slot (TS1 or TS2) |
| **LC** | Link Control — 12-byte metadata (srcId, dstId, FLCO, etc.) |
| **BPTC** | Block Product Turbo Code — FEC with Hamming in 2 dimensions |
| **Golay** | Golay(20,8) — FEC for slot type field |
| **CRC Mask** | Pattern XORed with RS check bytes per ETSI Table 9.16 |
| **RSSI** | Received Signal Strength Indicator — RF power level |
| **Flywheel** | Predictable timing after sync lock (no sync needed every burst) |
| **Sync Window** | ±N bits around predicted sync position for tolerance |
| **Slot Hysteresis** | Require 2 consecutive identical values before state change |

---

## Further Reading

- **ETSI TS 102 361-1**: DMR Air Interface Protocol (definitive specification)
  - Section 6.2: CACH, TC bit
  - Section 7.2.5: Slot type, Golay(20,8), color code
  - Section 9.2.5: LC, CRC masks, RS(12,9)
  - Section B.3.9: BPTC(196,96), interleave formula
  
- **MMDVM_HS (original)**: https://github.com/juribeparada/MMDVM_HS
  - Reference for standard duplex behavior
  - Source of RX/TX state machines (Session 6 modifications)

---

**Document Version**: 2026-04-29  
**Applicable Firmware**: fix/rx-signal-acknowledgment branch  
**Status**: Complete (Phases 1-3 validation passed)
