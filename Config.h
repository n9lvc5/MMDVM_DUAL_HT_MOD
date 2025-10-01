#if !defined(CONFIG_H)
#define  CONFIG_H

// Select one board (STM32F103 based boards)

// 1) ZUMspot RPi or ZUMspot USB:
// #define ZUMSPOT_ADF7021

///#define DUPLEX 
/* This Causes the NXD LED to flash and the SRV to not turn on

Is this a PIN problem?
If remove the duplex, then SRV blink like normal, but nothing else works


*/
//
// 2) Libre Kit board or any homebrew hotspot with modified RF7021SE and Blue Pill STM32F103:
//#define LIBRE_KIT_ADF7021 // <-- UNCOMMENT THIS ONE (Standard Blue Pill/Libre Kit)

// 4) MMDVM_HS_Dual_Hat revisions 1.0 (DB9MAT & DF2ET & DO7EN)
 #define MMDVM_HS_DUAL_HAT_REV10 // <-- COMMENT OUT THIS CONFLICTING LINE

// ... (KEEP ALL OTHER HARDWARE DEFINITIONS COMMENTED)

// Enable ADF7021 support:
#define ENABLE_ADF7021

// Enable full duplex support with dual ADF7021 (valid for homebrew hotspots only):
// #define DUPLEX // <-- COMMENT OUT to save RAM/Flash if using a single radio

// TCXO of the ADF7021
// For 14.7456 MHz:
#define ADF7021_14_7456

// Host communication selection:
#define STM32_USART1_HOST

// Enable mode detection:
// #define ENABLE_SCAN_MODE // <-- COMMENT OUT to save Flash/SRAM (Use a single mode)

// Send RSSI value:
// #define SEND_RSSI_DATA // <-- COMMENT OUT to save Flash/SRAM

// Enable Nextion LCD serial port repeater...:
// #define SERIAL_REPEATER // <-- COMMENT OUT to save Flash/SRAM

// ... (KEEP ALL OTHER LINES COMMENTED)

// Digital Modes (You MUST enable at least one)
// Enable D-Star support.
// #define MODE_DSTAR

// Enable DMR support.
#define MODE_DMR // <-- UNCOMMENT THIS ONE

// Enable System Fusion support.
// #define MODE_YSF

// ... (KEEP ALL OTHER MODES COMMENTED)

#endif