# Introduction 
# DO NOT TRY THIS VERSION, I"M STILL WORKING ON, IT DOES NOT WORK YET

Adrino - Generic STM32F103C Series, 20k Ram, 64k Flash, smallest, upload "Serial".
Additonal Board Manager URL http://dan.drown.org/stm32duino/package_STM32duino_index.json

Using a board similar to https://github.com/phl0/MMDVM_HS_Dual_Hat and instructions
It has two ADF7021 - one for RX the other for TX
Using a 14.74 oscillator. 
JP1 solored together for BOOT1+0
Geehy APM32F103C8T6 - a 64 KB flash, 20 KB SRAM microcontroller, similar to the STM32F103C8T6
Arduino 2.3.6 -IDE

Modified Pistar-modemupdate to not checksum; added to accecpted file list, the custom .bin

So far: Let's do some review on the gd77 firmware and the stockfirmware and ESTI. Why can't we transpose a received transmission from a BS and tell Pi-Star that's really a MS?

Builder

Thought process

Of course. That's an excellent and fundamental question. Let's step back and review the architecture. You're right to ask why this isn't straightforward, and the answer gets to the very core of our challenge.

In short, we absolutely can and must make a Base Station (BS) transmission look like a Mobile Station (MS) transmission for Pi-Star. The problem is that this "transposition" is more complex than just flipping a bit; it's about re-packaging the data according to the rules of the MMDVM protocol.

Here's a breakdown based on the firmware and standards:

### 1. What Pi-Star Actually Sees
Pi-Star's MMDVMHost software never sees the raw radio signal. It only receives data from our MMDVM device over a serial (USB) connection. The rules for that connection are defined in the MMDVM Protocol .

- The Protocol is Key : MMDVMHost doesn't know or care if our device is a hotspot, a repeater, or, in our case, a "monitoring" MS. It only cares that the data it receives follows the protocol.
- It Expects MS Frames : The protocol was designed for hotspots that listen to transmissions from a Mobile Station (MS). Therefore, MMDVMHost expects the incoming data to be formatted as if it came from an MS.
### 2. The Difference Between a BS and an MS Transmission
According to the ETSI DMR standard ( TS 102 361-1 and TS 102 361-2 ), the raw over-the-air frames from a Base Station are structured differently from a Mobile Station's frames. The most critical difference for us is how they carry Link Control (LC) data .

- What is Link Control (LC)? : This is the heart of the metadata for a call. It contains:
  - Source ID : The DMR ID of the person talking.
  - Destination ID : The Talkgroup or Private Call ID.
  - Call type, data type, etc.
- Why it Matters : Without the LC data, Pi-Star has no idea who is talking or on what talkgroup. The dashboard remains blank because, as far as it knows, no valid call is happening. This is the root cause of our "LC decode failed" messages.
### 3. The "Transposition" Challenge
This brings us to the core of your question. "Transposing" the BS signal for Pi-Star involves two main steps:

1. Receive and Synchronize : Our firmware must listen for the BS transmission and synchronize with its timing and structure. This part is now working correctly. We are successfully locking onto the BS sync pattern.
2. Extract, Re-Package, and Forward : This is the part that is failing. Our firmware must:
   
   - Find the Voice Header : Locate the initial frame of the transmission that contains the LC data.
   - Extract the LC : Decode the raw LC information from that frame.
   - Re-Package the Data : For every subsequent voice frame in that transmission, our firmware must embed a copy of that LC data into the frame it sends to Pi-Star.
The stock MMDVM_HS firmware does this naturally. It receives a frame from an MS that already has the LC in the right place, so it just forwards it. The OpenGD77 firmware , being a true MS, is concerned with transmitting its own LC, not forwarding someone else's.

Our firmware has a unique job: listen to a BS, intercept the LC, and then "impersonate" an MS by injecting that LC into every subsequent frame sent to Pi-Star.

### Where We Are Now
The good news is that our recent debugging has led us directly to this point. We've confirmed:

- We are synchronizing with the BS signal.
- We are attempting to process frames.
The final hurdle is to correctly find the frame with the LC data, extract it, and then properly embed it for Pi-Star. The compile error you just encountered is the last thing blocking us from getting the logs that will show us exactly what's inside the frames we are processing.

Once we fix that compile error and get the new logs, we will see what dataType the firmware is decoding and can finally implement the logic to handle it correctly.


    



