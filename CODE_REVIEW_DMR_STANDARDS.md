# DMR Standards Compliance & Bug Identification Review
## MMDVM_DUAL_HT_MOD BS→MS Forwarding Chain

**Date:** 2026-03-17
**Scope:** BS reception conversion to MS transmission format, forwarding to MMDVMHost/Pi-Star
**Mode:** MS_MODE (Mobile Station receive-only bridge)

---

## 1. SYNC PATTERN CONVERSION (DMRSlotRX.cpp:204-217)

### Current Implementation
Replaces BS sync bytes with MS sync bytes at frame[14..20] when `m_control != CONTROL_NONE`:
```c
if (m_control == CONTROL_VOICE) {
  const uint8_t* msSync = DMR_MS_VOICE_SYNC_BYTES;
} else {
  const uint8_t* msSync = DMR_MS_DATA_SYNC_BYTES;
}
frame[14U] = (frame[14U] & 0xF0U) | (msSync[0U] & 0x0FU);
frame[15U] = msSync[1U];
frame[16U] = msSync[2U];
// ... etc
```

### Issues Identified

#### **ISSUE 1.1: Partial Byte Masking at Sync Boundaries**
- **Location:** DMRSlotRX.cpp:210, 216 (frame[14] and frame[20] masking)
- **Problem:** The code masks nibbles (half-bytes) at sync boundaries using `0xF0U` and `0x0FU`, assuming 4-bit alignment. However:
  - Sync bits start at bit 108 of the 264-bit burst = byte 13.5 (byte 13, bit 4)
  - The frame structure has: frame[0]=control, frame[1..33]=payload (264 bits)
  - Bit 108 of payload = byte 13 + 4 bits = byte index 13, local bit 4
  - This should use proper bit-wise masking, not nibble masking

#### **ISSUE 1.2: Sync Pattern Hardcoded**
- **Location:** DMRSlotRX.cpp:209
- **Problem:** The code checks `m_control` to decide voice vs. data sync, but this is determined from pattern correlation earlier. If a sync pattern is inverted (DMR_BS_VOICE_SYNC_BITS_INV or DMR_BS_DATA_SYNC_BITS_INV), the inverted pattern is lost during re-encoding with clean MS sync bytes.
  - **DMR Spec Impact:** ETSI TS 102 361-1 allows sync pattern inversion for signal quality measurements. Re-encoding with fixed MS sync may lose this information.

---

## 2. TIME SLOT ASSIGNMENT (DMRSlotRX.cpp:541-557, 588-641)

