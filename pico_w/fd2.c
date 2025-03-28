#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include <time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
 
#define FD2
#include "emu6800.h"
#include "fd2.h"
#include "tcp.h"

#define PT68_1

/*
    The way we will implement this is to use as much of the existing code from SWTPCemuApp fd2.cs as possible and 
    replacing the access to the floppy image file with packets sent back and forth to a disk image file server via 
    WiFi. Once the original fd2.cs code has been converted to .c code, the only other changes that should need to 
    be made are to build packets to send to the server once a read or write command has been written to the floppy
    command register.

    All other writes to the floppy registers are only to manage the data that will be used to populate the packet
    to be sent to the server. Once the packet has been sent, we will wait for the response from the server signaling 
    completion of the request. Contained in the response will be the status of the request (whether it failed or was
    a success) and if the request was a read request the data buffer will also be returned. If the geometry will 
    indicate the number of bytes for the data buffer. We will make sure we have enough room for either 128, 256 or 
    512 byte sectors.

    The format of the packets sent will be as follows:

        Read request packet
            offset  size (in bytes) description
            0       1               command - this can be either 0x8x or 0x9x
                                        if we are doing multisector read or write, the bytes to transfer is the number 
                                        of bytes that are remaining on the track starting at the sector in the sector 
                                        register
            1       1               drive number (valid values 0 through 3)
            2       1               track        (valid values 0 through 0xFF)
            3       1               sector       (valid values 0 through 0xFF - start sector on multisector reads)

        Read Response buffer returns the following:
            offset  size (in bytes) description
            0       1               status of operation
            1       2               number of bytes in data buffer to retrieve
            3       <varies>        the start of the data buffer

        Write request packet
            offset  size (in bytes) description
            0       1               command - this can be either 0x8x or 0x9x
                                        if we are doing multisector read or write, the bytes to transfer is the number 
                                        of bytes that are remaining on the track starting at the sector in the sector 
                                        register
            1       1               drive number (valid values 0 through 3)
            2       1               track        (valid values 0 through 0xFF)
            3       1               sector       (valid values 0 through 0xFF - start sector on multisector writes)
            4       2               number of bytes in data buffer to be sent (make sure this matches the geomtry)
            6       <varies>        the start of the data buffer

        Write  Response buffer returns the following:
            offset  size (in bytes) description
            0       1               status of operation

    These are the commands that cam be sent to the command register

        command description             action on server
        ------- -----------             ----------------
        0x0X    RESTORE                 will do nothing 
        0x1X    SEEK                    will do nothing 
        0x2X    STEP                    will do nothing 
        0x3X    STEP W/TRACK UPDATE     will do nothing 
        0x4X    STEP IN                 will do nothing 
        0x5X    STEP IN  W/TRACK UPDATE will do nothing 
        0x6X    STEP OUT                will do nothing 
        0x7X    STEP OUT W/TRACK UPDATE will do nothing 
        0x8X    READ  SINGLE   SECTOR   reads a 256 byte sector
        0x9X    READ  MULTIPLE SECTORS  reads multiple 256 byte sectors starting at sector specified in sector to end of track
        0xAX    WRITE SINGLE   SECTOR   writes a 256 byte sector
        0xBX    WRITE MULTIPLE SECTORS  writes multiple 256 byte sectors starting at sector specified in sector to end of track
        0xCX    READ ADDRESS            will do nothing
        0xDX    FORCE INTERRUPT         will do nothing
        0xEX    READ TRACK              will do nothing
        0xFX    WRITE TRACK             will do nothing
*/

//             STATUS REGISTER SUMMARY (WD2797)
// 
// ALL     TYPE I      READ        READ        READ        WRITE       WRITE
// BIT     COMMANDS    ADDRESS     SECTOR      TRACK       SECTOR      TRACK
// ----------------------------------------------------------------------------------
// S7      NOT READY   NOT READY   NOT READY   NOT READY   NOT READY   NOT READY
// S6      WRITE       0           0           0           WRITE       WRITE
//         PROTECT                                         PROTECT     PROTECT
// S5      HEAD LOADED 0           RECORD TYPE 0           0           0
// S4      SEEK ERROR  RNF         RNF         0           RNF         0
// S3      CRC ERROR   CRC ERROR   CRC ERROR   0           CRC ERROR   0
// S2      TRACK 0     LOST DATA   LOST DATA   LOST DATA   LOST DATA   LOST DATA
// S1      INDEX PULSE DRO         DRO         DRO         DRO         DRO
// SO      BUSY        BUSY        BUSY        BUSY        BUSY        BUSY        
// ----------------------------------------------------------------------------------

uint8_t m_FDC_STATRegister;
uint8_t m_FDC_DRVRegister;
uint8_t m_FDC_DATARegister;
uint8_t m_FDC_SECRegister;
uint8_t m_FDC_CMDRegister;
uint8_t m_FDC_TRKRegister;

uint16_t m_nFDCTrack  = 0;
uint16_t m_nFDCSector = 0;

uint16_t m_nReadingTrack                        = false;
uint16_t m_nFDCReading                          = false;
uint16_t m_nFDCWriting                          = false;
uint16_t m_nWritingTrack                        = false;
uint16_t doingMultiSector                       = false;

uint16_t allowMultiSectorTransfers = false;

uint16_t m_nBytessToTransfer                    = 0;
uint16_t m_statusReadsWithoutDataRegisterAccess = 0;
uint16_t m_nFDCWritePtr                         = 0;
uint16_t m_nFDCReadPtr                          = 0;
uint16_t writeTrackWriteBufferIndex             = 0;
uint16_t m_nCurrentSideSelected                 = 0;
uint16_t readBufferIndex                        = 0;

uint16_t lastCommandWasType1        = false;
uint16_t lastIndexPusleDRQStatus    = false;
uint16_t indexPulseCounter          = 3000;
uint16_t indexPulseWidthCounter     = 20;

uint16_t _bInterruptEnabled = 0;

uint16_t sectorsPerTrack[4];
uint16_t bytessPerSector[4];

uint8_t m_caReadBuffer       [16384];   // use a really big buffer - we have 4MB of RAM to work with
uint8_t m_caWriteBuffer      [16384];   // use a really big buffer - we have 4MB of RAM to work with
uint8_t writeTrackWriteBuffer[16384];   // use a really big buffer - we have 4MB of RAM to work with
uint8_t readBuffer           [16384];   // make big enough to haold a track of 255 sectors and then some

// these are only used when we implememnt writing tracks.
uint16_t writeTrackTrack;
uint16_t sectorsInWriteTrackBuffer;
uint16_t currentWriteTrackState;
uint16_t writeTrackSide      = 0;
uint16_t writeTrackSector    = 0;
uint16_t writeTrackSize      = 0;

uint16_t writeTrackMinSector = 0;
uint16_t writeTrackMaxSector = 0;

uint16_t writeTrackBytesWrittenToSector  = 0;
uint16_t writeTrackBytesPerSector        = 0;
uint16_t writeTrackBufferOffset          = 0;    // used to put sector data into the track buffer since the sector do not come in order
uint16_t writeTrackBufferSize            = 0;
uint16_t totalBytesThisTrack             = 0;    // initial declaration

uint8_t  previousByte               = 0x00;
uint16_t sectorsInWriteTrackBuffer  = 0;
uint16_t lastFewBytesRead           = 0;
uint8_t  lastSectorAccessed         = 1;
uint16_t lastFewBytesRead;

const int
    currentPostIndexGapSize = 8,
    currentGap2Size         = 17,
    currentGap3Size         = 33;

uint16_t currentReadTrackState       = ReadPostIndexGap;
uint16_t currentPostIndexGapIndex    = 0;
uint16_t currentGap2Index            = 0;
uint16_t currentGap3Index            = 0;
uint16_t currentDataSectorNumber     = 1;

uint8_t IDRecordBytes[5];
uint16_t crcID = 0;
uint16_t crcData = 0;

uint16_t _spin = 0;

//
//    used to get the geometry of the disk we are using
//

#define sizeofVolumeLabel             11
#define sizeofDirEntry                24
#define sizeofSystemInformationRecord 24

#define offsetToSIRInfo               0x10

typedef struct FLEX_RAW_SIR
{
    uint8_t caVolumeLabel[sizeofVolumeLabel];   // $50 - $5A
    uint8_t cVolumeNumberHi;                    // $5B
    uint8_t cVolumeNumberLo;                    // $5C
    uint8_t cFirstUserTrack;                    // $5D
    uint8_t cFirstUserSector;                   // $5E
    uint8_t cLastUserTrack;                     // $5F
    uint8_t cLastUserSector;                    // $60
    uint8_t cTotalSectorsHi;                    // $61
    uint8_t cTotalSectorsLo;                    // $62
    uint8_t cMonth;                             // $63
    uint8_t cDay;                               // $64
    uint8_t cYear;                              // $65
    uint8_t cMaxTrack;                          // $66
    uint8_t cMaxSector;                         // $67
} FLEX_RAW_SIR;

FLEX_RAW_SIR sir [4];

/* X^16 + X^12 + X^5+ 1 */

uint16_t crc_table [256] =
{
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5,
    0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b,
    0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c,
    0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b,
    0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6,
    0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738,
    0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5,
    0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969,
    0xa90a, 0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96,
    0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc,
    0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03,
    0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
    0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6,
    0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb,
    0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1,
    0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c,
    0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2,
    0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb,
    0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447,
    0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
    0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2,
    0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9,
    0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827,
    0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d, 0xdb5c,
    0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0,
    0x2ab3, 0x3a92, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d,
    0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
    0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba,
    0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

FLEX_RAW_SIR GetGeometry (int nDrive)
{
    uint16_t m_nBytessToTransfer = 256;

    uint8_t packetData[7];
    uint8_t responseBuffer[1024];

    FLEX_RAW_SIR flexSir;

    packetData[0] = 'F';                        // this is a floppy request packet
    packetData[1] = 0x8C;                       // request command = read sector
    packetData[2] = nDrive &= 0x03;             // set the drive to read from 
    packetData[3] = 0;                          // set the track to read from
    packetData[4] = 3;                          // and the sector to read
    packetData[5] = m_nBytessToTransfer / 256;  // we do this in case of multi sector reads or writes
    packetData[6] = m_nBytessToTransfer % 256;

    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));

    if (responseLength == 257)
    {
        // the call to tcp_request will wait for the response before returning. When we return 
        // from tcp_request, we will have the response in the response buffer. the first byte 
        // in the response buffer will be the floppy controller status of the operation the 
        // rest will be the contents of the sector requested.

        int sirPointer = offsetToSIRInfo + 1;       // add one beciase the status is in the first byte

        for (int i = 0; i < sizeofVolumeLabel; i++)
            flexSir.caVolumeLabel[i] = responseBuffer[sirPointer++];

        flexSir.cVolumeNumberHi     = responseBuffer[sirPointer++];
        flexSir.cVolumeNumberLo     = responseBuffer[sirPointer++];
        flexSir.cFirstUserTrack     = responseBuffer[sirPointer++];
        flexSir.cFirstUserSector    = responseBuffer[sirPointer++];
        flexSir.cLastUserTrack      = responseBuffer[sirPointer++];
        flexSir.cLastUserSector     = responseBuffer[sirPointer++];
        flexSir.cTotalSectorsHi     = responseBuffer[sirPointer++];
        flexSir.cTotalSectorsLo     = responseBuffer[sirPointer++];
        flexSir.cMonth              = responseBuffer[sirPointer++];
        flexSir.cDay                = responseBuffer[sirPointer++];
        flexSir.cYear               = responseBuffer[sirPointer++];
        flexSir.cMaxTrack           = responseBuffer[sirPointer++];
        flexSir.cMaxSector          = responseBuffer[sirPointer++];

        sectorsPerTrack[nDrive] = flexSir.cMaxSector;
        bytessPerSector[nDrive] = 256;
    }

    return flexSir;
}

