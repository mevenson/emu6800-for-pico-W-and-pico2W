#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/util/datetime.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"

#include "pico/stdlib.h"        // needed for struct repeating_timer definition
#include "hardware/clocks.h"
#include "hardware/timer.h"

#define MC146818
#include "emu6800.h"
#include "mc146818.h"
#include "tcp.h"

// RegisterBBits - weighted bit positions in register B
#define DSE          1      // Day Ligt Savings enabled (1 = enabled)
#define HR_24_12     2      // 12 or 24 hour mode (0 = 12 hour, 1 = 24 hour)
#define DM           4      // data mode (1 = binary, 0 = BCD)
#define SQWE         8      // Square Wave Enable
#define UIE         16      // Update Ended Interrupt Enabled
#define AIE         32      // Alarm Interrupt Enable
#define PIE         64      // Periodic Interrupt Enable
#define SET        128      // 0 = normal update cycle, 1 = abort update sycle

//  RegisterCBits
#define IRQF       128
#define PF          64
#define AF          32
#define UF          16

#define Sunday      = 0
#define Monday      = 1
#define Tuesday     = 2
#define Wednesday   = 3
#define Thursday    = 4
#define Friday      = 5
#define Saturday    = 6

//  
//     The memory consists of 50 general purpose RAM bytes, 10 RAM bytes which normally contain the time, calendar, 
//     and alarm data, and four control and status bytes, All 64 bytes are directly readable and writable by the 
//     processor program  except for the following: 1) Registers C and D are read only, 2) bit 7 of Register A is 
//     read only, and 3) the high-order bit of the seconds byte is read only. The contents of four control and 
//     status registers (A, B, C, and D) are described in REGISTERS.
//
//     *clock board location
//     clock    equ   $E05C      6809 port #5 
//     *
//     *clock register assignments
//     regsec   equ   $00        seconds 
//     ragsec   equ   $01        seconds alarm 
//     regmin   equ   $02        minutes 
//     ragmin   equ   $03        minutes alarm 
//     reghou   equ   $04        hours + pm 
//     raghou   equ   $05        hours alarm + pm 
//     regdow   equ   $06        day of week       ( Sunday = 1, Saturday = 7)
//     regday   equ   $07        day of month 
//     regmon   equ   $08        month 
//     regyea   equ   $09        year 
//
//     regc0a   equ   $0a        register a 
//
//         MSB                         LSB Read/ Write
//         b7  b6  b5  b4  b3  b2  bl  bO  Register
//         UIP DV2 DV1 DVO RS3 RS2 RS1 RSO except UIP
//         0   1   0   1   0   0   1   1
//
//         UIP – The update in progress (UIP) bit is a status flag that
//         may be monitored by the program. When UIP is a “l”, the
//         update cycle is in progress or will soon begin. When UIP is a
//         “U’, the update cycle is not in progress and will not be for at
//         least 244 ps (for all time bases). This is detailed in Table 6.
//         The time, calendar, and alarm information in RAM is fully
//         available to the program when the UIP bit is zero – it is not
//         in transition. The UIP bit is a read-only bit, and is not 
//         affected by Reset. Writing the SET bit in Register B to a “l”
//         inhibits any update cycle and then clears the UIP status bit.
//
//         DV2, DVI, DVO – Three bits are used to permit the program to 
//         select various conditions of the 22-stage divider
//         chain. The divider selection bits identify which of the three
//         time-base frequencies is in use. Table 4 shows that the time
//         bases of 4.194304 MHz, 1.046576 MHz, and 32.768 kHz may
//         be used. The divider selection bits are also used to resEt the
//         divider chain. When the time/calendar is first initialized, the
//         program may start the divider at the precise time stored in
//         the RAM, When the divider reset is removed, the first update
//         cycle begins one-half second later. These three read/write
//         bits are not affected by !RESET.
//
//         RS3, RS2, RS1, RSO – The four rate selection bits select
//         one of 15 taps on the 22 stage divider, or disable the divider
//         output. The tap selected may be used to generate an output
//         square wave (SQW pin) and/or a periodic interrupt. The program 
//         may do one of the following: 1) enable the interrupt
//         with. the PIE bit, 2) enable the SQW output pin with the
//         SQWE bit, 3) enable both at the same time at the same rate,
//         or 4) enable neither. Table 5 lists the periodic interrupt rates
//         and the square wave frequencies that may be chosen with
//         the RS bits. These four bits are read/write bits which are not
//         affectected by !RESET.
//
//         regc0b   equ   $0b        register b 
//
//         MSB                             LSB Read/ Write
//         b7  b6  b5  b4  b3   b2  bl     bO  Register
//         SET PIE AIE UIE SQWE DM  24/12  DSE
//
//         SET – When the SET bit is a “O’, the update cycle functions 
//         normally by advancing the counts once-per-second. When the 
//         SET bit is written to a “1”, any update cycle in progress 
//         is aborted and the program may initialize the time and 
//         calendar bytes without an update occurring in the midst of 
//         initializing. SET is a read/write bit which is not modified
//         by RESET or internal functions of the MC146818A.
//
//         PIE – The periodic interrupt enable (PIE) bit is a
//         read/write bit which allows the periodic-interrupt flag (PF)
//         bit in Register C to cause the l~pin to be driven low. 
//         A program writes a “1” to the PIE bit in order to receive periodic
//         interrupts at the rate specified by the RS3, RS2, RSI, and
//         RSO bits in Register A, A zero in PIE blocks l~Q from being
//         initiated by a periodic interrupt, but the periodic flag (P~) bit
//         is still set at the periodic rate. PIE is not nodified by any 
//         internal MC146818 functions, but is cleared to "0" by a !RESET.
//
//         AIE – The alarm interrupt enable (Al/E) bit is a read/write
//         bit which when set to a “1” permits the alarm flag (AF) bit in
//         Register C to assert IRQ. An alarm interrupt occurs for each
//         second that the three time bytes the three alarm bytes
//         (including a “don’t care” alarm code by binary 1IXXXXX).
//         When the AIE bit is a “0", the AF bit does not initiate an !IRQ
//         signal. The RESET pin clears AIE to "0". The internal functions 
//         do not affect the AIE bit.
//
//         UIE – The UIE (update-ended interrupt enable) bit is a
//         read/write bit which enables the update-end flag (UF) bit in
//         Register C to assert !IRQ.The RESET pin going low or the
//         SET bit going high clears the UIE bit. 
//
//         SQW - When the square-wave enable (SQWE) bit is set to a “l" 
//         by the the program, a square-wave signal at the frequency spefified 
//         in the rate selection bits (RS3 to RSO) appears on the SQW pin. 
//         When the SQWE bit is set to a zero the SQW pin is held low. The 
//         state of SQWE is cleared by the !RESET pin. SQWE is a read/write bit.
//
//         DM – The data mode (DM ) bit indicates whether time
//         and calendar updates are to use binary or BCD formats. The
//         DM bit is written by the processor program and maybe read
//         by the program, but is not modified by any internal functions
//         or RESET. A “l” in DM signifies binary data, while a “0" in
//         DM specifies binary-coded-decimal (BCD) data.
//
//         24/12 – The 24/12 control bit establishes the format of
//         the hours bytes as either the 24hour mode (a “l”) or the
//         12-hour mode (a “0"), This is a read/write bit, which is 
//         affected on Iy by software.
//
//         DSE – The daylight savings enable (DSE) bit is a
//         read/write bit which allows the program to enable two
//         special updates (when DSE is a “1”). On the last Sunday in
//         April the time increments from 1:59:59 AM to 3:00:00 AM.
//         On the last Sunday in October when the time first reaches
//         1:59:59 AM it changes to 1:00:00 AM. These special updates
//         do not occur when the DSE bit is a "0". DSE is not changed
//         by any internal operations or reset.
//
//         regc0c   equ   $0c        register c    Read-Only Register
//
//         MSB                         LSB
//         b7   b6  b5  b4  b3  b2  bl  bO 
//         IRQF PF  AF  UF  0   0   0   0
//
//         IRQF – The interrupt request flag (IRQF) is set to a “l”
//         when one or more of the following are true:
//             PF = PIE = ”1”
//             AF = AIE = ”1”
//             UF = UIE = ”1”
//         i.e., IRQF = (PF & PIE) | (AF & AIE) | (UF & UIE)
//         Any time the IRQF bit is a “l”, the !IRQ pin is driven low.
//         All flag bits are cleared after Register C is read by the 
//         program or when the RESET pin is low
//
//         PF – The periodic interrupt flag (PF) is a read-only bit
//         which is set to a “l” when a particular edge is detected on
//         the selected tap of the divider chain. The RS3 to RSO bits
//         establish the periodic rate. PF is set to a “l” independent of
//         the state of the PIE bit. PF being a “l” initiates an !IRQ signal
//         and sets the IRQF bit when PIE is also a “l”. The PF bit is
//         cleared by a !RESET or a software read of Register C.
//
//         AF – A “l” in the AF (alarm interrupt flag) bit indicates
//         that the current time has matched the alarm time. A “l” in
//         the AF causes the ~ pin to go low, and a “l” to appear in
//         the IRQF bit, when the AIE bit also is a “1 .“ A !RESET or a
//         read of Register C clears AF.
//
//         UF – The update-ended interrupt flag (UF) bit is set after
//         each update cycle. when the UIE bit is a “l”, the “l” in UF
//         causes the IRQF bit to be a “l”, asserting !IRQ. UF is cleared
//         by a Register C read or a !RESET.
//
//         b3 TO bO – The unused bits of Status Register 1 are read
//         as “O’s”. They can not be written.
//
//         regc0d   equ   $0d        register d    Read Only Register
//
//         MSB                         LSB
//         b7  b6  b5  b4  b3  b2  bl  bO 
//         VRT 0   0   0   0   0   0   0
//
//         VRT – The valid RAM and time (VRT) bit indicates the
//         condition of the contents of the RAM, provided the power
//         sense (PS) pin is satisfactorily connected. A “O” appears in
//         the VRT bit when the power-sense pin is low. The processor
//         program can set the VRT bit when the time and calendar are
//         initialized to indicate that the RAM and time are valid. The
//         VRT is a read only bit which is not modified by the !RESET
//         pin. The VRT bit can only be set by reading Register D,
//
//         b6 TO bO – The remaining bits of Register D are unused.
//         They cannot be written, but are always read as “O's”.
//
//     clocka   equ   clock+1    address 
//     clockd   equ   clock+2    data          