### Current Implementation
- **Initial Slot (First Sync):** Reads TC bit from current burst's CACH at position `(m_dataPtr + DMR_BUFFER_LENGTH_BITS - 178) % 576`
- **Ongoing Updates:** Reads CACH at m_syncPtr+109 (next burst's CACH) and corrects slot identity at m_slotTimer==132

### Issues Identified

#### **ISSUE 2.1: CACH Reading at TC Bit Position**
- **Location:** DMRSlotRX.cpp:550-552
- **Specification:** ETSI TS 102 361-1 Section 6.1.1
  - CACH is 24 bits starting at bit 0 of the 288-bit slot (bits 0-23)
  - Sync ends at bit 179
  - Therefore: CACH bit 1 (TC) is 179 bits BEFORE sync end
  - Correct position: `(m_dataPtr + DMR_BUFFER_LENGTH_BITS - 178) % 576` ✓ **Correct**

**Verdict:** TC bit position calculation appears correct, but the timing between initial read and flywheel updates creates potential race conditions (see Issue 2.3).

#### **ISSUE 2.2: CACH Decoding Error Correction Missing**
- **Location:** DMRSlotRX.cpp:588-641 (decodeCACH function)
- **Problem:** The code extracts 24 bits and performs Hamming(7,4) error correction on the TACT bits, but:
  - Line 623: Multi-bit errors silently return without correction
  - Line 631: If slot identity differs from flywheel expectation, only corrects m_currentSlot without logging or tracking frequency of corrections
  - **No validation** that the corrected TC bit is stable across multiple bursts

#### **ISSUE 2.3: Slot Identity Flip During Voice Calls**
- **Location:** DMRSlotRX.cpp:630-639 + procSlot2 flywheel logic (480-485)
- **Problem:** The flywheel advances m_syncPtr by 288 bits every burst in procSlot2(), regardless of the decodeCACH correction applied 24 bits later.
  - procSlot2() fires at m_slotTimer==108 (before procSlot2 advances pointers)
  - decodeCACH() fires at m_slotTimer==132 (after procSlot2 already advanced m_endPtr += 288)
  - If decodeCACH corrects slot identity, the frame already sent to MMDVMHost has the wrong slot number

**DMR Standard Violation:** ETSI TS 102 361-1 Section 6.1.3 requires consistent time slot identity throughout a single transmission. A mid-call slot flip will cause MMDVMHost to log the call on the wrong slot.

---

## 3. COLOR CODE VALIDATION (DMRSlotRX.cpp:230)

### Current Implementation
```c
if (colorCode == m_colorCode || m_colorCode == 0U) {
  m_syncCount[0U] = 0U;
  m_syncCount[1U] = 0U;
  // ... process frame
}
```

### Issues Identified

#### **ISSUE 3.1: Color Code Mismatch Silent Discard**
- **Location:** DMRSlotRX.cpp:230
- **Problem:** When color code doesn't match and m_colorCode ≠ 0, the frame is silently discarded (no logging, no counter update).
  - No debug output indicating CC rejection
  - m_syncCount not reset, leading to false "sync lost" detection after 13 consecutive mismatches
  - User has no visibility into why frames are being dropped

**DMR Standard Compliance:** ETSI TS 102 361-1 requires BS to MS gateways to validate color code. Current implementation is compliant in behavior but lacks observability.

---

## 4. LINK CONTROL DATA EXTRACTION & RE-ENCODING (DMRSlotRX.cpp:257-323, 335-363)

### Current Implementation

**Voice Header (DT_VOICE_LC_HEADER):**
1. Extract BPTC-decoded 12-byte LC via CDMRLC::decode()
2. If lcValid: Re-apply CRC mask (lines 311-313)
3. Call bptcEnc.encode() to regenerate clean BPTC bits
4. Write re-encoded bits to frame payload (bits 0-97, 166-263)

**Terminator (DT_TERMINATOR_WITH_LC):**
- Same flow, different CRC mask

### Issues Identified

#### **ISSUE 4.1: CRC Mask Re-application Sequence**
- **Location:** DMRSlotRX.cpp:305-316
- **Problem:**
  ```c
  CBPTC19696 bptcEnc;
  bptcEnc.encode(lcClean, frame + 1U);
  ```
  The encode() function expects clean 12-byte LC data. The code re-applies the CRC mask BEFORE encoding. However:
  - lc.rawData has the CRC mask REMOVED by CDMRLC::decode() → applyMask()
  - Re-applying the mask yields the "RS-protected" form
  - MMDVMHost's CFullLC::decode() calls applyMask() and then RS check
  - **Correct behavior:** Encode the mask-removed form, not the mask-reapplied form

**Potential Issue:** If MMDVMHost applies the mask again, the mask will be double-applied (XOR with 0 = identity), yielding correct result. **However**, this creates hidden fragility: if the mask constants change or are applied inconsistently, decoding will fail silently.

#### **ISSUE 4.2: LC Validity Check Without RS Validation**
- **Location:** DMRLC.cpp:56-63
- **Problem:** The decode() function accepts LC via "plausibility check" even when Reed-Solomon check fails:
  ```c
  if (!rsOk && plausible) {
    DEBUG1("LC accepted via plausibility check");
    return true;  // ACCEPTED without RS validation
  }
  ```
  - Plausibility check: flco ≤ 1 AND dstId ∈ [1, 16777215]
  - **No validation** of srcId, FID, service options, or the parity bytes
  - **DMR Spec Violation:** ETSI TS 102 361-1 Table 9.18 specifies RS(12,9) must be checked. Accepting without RS validation may introduce corrupted LC data into the network.

#### **ISSUE 4.3: Missing Validation of LC Fields**
- **Location:** DMRLC.cpp:51-52, DMRRX.cpp (implicit)
- **Problem:** After LC decode, no validation of:
  - FLCO opcode (must be valid for the use case)
  - Feature ID (must be valid for FLCO)
  - Service options (bits must be valid per spec)
  - srcId (valid caller ID range)

**No rejection** of invalid combinations like:
  - FLCO=Group Call with invalid TG
  - FID=0xFF (reserved) with valid FLCO
  - dstId=0 (reserved, only used internally)

---

## 5. SLOT TYPE GOLAY RE-ENCODING (DMRSlotRX.cpp:224-228)

### Current Implementation
```c
CDMRSlotType slotType;
slotType.decode(frame + 1U, colorCode, dataType);
slotType.encode(colorCode, dataType, frame + 1U);
```

### Issues Identified

#### **ISSUE 5.1: Golay Encoding Without Inversion Handling**
- **Location:** DMRSlotRX.cpp:224-228
- **Problem:** The Golay decode/encode cycle doesn't preserve inversion state.
  - If the received BS sync pattern was inverted (DMR_BS_DATA_SYNC_BITS_INV), the slot type was extracted assuming inversion
  - After re-encoding with encode(), the slot type is written without inversion
  - **DMR Spec Implication:** ETSI TS 102 361-1 doesn't mandate sync inversion, but some repeaters use it for quality control. Loss of inversion flag may indicate degraded signal quality

**Current Behavior:** Frame is presented to MMDVMHost with different signal quality indication than received. This is masking, not a protocol error, but reduces observability.

---

## 6. VOICE FRAME CONTINUATION LOGIC (DMRSlotRX.cpp:449-457)

### Current Implementation
Voice frames are forwarded as-is after voice header, using a frame counter:
```c
if (m_state[slot] == DMRRXS_VOICE) {
  if (m_n[slot] >= 5U) {
    frame[0U] = CONTROL_VOICE;
    m_n[slot] = 0U;
  } else {
    frame[0U] = ++m_n[slot];
  }
  serial.writeDMRData(slot, frame, DMR_FRAME_LENGTH_BYTES + 1U);
}
```

### Issues Identified

#### **ISSUE 6.1: Frame Counter Semantics**
- **Location:** DMRSlotRX.cpp:450-455
- **Problem:** The logic encodes:
  - m_n[slot] = 0 or 1-5 → frame[0] = 1-5 (continuation frames B-F)
  - m_n[slot] = 6 (after >=5) → frame[0] = CONTROL_VOICE (0x20)

  **Ambiguity:** What does frame[0] = CONTROL_VOICE (0x20) mean when sent mid-call?
  - **DMR Spec:** Voice frames within a superframe should NOT have the SYNC bit set (CONTROL_VOICE implies sync)
  - If this is intended to reset the counter after 6 frames, it should use a different control byte (e.g., 0x00 for "no sync")

**Current Behavior:** Every 6th frame is marked as CONTROL_VOICE, which may cause MMDVMHost to treat it as containing a sync pattern when it doesn't.

#### **ISSUE 6.2: Voice Frame Timing Robustness**
- **Location:** DMRSlotRX.cpp:449-467
- **Problem:** The code assumes all received bursts will be marked as voice (m_state[slot] == DMRRXS_VOICE) for the duration of the call.
  - No validation that voice frames arrive in expected order or timing
  - If a frame is lost (gap in m_dataPtr), m_n[slot] counter may become desynchronized
  - No recovery mechanism if counter gets out of sync with the actual voice burst sequence

---

## 7. FRAME STRUCTURE & SERIAL PROTOCOL (SerialPort.cpp:960-983)

### Current Implementation
```c
uint8_t reply[40U];
reply[0U] = MMDVM_FRAME_START;
reply[1U] = 0U;
reply[2U] = slot ? MMDVM_DMR_DATA2 : MMDVM_DMR_DATA1;

uint8_t count = 3U;
for (uint8_t i = 0U; i < length; i++, count++)
  reply[count] = data[i];
reply[1U] = count;

writeInt(1U, reply, count);
```

### Issues Identified

#### **ISSUE 7.1: Control Byte Encoding Inconsistency**
- **Location:** SerialPort.cpp:974
- **Problem:** The mapping is:
  - slot=0 (TS1) → MMDVM_DMR_DATA1 (0x18)
  - slot=1 (TS2) → MMDVM_DMR_DATA2 (0x1A)

  However, DMRSlotRX uses:
  - m_currentSlot = 1 → TS1
  - m_currentSlot = 2 → TS2

  And writeRSSIData() maps:
  ```c
  uint8_t slot = m_currentSlot - 1U;  // 0 or 1
  serial.writeDMRData(slot, frame, ...);
  ```

  **Correct conversion** is happening (m_currentSlot - 1 = 0 or 1 maps to DATA1/DATA2). However, the parameter name `slot` is ambiguous: sometimes it's 0-based, sometimes 1-based.

#### **ISSUE 7.2: Frame Size Assumption**
- **Location:** SerialPort.cpp:970, 977-980
- **Problem:**
  - length parameter expected to be DMR_FRAME_LENGTH_BYTES + 1 (34 bytes = frame[0] control + 33 payload)
  - Array `reply[40U]` provides: 3 header bytes + up to 37 payload bytes = 40 bytes total
  - If length = 34, count = 37, array bounds are OK
  - **No validation** that length ≤ 34; if length > 34, buffer overflow occurs

---

## 8. MS_MODE SYNC LOCKING & INITIALIZATION (DMRSlotRX.cpp:541-557)

### Current Implementation
On first sync detection:
```c
if (!m_syncLocked) {
  uint16_t tcBitPos = (m_dataPtr + DMR_BUFFER_LENGTH_BITS - 178U) % DMR_BUFFER_LENGTH_BITS;
  bool initTc = READ_BIT1(m_buffer, tcBitPos);
  m_currentSlot = initTc ? 2U : 1U;
  m_syncLocked = true;
}
```

### Issues Identified

#### **ISSUE 8.1: Initial TC Bit Reading Before Full Buffer**
- **Location:** DMRSlotRX.cpp:550-552
- **Problem:** m_dataPtr is the CURRENT bit position being written. Reading CACH at `m_dataPtr - 178` assumes 178 bits have already been written into m_buffer at that position.
  - On the first few bursts (while m_dataPtr < 178), this wraps around to the end of the circular buffer, reading stale data
  - Example: If sync is detected at m_dataPtr = 100, the read tries to access position (100 + 576 - 178) % 576 = 498, which may contain old data from a previous transmission

**Severity:** Mitigated by the fact that sync is only searched when m_state == DMRRXS_NONE (not in a call), and the flywheel quickly resynchronizes. However, the initial slot assignment could be wrong.

#### **ISSUE 8.2: No Hysteresis on Slot Identity Changes**
- **Location:** DMRSlotRX.cpp:631-641
- **Problem:** decodeCACH corrects m_currentSlot on every burst if it differs. If the TC bit flips due to noise or multi-bit errors in the CACH:
  - No hysteresis (requiring N consecutive bursts with same slot before switching)
  - Frame is already sent with wrong slot identity before correction
  - Causes MMDVMHost to see frames on alternating slots

---

## 9. CALL STATE MANAGEMENT (DMRSlotRX.cpp:259-289, 372-378)

### Current Implementation
- **Call Start (Voice Header):** `newVoiceCall = (m_state[0U] == DMRRXS_NONE && m_state[1U] == DMRRXS_NONE)`
- **Call End (Terminator):** m_state[slot] = DMRRXS_NONE

### Issues Identified

#### **ISSUE 9.1: Other Slot State Reset During New Call**
- **Location:** DMRSlotRX.cpp:264-265
- **Problem:**
  ```c
  if (newVoiceCall)
    m_state[slot ^ 1U] = DMRRXS_NONE;
  ```
  When a new call starts, the OTHER slot is reset to DMRRXS_NONE. However:
  - In MS_MODE, there's only ONE physical receiver, so m_currentSlot alternates
  - m_state[0] and m_state[1] don't track two independent slots; they track the same flywheel alternating
  - This code resets "the other slot" that doesn't exist in MS_MODE

**Potential Issue:** If m_currentSlot flips during frame extraction (due to decodeCACH correction), the state reset may affect the wrong logical slot.

#### **ISSUE 9.2: Missing Call Duration Logging**
- **Location:** DMRSlotRX.cpp:370-432
- **Problem:** When a voice call ends due to explicit DT_TERMINATOR_WITH_LC:
  - m_callActive[slot] is set to false
  - Call duration is logged in the "sync lost" handler (425-432), not the terminator handler

  When a call ends normally (terminator received):
  - No call duration logged
  - MMDVMHost sees the terminator but has no record of call start→end in the modem logs
  - User cannot verify call completion from modem logs alone

---

## 10. MISSING FEATURES & DATA FIELDS

### Issues Identified

#### **ISSUE 10.1: No GPS Data Forwarding**
- **Scope:** DMRSlotRX.cpp, DMRLC.h
- **Problem:** The LC data structure (DMRLC_T) extracts:
  - srcId, dstId, FLCO, FID, options
  - **Missing:** GPS data (if present in LC)
  - **Missing:** Priority level
  - **Missing:** Call duration tracking

**Impact:** Pi-Star cannot display caller location or call priority. Some applications require this data for routing decisions.

#### **ISSUE 10.2: No Encryption Flag Handling**
- **Scope:** DMRSlotRX.cpp, DMRLC.h
- **Problem:** Service options byte (lc.rawData[2]) contains encryption flag, but:
  - No extraction or logging of encryption state
  - No flag to inform MMDVMHost that this transmission is encrypted
  - Encrypted calls may be incorrectly decoded or cause errors in voice chains

**DMR Spec Compliance:** ETSI TS 102 361-1 Section 9.4 defines the encryption flag in byte 2, bits 0-2. Current code ignores this.

#### **ISSUE 10.3: No Full LC Continuity Data (LCSS)**
- **Scope:** decodeCACH() at DMRSlotRX.cpp:588-641
- **Problem:** The CACH includes LCSS (LC Continuation State Synchronization) bits, but:
  - Code extracts them: `t[2] = c[5]; // LCSS1` and `t[3] = c[6]; // LCSS0`
  - **No use** of LCSS bits to track LC data across voice frames
  - Each voice frame in the superframe may contain different LC data (continuation), but this is not tracked

**DMR Spec Requirement:** ETSI TS 102 361-1 Section 6.1.1 specifies LCSS for managing multi-part LC data (e.g., long talkgroup names). Current implementation only captures the static LC from header/terminator.

#### **ISSUE 10.4: No Alias Resolution**
- **Scope:** SerialPort output to MMDVMHost
- **Problem:** The code forwards srcId and dstId as 3-byte integers, but:
  - No lookup of alias/user database
  - No name resolution (user name, talkgroup name)
  - Pi-Star's MMDVMHost can perform this lookup, but the modem provides no hints

**Not a standards violation,** but reduces usability for remote monitoring.

---

## 11. LOST FRAME DETECTION & RECOVERY (DMRSlotRX.cpp:414-447)

### Current Implementation
```c
if (m_syncCount[slot] >= MAX_SYNC_LOST_FRAMES) {
  // After 13 consecutive non-sync frames
  serial.writeDMRLost(slot);
  reset();
}
```

### Issues Identified

#### **ISSUE 11.1: Sync Loss Detection in MS_MODE**
- **Location:** DMRSlotRX.cpp:417-422
- **Problem:** Both m_syncCount[0] and m_syncCount[1] are reset on EVERY sync found (lines 234-235):
  ```c
  m_syncCount[0U] = 0U;
  m_syncCount[1U] = 0U;
  ```
  This is correct for full-duplex (two independent slots), but in MS_MODE:
  - m_currentSlot alternates every burst, so effectively only ONE slot is active
  - Resetting both counters every sync prevents sync-lost detection from triggering for ~13 bursts (~390ms)
  - If the BS suddenly goes silent, it takes nearly 400ms to detect (instead of ~100ms for normal DMR)

**Severity:** Low (acceptable for a bridge), but slower than standard duplex modem.

#### **ISSUE 11.2: No Graceful Degradation**
- **Location:** DMRSlotRX.cpp:418-437
- **Problem:** On sync loss:
  - Immediate call termination (m_callActive[slot] = false)
  - Immediate state reset (reset())
  - No attempt to maintain partial call data or buffer lost frames for later recovery

**User Impact:** If the BS signal drops for 400ms, the call is terminated even if the BS recovers seconds later. No "call interrupted" notification is sent to MMDVMHost; just a DMR_LOST message.

---

## 12. POTENTIAL RACE CONDITIONS & TIMING ISSUES

### Issues Identified

#### **ISSUE 12.1: Flywheel & decodeCACH Timing Mismatch**
- **Location:** DMRSlotRX.cpp:168, 171-173, 480-485
- **Sequence:**
  1. m_slotTimer increments in databit() (line 139)
  2. procSlot2() fires at m_slotTimer == 108 → SENDS FRAME with current m_currentSlot
  3. procSlot2() advances: m_syncPtr += 288, m_endPtr += 288
  4. decodeCACH() fires at m_slotTimer == 132 → READS NEXT CACH, corrects m_currentSlot IF NEEDED
  5. But frame already sent with OLD slot identity!

**Result:** Mid-call slot corrections are delayed by one burst. Next burst sent with correct slot, but this burst is on wrong slot. MMDVMHost may log as two separate calls on different slots.

#### **ISSUE 12.2: m_dataPtr Circular Buffer Wrap & Bit Extraction**
- **Location:** DMRSlotRX.cpp:131, 176-179, 644-681 (bitsToBytes)
- **Problem:** The m_dataPtr is incremented after every bit (line 176), wrapping at DMR_BUFFER_LENGTH_BITS (576).
  - bitsToBytes() reads bits from m_buffer[576/8 = 72 bytes] in circular fashion
  - No validation that the bits being read have actually been written yet
  - Example: If m_dataPtr = 100 and we want to read from startPtr = 200, we're reading 100 bits ahead

**Mitigation:** The sync detect logic ensures startPtr and endPtr are set to valid positions within the circular buffer relative to syncPtr, so this may not cause actual data corruption. However, the timing is implicit and fragile.

---

## 13. CONFIGURATION & TESTING GAPS

### Issues Identified

#### **ISSUE 13.1: Color Code 0 as "Don't Care"**
- **Location:** DMRSlotRX.cpp:230, SerialPort.cpp:368
- **Problem:** Configuration allows m_colorCode = 0, meaning "accept any color code."
  - This is a valid configuration for network monitoring or testing
  - But provides no feedback to the user (no "accepting any CC" message in logs)
  - Deployed without realizing CC validation is disabled

#### **ISSUE 13.2: No Validation of CONFIG Parameters**
- **Location:** SerialPort.cpp (setConfig function)
- **Problem:** Parameters accepted without range checking:
  - dmrDelay: Divided by 5 with no bounds check (could allow negative or very large delays)
  - txDelay: No bounds check
  - colorCode: No bounds check (valid range 0-15 per spec)

---

## 14. STANDARDS COMPLIANCE SUMMARY

| Area | ETSI TS 102 361-1 Requirement | Current Status | Severity |
|------|-----|--------|----------|
| Color Code Validation | MUST validate CC against configured value | ✓ Implemented (CC=0 allows any) | LOW |
| Time Slot Assignment | MUST use TC bit from CACH | ✓ Implemented | MEDIUM* |
| Sync Pattern Conversion | BS→MS sync replacement optional | ✓ Implemented | INFO |
| Link Control Decoding | MUST validate RS(12,9) parity | ✗ Accepts unvalidated data | HIGH |
| Slot Type Golay | MUST encode with same CC/DT as received | ✓ Implemented | LOW |
| CACH Hamming(7,4) | MUST correct single-bit errors | ✓ Implemented | LOW |
| Frame Structure | MMDVM protocol compliant | ✓ Implemented | LOW |
| Call Duration Tracking | No requirement, optional feature | ✗ Not tracked | LOW |
| GPS Data Forwarding | No requirement, optional feature | ✗ Not implemented | LOW |
| Encryption Flag | SHOULD indicate if encrypted | ✗ Not indicated | MEDIUM |

*MEDIUM severity due to timing issue between procSlot2() and decodeCACH()

---

## APPENDIX: KEY DATA STRUCTURES

### DMRLC_T (DMRLC.h:17-26)
Decoded Link Control data:
- PF: Priority Flag
- R: Reserved
- FLCO: Full LC Opcode (0=Group Call, 1=Private Call)
- FID: Feature ID
- options: Service Options (encryption, emergency, etc.)
- dstId: Destination ID (24-bit)
- srcId: Source ID (24-bit)
- rawData[12]: Raw 12-byte LC (before CRC removal)

### Frame Structure Sent to Pi-Star (SerialPort.cpp:970-982)
```
reply[0]   = MMDVM_FRAME_START (0xE0)
reply[1]   = Frame length (count)
reply[2]   = MMDVM_DMR_DATA1 (0x18) or MMDVM_DMR_DATA2 (0x1A)
reply[3]   = Control byte (CONTROL_VOICE=0x20 or CONTROL_DATA=0x40, plus DT)
reply[4-36] = 33 bytes of DMR payload
```

---

## SUMMARY FOR USER

**Critical Issues (Must Fix):**
1. **ISSUE 4.2:** LC acceptance without RS validation
2. **ISSUE 12.1:** Slot assignment delay between procSlot2() and decodeCACH()

**High Priority (Should Fix):**
1. **ISSUE 10.2:** No encryption flag forwarding to MMDVMHost
2. **ISSUE 7.2:** No buffer overflow check in writeDMRData()

**Medium Priority (Consider Fixing):**
1. **ISSUE 1.1:** Sync pattern masking at byte boundaries
2. **ISSUE 2.3:** Slot identity flip during voice calls with no hysteresis
3. **ISSUE 6.1:** Ambiguous voice frame counter semantics

**Low Priority (Documentation/Observability):**
1. **ISSUE 3.1:** No logging for color code mismatches
2. **ISSUE 11.2:** No graceful degradation on sync loss
3. **ISSUE 13.1:** No warning when CC=0

