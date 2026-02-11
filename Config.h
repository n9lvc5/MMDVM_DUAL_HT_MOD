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

// Digital Modes
#define MODE_DMR
#define MODE_YSF

// Mobile Station Mode
#define MS_MODE

#endif