// Zeller's Congruence - Function to compute the day of the week (0=Saturday, 1=Sunday, ..., 6=Friday)
int day_of_week(int day, int month, int year) 
{
    if (month < 3)
    {
        month += 12;
        year -= 1;
    }
    
    int K = year % 100;  // Year within century
    int J = year / 100;  // Century

    int h = (day + (13 * (month + 1)) / 5 + K + (K / 4) + (J / 4) + (5 * J)) % 7;

    return h;
}

// Convert an unsigned byte into BCD
uint8_t byte_to_bcd(uint8_t value) 
{
    return ((value / 10) << 4) | (value % 10);
}

void ntp_receive_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) 
{
    if (p->len == 48) 
    {
        unsigned char *ptr = (unsigned char*)p->payload;
        uint32_t t = (ptr[43] | (ptr[42] << 8) | (ptr[41] << 16) | (ptr[40] << 24)) - 2208988800UL;
        t += TIMEZONE_OFFSET_HOURS * 3600; // Apply timezone offset in seconds
        time_t time_received = (time_t)t;
        printf("Local Time: %s", ctime(&time_received));

        timeinfo = localtime(&time_received);   // Convert to local time structure
        year = timeinfo->tm_year - 100;         // number of years since 1900 (subtract 100 since we just want since 2000)
        mon  = timeinfo->tm_mon  + 1;           // month is 0 based (0 = Jan, etc)
        mday = timeinfo->tm_mday ;              // day is already 1 based
        hour = timeinfo->tm_hour ;
        min  = timeinfo->tm_min  ;  
        sec  = timeinfo->tm_sec  ; 

        regsec = byte_to_bcd(sec);
        ragsec = byte_to_bcd(sec);
        regmin = byte_to_bcd(min);
        ragmin = byte_to_bcd(min);
        reghou = byte_to_bcd(hour);
        raghou = byte_to_bcd(hour);
        regday = byte_to_bcd(mday);
        regmon = byte_to_bcd(mon);
        regyea = byte_to_bcd(year);
    
        regdow = day_of_week(mday, mon, year) ;

        got_time = true;
    }
    pbuf_free(p);
}

