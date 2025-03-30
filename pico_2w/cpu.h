// everybosy gets a look a look at these

#define MEMORY_SIZE 0x10000  // 64KB

// these are states to use for loading the ROM image into memory. It is in STX format
enum states
{
    gettingSTX = 0,
    gettingAddressHi,
    gettingAddressLo,
    gettingSize,
    gettingData
};

typedef struct 
{
    uint8_t  A, B;                  // Accumulators
    uint16_t X;                     // Index Register
    uint16_t PC;                    // Program Counter
    uint16_t SP;                    // Stack Pointer
    uint8_t  CCR;                   // Condition Code Register
    uint8_t  memory[MEMORY_SIZE];   // Emulated RAM
} CPU6800;

typedef struct
{
    uint8_t     mneunonic[5];   // the actual mnemonic text
    uint8_t     OpCode;         // the actual op code
    uint16_t    attribute;      // Addressing Mode
    uint8_t     numbytes;       // how many bytes to the instruction
    uint8_t     cycles;         // how many cycles does it take to execute
    uint8_t     ccr_rules[6];   // which CCR rules apply
} opcodeTableEntry;

typedef struct BREAKPOINT
{
    uint16_t address;
    char *description;
    bool printLine;
} BREAKPOINT;

#ifdef CPU

    #define AM_ILLEGAL          0x0080  // Invalid Op Code
    #define AM_DIRECT_6800      0x0040  // direct addressing
    #define AM_RELATIVE_6800    0x0020  // relative addressing
    #define AM_EXTENDED_6800    0x0010  // extended addressing allowed
    #define AM_IMM16_6800       0x0008  // 16 bit immediate
    #define AM_IMM8_6800        0x0004  // 8 bit immediate
    #define AM_INDEXED_6800     0x0002  // indexed addressing allowed
    #define AM_INHERENT_6800    0x0001  // inherent addressing

    #define CCR_HALFCARRY   0x20
    #define CCR_INTERRUPT   0x10
    #define CCR_NEGATIVE    0x08
    #define CCR_ZERO        0x04
    #define CCR_OVERFLOW    0x02
    #define CCR_CARRY       0x01

    #define CONSOLE uart1
    #define AUXPORT uart0

#else

    extern void load_rom(const uint8_t [], uint16_t);
    extern void execute_next_instruction();

    extern CPU6800  cpu;
    extern uint8_t  _opCode;
    extern uint8_t  _cycles;
    extern uint8_t  _numBytes;
    extern uint16_t _attribute;

    extern uint16_t _operand;

    extern uint16_t _cf;
    extern uint16_t _vf;
    extern uint16_t _hf;

    extern uint8_t inWait;

    extern BREAKPOINT *breakpoints;
#endif

