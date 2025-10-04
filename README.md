# Introduction 
# DO NOT TRY THIS VERSION, I"M STILL WORKING ON, IT DOES NOT WORK YET

Using a board similar to https://github.com/phl0/MMDVM_HS_Dual_Hat and instructions
It has two ADF7021. Using a 14.74 oscillator. Geehy APM32F103C8T6 - a 64 KB flash, 20 KB SRAM microcontroller, similar to the STM32F103C8T6
Arduino 2.3.6
Using http://dan.drown.org/stm32duino/package_STM32duino_index.json for additional library. STM32F1xx/GD32F1XX - Generic STM31F103C series; Upload is Serial
Modified Pistar-modemupdate to not checksum; added to accecpted file list, the custom .bin


The following options in MMDVM.ini ([Modem] section) have not any effect for MMDVM_HS boards:

    TXInvert
    RXInvert
    PTTInvert
    RXLevel
    RXDCOffset
    TXDCOffset

The following options in MMDVM.ini ([Modem] section) are very important for MMDVM_HS boards:

    RXOffset: RX frequency offset (HS RX BER improvement)
    TXOffset: TX frequency offset (radio RX improvement)
    TXLevel: default deviation setting (recommended value: 50)
    RFLevel: RF power output (recommended value: 100)
    CWIdTXLevel: CW ID deviation setting (recommended value: 50)
    D-StarTXLevel: D-Star deviation setting (recommended value: 50)
    DMRTXLevel: DMR deviation setting (recommended value: 50)
    YSFTXLevel: YSF deviation setting (recommended value: 50)
    P25TXLevel: P25 deviation setting (recommended value: 50)
    NXDNTXLevel: NXDN deviation setting (recommended value: 50)
    POCSAGTXLevel: POCSAG deviation setting (recommended value: 50)

# Important notes

The ADF7021 (or RF7021SE module) must operate with a 14.7456 MHz TCXO and with at least 2.5 ppm of frequency stability or better. For 800-900 MHz frequency band you will need even a better frequency stability TCXO. You could use also 12.2880 MHz TCXO. Any other TCXO frequency is not supported. Please note that a bad quality TCXO not only affects the frequency offset, also affects clock data rate, which is not possible to fix and will cause BER issues.

Please set TXLevel=50 in MMDVM.ini to configure default deviation levels for all modes. You could modify this value and other TXLevel paramenters to change deviation levels. Use [MMDVMCal](https://github.com/g4klx/MMDVMCal) to check DMR deviation level and TX frequency offset with calibrated test equipment.

The jumper wire to CLKOUT in RF7021SE module is not longer required for lastest MMDVM_HS firmware. But CE pin connection of ADF7021 is required for proper operation of ZUMspot.

Be aware that some Blue Pill STM32F103 board are defectives. If you have trouble with USB, please check this [link](http://wiki.stm32duino.com/index.php?title=Blue_Pill).

VHF (144-148 MHz) support for ZUMSpot is added by an external 18 nH inductor between L1 and L2 pins of ADF7021. This will enable dual band (VHF/UHF) operation.

Dual ADF7021 for full duplex operation (#define DUPLEX in Config.h) will work only with a big RX/TX frequency separation (5 MHz or more in UHF band for example) and proper antenna filtering. At the moment #define LIBRE_KIT_ADF7021 (Config.h) with STM32F103 platform is supported. Please see [BUILD.md](BUILD.md) for pinout details.

If you can't decode any 4FSK modulation (DMR, YSF, P25 or NXDN) with your ZUMspot, the common solution is to adjust RX frequency offset (RXOffset) in your MMDVM.ini file. Please try with steps of +-100 Hz until you get low BER. If you don't have test equipment, the procedure is trial and error. In some cases TXOffset adjustment is also required for proper radio decoding. If you have test equipment, please use [MMDVMCal](https://github.com/g4klx/MMDVMCal).

If you have problems updating firmware using USB bootloader (DFU mode) on Orange Pi or any other system different from RPi, you could compile the dfu tool directly. You could get the source code of a dfu tool [here](https://sourceforge.net/projects/dfu-programmer/files/dfu-programmer/0.7.0/).


    