void ntp_request() {
    struct udp_pcb *pcb;
    struct pbuf *p;
    ip4_addr_t server_addr;
    pcb = udp_new();
    IP4_ADDR(&server_addr, 129, 6, 15, 28);     // pool.ntp.org (one of its IPs)
    udp_connect(pcb, &server_addr, NTP_PORT);

    char ntp_packet[48] = {0};
    ntp_packet[0] = 0x1B; // NTP request header

    p = pbuf_alloc(PBUF_TRANSPORT, sizeof(ntp_packet), PBUF_RAM);
    memcpy(p->payload, ntp_packet, sizeof(ntp_packet));

    udp_send(pcb, p);
    pbuf_free(p);

    printf("Getting GMT time from pool.ntp.org\n");
    udp_recv(pcb, ntp_receive_callback, NULL);
}

void stop_timer(void)
{
    cancel_repeating_timer(&timer);
}

// Function to print the day of the week as a string
const char* get_day_name(int dow) 
{
    const char* days[] = {"Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
    return days[dow];
}

// Convert BCD byte into an unsigned integer 
uint16_t BCDToInt(uint8_t bcd)
{
    uint16_t outInt = 0;

    int mul = (int)pow(10, (0 * 2));
    outInt += (uint)(((bcd & 0xF)) * mul);
    mul = (int)pow(10, (0 * 2) + 1);
    outInt += (uint)(((bcd >> 4)) * mul);

    return outInt;
}

// Convert an unsigned integer into BCD
uint8_t bcd[2];
uint8_t *IntToBCD5(uint16_t numericvalue)
{
    int bytesize = 1;

    for (int byteNo = 0; byteNo < bytesize; ++byteNo)
        bcd[byteNo] = 0;

    for (int digit = 0; digit < bytesize * 2; ++digit)
    {
        uint hexpart = numericvalue % 10;
        bcd[digit / 2] |= (uint8_t)(hexpart << ((digit % 2) * 4));
        numericvalue /= 10;
    }

    return(bcd);
}

//  Thirty days has September,
//  April, June, and November,
//  All the rest have thirty-one,
//  Save February at twenty-eight,
//  But leap year, coming once in four,
//  February then has one day more.

int ticks = 0;
int reportingInterval = 5;      // number of seconds between sending cycles per second to server

//                   jan feb mar apr may jun jul aug sep oct nov dec                
int daysInMonth[] = {31, 28, 31, 30, 30, 30, 31, 31, 30, 31, 30, 31};
bool timer_callback(struct repeating_timer *t)
{
    // the timer will interrupt every 1 second.
    int daysThisMonth = daysInMonth[mon - 1];

    sec  += 1;
    if (sec == 60)
    {
        sec = 0;
        min += 1;
        if (min == 60)
        {
            min = 0;
            hour += 1;
            if (hour == 24)
            {
                hour = 0;
                mday  += 1;
                if (mday > daysThisMonth)
                {
                    mday = 1;
                    mon  += 1;
                    if (mon > 12)
                    {
                        mon = 1;
                        year += 1;
                    }
                }
                regdow = day_of_week(mday, mon, year) ;
            }
        }
    }

    // now store in the registers as BCD

    regsec = byte_to_bcd(sec);
    ragsec = byte_to_bcd(sec);
    regmin = byte_to_bcd(min);
    ragmin = byte_to_bcd(min);
    reghou = byte_to_bcd(hour);
    raghou = byte_to_bcd(hour);
    regday = byte_to_bcd(mday);
    regmon = byte_to_bcd(mon);
    regyea = byte_to_bcd(year);

    regc0c |= (uint8_t)PF;     // set the PF bit to let the chip know that a periodic time interrupt has occured

    // every reportingInterval seconds - show cycles executed by processor in the last 10 seconds
    if (++ticks % reportingInterval == 0)
    {
        ticks = 0;

        cyclesExecuted = cyclesExecuted / reportingInterval;

        // since we only do this every 10 seconds - adjust for average over the last 10 seconds.

        cyclesPacketData[0] = 0xFF;
        cyclesPacketData[1] = (cyclesExecuted >> 24) & 0xFF;
        cyclesPacketData[2] = (cyclesExecuted >> 16) & 0xFF;
        cyclesPacketData[3] = (cyclesExecuted >>  8) & 0xFF;
        cyclesPacketData[4] = (cyclesExecuted      ) & 0xFF;
    
        cyclesExecuted = 0;     // reset the number of cycles executed in the last second
        sendCycles = true;      // tell main loop that it is time to send to server - we cannot do from within an ISR
    }
}

void start_timer(void)
{
    add_repeating_timer_us(TIMER_FREQ, timer_callback, NULL, &timer);
}

// typedef struct DateTime
// {
//     int year;
//     int month;
//     int day; 
//     int hour; 
//     int minute; 
//     int second; 
//     int millisecond;
//     int DayOfWeek;
// } DateTime;

void UpdateDateTimeRegisters(datetime_t _now)
{
    // A “l” in DM signifies binary data, while a “0" in DM specifies binary-coded-decimal (BCD) data.

    regdow = (uint8_t)_now.dotw;

    if (dataModeBinary)
    {
        // the data needs to be Binary - it already is in _now

        regsec = (uint8_t)_now.sec;
        ragsec = 0;
        regmin = (uint8_t)_now.min;
        ragmin = 0;
        reghou = (uint8_t)_now.hour;
        raghou = 0;
        regday = (uint8_t)_now.day;
        regmon = (uint8_t)_now.month;
        regyea = (uint8_t)(_now.year - 2000);
    }
    else        //if ((regc0b & 0x02) == 0x02)
    {
        // the data needs to be BCD

        regsec = IntToBCD5((uint)_now.sec)[0];
        ragsec = 0;
        regmin = IntToBCD5((uint)_now.min)[0];
        ragmin = 0;
        reghou = IntToBCD5((uint)_now.hour)[0];
        raghou = 0;
        regday = IntToBCD5((uint)_now.day)[0];
        regmon = IntToBCD5((uint)_now.month)[0];
        regyea = IntToBCD5((uint)(_now.year - 2000))[0];
    }
}

uint8_t readMC146818(uint16_t m)
{
    uint8_t b = 0xff;

    switch (m & 0x03)       // only 4 port addresses on 6818
    {
        case 0:     // E01C & E01E
        case 2:             // read from currently selected register (memory location ion the chip);
            {
                switch (currentlySelectedRegister)
                {
                    case 0: b = regsec; break;
                    case 1: b = ragsec; break;
                    case 2: b = regmin; break;
                    case 3: b = ragmin; break;
                    case 4: b = reghou; break;
                    case 5: b = raghou; break;
                    case 6: b = regdow; break;
                    case 7: b = regday; break;
                    case 8: b = regmon; break;
                    case 9: b = regyea; break;
                    case 10: b = regc0a; break;
                    case 11: b = regc0b; break;
                    case 12: b = regc0c; break;
                    case 13: b = regc0d; break;      // check VRT
                    default:
                        if (currentlySelectedRegister < 64)
                            b = memory[currentlySelectedRegister - 14];
                        break;
                }

                //                MSB                         LSB
                //                b7   b6  b5  b4  b3  b2  bl  bO 
                //                IRQF PF  AF  UF  O   0   0   0

                // IRQF – The interrupt request flag (IRQF) is set to a “l”
                //  when one or more of the following are true:
                //      PF = PIE = ”1”
                //      AF = AIE = ”1”
                //      UF = UIE = ”1”
                //  i.e., IRQF = (PF & PIE) | (AF & AIE) | (UF & UIE)
                //  Any time the IRQF bit is a “l”, the !IRQ pin is driven low.
                //  All flag bits are cleared after Register C is read by the 
                //  program or when the RESET pin is low

                regc0c = regc0c & 0x0F;     // clear the INTRQ, PF, AF and UF bits
            }
            break;

        case 1:     // E01D & E01F
        case 3:     // does no goof to read this register - it is write only used to select the register to access for read and write.
            break;

        default:
            break;
    }

    return b;
}

void writeMC146818(uint16_t m, uint8_t c)
{
    bool updateRequired = false;

    switch (m & 0x03)       // only 4 port addresses on 6818
    {
        case 0:
        case 2:
            switch (currentlySelectedRegister)
            {
                case 0: regsec = c; updateRequired = true; break;
                case 1: ragsec = c; break;
                case 2: regmin = c; updateRequired = true; break;
                case 3: ragmin = c; break;
                case 4: reghou = c; updateRequired = true; break;
                case 5: raghou = c; break;
                case 6: regdow = c; break;
                case 7: regday = c; updateRequired = true; break;
                case 8: regmon = c; updateRequired = true; break;
                case 9: regyea = c; updateRequired = true; break;

                case 10:
                    //
                    // regc0a   equ   $0a        register a 
                    //
                    //     MSB                         LSB Read/ Write
                    //     b7  b6  b5  b4  b3  b2  bl  bO  Register
                    //     UIP DV2 DV1 DVO RS3 RS2 RS1 RSO except UIP
                    //     0   1   0   1   0   0   1   1
                    //
                    //  Select Bits             4.194304 or 1.048576 MHz                            32.768 kHz 
                    //  Register A              Time Base                                           Time Base
                    //                          Periodic Interrupt Rate    SQW Output Frequency     Periodic Interrupt Rate     SQW Output  Frequency
                    //                          tpl                                                 tpl               
                    //  RS3 RS2 RS1 RSO
                    //  0   0   0   0           None                        None                    None                        None
                    //  0   0   0   1           30.517   us                 32.768 kHz              3.90625  ms                 256Hz
                    //  0   0   1   0           61.035   us                 16.384 kHz              7.8125   ms                 128Hz
                    //  0   0   1   1           122.070  us                 8.192 kHz               122.070  us                 8.192 kHz
                    //  0   1   0   0           244.141  us                 4.096 kHz               244.141  us                 4.096 kHz
                    //  0   1   0   1           488.281  us                 2.048 kHz               488.281  us                 2.048 kHz
                    //  0   1   1   0           976.562  us                 1.024 kHz               976.562  us                 1.024 kHz
                    //  0   1   1   1           1.953125 ms                 512Hz                   1.953125 ms                 512Hz
                    //  1   0   0   0           3.90625  ms                 256Hz                   3.90625  ms                 256Hz
                    //  1   0   0   1           7.8125   ms                 128Hz                   7.8125   ms                 128Hz
                    //  1   0   1   0           15.625   ms                 64Hz                    15.625   ms                 64Hz
                    //  1   0   1   1           31.25    ms                 32Hz                    31.25    ms                 32Hz
                    //  1   1   0   0           62.5     ms                 16Hz                    62.5     ms                 16Hz        <------ this is the one that is set for OS9
                    //  1   1   0   1           125      ms                 8Hz                     125      ms                 8Hz
                    //  1   1   1   0           250      ms                 4Hz                     250      ms                 4Hz
                    //  1   1   1   1           500      ms                 2Hz                     500      ms                 2Hz

                    regc0a = c; 
                    // switch (c & 0x0F)
                    // {
                    //     case 8:
                    //         rateMC146818 = 4;                    // 4 milliseconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 9:
                    //         rateMC146818 = 8;                    // 8 milliseconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 10:
                    //         rateMC146818 = 15;                    // 15 milliseconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 11:
                    //         rateMC146818 = 31;                    // 31 milli seconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 12:
                    //         rateMC146818 = 62;                    // 62 milli seconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 13:
                    //         rateMC146818 = 125;                    // 125 milli seconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 14:
                    //         rateMC146818 = 250;                    // 250 milli seconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     case 15:
                    //         rateMC146818 = 500;                    // 500 milli seconds
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    //     default:
                    //         rateMC146818 = 1000;                    // one second if not able to comply
                    //         timerMC146818.Change(0, rateMC146818);
                    //         break;
                    // }

                    break;
                
                case 11:
                    regc0b = c;
                    bool olddataModeBinary = dataModeBinary;

                    if ((c & (uint8_t)DM) == 0) dataModeBinary = false; else dataModeBinary = true;
                    if (olddataModeBinary != dataModeBinary)
                    {
                        datetime_t newNow; 
                        newNow.day   = mday;
                        newNow.hour  = hour;
                        newNow.min   = min;
                        newNow.month = mon;
                        newNow.sec   = sec;
                        newNow.year  = year;
                        UpdateDateTimeRegisters(newNow);

                        now = newNow;
                        then = newNow;
                    }

                    if ((c & (uint8_t)HR_24_12) == 0) hourMode24 = false; else hourMode24 = true;
                    if ((c & (uint8_t)DSE) == 0) dayLightSavingEnable = false; else dayLightSavingEnable = true;
                    break;
                case 12: regc0c = c; break;
                case 13: regc0d = c; break;
                default:
                    if (currentlySelectedRegister < 64)
                        memory[currentlySelectedRegister - 14] = c;
                    break;
            }
            break;

        case 1:             // select the register (memory location in the chip) to interact with
        case 3:
            currentlySelectedRegister = c;
            break;
        default:
            break;
    }

    // when ever the values are changed in the year, month, day, hour minute or seconds registers,
    // we need to upda te the global now value since it is what is used to update the registers
    // when the time goes off every second.

    if (updateRequired)
    {
        uint16_t year, month, day, hour, minute, second;

        // make sure we get the year, month, day, hour minute or seconds register values as binary

        if (dataModeBinary)             // the data in the registers is already binary
        {
            year    = regyea;
            month   = regmon;
            day     = regday;
            hour    = reghou;
            minute  = regmin;
            second  = regsec;
        }
        else                            //if ((regc0b & 0x02) == 0x02)        // the data in the registers is BCD
        {
            year    = BCDToInt(regyea);
            month   = BCDToInt(regmon);
            day     = BCDToInt(regday);
            hour    = BCDToInt(reghou);
            minute  = BCDToInt(regmin);
            second  = BCDToInt(regsec);
        }

        now.year  = then.year  = year + 2000;
        now.month = then.month = month;
        now.day   = then.day   = day;
        now.hour  = then.hour  = hour;
        now.min   = then.min   = minute;
        now.sec   = then.sec   = second;
    }
}

bool init_MC146818() 
{
    got_time = false;
    sendCycles = false;
    cyclesExecuted = 0;
    currentlySelectedRegister = 0;

    ntp_request();
    while (!got_time)       // wait for the ntp server to return the current time
    {
        cyw43_arch_poll();
        sleep_ms(500);
    }

    now = started;
    then = started;

    /* 
        When RESET is low the following occurs:

            // Register $0B     SET PIE AIE UIE SQWE DM  24/12  DSE

        a) Periodic Interrupt Enable (PIE)      bit is cleared to zero (register B bit 6)
        b) Alarm Interrupt Enable (AIE)         bit is cleared to zero (register B bit 5)
        c) Update ended Interrupt Enable (UIE)  bit is cleared to zero (register B bit 4)

            // Register $0C     IRQF PF  AF  UF  O   0   0   0

        d) Update ended Interrupt Flag (UF)     bit is cleared to zero (register C bit 4)
        e) Interrupt Request status Flag (IRQF) bit is cleared to zero (register C bit 7)
        f) Periodic Interrupt Flag (PF)         bit is cleared to zero (register C bit 6)
        g) The part is not accessible.
        h) Alarm Interrupt Flag (AF)            bit is cleared to zero (register C bit 5)

        i) IRQ pin is in high-impedance state, 
        j) Square Wave output Enable (SQWE)     bit is cleared to zero (register B bit 3)

     */

    // start out with Daylight Saving Enabled, 24 hour format and datMode = BCD

    regc0b = (uint8_t)HR_24_12 | (uint8_t)DSE;        // leave RegisterBBits.DM = 0

    if ((regc0b & (uint8_t)DM)       == 0) dataModeBinary       = false; else dataModeBinary       = true;
    if ((regc0b & (uint8_t)HR_24_12) == 0) hourMode24           = false; else hourMode24           = true;
    if ((regc0b & (uint8_t)DSE)      == 0) dayLightSavingEnable = false; else dayLightSavingEnable = true;

    regc0c = 0x00;
    regc0d = 0x80;          // turn on VRT (RAM is valid)

    start_timer();

    return true;
}

