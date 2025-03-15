
#ifndef SDCARD
    // these will be made public
    extern uint16_t initialize_sdcard();
    extern void SDCARDRegisterWrite (uint16_t m, uint8_t b);
    extern uint8_t SDCARDRegisterRead(uint16_t m);
#else
    //     // these will be private to sdcard.c

    #define RD_LED_PIN 14
    #define WR_LED_PIN 15


    uint8_t dataRegister;              // DATA REGISTER
    uint8_t errorRegister;             // ERROR REGISTER
    uint8_t writePrecompRegister;      // WRITE PRECOMP REGISTER
    uint8_t sectorCountRegister;       // SECTOR COUNT
    uint8_t sectorNumberRegister;      // SECTOR NUMBER
    uint8_t cylinderLowRegister;       // CYLINDER NUMBER (LSB)
    uint8_t cylinderHiRegister;        // CYLINDER NUMBER (MSB)
    uint8_t sizeDriveHeadRegister;     // SIZE/DRIVE/HEAD REGISTER
    uint8_t statusRegister;            // STATUS REGISTER
    uint8_t commandRegister;           // COMMAND REGISTER

    uint16_t upper4BitsOfLSN = 0;
    uint32_t fileSize = 0;

    bool writing = false;
    bool reading = false;

    uint8_t *sectorBuffer = NULL;
    uint16_t  sectorBufferIndex = 0;
    bool busyHasBeenSet = false;

#endif

#define sdCardRDCommand 0x20    // read command
#define sdCardWRCommand 0x30    // write command

#define sdcardBaseAddress 0x8008
