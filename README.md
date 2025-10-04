# Introduction 
# DO NOT TRY THIS VERSION, I"M STILL WORKING ON, IT DOES NOT WORK YET

Using a board similar to https://github.com/phl0/MMDVM_HS_Dual_Hat and instructions
It has two ADF7021. 
Using a 14.74 oscillator. 
Geehy APM32F103C8T6 - a 64 KB flash, 20 KB SRAM microcontroller, similar to the STM32F103C8T6
Arduino 2.3.6
Using http://dan.drown.org/stm32duino/package_STM32duino_index.json for additional library. 
STM32F1xx/GD32F1XX - Generic STM31F103C series; Upload is Serial
Modified Pistar-modemupdate to not checksum; added to accecpted file list, the custom .bin
JP1 solored together for BOOT1+0

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

    Disclaimer: This , like many other codes on Github, do not come with any warranty or guaentee, or the corrected spelling. Enter at your own risk
