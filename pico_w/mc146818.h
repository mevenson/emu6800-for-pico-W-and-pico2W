#ifndef MC146818
    extern bool got_time;
    extern bool sendCycles;
    extern struct tm *timeinfo;

    extern uint8_t cyclesPacketData[5];
    extern uint8_t cyclesResponseBuffer[1];

    extern int year;    //: years since 1900.
    extern int mon;     //: months since January (0-11).
    extern int mday;    //: day of the month (1-31).
    extern int hour;    //: hours since midnight (0-23).
    extern int min;     //: minutes after the hour (0-59).
    extern int sec;     //: seconds after the minute (0-60).

    extern long cyclesExecuted;
    extern struct repeating_timer timer;

    extern void start_timer(void);
    extern void stop_timer(void);

    extern uint8_t readMC146818(uint16_t m);
    extern void writeMC146818(uint16_t m, uint8_t c);
#else
    bool got_time;
    bool sendCycles;
    struct tm *timeinfo;

    uint8_t cyclesPacketData[5];
    uint8_t cyclesResponseBuffer[1];

    int year;    //: years since 1900.
    int mon;     //: months since January (0-11).
    int mday;    //: day of the month (1-31).
    int hour;    //: hours since midnight (0-23).
    int min;     //: minutes after the hour (0-59).
    int sec;     //: seconds after the minute (0-60).

    long cyclesExecuted;
    struct repeating_timer timer;

    bool dayLightSavingEnable = false;
    bool hourMode24 = false;
    bool dataModeBinary = false;

    int m_nRow;
    uint8_t regsec;   // equ   $00        seconds
    uint8_t ragsec;   // equ   $01        seconds alarm
    uint8_t regmin;   // equ   $02        minutes
    uint8_t ragmin;   // equ   $03        minutes alarm
    uint8_t reghou;   // equ   $04        hours + pm
    uint8_t raghou;   // equ   $05        hours alarm + pm
    uint8_t regdow;   // equ   $06        day of week
    uint8_t regday;   // equ   $07        day of month
    uint8_t regmon;   // equ   $08        month
    uint8_t regyea;   // equ   $09        year

    uint8_t regc0a;   // equ   $0a register a
    uint8_t regc0b;   // equ   $0a register a
    uint8_t regc0c;   // equ   $0a register a
    uint8_t regc0d;   // equ   $0a register a

    uint8_t memory[50];   // the chip has 50 general purpose bytes of memory

    uint8_t currentlySelectedRegister;

    datetime_t started;
    datetime_t now;
    datetime_t then;

    //Timer timerMC146818;
    int rateMC146818;
    long timesCalled = 0;
#endif

#define NTP_SERVER "pool.ntp.org"
#define NTP_PORT 123

#define TIMER_FREQ      1000000     // 1000000 uSec = 1 second
#define TIMEZONE_OFFSET_HOURS -5    // Example: EST (UTC-5)
