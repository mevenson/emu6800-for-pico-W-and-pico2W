#define SDCARD

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include "sdcard.h"
#include "tcp.h"

// these two use the wifi to write and read data to/from the SD Card image
uint16_t BuildAndSendSDCARDWriteRequestPacket (uint8_t *responseBuffer)
{
    // build the task buffer to send to the wifi server
    commandRegister = 0x30;
    uint8_t packetData[7+256];

    packetData[0] = 'S';                            // this is an sdcard request packet
    packetData[1] = commandRegister;                // request command = read sector
    packetData[2] = sizeDriveHeadRegister;          // SIZE/DRIVE/HEAD REGISTER
    packetData[3] = cylinderHiRegister;             // cylinder number high byte
    packetData[4] = cylinderLowRegister;            // cylinder number low byte
    packetData[5] = sectorNumberRegister;           // secttor in the cylinder to read
    packetData[6] = sectorCountRegister;            // number of sectors to read

    // fill the packetData sector buffer from sectorBuffer
    for (int i = 0; i < 256; i++)
        packetData[i + 7] = sectorBuffer[i];

    // send the packet to the server
    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));

    return (responseLength);    
}

uint16_t BuildAndSendSDCADReadRequestPacket (uint8_t *responseBuffer)
{
    // build the task buffer to send to the wifi server
    commandRegister = 0x20;
    uint8_t packetData[7];

    packetData[0] = 'S';                            // this is an sdcard request packet
    packetData[1] = commandRegister;                // request command = read sector
    packetData[2] = sizeDriveHeadRegister;          // SIZE/DRIVE/HEAD REGISTER
    packetData[3] = cylinderHiRegister;             // cylinder number high byte
    packetData[4] = cylinderLowRegister;            // cylinder number low byte
    packetData[5] = sectorNumberRegister;           // secttor in the cylinder to read
    packetData[6] = sectorCountRegister;            // number of sectors to read

    uint16_t responseLength = tcp_request(packetData, responseBuffer, sizeof(packetData));

    return (responseLength);    
}

