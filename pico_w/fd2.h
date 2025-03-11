#ifndef FD2
    // these will be made public
    extern uint16_t initialize_floppy_interface();
    extern void FloppyRegisterWrite (uint16_t m, uint8_t b);
    extern uint8_t FloppyRegisterRead(uint16_t m);
// #else
//     // these will be private to fd2.c
//     uint16_t floppyBaseAddress = 0x8014;
#endif

#define floppyBaseAddress 0x8014
#define TCP_PORT 6800

// define the offsets on the floppy controller to the registers
#define FDC_DRVREG_OFFSET   0
#define FDC_STATREG_OFFSET  4
#define FDC_CMDREG_OFFSET   4
#define FDC_TRKREG_OFFSET   5
#define FDC_SECREG_OFFSET   6
#define FDC_DATAREG_OFFSET  7

#define FDC_BUSY       0x01
#define FDC_DRQ        0x02
#define FDC_TRKZ       0x04
#define FDC_LOSTDATA   0x04
#define FDC_CRCERR     0x08
#define FDC_SEEKERR    0x10
#define FDC_RNF        0x10
#define FDC_HDLOADED   0x20

//Function of the RTS Bit
//RTS = 0 -> The sector contains normal data (written with a standard write command).
//RTS = 1 -> The sector contains deleted data (written using a "Write Deleted Data" command).
#define FDC_RECORDTYPE 0x20

#define FDC_WRTPROTECT 0x40
#define FDC_INDEXPULSE 0x40
#define FDC_NOTREADY   0x80

#define DRV_DRQ        0x80
#define DRV_INTRQ      0x40

#define DISK_FORMAT_UNKNOWN  0
#define DISK_FORMAT_FLEX     1
#define DISK_FORMAT_FLEX_IMA 2
#define DISK_FORMAT_OS9      3
#define DISK_FORMAT_UNIFLEX  4
#define DISK_FORMAT_MINIFLEX 5
#define DISK_FORMAT_CP68     6
#define DISK_FORMAT_FDOS     7

#define ReadPostIndexGap           0
#define ReadIDRecord               1 
#define ReadIDRecordTrack          2
#define ReadIDRecordSide           3
#define ReadIDRecordSector         4
#define ReadIDRecordSectorLength   5
#define ReadIDRecordCRCHi          6
#define ReadIDRecordCRCLo          7
#define ReadGap2                   8
#define ReadDataRecord             9
#define ReadDataBytes             10
#define ReadDataRecordCRCHi       11
#define ReadDataRecordCRCLo       12
#define ReadGap3                  13

#define WaitForIDRecordMark     0
#define WaitForIDRecordTrack    1
#define WaitForIDRecordSide     2
#define WaitForIDRecordSector   3
#define WaitForIDRecordSize     4
#define WaitForIDRecordWriteCRC 5
#define WaitForDataRecordMark   6
#define GettingDataRecord       7
#define GetLastFewBytes         8
