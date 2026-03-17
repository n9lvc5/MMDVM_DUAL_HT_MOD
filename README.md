# Introduction

## Disclaimer
**I'm still working on this. Use at your own risk. There might be a RF signal problem.**

[Firmware Disclaimer Document](https://github.com/n9lvc5/MMDVM_DUAL_HT_MOD/blob/main/Firmware_Disclaimer.docx)

## Hardware and Setup Details
- **Arduino:** Generic STM32F103C Series, 20k RAM, 64k Flash, smallest form factor, upload via "Serial".
- **Additional Board Manager URL:** [http://dan.drown.org/stm32duino/package_STM32duino_index.json](http://dan.drown.org/stm32duino/package_STM32duino_index.json)
- **Using a board similar to:** [https://github.com/phl0/MMDVM_HS_Dual_Hat](https://github.com/phl0/MMDVM_HS_Dual_Hat) and following its instructions.
- **Features:**
  - Has two ADF7021 modules — one for RX and one for TX.
  - Uses a 14.74 MHz oscillator.
  - JP1 is soldered together for BOOT1+0.
- **Microcontroller:** Geehy APM32F103C8T6 — a microcontroller with 64 KB flash and 20 KB SRAM, similar to the STM32F103C8T6.
- **IDE Version:** Arduino IDE 2.3.6.

# Changes and File Details

- Modified `Pistar-modemupdate` to not checksum.
- Added the custom `.bin` file to the accepted file list.

## Files in `/home/pi-star`

| Permissions | Owner | Group | Size | Date | Filename |
|--------------|--------|-------|-------|-------|----------|
| `-rw-r--r--` | pi-star | pi-star | 52219 | Mar 5 14:20 | functions.php |
| `-rwxrwxrwx` | pi-star | pi-star | 51160 | Mar 5 14:22 | MMDVM_DUAL_HT_MOD.ino.bin |
| `-rwxrwxrwx` | pi-star | pi-star | 12636 | Mar 5 14:22 | pmu |

## Commands Executed

```bash
sudo sed -i 's/^Debug=0/Debug=1/' /etc/mmdvmhost
sudo systemctl restart mmdvmhost
pi-star@pi-star2(~):~$ rpi-rw
pi-star@pi-star2(~):~$ sudo ./pmu up
```


# Code Review Summary

I've completed a comprehensive code review identifying bugs and DMR standards compliance issues. Here's the executive summary:

## **Critical Findings**

### 🔴 **Critical Issues (Must Address)**

1. **Link Control Validation Bypass (ISSUE 4.2)** - DMRLC.cpp
   - LC data is accepted even when Reed-Solomon CRC fails if it looks "plausible"
   - No validation of srcId, FID, or service options
   - Could allow corrupted caller IDs into the network
   - **DMR Spec:** ETSI TS 102 361-1 Table 9.18 mandates RS validation

2. **Slot Identity Race Condition (ISSUE 12.1)** - DMRSlotRX.cpp
   - Frame is sent with OLD slot identity, THEN corrected from CACH
   - 24-bit delay between sending frame and reading TC bit correction
   - Causes mid-call slot flips to be delayed by one burst
   - **Result:** MMDVMHost may log as separate calls on different slots

### 🟠 **High Priority Issues**

3. **Encryption Flag Lost (ISSUE 10.2)** - DMRSlotRX.cpp/DMRLC.cpp
   - Service options byte contains encryption flag but never extracted
   - MMDVMHost receives no indication call is encrypted
   - Could cause voice chain errors or silent decryption failures

4. **Buffer Overflow Vulnerability (ISSUE 7.2)** - SerialPort.cpp:970-980
   - writeDMRData() allocates 40-byte buffer but no length validation
- If length > 34, buffer overflow occurs
   - Currently mitigated because callers use fixed 34-byte frames

5. **Partial Byte Sync Masking (ISSUE 1.1)** - DMRSlotRX.cpp:210-216
   - Uses nibble masking (0xF0/0x0FU) instead of bit-wise operations.
   - Sync pattern starts at bit 108 = byte 13.5, not aligned on nibble boundary.
   - May corrupt last bits of slot type or first bits of next field.

### 🟡 **Medium Priority Issues**

6. **CACH TC Bit Jitter (ISSUE 2.3)** - DMRSlotRX.cpp:631-639
   - No hysteresis on slot identity changes.
   - Single bit flip in CACH causes immediate slot correction.
   - Frames sent before correction, causing slot flip mid-call.

7. **Voice Frame Counter Ambiguity (ISSUE 6.1)** - DMRSlotRX.cpp:450-455 
   - Every 6th frame marked as CONTROL_VOICE (contains sync).
   - But should contain actual voice data, not sync.
   - May cause MMDVMHost to expect sync pattern in voice payload.

### 🔵 **Lower Priority (Observability/Robustness)**
- No logging for color code rejections (ISSUE 3.1)
- Initial TC bit read before buffer filled (ISSUE 8.1)
- Missing call duration logging on normal termination (ISSUE 9.2)
- No GPS data extraction/forwarding (ISSUE 10.1)
- No hysteresis on sync loss detection (ISSUE 11.1)
 
## **Key Data Forwarding Gaps**
The code successfully forwards:
- ✅ Source ID (srcId)
- ✅ Target ID/Talkgroup (dstId)
- ✅ Time slot number (via TC bit)
- ✅ Color code validation 
- ✅ Voice/Data distinction 
 
the code *does NOT forward*:
does NOT forward:
a) Caller aliases/names *(requires external lookup)*
b) Encryption flag 
c) GPS data *(not extracted)*
d) Call priority level 
e) Multi-part LC continuation *(LCSS not tracked)*
f) Signal quality indicators *(sync inversion lost)*
 
a detailed analysis has been saved to [CODE_REVIEW_DMR_STANDARDS.md](./CODE_REVIEW_DMR_STANDARDS.md) with 14 issues documented, severity ratings, impact analysis, and DMR specification references.


    