void SDCARDRegisterWrite(uint16_t m, uint8_t b)
{
    //  only least 3 bits are relavent - address starts on 8 byte boundary for 8 bytes
    //  so if the base address is $E008 (like the PT69-5 Winchester) or $E050 (like the SWTPC SS-30 card)
    //  the register offsets will be the same

    switch (m & 0x07)
    {
        case 0:     // data register
            dataRegister = b;
            if (writing)
            {
                sectorBuffer[sectorBufferIndex++] = b;
                if (sectorBufferIndex > 255)
                {
                    if ((statusRegister & 0x01) == 0x00)        // no error
                    {
                        uint8_t responseBuffer[16384];
                        BuildAndSendSDCARDWriteRequestPacket(responseBuffer);

                        statusRegister &= 0x7F;         // clear BUSY bit = set not busy
                        statusRegister &= 0xF7;         // clear DRQ bit = set no data request
                        statusRegister |= 0x40;         // turn on DRDY bit while not reading  or writing (able to accept a command)
                        statusRegister &= 0xDE;         // turn off DWF
                        statusRegister &= 0xFE;         // turn off ERROR 
                    }
                    else
                    {
                        statusRegister &= 0x7F;         // clear BUSY bit = set not busy
                        statusRegister &= 0xF7;         // clear DRQ bit = set no data request
                        statusRegister |= 0x40;         // turn on DRDY bit while not reading  or writing (able to accept a command)
                    }

                    sectorBufferIndex = 0;          // wrap if > than sector size of 256
                    sectorCountRegister = 0;        // this gets set back to 0 whenever a command completes
                }
            }
            break;

        case 1:     // error register on read and write precomp register on write
            writePrecompRegister = b;
            break;

        case 2:     // sector count
            // allocate memory to hold the bytes returned
            sectorCountRegister = b;

            // if the sector buffer has memory allocated to it - free it
            if (sectorBuffer != NULL)
                free(sectorBuffer);

            sectorBuffer = (uint8_t*)malloc(sectorCountRegister * 256);
            if (sectorBuffer == NULL)
            {
                printf("out of memory on malloc from setting SD Card sector count\n");
                exit(1);
            }
            break;

        // the following four bytes make up the 26 bit LSN of the sector in 
        // LBA mode. Multiply by 256 to get offset into the file for the data
        //
        //  There can be up to 4 logical drives in each image. that requires
        //  two bits. These two bits are put in the head select register in
        //  the ide interfare when in LBA mode. This means that the sector 
        //  provides the lower 8 buts of the lower 16 bits of the LSN and 
        //  the track provides the upper 8 bits of the lower 16 bits of the
        //  LSN and partition offset provides the next 8 bits of the 26 bit
        //  LSN while the physical controller drive number provides the
        //  bit 4 of the sizeDriveHeadRegister.
        //
        //      Setting any of these next four recalculates the offset

        case 3:     // sector number        $E00B on PT69-5, $E050 on SWTPC SS-30 SD Card
            sectorNumberRegister = b; ;       // this gets the low byte of the LSN in LBA mode (sector)
            break;

        case 4:     // cylinder number (LSB)
            cylinderLowRegister = b;        // this gets the hi byte of the LSN in LBA mode (track)
            break;

        case 5:     // cylinder number (MSB)
            cylinderHiRegister = b;         // this gets the partition number from the drive table
            break;

        case 6:     // size/drive/head register
            // this gets the physical FLEX drive 0 and LBA mode or'd with #$E0 number for FLEX
            // this is 0x88 for OS9

            sizeDriveHeadRegister = b;      
            upper4BitsOfLSN = b & 0x0F;
            break;

        case 7:     // command register
            commandRegister = b;

            statusRegister |= 0x80;         // set BUSY bit
            statusRegister |= 0x08;         // set DRQ bit
            statusRegister |= 0x10;         // set DSC bit
            statusRegister &= 0xBF;         // turn off DRDY bit while reading (not able to accept a command)
            statusRegister &= 0xDE;         // turn off SWF
            statusRegister &= 0xFE;         // turn off ERROR 

            errorRegister  &= 0xEF;         // turn off IDNF

            sectorBufferIndex = 0;

            if ((b & 0xF0) == sdCardRDCommand)
            {
                reading = true;
                writing = false;

                if ((statusRegister & 0x01) == 0x00)        // no error
                {
                    uint8_t responseBuffer[16384];
                    BuildAndSendSDCADReadRequestPacket(responseBuffer);

                    int status = responseBuffer[0];

                    // the data has already been converted from RAW format by the server
                    for (int i = 0; i < sectorCountRegister * 256; i++)
                    {
                        sectorBuffer[i] = responseBuffer[i + 1];    // the first byte is the status byte - the sector data starts at 1
                    }
                }
            }
            else if ((b & 0xF0) == sdCardWRCommand)
            {
                writing = true;
                reading = false;
            }
            else
            {
                if (b < 0x20)
                {
                    // this is   Recalibrate command (0x00 - 0x1F)
                }
                else
                {
                    // could be 
                    //
                    //      0x40 - verify
                    //      0x50 - format track
                    //      0x70 - seek
                    //      0x90 - diagnostic
                    //      0x91 - set drive parameters
                    //      
                    //  optional (enhanced IDE) commands
                    //
                    //      0x3C - Write verification.
                    //      0xC4 - Read multiple sectors.
                    //      0xC5 - Write multiple sectors.
                    //      0xC6 - Set multiple mode.
                    //      0xC8 - DMA read, with retry (>C9 without retry). Reads sector in direct memory access mode.
                    //      0xCA - DMA write, with retry (>CB without retry).
                    //      0xDB - Acknowledge medium change.
                    //      0xDE - Lock drive door.
                    //      0xDF - Unlock drive door.
                    //      0xE0 or > 94 - Standby immediate. Spins down the drive at once. No parameters.
                    //      0xE1 or > 95 - Idle immediate.
                    //      0xE2 or > 96 and >E3 or > 97 - Set standby mode. Write in the sector count register the time (5 seconds units) of non-activity after which the disk will spin-down. Write the command to the command register and the disk is set in an auto-power-save modus. The disk will automatically spin-up again when you issue read/write commands. >E2 (or >96) will spin-down, >E3 (or >97) will keep the disk spinning after the command has been given. Example: write >10 in the sector count register, give command >E2 and the disk will spin-down after 16*5=80 seconds of non-activity.
                    //      0xE4 - Read buffer. Reads the content of the controller buffer (usefull for tests).
                    //      0xE5 or > 98 - Checks for active, idle, standby, sleep.
                    //      0xE6 or > 99 - Set sleep mode.
                    //      0xE8 - Write buffer. Loads data into the buffer but does not write anything on disk.
                    //      0xE9 - Write same sector.
                    //      0xEC - Identify drive. This command prepares a buffer (256 16-bit words) with information about the drive. To use it: simply give the command, wait for DRQ and read the 256 words from the drive.
                    //      0xEF - Set features.
                    //      0xF2 and > F3 - The same as > E2 and > E3, only the unit in the sector count register is interpreted as 0.1 sec units.
                    //      Codes > 80-8F, > 9A, > C0-C3, and > F5-FF are manufacturer dependent commands. All other codes are reserved for future extension.
                }
            }

            busyHasBeenSet = true;

            break;
    }
}