uint16_t CRCCCITT(uint8_t data[], uint16_t startIndex, uint16_t length, uint16_t seed, uint16_t final)
{

    uint16_t count;
    uint16_t crc = seed;
    uint16_t temp;
    uint16_t dataindex = startIndex;

    for (count = 0; count < length; ++count)
    {
        temp = (data[dataindex++] ^ (crc >> 8)) & 0xff;
        crc = crc_table[temp] ^ (crc << 8);
    }

    return (uint16_t)(crc ^ final);
}

void ClearInterrupt()
{
    return;
}

void SetInterrupt(uint16_t _spin)
{
    return;
}

void WriteTrackToImage(uint16_t nDrive)
{
    // TO DO

    return;
}

uint16_t DriveOpen(uint8_t nDrive)
{
    return (0);     // false for now - TODO: ask wifi server if there is a floppy mounted in the drive
}

uint16_t WriteProtected(uint8_t nDrive)
{
    return (0);     // false for now - TODO: ask wifi server if there is a floppy mounted in the drive is read only
}

uint16_t CheckReady(uint8_t nDrive)
{
    // returns true is drive is ready, 0 if not

    if (DriveOpen(nDrive) == true) // see if current drive is READY
        m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;
    else
        m_FDC_STATRegister &= (uint8_t)~FDC_NOTREADY;

    return (m_FDC_STATRegister & (uint8_t)FDC_NOTREADY) == 0 ? true : false;
}

uint16_t CheckWriteProtect(uint8_t nDrive)
{
    if (WriteProtected(nDrive) == true)          // see if write protected
        m_FDC_STATRegister |= (uint8_t)FDC_WRTPROTECT;
    else
        m_FDC_STATRegister &= (uint8_t)~FDC_WRTPROTECT;

    return (m_FDC_STATRegister & (uint8_t)FDC_WRTPROTECT) == 0 ? true : false;
}

uint16_t CheckReadyAndWriteProtect(uint8_t nDrive)
{
    uint16_t ready = false;
    uint16_t writeProtected = false;

    ready = CheckReady(nDrive);
    
    // we cannot be write protected if we are not ready.

    if (ready)
        writeProtected = CheckWriteProtect(nDrive);

    return ready | writeProtected;
}

uint16_t DiskFormat(uint8_t nDrive)
{

}

void initialize_floppy_interface()
{
    // Initialize the LED pins
    gpio_init(RD_LED_PIN);    gpio_set_dir(RD_LED_PIN, GPIO_OUT);
    gpio_init(WR_LED_PIN);    gpio_set_dir(WR_LED_PIN, GPIO_OUT); 


    //  currently the server will only support FLEX formatted floppies of the .DSK type. It does not
    //  support any other file formats or .IMA files. The number of sectors on track 0 must be the
    //  same as the number of sectors specified in the System Informaiton Record as max_sectors. 

    sir[0] = GetGeometry(0);
    sir[1] = GetGeometry(1);
    sir[2] = GetGeometry(2);
    sir[3] = GetGeometry(3);
}

uint16_t BuildAndSendFloppyWriteRequestPacket(uint8_t *responseBuffer)
{
    uint8_t packetData[7 + 256];        // 6 for the header and 256 for the sector data

    gpio_put(WR_LED_PIN, 1);                        // Turn LED on
    packetData[0] = 'F';                            // this is a floppy request packet
    packetData[1] = m_FDC_CMDRegister;              // request command = read sector
    packetData[2] = m_FDC_DRVRegister &= 0x03;      // set the drive to read from 
    packetData[3] = m_FDC_TRKRegister;              // set the track to read from
    packetData[4] = m_FDC_SECRegister;              // and the sector to read
    packetData[5] = m_nBytessToTransfer / 256;      // we do this in case of multi sector reads or writes
    packetData[6] = m_nBytessToTransfer % 256;
    for (int i = 0; i < 256; i++)
    {
        packetData[7 + i] = m_caWriteBuffer[i];
    }

    // turn off all of the bits that the sector server COULD return as error bits
    m_FDC_STATRegister &= ~(FDC_NOTREADY | FDC_LOSTDATA | FDC_WRTPROTECT | FDC_RNF | FDC_CRCERR);
    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));
    m_FDC_STATRegister != responseBuffer[0];

    gpio_put(WR_LED_PIN, 0);                        // Turn LED off
    return (responseLength);
}

uint16_t BuildAndSendFloppyReadRequestPacket(uint8_t *responseBuffer)
{
    uint8_t packetData[7];

    gpio_put(RD_LED_PIN, 1);                        // Turn LED on
    packetData[0] = 'F';                            // this is a floppy request packet
    packetData[1] = m_FDC_CMDRegister;              // request command = read sector
    packetData[2] = m_FDC_DRVRegister &= 0x03;      // set the drive to read from 
    packetData[3] = m_FDC_TRKRegister;              // set the track to read from
    packetData[4] = m_FDC_SECRegister;              // and the sector to read
    packetData[5] = m_nBytessToTransfer / 256;      // we do this in case of multi sector reads or writes
    packetData[6] = m_nBytessToTransfer % 256;

    // turn off all of the bits that the sector server COULD return as error bits
    m_FDC_STATRegister &= ~(FDC_NOTREADY | FDC_LOSTDATA | FDC_RECORDTYPE | FDC_RNF | FDC_CRCERR);
    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));
    m_FDC_STATRegister != responseBuffer[0];

    // the call to tcp_request will wait for the response before returning. When we return 
    // from tcp_request, we will have the response in the response buffer. the first byte 
    // in the response buffer will be the floppy controller status of the operation the 
    // rest will be the contents of the sector requested.

    gpio_put(RD_LED_PIN, 0);                        // Turn LED off
    return (responseLength);
}

uint16_t BuildAndSendTypeOnePacket(uint8_t command, uint8_t *responseBuffer)
{
    uint8_t packetData[7];

    packetData[0] = 'F';                            // this is a floppy request packet
    packetData[1] = m_FDC_CMDRegister;              // request command = read sector
    packetData[2] = m_FDC_DRVRegister &= 0x03;      // set the drive to read from 
    packetData[3] = m_FDC_TRKRegister;              // set the track to read from
    packetData[4] = m_FDC_SECRegister;              // and the sector to read
    packetData[5] = m_nBytessToTransfer / 256;      // we do this in case of multi sector reads or writes
    packetData[6] = m_nBytessToTransfer % 256;

    // turn off all of the bits that the sector server COULD return as error bits
    m_FDC_STATRegister &= ~(FDC_NOTREADY | FDC_WRTPROTECT | FDC_HDLOADED | FDC_SEEKERR | FDC_CRCERR | FDC_TRKZ | FDC_INDEXPULSE);
    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));
    m_FDC_STATRegister != responseBuffer[0];

    // the call to tcp_request will wait for the response before returning. When we return 
    // from tcp_request, we will have the response in the response buffer. the first byte 
    // in the response buffer will be the floppy controller status of the operation the 
    // rest will be the contents of the sector requested.

    return (responseLength);
}

