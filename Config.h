/*
 * MMDVM_HS_Dual_Hat - Definitive GPIO Configuration
 * As per https://github.com/phl0/MMDVM_HS_Dual_Hat
 * This is the final, manufacturer-recommended configuration.
 */

#if !defined(CONFIG_H)
#define  CONFIG_H

// Board Selection
#define MMDVM_HS_DUAL_HAT_REV10

// ADF7021 Support
#define ENABLE_ADF7021

// Duplex Mode
#define DUPLEX

// TCXO Frequency
#define ADF7021_14_7456

// Host Communication
#define STM32_USART1_HOST

// Scan Mode
#define ENABLE_SCAN_MODE

// Digital Modes - Only DMR for MS_MODE
#define MODE_DMR
// Removed: MODE_YSF, MODE_DSTAR, MODE_P25, MODE_NXDN, MODE_POCSAG
// to free flash space for LC decoder

// Mobile Station Mode
#define MS_MODE

// Debug Mode
#define ENABLE_DEBUG

// RSSI data breaks MMDVM frame format alignment in MS_MODE
// MMDVMHost expects: control (1) + burst (33) = 34 bytes
// With RSSI: control (1) + burst (33) + rssi (2) = 36 bytes → parser misalignment
// In MS_MODE, RSSI is unnecessary (MMDVMHost handles metrics, modem just relays BS downlink)
//#define SEND_RSSI_DATA

#endif