uint8_t SDCARDRegisterRead(uint16_t m)
{
    uint8_t b = 0xff;

    //  only least 3 bits are relavent - address starts on 8 byte boundary for 8 bytes
    //  so if the base address is $E008 (like the PT69-5 Winchester) or $E050 (like the SWTPC SS-30 card)
    //  the register offsets will be the same

    switch (m & 0x07)
    {
        case 0:     // data register
            b = sectorBuffer[sectorBufferIndex++];

            if (sectorBufferIndex > (sectorCountRegister * 256) - 1)
            {
                statusRegister &= 0x7F;         // clear BUSY bit = set not busy
                statusRegister &= 0xF7;         // clear DRQ bit = set no data request
                statusRegister |= 0x40;         // turn on DRDY bit while not reading  or writing (able to accept a command)
                statusRegister &= 0xDE;         // turn off DWF
                statusRegister &= 0xFE;         // turn off ERROR 

                sectorBufferIndex = 0;          // wrap if > than sector size of 256

                sectorCountRegister = 0;        // this gest set back to 0 whenever a command completes
                free(sectorBuffer);
                sectorBuffer = NULL;
            }
            break;

        case 1:     // error register on read and write precomp register on write
            b = errorRegister;
            break;

        case 2:     // sector count
            b = sectorCountRegister;
            break;

        case 3:     // sector number        $E00B on PT69-5, $E050 on SWTPC SS-30 SD Card
            b = sectorNumberRegister;
            break;

        case 4:     // cylinder number (LSB)
            b = cylinderLowRegister;
            break;

        case 5:     // cylinder number (MSB)
            b = cylinderHiRegister;
            break;

        case 6:     // size/drive/head register
            b = sizeDriveHeadRegister;
            break;

        case 7:     // status register on read / command register on write
            b = statusRegister;

            if (busyHasBeenSet)
            {
                // if we just set it as a result of setting a command in the command register - then clear it
                // so the next time we check the status we can Clear the BUSY bit

                busyHasBeenSet = false;
            }
            else
            {
                statusRegister &= 0x7F;         // clear BUSY bit = set not busy
            }
            break;
    }

    return b;
}

void initialize_sdcard(int nWhichController, uint8_t *sMemoryBase, uint16_t sBaseAddress, int nRow, bool bInterruptEnabled)
{

}
