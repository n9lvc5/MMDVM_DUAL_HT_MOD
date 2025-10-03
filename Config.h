/*
 * MMDVM_HS_Dual_Hat - GPIO Configuration in Simplex Mode
 * Forcing single-radio operation to bypass suspected hardware fault.
 */

#if !defined(CONFIG_H)
#define  CONFIG_H

// Board Selection
#define MMDVM_HS_DUAL_HAT_REV10

// ADF7021 Support
#define ENABLE_ADF7021

// Duplex Mode (Disabled to force simplex operation)
// #define DUPLEX

// TCXO Frequency
#define ADF7021_14_7456
// #define ADF7021_12_2880

// Host Communication
#define STM32_USART1_HOST
// #define STM32_USB_HOST

// Scan Mode
#define ENABLE_SCAN_MODE

//
// Digital Modes (You MUST enable at least one)
//
// Enable D-Star support.
// #define MODE_DSTAR
// Enable DMR support.
#define MODE_DMR
// Enable System Fusion support.
#define MODE_YSF
// Enable P25 support.
// #define MODE_P25
// Enable NXDN support.
// #define MODE_NXDN
// Enable M17 support.
// #define MODE_M17
// Enable POCSAG support.
// #define MODE_POCSAG

#endif