void FloppyRegisterWrite (uint16_t m, uint8_t b)
{
    uint16_t nDrive         = m_FDC_DRVRegister & 0x03;
    uint16_t nType          = 1;
    uint16_t nWhichRegister = m - (uint16_t)floppyBaseAddress;

    ClearInterrupt();

    uint16_t driveReady = false;
    uint16_t writeProtected = false;

    driveReady = CheckReady(nDrive);
    if (driveReady)
        writeProtected = CheckWriteProtect(nDrive);

    switch (nWhichRegister)
    {
        case (int)FDC_DATAREG_OFFSET:
            {
                m_FDC_DATARegister = b;
                if (((m_FDC_STATRegister & FDC_BUSY) == FDC_BUSY) && m_nFDCWriting)
                {
                    m_statusReadsWithoutDataRegisterAccess = 0;

                    if (!m_nWritingTrack)
                    { 
                        m_caWriteBuffer[m_nFDCWritePtr++] = b;
                        if (m_nFDCWritePtr == m_nBytessToTransfer)
                        {
                            // we havw written all of the bytes to the buffer - build and send the packet to the server
                            // and wait for the response

                            if (CheckReady(nDrive))
                            {
                                uint8_t responseBuffer[16384];
                                BuildAndSendFloppyWriteRequestPacket(responseBuffer);       // this functio will set m_FDC_STATRegister  = m_FDC_STATRegister | responseBuffer[0];

                                if (doingMultiSector)
                                {
                                    // We need to make sure we update the FDC_StatusRegister if this is a multi sector write

                                    m_FDC_SECRegister = sectorsPerTrack[nDrive];
                                }
                                else
                                    m_FDC_SECRegister++;
                            }

                            m_FDC_STATRegister &= (uint8_t)~(FDC_DRQ | FDC_BUSY);
                            m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;                // turn off high order bit in drive status register
                            m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;               // assert INTRQ at the DRV register when the operation is complete
                            m_nFDCWriting = false;

                            ClearInterrupt();
                        }
                        else
                        {
                            m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                            m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;
                        }

                        if (m_nFDCWritePtr % bytessPerSector[nDrive] == 0 && doingMultiSector)
                        {
                            // we just wrote a full sector - bump FDC_SectorRegister unless we just wrote the whole track

                            if (m_FDC_SECRegister != sectorsPerTrack[nDrive])
                                m_FDC_SECRegister++;

                            //doingMultiSector = false;
                        }
                    }
                    else
                    {
                        // this is where we will use a state machine to grab the track, side and sector information from the
                        // incoming data stream by detecting and parsing the ID Record and then wait for the data record to
                        // start building the track's worth of data to save to the image.

                        //      300 rpm SD      3125 uint8_ts
                        //      300 rpm DD/QD   6250 uint8_ts
                        //      360 rpm SD      5208 uint8_ts (e.g., 8" drive)
                        //      360 rpm DD     10417 uint8_ts (e.g., 8" drive)

                        // first a sanity check to keep from going on forever and ever

                        if (writeTrackWriteBufferIndex > 10417)
                        {
                            // we should be done at this point so we need to write the track to the diskette image in case the
                            // emulated host does not check the BUSY or DRQ for writing the next uint8_t. Some emulated machine
                            // programs that use write track do not poll the DRQ or BUSY bits - they just keep sending data
                            // until an an interrupt occurs when the index pulse is detected. For these - we need to terminate
                            // the write track operation by raising the IRQ ourselces. Since the status register is not 
                            // being polled (which is where we would write the track to the image), we need to do it here

                            m_statusReadsWithoutDataRegisterAccess = 1000;  // make it big enough to trigger the end

                            // write the track to the image and clear everything so the next poll of the status register does not write anything.

                            WriteTrackToImage(nDrive);

                            /* we are not yet using interrupts
                            if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                            {
                                SetInterrupt(_spin);
                                if (Program._cpu != null)
                                {
                                    if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                                    {
                                        try
                                        {
                                            Program.CpuThread.Resume();
                                        }
                                        catch (ThreadStateException e)
                                        {
                                            // do nothing if thread is not suspended
                                        }
                                    }
                                }
                            }
                            */
                            writeTrackWriteBufferIndex = 0;     // rest it for next time
                        }
                        else
                        {
                            // not at max number of uint8_ts for an 8" DD track - so check if this track is finished.

                            if (writeTrackTrack == 0)
                            {
                                // track zero for an FD-2 is only allowed to have FM data (single density. that is 10 sectors

                                if (sectorsInWriteTrackBuffer == 10)        // Program.SectorsOnTrackZeroSideZero[nDrive])
                                {
                                    // we just got the last 0xF7 to write the data CRC for the last sector, so let's get a few more uint8_ts before we say we are complete

                                    currentWriteTrackState = (int)GetLastFewBytes;

                                    if (lastFewBytesRead == 7)
                                    {
                                        m_statusReadsWithoutDataRegisterAccess = 1000;  // make it big enough to trigger the end

                                        // write the track to the image and clear everything so the next poll of the status register does not write anything.

                                        WriteTrackToImage(nDrive);
                                        currentWriteTrackState = (int)WaitForIDRecordMark;

                                        /* not doing this now
                                        if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                                        {
                                            SetInterrupt(_spin);
                                            if (Program._cpu != null)
                                            {
                                                if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                                                {
                                                    try
                                                    {
                                                        Program.CpuThread.Resume();
                                                    }
                                                    catch (ThreadStateException e)
                                                    {
                                                        // do nothing if thread is not suspended
                                                    }
                                                }
                                            }
                                        }
                                        */
                                        writeTrackWriteBufferIndex = 0;     // rest it for next time
                                    }
                                }
                            }
                            else
                            {
                                //  here we need to use the sectors per track for this media that was read from offset 0x0227 in the image
                                //  when the diskette image was loaded by Program.LoadDrives
                                //
                                //      For real media for the FD-2 controller these are the only legitimate sectors sizes
                                //      on a 5 1/4" or 3 1/2" drive:
                                //
                                //              Track 0     NON Track zero  Program.sectorsPerTrack[nDrive];
                                //      SSSD    10          10              10
                                //      SSDD    10          18              18
                                //      DSSD    20          10              20
                                //      DSDD    20          18              36
                                //

                                uint16_t numberOfSectorsPerCylinder = sectorsPerTrack[nDrive];
                                uint16_t numberOfsectorsPerTrack = numberOfSectorsPerCylinder;

                                if (numberOfsectorsPerTrack >= 20)
                                {
                                    // then this is a double sided diskette image so we will only be writing
                                    // half the number of sectors specified in Program.sectorsPerTrack[nDrive]

                                    numberOfsectorsPerTrack = numberOfsectorsPerTrack / 2;
                                }

                                if (sectorsInWriteTrackBuffer == numberOfsectorsPerTrack)
                                {
                                    // we just got the last 0xF7 to write the data CRC for the last sector, so let's get a few more uint8_ts before we say we are complete

                                    currentWriteTrackState = (int)GetLastFewBytes;

                                    if (lastFewBytesRead == 7)
                                    {
                                        m_statusReadsWithoutDataRegisterAccess = 1000;  // make it big enough to trigger the end

                                        // write the track to the image and clear everything so the next poll of the status register does not write anything.

                                        WriteTrackToImage(nDrive);
                                        currentWriteTrackState = (int)WaitForIDRecordMark;

                                        /* not doing this now
                                        if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                                        {
                                            SetInterrupt(_spin);
                                            if (Program._cpu != null)
                                            {
                                                if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                                                {
                                                    try
                                                    {
                                                        Program.CpuThread.Resume();
                                                    }
                                                    catch (ThreadStateException e)
                                                    {
                                                        // do nothing if thread is not suspended
                                                    }
                                                }
                                            }
                                        }
                                        */
                                        writeTrackWriteBufferIndex = 0;     // rest it for next time
                                    }
                                }
                            }
                        }

                        switch (currentWriteTrackState)
                        {
                            case (int)WaitForIDRecordMark:
                                {
                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b == 0xFE)
                                    {
                                        // we are about to write the IDRecord mark, so get ready to save the track, side, sector and size information

                                        currentWriteTrackState = (int)WaitForIDRecordTrack;
                                    }
                                }
                                break;

                            case (int)WaitForIDRecordTrack:
                                {
                                    // we will only allow real diskette image number of tracks (up to 80 tracks)

                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b < 80)
                                    {
                                        // this is a valid max track size

                                        writeTrackTrack = b;
                                        currentWriteTrackState = (int)WaitForIDRecordSide;
                                    }
                                    else
                                    {
                                        // if the track specified is > 79 then reset the state machine to look for another ID record mark

                                        currentWriteTrackState = (int)WaitForIDRecordMark;
                                    }
                                }
                                break;

                            case (int)WaitForIDRecordSide:
                                {
                                    // we will only allow real diskette image number of sides (0 or 1)

                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b <= 1)
                                    {
                                        writeTrackSide = b;
                                        currentWriteTrackState = (int)WaitForIDRecordSector;
                                    }
                                    else
                                    {
                                        // if the side specified is > 1 then reset the state machine to look for another ID record mark

                                        currentWriteTrackState = (int)WaitForIDRecordMark;
                                    }
                                }
                                break;

                            case (int)WaitForIDRecordSector:
                                {
                                    // we will only allow real diskette image max number of sectors per track (up to 52 for 8" DD)

                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b <= 52)
                                    {
                                        writeTrackSector = b;
                                        currentWriteTrackState = (int)WaitForIDRecordSize;

                                        // we will need these values to write the track to the image.

                                        if (b < writeTrackMinSector)
                                            writeTrackMinSector = b;
                                        if (b > writeTrackMaxSector)
                                            writeTrackMaxSector = b;
                                    }
                                    else
                                    {
                                        // if the sector specified is > 52 then reset the state machine to look for another ID record mark

                                        currentWriteTrackState = (int)WaitForIDRecordMark;
                                    }
                                }
                                break;

                            case (int)WaitForIDRecordSize:
                                {
                                    // we will only allow real diskette image record sizes (0, 1, 2, 3)

                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b <= 3)
                                    {
                                        writeTrackSize = b;
                                        currentWriteTrackState = (int)WaitForIDRecordWriteCRC;
                                    }
                                    else
                                    {
                                        // if the sector record size is > 3 then reset the state machine to look for another ID record mark

                                        currentWriteTrackState = (int)WaitForIDRecordMark;
                                    }
                                }
                                break;

                            case (int)WaitForIDRecordWriteCRC:
                                {
                                    // this next uint8_t MUST be 0xF7 to write CRC

                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b == 0xF7)
                                    {
                                        currentWriteTrackState = (int)WaitForDataRecordMark;
                                    }
                                    else
                                    {
                                        // if the write CRC uint8_t is not 0xF7 then reset the state machine to look for another ID record mark

                                        currentWriteTrackState = (int)WaitForIDRecordMark;
                                    }
                                }
                                break;

                            case (int)WaitForDataRecordMark:
                                {
                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (b == 0xFB)
                                    {
                                        // we have received the Data Record Mark. prepare to receive 2^size * 128 uint8_ts of data
                                        currentWriteTrackState = (int)GettingDataRecord;

                                        // we are going to build the track in the m_caWriteBuffer. We need to use the sector number (FLEX sectors start at 1 except for track 0
                                        // which starts at sector 0 and has no sector 1.
                                        //
                                        //  Let's do this calcuation here so we only have to do it once for each sector. We can only write to one head at a time
                                        //  so we need to figure out which side this is on so we can adjust the sector number we use for calculating the offset.
                                        //  If the track is - and the sector number is > 10, we are doing the second side so we need to subtract 10 fomr the sector
                                        //  number to calculate the buffer offset (writeTrackBufferOffset). This value will also be used to set the number of uint8_ts
                                        //  to write to the file when this track is written to the image. If the track is not track 0, we need to subtract the number
                                        //  of sectors per side for the second side.

                                        uint16_t sector = writeTrackSector;
                                        if (writeTrackTrack == 0 && writeTrackSector == 0 && DiskFormat(nDrive) == DISK_FORMAT_FLEX_IMA)
                                        {
                                            // adjust track 0 sector 0, because we are going to subtract 1 from it later, so it CANNOT be zero

                                            sector = 1;
                                        }

                                        writeTrackBytesPerSector = 128 * (1 << writeTrackSize);

                                        if (DiskFormat(nDrive) == DISK_FORMAT_FLEX_IMA)
                                        {
                                            writeTrackBufferOffset = writeTrackBytesPerSector * (sector - 1);
                                        }
                                        else if (DiskFormat(nDrive) == DISK_FORMAT_FDOS)
                                        {
                                            writeTrackBufferOffset = writeTrackBytesPerSector * (sector);
                                        }
                                        writeTrackBytesWrittenToSector = 0;
                                    }
                                }
                                break;

                            case (int)GettingDataRecord:
                                {
                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    if (writeTrackBytesWrittenToSector < writeTrackBytesPerSector)
                                    {
                                        // still writing uint8_ts to the sector

                                        // we use writeTrackBufferOffset because the sectors may be interleaved.
                                        // this value gets reset to where the sector should reside in the buffer
                                        // based on the sector value in the IDRecord

                                        m_caWriteBuffer[writeTrackBufferOffset] = b;
                                        writeTrackBufferOffset++;
                                        writeTrackBytesWrittenToSector++;

                                        totalBytesThisTrack++;  // added a uint8_t to the buffer to write to the image

                                        // keep track of the max size of the buffer we need to write.

                                        if (writeTrackBufferSize < writeTrackBufferOffset)
                                            writeTrackBufferSize = writeTrackBufferOffset;
                                    }
                                    else
                                    {
                                        // we are done writing this sector - set up for the next one - we will just ignore the writing of the CRC uint8_ts
                                        // and set the next state to get the next ID record mark.

                                        currentWriteTrackState = (int)WaitForIDRecordMark;

                                        // increment the number of sectors in the m_caWriteBuffer

                                        sectorsInWriteTrackBuffer++;
                                    }
                                }
                                break;

                            case (int)GetLastFewBytes:
                                {
                                    writeTrackWriteBuffer[writeTrackWriteBufferIndex++] = b;
                                    lastFewBytesRead++;
                                }
                                break;
                        }
                    }
                }
                previousByte = b;
            }
            break;

        case (int)FDC_DRVREG_OFFSET:     // Ok to write to this register if drive is not ready or write protected
            {
                driveReady = CheckReady(b & (uint8_t)0x03);
                if (driveReady)
                    writeProtected = CheckWriteProtect(b & (uint8_t)0x03);

                if ((b & 0x40) == 0x40)         // we are selecting side 1
                    m_nCurrentSideSelected = 1;
                else                            // we are selecting side 0
                    m_nCurrentSideSelected = 0;

                // preserve the high order bit of m_FDC_DRVRegister since it is not writable by the processor (read only)
                // and can only be set by the controller. The is the DRQ signal that is set by DRQ from the 2797. Also
                // preserve bit 6 (the INTRQ signal from the 2797). These are here so the user can see these hardware
                // bits with reading and clearing the status register. Reading the status register should clear INTRQ
                // but not DRQ. reading the data regsiter should clear DRQ. Any write to the command register should also
                // clear INTRQ.

                if ((m_FDC_DRVRegister & 0x80) == 0x80) m_FDC_DRVRegister = (uint8_t)(b | 0x80);   // preserve DRQ in DRV register
                if ((m_FDC_DRVRegister & 0x40) == 0x40) m_FDC_DRVRegister = (uint8_t)(b | 0x40);   // preserve INTRQ in DRV register

                // now we can add in the bits that user can set by writing to the DRV register. but first we need to strip the drive
                // select bits.

                m_FDC_DRVRegister &= 0xFC;
                m_FDC_DRVRegister |= (uint8_t)(b & (uint8_t)0x03);
            }
            break;

            // Ok to write to this register if drive is not ready or write is protected
            // but we cannot read from the drive if it is not ready, nor can we write
            // to it


        case (int)FDC_CMDREG_OFFSET:     
            {
                // any write to the command register should clear the INTRQ signal presented to bit 6 of the DRV register
                // we also need to remember if this is a type 1 command. If it is , reads of the status register should
                // the index pulse as DRQ present until another non-type 1 command is issued. The DRQ bit should be toggled
                // on each x number of reads where x is determined by the cuurnt processor speed.
                //
                //  so first - set the boolean that says that the last command type was type 1. this will be tested with
                //  every status read. default to false and set to true if type 1.
                //
                //  any command write must now clear DRQ in the status register

                lastCommandWasType1 = false;
                lastIndexPusleDRQStatus = false;
                indexPulseCounter = 3000;
                indexPulseWidthCounter = 20;

                m_FDC_STATRegister&= (uint8_t)~FDC_DRQ;        // clear DRQ in the controller
                m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;        // turn off high order bit in drive status register (hardware DRQ)
                m_FDC_DRVRegister &= (uint8_t)~DRV_INTRQ;      // de-assert INTRQ at the DRV register

                ClearInterrupt();

                m_FDC_CMDRegister = b;      // save command register write for debugging purposes. The FD-2 emulation does not use it
                m_nReadingTrack = m_nWritingTrack = m_nFDCReading = m_nFDCWriting = false;      // can't be read/writing if commanding

                m_statusReadsWithoutDataRegisterAccess = 0;
                uint16_t drive = m_FDC_DRVRegister & 0x03;

                switch (b & 0xF0)
                {
                    // TYPE I

                    case 0x00:  //  0x0X = RESTORE
                        {
                            lastCommandWasType1 = true;

                            m_nFDCTrack = 0;
                            m_FDC_TRKRegister = 0;  // restore sets the track register in the chip to zero

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on RESTORE command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x10:  //  0x1X = SEEK
                        {
                            lastCommandWasType1 = true;

                            m_nFDCTrack = m_FDC_DATARegister;
                            m_FDC_TRKRegister = (uint8_t)m_nFDCTrack;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on SEEK command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x20:  //  0x2X = STEP
                        {
                            lastCommandWasType1 = true;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x30:  //  0x3X = STEP W/TRACK UPDATE
                        {
                            lastCommandWasType1 = true;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP WITH TRACK UPDATE command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x40:  //  0x4X = STEP IN
                        {
                            lastCommandWasType1 = true;

                            if (m_nFDCTrack < 79)
                                m_nFDCTrack++;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP IN command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x50:  //  0x5X = STEP IN  W/TRACK UPDATE
                        {
                            lastCommandWasType1 = true;

                            if (m_nFDCTrack < 79)
                            {
                                m_nFDCTrack++;
                                m_FDC_TRKRegister = (uint8_t)m_nFDCTrack;
                            }
                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP IN WITH TRACK UPDATE command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x60:  //  0x6X = STEP OUT
                        {
                            lastCommandWasType1 = true;

                            if (m_nFDCTrack > 0)
                                --m_nFDCTrack;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP OUT command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    case 0x70:  //  0x7X = STEP OUT W/TRACK UPDATE
                        {
                            lastCommandWasType1 = true;

                            if (m_nFDCTrack > 0)
                            {
                                --m_nFDCTrack;
                                m_FDC_TRKRegister = (uint8_t)m_nFDCTrack;
                            }

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on STEP OUT WITH TRACK UPDATE command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            // set NOT _READY on restore if drive is not loaded with an image
                            uint8_t responseBuffer[16];
                            BuildAndSendTypeOnePacket(b, responseBuffer);
                        }
                        break;

                    // TYPE II

                    case 0x80:  //  0x8X = READ  SINGLE   SECTOR
                    case 0x90:  //  0x9X = READ  MULTIPLE SECTORS
                    case 0xA0:  //  0xAX = WRITE SINGLE   SECTOR
                    case 0xB0:  //  0xBX = WRITE MULTIPLE SECTORS
                        {
                            m_nReadingTrack = m_nWritingTrack = false;
                            doingMultiSector = false;

                            m_FDC_STATRegister |= (uint8_t)FDC_HDLOADED;

                            nType = 2;
                            m_nFDCTrack = m_FDC_TRKRegister;

                            m_nBytessToTransfer = bytessPerSector[nDrive];

                            // if we are doing multisector read or write, the bytes to transfer is the number of bytes
                            // that are remaining on the track starting at the sector in the sector register

                            if ((b & 0x10) == 0x10)
                            {
                                if (allowMultiSectorTransfers)
                                {
                                    m_nBytessToTransfer = (sectorsPerTrack[nDrive] - m_nFDCSector) * bytessPerSector[nDrive];
                                    doingMultiSector = true;
                                }
                            }

                            if ((b & 0x20) == 0x20)   // WRITE
                            {
                                m_nFDCReading = false;
                                m_nFDCWriting = true;
                                m_nFDCWritePtr = 0;
                                m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                                m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;
                                m_statusReadsWithoutDataRegisterAccess = 0;
                            }
                            else                    // READ
                            {
                                m_nFDCReading = true;
                                readBufferIndex = 0;

                                m_nFDCWriting = false;
                                m_nFDCReadPtr = 0;
                                m_statusReadsWithoutDataRegisterAccess = 0;

                                if (CheckReady(nDrive))
                                {
                                    // this will send the read request packet, wait for the response, set the status and fill the m_caReadBuffer
                                    uint8_t responseBuffer[16384];
                                    uint16_t responseLength = BuildAndSendFloppyReadRequestPacket(responseBuffer);  // this functio will set m_FDC_STATRegister  = m_FDC_STATRegister | responseBuffer[0];

                                    int status = responseBuffer[0];
    
                                    // shift the buffer up one byte to account for the status byte
                                    for (int i = 0; i < responseLength - 1; i++)
                                        responseBuffer[i] = responseBuffer[i + 1];

                                    // now transfer the data to the floppy read buffer we can obly handle 256 bytes at a time
                                    for (int i = 0; i < 256; i++)
                                        m_caReadBuffer[i] = responseBuffer[i];
                                
                                    m_FDC_DATARegister = m_caReadBuffer[0];
                                    m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                                    m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;
                                }
                            }

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            //totalBytesThisTrack = 0;    // reset on 0x8X 0x9X 0xAX 0xBX command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            if (!CheckReady(nDrive))
                            {
                                m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
                            }
                        }
                        break;

                    // TYPE III

                    case 0xC0:  //  0xCX = READ ADDRESS
                        {
                            // not yet implememnted - for not - return drive not ready 
                            m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands

                            /*
                            {
                                //
                                // Upon receipt of the Read Address command, the head is loaded and the BUSY Status Bit is set. The next
                                // encountered ID field is then read in from the disk and the siz data uint8_ts of the ID field are transferred 
                                // to the Data Register, and an DRQ is generated for each uint8_t. Those siz uint8_ts are as follows:
                                //
                                //  TRACK ADDR      1 uint8_t      m_FDC_TRKRegister
                                //  ZEROS           1 uint8_t      0x00
                                //  SECTOR ADDR     1 uint8_t      m_FDC_SECRegister
                                //  SECTOR LENGTH   1 uint8_t      256
                                //  CRC             2 uint8_ts     
                                //
                                // Although the CRC characters are transferred to the computer, the FD1771 chacks for validity and the CRC
                                // error status bit is set if there is a CRC error. The sector address of the ID field is written into the
                                // sector register. At the end of the operation, an interrupt is generated and the BUSY bit is cleared.
                                //
                                //  Each of the Type II commands contain a flag (b - bit 3) which in conjunction with the sector length
                                // field contents of the ID determines the length (number of uint8_ts) of the Data field.
                                //
                                //  For IBM compatibility, the b flag should be equal 1. The number of uint8_ts in the Data field (sector)
                                // is then 128 * 2 to the n where n = 0, 1, 2, 3
                                //
                                //      for b = 1
                                //
                                //          Sector Length Field     Number of uint8_ts in sector
                                //              00                      128
                                //              01                      256
                                //              02                      512
                                //              03                     1024
                                //
                                //  When the b flag equals zero, the sector length field (n) multiplied by 16 determines the number of uint8_ts
                                //  in the sector Data field as shown below:
                                //
                                //      for b = 0
                                //
                                //          Sector Length Field     Number of uint8_ts in sector
                                //              01                       16
                                //              02                       32
                                //              03                       48
                                //              04                       64
                                //               .                     
                                //               .                     
                                //               .                     
                                //              FF                     4080
                                //              00                     4096
                                //

                                //lastSectorAccessed = m_FDC_SECRegister;     // save the last sector we accessed

                                nType = 3;
                                m_FDC_STATRegister |= (uint8_t)FDC_HDLOADED;

                                m_nFDCReading = true;
                                m_nFDCWriting = false;

                                readBufferIndex = 0;

                                m_caReadBuffer[0] = 0xFE;
                                m_caReadBuffer[1] = m_FDC_TRKRegister;
                                m_caReadBuffer[2] = (uint8_t)m_nCurrentSideSelected;

                                m_statusReadsWithoutDataRegisterAccess = 0;

                                // determine what the value should be for the sector number returned. This is
                                // determined by the the disk geometry. 

                                if (DiskFormat(nDrive) == DISK_FORMAT_MINIFLEX)
                                {
                                    m_caReadBuffer[3] = m_FDC_SECRegister;
                                    m_caReadBuffer[4] = 0x00;   // 256 uint8_t sectors
                                }
                                else if (DiskFormat(nDrive) == DISK_FORMAT_FDOS)
                                {
                                    uint8_t maxSector = 9;

                                    if (m_FDC_SECRegister < maxSector + 1)
                                        m_caReadBuffer[3] = (uint8_t)(m_FDC_SECRegister);
                                    else
                                        m_caReadBuffer[3] = 0;

                                    m_caReadBuffer[4] = 0x01;   // 256 uint8_t sectors (bytessPerSector[nDrive] / 256 maybe?)

                                    m_FDC_SECRegister = m_caReadBuffer[3];
                                }
                                else if ((DiskFormat(nDrive) == DISK_FORMAT_FLEX) || (DiskFormat(nDrive) == DISK_FORMAT_UNIFLEX))
                                {
                                    uint8_t maxSector = sectorsPerTrack[nDrive];

                                    if (m_FDC_SECRegister < maxSector)
                                        m_caReadBuffer[3] = (uint8_t)(m_FDC_SECRegister + 1);
                                    else
                                        m_caReadBuffer[3] = 1;

                                    m_caReadBuffer[4] = 0x01;   // 256 uint8_t sectors (bytessPerSector[nDrive] / 256 maybe?)

                                    m_FDC_SECRegister = m_caReadBuffer[3];
                                }
                                else if (DiskFormat(nDrive) == DISK_FORMAT_FLEX_IMA)
                                {
                                    uint8_t maxSector = sectorsPerTrack[nDrive];
                                    uint8_t minSector = 1;

                                    if (m_nCurrentSideSelected == 0)
                                    {
                                        if (m_nFDCTrack == 0)
                                        {
                                            maxSector = 10;
                                            minSector = 1;
                                        }
                                        else
                                        {
                                            maxSector = 10;     // set to 10 since we don't know if this is a single or double sided image
                                            minSector = 1;
                                        }
                                    }
                                    else
                                    {
                                        if (m_nFDCTrack == 0)
                                        {
                                            maxSector = 20;     // max sectors on cylinder 0
                                            minSector = 11;
                                        }
                                        else
                                        {
                                            maxSector = sectorsPerTrack[nDrive];    // set to actual max sector since we know this is a double sided image
                                            minSector = 11;
                                        }
                                    }

                                    // say that we are over the last sector accessed so that it appears as though we are actually  rotating

                                    lastSectorAccessed++;
                                    if (lastSectorAccessed < maxSector)
                                        m_caReadBuffer[3] = (uint8_t)(lastSectorAccessed);
                                    else
                                        m_caReadBuffer[3] = minSector;

                                    lastSectorAccessed = m_caReadBuffer[3];     // do this in case it got set back to minSector

                                    m_caReadBuffer[4] = 0x01;   // 256 uint8_t sectors (bytessPerSector[nDrive] / 256 maybe?)
                                }
                                else
                                {
                                    // this is an OS-9 diskette image

                                    m_caReadBuffer[3] = m_FDC_SECRegister;
                                    m_caReadBuffer[4] = 0x01;   // 256 uint8_t sectors (bytessPerSector[nDrive] / 256 maybe?)

                                    m_FDC_SECRegister = m_caReadBuffer[3];
                                }

                                // now calculate the CRC
                                uint16_t readAddressCRC = CRCCCITT(m_caReadBuffer, 0, 5, 0xffff, 0);

                                m_caReadBuffer[5] = (uint8_t)(readAddressCRC / 256);
                                m_caReadBuffer[6] = (uint8_t)(readAddressCRC % 256);

                                // we included the address mark in the buffer to SRC it into the CRC value. But we
                                // do not return it to the user. So start at offset 1 in the buffer and set uint8_ts 
                                // to read and buffer pointer accordingly. 

                                m_nFDCReadPtr = 1;
                                m_nBytessToTransfer = 6;
                                m_FDC_DATARegister = m_caReadBuffer[1];

                                // do this so we can log whare we are when we are reading the address mark data
                                // ------------------------------------------
                                uint8_t FDCSectorSave = (uint8_t)m_nFDCSector;
                                m_nFDCSector = m_caReadBuffer[3];
                                CalcFileOffset(nDrive);
                                m_nFDCSector = FDCSectorSave;
                                // ------------------------------------------

                                m_FDC_STATRegister |= (uint8_t)FDC_BUSY;

                                writeTrackBufferSize = 0;
                                //totalBytesThisTrack = 0;    // reset on READ ADDRESS command to floppy controller
                                writeTrackWriteBufferIndex = 1;

                                writeTrackMinSector = 0xff;
                                writeTrackMaxSector = 0;
                            }

                            if (!CheckReady(nDrive))
                            {
                                m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
                            }
                            */
                        }
                        break;

                    case 0xE0:  //  0xEX = READ TRACK
                        {
                            // not yet implememnted - for not - return drive not ready 
                            m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
/*
                            // Read Track is 1110010s where s is the O flag. 1 = synchronizes to address mark, 0 = do not synchronize to address mark.
                            if ((b & 0x04) == 0x04)
                            {
                                m_nFDCReading = true;
                                readBufferIndex = 0;

                                m_nFDCWriting = false;
                                m_nFDCReadPtr = 0;

                                m_statusReadsWithoutDataRegisterAccess = 0;

                                //
                                // Upon receipt of the Read Track command, the head is loaded and the BUSY bit is set. Reading starts with
                                // the leading edge of first encountered index mark and continues until the next index pulse. As each uint8_t
                                // is assembled it is transferred to the Data Register and the Data Request is generated for each uint8_t. No
                                // CRC checking is performed. Gaps are included in the input data stream. If bit O of the command regoster
                                // is 0, the accumlation of uint8_ts is synchronized to each Address Mark encountered. Upon completion of the
                                // command, the interrupt is activated.
                                //
                                // Bit zero is the O flag. 1 = synchronizes to address mark, 0 = do not synchronize to address mark.
                                //
                                //      1:  set proper type, status bits and track register
                                nType = 3;
                                m_FDC_STATRegister |= (uint8_t)FDC_HDLOADED;
                                m_nFDCTrack = m_FDC_TRKRegister;

                                //      2:  using the data in the track register calculate the offset to the start of the track
                                //          and set number of uint8_ts to transfer. The sector gets set to 0 if on track 0 and to 1
                                //          if on any other track. The CalcFileOffset takes care of adjusting the sector number
                                //          internally based on image file format.

                                m_nFDCSector = m_nFDCTrack == 0 ? 0 : 1;

                                Let the WIFI code hanlde this part
                                CalcFileOffset(nDrive);

                                m_nBytessToTransfer = m_nFDCTrack == 0 ? (SectorsOnTrackZeroSideZero[nDrive]) * bytessPerSector[nDrive] : (sectorsPerTrack[nDrive]) * bytessPerSector[nDrive];

                                //      4:  read the entire track into memory
                                if (CheckReady(nDrive))
                                {
                                    FloppyDriveStream[nDrive].Seek(m_lFileOffset, SeekOrigin.Begin);
                                    FloppyDriveStream[nDrive].Read(m_caReadBuffer, 0, m_nBytessToTransfer);

                                    LogFloppyRead(m_caReadBuffer, 0, m_nBytessToTransfer);   // ???

                                    // start with a single uint8_t of 0x00

                                    m_FDC_DATARegister = 0x00;
                                }
                                
                                //      5:  indicate that this a track read and not a sector read.
                                m_nReadingTrack = true;
                                m_nWritingTrack = false;

                                //      6:  during the reading while serviceing the DRQ, the sector pre and post uint8_ts will be sent around each bytessPerSector[nDrive] uint8_t boundary.
                                //          A state machine will be used to do this, so start the stae machine at the beginning

                                currentReadTrackState = (int)ReadPostIndexGap;

                                // set statemachine indexs to initial values.

                                currentPostIndexGapIndex = 0;
                                currentGap2Index = 0;
                                currentGap3Index = 0;
                                currentDataSectorNumber = 1;

                                m_statusReadsWithoutDataRegisterAccess = 0;
                                writeTrackBufferSize = 0;
                                //totalBytesThisTrack = 0;    // reset on READ TRACK command to floppy controller
                                writeTrackWriteBufferIndex = 0;

                                writeTrackMinSector = 0xff;
                                writeTrackMaxSector = 0;
                            }

                            if (!CheckReady(nDrive))
                            {
                                m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
                            }
                            */
                        }
                        break;

                    case 0xF0:  //  0xFX = WRITE TRACK
                        {
                            // not yet implememnted - for not - return drive not ready 
                            m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands

                            /*
                            if (DiskFormat(nDrive) == DISK_FORMAT_FLEX_IMA || DiskFormat(nDrive) == DISK_FORMAT_FDOS)
                            {
                                //
                                //  Upon receipt of the Write Track command, the head is loaded and the BUSY bit is set. Writing starts with
                                // the leading edge of first encountered index mark and continues until the next index pulse, at which time
                                // an interrupt is generated. The Data Request is activated immediately upon receiving the command, but writing
                                // will not start until after the first uint8_t has been loaded into the Data Register. If the DR has not been
                                // loaded by the time the index pulse is encountered the operaiton is terminated making the device Not Busy.
                                // The Lost Data Status Bit is set and the interrupt is activated. If a uint8_t is not present in the DR as 
                                // needed, a uint8_t of zeros is substituted. Address Marks and CRC characters are written on the disk by
                                // detecting certain data uint8_t patterns in the outgoing data stream as shown in the table below. The CRC
                                // generator is initialized when any data uint8_t from F8 to FE is about to be transferred from the DR to the DSR.
                                //
                                //      CONTROL uint8_tS FOR INITIALIZATION
                                //
                                //      data pattern    interpretation          clock mark
                                //          F7          Write CRC character     FF
                                //          F8          Data Address Mark       C7
                                //          F9          Data Address Mark       C7
                                //          FA          Data Address Mark       C7
                                //          FB          Data Address Mark       C7
                                //          FC          Data Address Mark       D7
                                //          FD          Spare
                                //          FE          ID Address Mark         C7
                                //
                                // The Write Track command will not execute if the \DINT input is grounded. Instead the write protect status
                                // bit is set and the interrupt is activated. Note that 1 F7 pattern generates 2 CRC uint8_ts.
                                //

                                // formatting a track only works if the image files has the SIR already set up.

                                //  On each write track buffer command, reset the number of sectors in the buffer to 0.
                                //  As each address ID record is written, this number will get incremented/.
                                //  When the count is equal to the number of allowable sectors on the track being written, it is time
                                //  to signle complete by dropping BUSY and Setting the interrupt and stop setting DRQ after writing
                                //  the uint8_t to the sector track buffer.

                                sectorsInWriteTrackBuffer = 0;
                                lastFewBytesRead = 0;

                                m_nFDCReading = false;
                                m_nReadingTrack = false;

                                m_nFDCWriting = true;
                                m_nWritingTrack = true;

                                //      1:  set proper type, status bits and track register

                                nType = 3;
                                m_FDC_STATRegister |= (uint8_t)FDC_HDLOADED;
                                m_nFDCTrack = m_FDC_TRKRegister;

                                //      2:  reset the counters and indexes for a new track 

                                m_statusReadsWithoutDataRegisterAccess = 0;
                                writeTrackBufferSize = 0;
                                totalBytesThisTrack = 0;    // reset on WRITE TRACK command to floppy controller
                                writeTrackWriteBufferIndex = 0;

                                writeTrackMinSector = 0xff;
                                writeTrackMaxSector = 0;

                                currentWriteTrackState = (int)WaitForIDRecordMark;
                            }
                            else
                            {
                                nType = 3;
                                m_FDC_STATRegister &= (uint8_t)~FDC_HDLOADED;
                                m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;
                            }

                            if (!CheckReady(nDrive))
                            {
                                m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
                            }
                            */
                        }
                        break;

                    // TYPE IV

                    case 0xD0:  //  0xDX = FORCE INTERRUPT
                        {
                            //
                            // This command can be loaded into the command register at any time. If there is a current command under
                            // execution (Busy Status Bit set), the command will be terminated and an interrupt will be generated
                            // when the condition specified in the I0 through I3 field is detected.
                            //
                            //      I0  Not-Ready-To-Ready Transition
                            //      I1  Ready-To-Not-Ready Transition
                            //      I2  Every Index Pulse
                            //      I3  Immediate Interrupt
                            //
                            // If they are all equal to 0, there is no interrupt generated but the current command is terminated
                            // and the Busy Bit is cleared.
                            //
                            nType = 4;

                            m_statusReadsWithoutDataRegisterAccess = 0;
                            writeTrackBufferSize = 0;
                            totalBytesThisTrack = 0;    // reset on FORCE INTERRUPT command to floppy controller
                            writeTrackWriteBufferIndex = 0;

                            writeTrackMinSector = 0xff;
                            writeTrackMaxSector = 0;

                            if (!CheckReady(nDrive))
                            {
                                m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands
                            }
                        }
                        break;
                }

                //  since this may get set as a result of issuing a read command without actually 
                //  reading anything - just waiting for the controller to go not busy to do a CRC 
                //  check of the sector only, we need some way to make sure this status gets cleared
                //  after some time. Lets try by counting the number of times the status is checked
                //  without a data read and after bytessPerSector[nDrive] status reads withiout
                //  a data read - we will  set the status to not busy.

                if (driveReady)
                    m_FDC_STATRegister |= (uint8_t)FDC_BUSY;
                else
                {
                    // do not set busy if drive not ready

                    if ((m_FDC_STATRegister & (uint8_t)FDC_NOTREADY) == 0)
                        m_FDC_STATRegister |= (uint8_t)FDC_BUSY;
                }

                m_statusReadsWithoutDataRegisterAccess = 0;

                // do status regoster clean up based on command type

                switch (nType)
                {
                    case 1:
                        // 0x0X = RESTORE
                        // 0x1X = SEEK
                        // 0x2X = STEP
                        // 0x3X = STEP     W/TRACK UPDATE
                        // 0x4X = STEP IN
                        // 0x5X = STEP IN  W/TRACK UPDATE
                        // 0x6X = STEP OUT
                        // 0x7X = STEP OUT W/TRACK UPDATE

                        // all of the type 1 commands call BuildAndSendTypeOnePacket() and it sets the m_FDC_STATRegister
                        // so there is no need to do it here
                        //
                        // {
                        //     m_FDC_STATRegister |= (uint8_t)FDC_HDLOADED;
                        //     if (m_nFDCTrack == 0)
                        //         m_FDC_STATRegister |= (uint8_t)FDC_TRKZ;
                        //     else
                        //         m_FDC_STATRegister &= (uint8_t)~FDC_TRKZ;

                        //     m_FDC_STATRegister |= (uint8_t)FDC_DRQ;        // TESTING
                        // }
                        break;

                        // handle type 2 and type 3 the same

                    case 2:
                        //  0x8X = READ  SINGLE   SECTOR
                        //  0x9X = READ  MULTIPLE SECTORS
                        //  0xAX = WRITE SINGLE   SECTOR
                        //  0xBX = WRITE MULTIPLE SECTORS

                    case 3:
                        //  0xCX = READ ADDRESS
                        //  0xEX = READ TRACK
                        //  0xFX = WRITE TRACK
                        {
                            // not yet implememnted - for not - return drive not ready 
                            m_FDC_STATRegister &= (uint8_t)~FDC_NOTREADY;   // make sure we signal the drive is ready
                            m_FDC_STATRegister &= (uint8_t)~FDC_TRKZ;

                            // if (CheckReady(nDrive))
                            // {
                            //     m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                            //     m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;
                            //
                            //     if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                            //     {
                            //         SetInterrupt(_spin);
                            //         if (Program._cpu != null)
                            //         {
                            //             if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                            //             {
                            //                 try
                            //                 {
                            //                     Program.CpuThread.Resume();
                            //                 }
                            //                 catch (ThreadStateException e)
                            //                 {
                            //                     // do nothing if thread is not suspended
                            //                 }
                            //             }
                            //         }
                            //     }
                            // }
                            // else
                            // {
                            //     m_FDC_STATRegister &= (uint8_t)~FDC_DRQ;
                            //     m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;
                            // }
                        }
                        break;

                    //  0xDX = FORCE INTERRUPT

                    case 4:
                        m_FDC_STATRegister &= (uint8_t)~(
                                                    FDC_TRKZ    |   // clear TRKZ and LOSTDATA
                                                    FDC_DRQ     |   // clear Data Request Bit
                                                    FDC_SEEKERR |   // clear SEEK Error Bit
                                                    FDC_CRCERR  |   // clear CRC Error Bit
                                                    FDC_RNF     |   // clear Record Not Found Bit
                                                    FDC_BUSY        // clear BUSY bit
                                                  );
                        m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;        // turn off high order bit in drive status register
                        m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;       // assert INTRQ at the DRV register on force interrupt
                        break;
                }
            }
            break;

        case (int)FDC_TRKREG_OFFSET: // Ok to write to this register if drive is not ready or write protected
            {
                m_FDC_TRKRegister = b;
                m_nFDCTrack = b;
            }
            break;

        case (int)FDC_SECREG_OFFSET:
            {
                m_FDC_SECRegister = b;
                m_nFDCSector = b;
            }
            break;

        default:
            {
                //Program._cpu.WriteToFirst64K(m, b);
            }
            break;
    }
}

uint8_t FloppyRegisterRead(uint16_t m)
{
    uint8_t d;
    uint16_t nWhichRegister = m - (uint16_t)floppyBaseAddress;
    uint16_t nDrive         = m_FDC_DRVRegister & 0x03;           // get active drive

    uint16_t driveReady = false;
    uint16_t writeProtected = false;

    driveReady = CheckReady(nDrive);
    if (driveReady)
        writeProtected = CheckWriteProtect(nDrive);

    ClearInterrupt();

    switch (nWhichRegister)
    {
        case (int)FDC_DATAREG_OFFSET:
            {
                if (m_nFDCReading)
                {
                    m_statusReadsWithoutDataRegisterAccess = 0;
                    if (!m_nReadingTrack)
                    {
                        // this is a regular sector read or a read address read - no state machine required
                        // the uint8_ts are already in the m_caBuffer from executing the command write to the 
                        // command regiuster

                        m_FDC_DATARegister = m_caReadBuffer[m_nFDCReadPtr];
                        m_nFDCReadPtr++;

                        // we just bumped the pointer. When it gets bumped past the number of uint8_ts to read 
                        // this means that the caller is responding to what is the last DRQ in the data 
                        // stream. - it's time to stop sending DRQ's and signal NOT BUSY. kEEP IN MIND THAT
                        // m_nFDCReadPtr is zero based and m_nBytessToTransfer is a count.

                        uint8_t temp[6];
                        uint16_t weAreDone = false;

                        if (m_nBytessToTransfer == 6)
                        {
                            // we are doing a read address so we need to check if the m_nFDCReadPtr > m_nBytessToTransfer
                            // because the offset to read starts at 1 and not 0.

                            if (m_nFDCReadPtr > m_nBytessToTransfer)
                            {
                                m_FDC_DRVRegister = 0x00;
                                weAreDone = true;
                            }
                        }
                        else
                        {
                            if (m_nFDCReadPtr % bytessPerSector[nDrive] == 0 && doingMultiSector)
                            {
                                // we just wrote a full sector - bump FDC_SectorRegister

                                m_FDC_SECRegister++;
                            }

                            if (m_nFDCReadPtr >= m_nBytessToTransfer)
                            {
                                weAreDone = true;
                                m_FDC_DRVRegister = 0x00;
                                doingMultiSector = false;
                            }
                        }

                        if (weAreDone)
                        {
                            // this is for debugging - not needed by the application
                            //
                            //if (m_nBytessToTransfer == 6)
                            //{
                            //    for (uint16_t i = 1; i < 7; i++)
                            //    {
                            //        temp[i - 1] = m_caReadBuffer[i];
                            //    }
                            //}

                            // we are done with this sector - stop sending DRQ's and set NOT BUSY

                            m_FDC_STATRegister &= (uint8_t)~FDC_DRQ;
                            m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;

                            // slao turn off the INTRQ signal that DRQ provides to the bus.

                            m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;
                            m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;               // assert INTRQ at the DRV register when the operation is complete

                            // not running cpu as a thread.
                            /*
                            if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                            {
                                SetInterrupt(_spin);
                                if (Program._cpu != null)
                                {
                                    if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                                    {
                                        try
                                        {
                                            Program.CpuThread.Resume();
                                        }
                                        catch (ThreadStateException e)
                                        {
                                            // do nothing if thread is not suspended
                                        }
                                    }
                                }
                            }
                            */
                        }
                        else
                        {
                            // signal that there is data in the data register that needs to be read.

                            m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                            m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;

                            // only interrupt when the command is complete

                            /* the WIFI code will handle this
                            if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                            {
                                SetInterrupt(_spin);
                                if (Program._cpu != null)
                                {
                                    if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                                    {
                                        try
                                        {
                                            Program.CpuThread.Resume();
                                        }
                                        catch (ThreadStateException e)
                                        {
                                            // do nothing if thread is not suspended
                                        }
                                    }
                                }
                            }
                            */
                        }
                    }
                    else
                    {
                        // not yet implememnted - for not - return drive not ready 
                        m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands

                        // // we are doing a track read so we need to use a state machine to send the
                        // //      post index Gap and then record id, Gap2, data field, Gap3 for each
                        // //      of the sectors
                        // //
                        // //      The Post Index Gap is defined as 32 uint8_ts of zeros or 0xFF
                        // //      The ID Record is 6 uint8_ts of id information preceeded by a uint8_t of 0xFE
                        // //
                        // //          TRACK ADDR      1 uint8_t      m_FDC_TRKRegister
                        // //          SIDE            1 uint8_t      
                        // //          SECTOR ADDR     1 uint8_t      m_FDC_SECRegister
                        // //          SECTOR LENGTH   1 uint8_t      256 (0 = 128, 1 = 256, 2 = 512, 3 = 1024)
                        // //          CRC             2 uint8_ts     
                        // //
                        // //      Gap2 is defined as 11 uint8_ts of 0x00 followed by 6 uint8_ts of 0xFF
                        // //      the data uint8_ts are 256 uint8_ts of data preceeded by a dta mark of 0xFB
                        // //      Gap3 is defined as  1 uint8_t of 0xFF followed by 32 uint8_ts of 0x00 or 0xFF
                        // //
                        // //
                        // //  read track states                   next state                  FM                      MFM
                        // //                                                              count   value               count   value
                        // //      ReadPostIndexGap                ReadIDRecord            40      0xFF or 0x00        80      0x4E
                        // //                                                               6      0x00                12      0x00
                        // //                                                                                           3      0xF6 (writes C2)
                        // //                                                               1      0xFC (index mark)    1      0xFC (index mark)
                        // //                                                              26      0x00 or 0xFF        50      0x4E (both write bracketed field 26 times)
                        // //                  Repeat for each sector
                        // //  -----------------------
                        // //  |                                                            6      0x00                12      0x00
                        // //  |                                                                                        3      0xF5 (writes 0xA1's)
                        // //  |   ReadIDRecord (0xFE)             ReadIDRecordTrack        1      0xFE                 1      0xFE
                        // //  |       ReadIDRecordTrack           ReadIDRecordSide         1      track number         1      track number
                        // //  |       ReadIDRecordSide            ReadIDRecordSector       1      side number (0 or 1) 1      side number (0 or 1)
                        // //  |       ReadIDRecordSector          ReadIDRecordSectorLength 1      sector number        1      sector number
                        // //  |       ReadIDRecordSectorLength    ReadIDRecordCRCHi        1      sector length        1      sector length (both 128 * 2^n where n = sector length
                        // //  |       ReadIDRecordCRCHi           ReadIDRecordCRCLo        1      0xF7                 1      0xF7 (both write 2 uint8_t CRC)
                        // //  |       ReadIDRecordCRCLo           ReadGap2                
                        // //  |   ReadGap2                        ReadDataRecord          11      0xFF or 0x00        22      0x4E
                        // //  |                                                            6      0x00                12      0x00
                        // //  |                                                                                        3      0xF5 (writes 0xA1's)
                        // //  |   ReadDataRecord (0xFB)           ReadDataBytes            1      0xFB                 1      0xFB
                        // //  |       ReadDataBytes               ReadDataRecordCRCHi    xxx      Data               xxx      Data where xxx is number of uint8_ts per sector)
                        // //  |       ReadDataRecordCRCHi         ReadDataRecordCRCLo      1      0xF7                 1      0xF7 (both write 2 uint8_t CRC)
                        // //  |       ReadDataRecordCRCLo         ReadGap3                27      0xFF or 0x00        54      x04E
                        // //  |   ReadGap3                        ReadIDRecord
                        // //  -----------------------
                        // //
                        // //      exit state machine after no more data to read.
                        //
                        // // when we drop out of here the uint8_t to return to the user should be in m_FDC_DATARegister
                        //
                        // switch (currentReadTrackState)
                        // {
                        //     case (int)ReadPostIndexGap:
                        //         if (currentPostIndexGapIndex < currentPostIndexGapSize)
                        //         {
                        //             m_FDC_DATARegister = 0x00;
                        //             currentPostIndexGapIndex++;
                        //         }
                        //         else
                        //         {
                        //             currentPostIndexGapIndex = 0;
                        //             currentReadTrackState = (int)ReadIDRecord;
                        //         }
                        //         break;
                        //
                        //     case (int)ReadIDRecord:
                        //         m_FDC_DATARegister = 0xFE;
                        //         IDRecorduint8_ts[0] = m_FDC_DATARegister;
                        //         currentReadTrackState = (int)ReadIDRecordTrack;
                        //         break;
                        //
                        //     case (int)ReadIDRecordTrack:
                        //         m_FDC_DATARegister = (uint8_t)m_nFDCTrack;
                        //         IDRecorduint8_ts[1] = m_FDC_DATARegister;
                        //         currentReadTrackState = (int)ReadIDRecordSide;
                        //         break;
                        //
                        //     case (int)ReadIDRecordSide:
                        //         m_FDC_DATARegister = (uint8_t)m_nCurrentSideSelected;
                        //         IDRecorduint8_ts[2] = m_FDC_DATARegister;
                        //         currentReadTrackState = (int)ReadIDRecordSector;
                        //         break;
                        //
                        //     case (int)ReadIDRecordSector:
                        //         if (m_nFDCTrack == 0 && currentDataSectorNumber == 1 && (DiskFormat(nDrive) == DISK_FORMAT_FLEX || DiskFormat(nDrive) == DISK_FORMAT_FLEX_IMA))
                        //             m_FDC_DATARegister = 0x00;
                        //         else
                        //             m_FDC_DATARegister = (uint8_t)currentDataSectorNumber;
                        //
                        //         IDRecorduint8_ts[3] = m_FDC_DATARegister;
                        //         currentReadTrackState = (int)ReadIDRecordSectorLength;
                        //
                        //         break;
                        //
                        //     case (int)ReadIDRecordSectorLength:
                        //         m_FDC_DATARegister = 0x01;      // 256 uint8_t sectors (maybe this should be bytessPerSector[nDrive])
                        //         IDRecorduint8_ts[4] = m_FDC_DATARegister;
                        //         currentReadTrackState = (int)ReadIDRecordCRCHi;
                        //         break;
                        //
                        //     case (int)ReadIDRecordCRCHi:
                        //         crcID = CRCCCITT(IDRecorduint8_ts, 0, IDRecorduint8_ts.Length, 0xffff, 0);
                        //         m_FDC_DATARegister = (uint8_t)(crcID / 256);
                        //         currentReadTrackState = (int)ReadIDRecordCRCLo;
                        //         break;
                        //
                        //     case (int)ReadIDRecordCRCLo:
                        //         m_FDC_DATARegister = (uint8_t)(crcID % 256);
                        //         currentReadTrackState = (int)ReadGap2;
                        //         break;
                        //
                        //     case (int)ReadGap2:
                        //         if (currentGap2Index < currentGap2Size)
                        //         {
                        //             if (currentGap2Index < currentGap2Size - 6)
                        //                 m_FDC_DATARegister = 0x00;
                        //             else
                        //                 m_FDC_DATARegister = 0xFF;
                        //
                        //             currentGap2Index++;
                        //         }
                        //         else
                        //         {
                        //             currentGap2Index = 0;
                        //             currentReadTrackState = (int)ReadDataRecord;
                        //         }
                        //         break;
                        //
                        //     case (int)ReadDataRecord:
                        //         m_FDC_DATARegister = 0xFB;
                        //         currentReadTrackState = (int)ReadDataBytes;
                        //         break;
                        //
                        //     case (int)ReadDataBytes:
                        //         m_FDC_DATARegister = m_caReadBuffer[m_nFDCReadPtr];
                        //         lock (Program._cpu.buildingDebugLineLock)
                        //         {
                        //             m_nFDCReadPtr++;
                        //         }
                        //
                        //         // see if we are done with this sector worth of data. If we are
                        //         // move on to the next state - read the Data CRC.
                        //
                        //         if (m_nFDCReadPtr % bytessPerSector[nDrive] == 0)
                        //             currentReadTrackState = (int)ReadDataRecordCRCHi;
                        //         break;
                        //
                        //     case (int)ReadDataRecordCRCHi:
                        //         {
                        //             uint8_t[] thisSector = new uint8_t[257];
                        //             thisSector[0] = 0xFB;
                        //
                        //             // make a buffer with the data mark that we can pass to the CRC calculator
                        //
                        //             for (uint16_t i = 0; i < bytessPerSector[nDrive]; i++)
                        //             {
                        //                 thisSector[i + 1] = m_caReadBuffer[i];
                        //             }
                        //
                        //             // calculate the CRC, pass back the hi uint8_t and set the next state to pass back the lo uint8_t
                        //
                        //             crcData = CRCCCITT(thisSector, 0, thisSector.Length, 0xffff, 0);
                        //             m_FDC_DATARegister = (uint8_t)(crcData / 256);
                        //             currentReadTrackState = (int)ReadDataRecordCRCLo;
                        //         }
                        //         break;
                        //
                        //     case (int)ReadDataRecordCRCLo:
                        //         m_FDC_DATARegister = (uint8_t)(crcData % 256);
                        //         currentReadTrackState = (int)ReadGap3;
                        //
                        //         // now is the time to increment the current data sector number
                        //
                        //         currentDataSectorNumber++;
                        //         break;
                        //
                        //     case (int)ReadGap3:
                        //         if (currentGap3Index < currentGap3Size)
                        //         {
                        //             if (currentGap3Index == 0)
                        //                 m_FDC_DATARegister = 0x00;
                        //             else
                        //                 m_FDC_DATARegister = 0xFF;
                        //
                        //             currentGap3Index++;
                        //         }
                        //         else
                        //         {
                        //             currentGap3Index = 0;
                        //             currentReadTrackState = (int)ReadIDRecord;
                        //         }
                        //
                        //         // after responding to DRQ on last uint8_t of every GAP3 we need to see if we are done by comparing
                        //         // the m_nFDCReadPtr to the number of uint8_ts to read. If they are equal, we should not issue any
                        //         // more DRQ's or interrupts and we should turn off the drive LED. Basically - shut it down.
                        //
                        //         if (m_nFDCReadPtr == m_nBytessToTransfer)
                        //         {
                        //             lock (Program._cpu.buildingDebugLineLock)
                        //             {
                        //                 m_FDC_STATRegister &= (uint8_t)~(FDC_DRQ | FDC_BUSY);
                        //                 m_FDC_DRVRegister  &= (uint8_t)~DRV_DRQ;            // turn off high order bit in drive status register
                        //                 m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;            // assert INTRQ at the DRV register when the operation is complete
                        //
                        //                 m_nFDCReading = false;
                        //
                        //                 LogFloppyRead(readBuffer, 0, m_nBytessToTransfer);
                        //                 readBufferIndex = 0;
                        //
                        //                 ClearInterrupt();
                        //                 activityLedColor = (int)ActivityLedColors.greydot;
                        //
                        //                 // reset the state machine
                        //
                        //                 currentReadTrackState = (int)ReadPostIndexGap;
                        //             }
                        //         }
                        //         else
                        //         {
                        //             m_FDC_STATRegister |= (uint8_t)FDC_DRQ;
                        //             m_FDC_DRVRegister  |= (uint8_t)DRV_DRQ;
                        //
                        //             if (_bInterruptEnabled && (m_FDC_DRVRegister & (uint8_t)DRV_INTRQ) == (uint8_t)DRV_INTRQ)
                        //             {
                        //                 lock (Program._cpu.buildingDebugLineLock)
                        //                 {
                        //                     SetInterrupt(_spin);
                        //                     if (Program._cpu != null)
                        //                     {
                        //                         if ((Program._cpu.InWait || Program._cpu.InSync) && Program.CpuThread.ThreadState == System.Threading.ThreadState.Suspended)
                        //                         {
                        //                             try
                        //                             {
                        //                                 Program.CpuThread.Resume();
                        //                             }
                        //                             catch (ThreadStateException e)
                        //                             {
                        //                                 // do nothing if thread is not suspended
                        //                             }
                        //                         }
                        //                     }
                        //                 }
                        //             }
                        //         }
                        //         break;
                        //
                        //     default:
                        //         break;
                        // }
                    }

                    // --------------------
                    d = m_FDC_DATARegister;

                    if (readBufferIndex < 65536)
                    {
                        readBuffer[readBufferIndex] = d;
                        readBufferIndex++;
                    }
                }
                else
                {
                    d = m_FDC_DATARegister;
                    readBufferIndex = 0;
                }
            }
            break;

        case (int)FDC_STATREG_OFFSET:
            {
                // first see if lastCommandWasType1 is true. If it was - we just need to maintain the status of the index pulse

                if (lastCommandWasType1)
                {
                    // say done with type 1 command by turning off BUSY

                    m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;

                    if (lastIndexPusleDRQStatus)    // we are reporting index pulse started
                    {
                        // decrement both counters

                        indexPulseWidthCounter--;
                        indexPulseCounter--;

                        if (indexPulseWidthCounter <= 0)
                        {
                            m_FDC_STATRegister &= (uint8_t)~FDC_DRQ;      // de-assert DRQ in the status reister
                            m_FDC_DRVRegister  &= (uint8_t)~DRV_DRQ;
                            indexPulseWidthCounter = 20;
                            indexPulseCounter = 3000;
                            lastIndexPusleDRQStatus = false;
                        }
                        else
                            m_FDC_STATRegister |= (uint8_t)FDC_DRQ;      // assert DRQ in the status reister
                    }
                    else
                    {
                        // decrement jsut the rotation counter

                        indexPulseCounter--;
                        if (indexPulseCounter <= 0)
                        {
                            m_FDC_STATRegister |= (uint8_t)FDC_DRQ;      // assert DRQ in the status reister
                            indexPulseWidthCounter = 20;
                            indexPulseCounter = 3000;
                            lastIndexPusleDRQStatus = true;
                        }
                        else
                        {
                            m_FDC_STATRegister &= (uint8_t)~FDC_DRQ;      // de-assert DRQ in the status reister
                            m_FDC_DRVRegister  &= (uint8_t)~DRV_DRQ;
                        }
                    }

                    d = m_FDC_STATRegister;                      // set value to return
                }
                else
                {
                    // see if we are reading the address mark data. we could also use the value in the m_FDC_CMDRegister.
                    // if it is m_FDC_CMDRegister & 0xC0 = 0xC0 then we are doing a read address.

                    if (m_nBytessToTransfer == 6)
                    {
                        // not yet implememnted - for not - return drive not ready 
                        m_FDC_STATRegister |= (uint8_t)FDC_NOTREADY;     // set not ready for type I commands

                        // // if we are - then see if we are done - checking for > because we start at offset 1
                        //
                        // if ((m_nFDCReadPtr > m_nBytessToTransfer) || (m_statusReadsWithoutDataRegisterAccess > 3))
                        // {
                        //     // we are done reading the requested number of bytes
                        //
                        //     lock (Program._cpu.buildingDebugLineLock)
                        //     {
                        //         m_FDC_STATRegister &= (uint8_t)~(FDC_DRQ | FDC_BUSY);
                        //         m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;            // turn off high order bit in drive status register
                        //         m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;           // assert INTRQ at the DRV register when the operation is complete
                        //
                        //         m_nFDCReading = false;
                        //
                        //         // we are using readBuffer here on purpose and not m_caReadBuffer - it is 0 based (no address
                        //         // mark was stored here - just the 6 uint8_ts of data)
                        //
                        //         LogFloppyRead(readBuffer, 0, m_nBytessToTransfer);
                        //         readBufferIndex = 0;
                        //
                        //         ClearInterrupt();
                        //     }
                        // }
                    }
                    else
                    {
                        /* 2797 commands - contents of m_FDC_CMDRegister
                            * 
                            0x0X = RESTORE
                            0x1X = SEEK
                            0x2X = STEP
                            0x3X = STEP     W/TRACK UPDATE
                            0x4X = STEP IN
                            0x5X = STEP IN  W/TRACK UPDATE
                            0x6X = STEP OUT
                            0x7X = STEP OUT W/TRACK UPDATE
                            0x8X = READ  SINGLE   SECTOR
                            0x9X = READ  MULTIPLE SECTORS
                            0xAX = WRITE SINGLE   SECTOR
                            0xBX = WRITE MULTIPLE SECTORS
                            0xCX = READ ADDRESS
                            0xDX = FORCE INTERRUPT
                            0xEX = READ TRACK
                            0xFX = WRITE TRACK
                            *
                            */

                        // some debugging code - 
                        //
                        //// ignore write track, seek force restore and restore commands while debugging
                        //if (m_FDC_CMDRegister != 0xf4 && m_FDC_CMDRegister != 0x1b && m_FDC_CMDRegister != 0x0b && m_FDC_CMDRegister != 0xd4)
                        //{
                        //    if (m_nFDCReadPtr >= 0x00ff)
                        //    {
                        //        uint16_t x = 1;
                        //    }
                        //}

                        CheckReadyAndWriteProtect(nDrive);

                        if (!m_nFDCReading && !m_nFDCWriting)           // turn off BUSY if not read/writing
                        {
                            m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;
                        }

                        if ((m_statusReadsWithoutDataRegisterAccess > (m_nBytessToTransfer / 16)) && (m_nBytessToTransfer > 0) && m_nFDCReading)
                        {
                            m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;     // clear BUSY if data not read
                            m_FDC_STATRegister &= (uint8_t)~FDC_DRQ;       // TESTING
                            m_FDC_DRVRegister  &= (uint8_t)~DRV_DRQ;
                            ClearInterrupt();
                        }

                        if (m_statusReadsWithoutDataRegisterAccess >= 16 && !m_nWritingTrack && m_nFDCWriting)
                        {
                            if (doingMultiSector && m_nFDCWritePtr != m_nBytessToTransfer)
                            {
                                // the caller has issued a multi sector write and is not kind enough to fill the buufer, so
                                // we need to flush the buffer to disk.

                                /* this needs to be done in the WIFI code
                                CalcFileOffset(nDrive);

                                if (CheckReady(nDrive))
                                {
                                    FloppyDriveStream[nDrive].Seek(m_lFileOffset, SeekOrigin.Begin);
                                    FloppyDriveStream[nDrive].Write(m_caWriteBuffer, 0, m_nFDCWritePtr);

                                    if (doingMultiSector)
                                    {
                                        // We need to make sure we update the FDC_StatusRegister if this is a multi sector write

                                        m_FDC_SECRegister = sectorsPerTrack[nDrive];
                                    }
                                    else
                                        m_FDC_SECRegister++;

                                    LogFloppyWrite(m_lFileOffset, m_caWriteBuffer, 0, m_nBytessToTransfer);

                                    FloppyDriveStream[nDrive].Flush();
                                }
                                */

                                m_FDC_STATRegister &= (uint8_t)~(FDC_DRQ | FDC_BUSY);
                                m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;                // turn off high order bit in drive status register
                                m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;               // assert INTRQ at the DRV register when the operation is complete
                                m_nFDCWriting = false;

                                ClearInterrupt();
                            }

                            m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;     // clear BUSY if data not read
                            ClearInterrupt();
                        }

                        // this is where we will get out of writing track by dumping the track to the file

                        if (m_statusReadsWithoutDataRegisterAccess >= 16 && m_nWritingTrack)
                        {
                            WriteTrackToImage(nDrive);
                        }

                        if (m_nFDCReadPtr > m_nBytessToTransfer || m_nBytessToTransfer == 0)
                        {
                            // we are done reading the requested number of bytes

                            m_FDC_STATRegister &= (uint8_t)~(FDC_DRQ | FDC_BUSY);
                            m_FDC_DRVRegister &= (uint8_t)~DRV_DRQ;            // turn off high order bit in drive status register
                            m_FDC_DRVRegister |= (uint8_t)DRV_INTRQ;           // assert INTRQ at the DRV register when the operation is complete

                            m_nFDCReading = false;

                            readBufferIndex = 0;

                            ClearInterrupt();
                        }
                    }

                    /* status register bits
                        0x01 = BUSY
                        0x02 = DRQ
                        0x04 = 
                        0x08 = 
                        0x10 = 
                    */

                    d = m_FDC_STATRegister;                      // get controller status
                    m_statusReadsWithoutDataRegisterAccess++;

                    if (m_statusReadsWithoutDataRegisterAccess > 1)
                    {
                        // any read of the status register should clear this flag unless we just finished reading

                        m_FDC_DRVRegister &= (uint8_t)~DRV_INTRQ;      // de-assert INTRQ at the DRV register
                        ClearInterrupt();
                    }
                }
            }
            break;

        case (int)FDC_DRVREG_OFFSET:
            {
                m_statusReadsWithoutDataRegisterAccess++;
                if ((m_statusReadsWithoutDataRegisterAccess > (m_nBytessToTransfer / 16)) && (m_nBytessToTransfer > 0) && m_nFDCReading)
                {
                    m_FDC_STATRegister &= (uint8_t)~FDC_BUSY;     // clear BUSY if data not read
                    ClearInterrupt();
                }

                // only return the bits that the user can read. Mask out out all but DRQ and INTRQ (bots 7 and 6)
                //
                //      7/20/2023 - added check for track 0

#ifndef PT68_1                
                if (m_FDC_TRKRegister == 0 &&)
                    m_FDC_DRVRegister |= 0x04;
                else
                    m_FDC_DRVRegister &= 0xFB;
#else
                m_FDC_DRVRegister &= 0xFB;    // preserve the INTRQ and DRQ bits
#endif                    
                d = (uint8_t)(m_FDC_DRVRegister & 0xC4);
            }
            break;

        case (int)FDC_TRKREG_OFFSET:
            {
                d = m_FDC_TRKRegister;                      // get Track Register
            }
            break;

        case (int)FDC_SECREG_OFFSET:
            {
                d = m_FDC_SECRegister;                      // get Track Register
            }
            break;

        default:
            //d = Program._cpu.ReadFromFirst64K(m);   // memory read
            break;
    }

    return (d);
}
