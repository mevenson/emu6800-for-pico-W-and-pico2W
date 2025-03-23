#define CPU

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hardware/regs/uart.h"
#include "hardware/uart.h"

#include "emu6800.h"
#include "cpu.h"
#include "tcp.h"
#include "fd2.h"
#include "sdcard.h"
#include "mc146818.h"

extern uint8_t newbug[3150];            // this is for SWTPC without SD Card
extern uint8_t swtbug_justtherom[1168]; // this one is for CP68 without SD Card
extern uint8_t swtbuga_v1_303[4131];    // this if for the PT68-1

CPU6800  cpu;
uint8_t  _opCode;
uint8_t  _cycles;
uint8_t  _numBytes;
uint16_t _attribute;

uint16_t _operand;

uint16_t _cf = 0;
uint16_t _vf = 0;
uint16_t _hf = 0;

uint8_t inWait = 0;

//  Define 6800 CPU State
//  We'll create a struct for the 6800 registers and memory:

const opcodeTableEntry opctbl [256] = 
{
//  mneunonic OpCode attribute           numbytes cycles ccr_rules
//  --------- ------ ------------------- -------- ------ ---------------------
    {"NOP  ", 0x00,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0,  0,  0},
    {"NOP  ", 0x01,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x02,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x03,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x04,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x05,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"TAP  ", 0x06,  AM_INHERENT_6800,   1,        2,    12, 12, 12, 12, 12, 12},
    {"TPA  ", 0x07,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0,  0,  0},
    {"INX  ", 0x08,  AM_INHERENT_6800,   1,        4,     0,  0,  0, 15,  0,  0},
    {"DEX  ", 0x09,  AM_INHERENT_6800,   1,        4,     0,  0,  0, 15,  0,  0},
    {"CLV  ", 0x0A,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0, 14,  0},
    {"SEV  ", 0x0B,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0, 13,  0},
    {"CLC  ", 0x0C,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0,  0, 14},
    {"SEC  ", 0x0D,  AM_INHERENT_6800,   1,        2,     0,  0,  0,  0,  0, 13},
    {"CLI  ", 0x0E,  AM_INHERENT_6800,   1,        2,     0, 14,  0,  0,  0,  0},
    {"SEI  ", 0x0F,  AM_INHERENT_6800,   1,        2,     0, 13,  0,  0,  0,  0},
    {"SBA  ", 0x10,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 15, 15},
    {"CBA  ", 0x11,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0x12,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x13,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x14,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x15,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"TAB  ", 0x16,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14,  0},
    {"TBA  ", 0x17,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14,  0},
    {"~~~~~", 0x18,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"DAA  ", 0x19,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 15,  3},
    {"~~~~~", 0x1A,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"ABA  ", 0x1B,  AM_INHERENT_6800,   1,        2,    15,  0, 15, 15, 15, 15},
    {"~~~~~", 0x1C,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x1D,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x1E,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x1F,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"BRA  ", 0x20,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x21,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"BHI  ", 0x22,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BLS  ", 0x23,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BCC  ", 0x24,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BCS  ", 0x25,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BNE  ", 0x26,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BEQ  ", 0x27,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BVC  ", 0x28,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BVS  ", 0x29,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BPL  ", 0x2A,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BMI  ", 0x2B,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BGE  ", 0x2C,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BLT  ", 0x2D,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BGT  ", 0x2E,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"BLE  ", 0x2F,  AM_RELATIVE_6800,   2,        4,     0,  0,  0,  0,  0,  0},
    {"TSX  ", 0x30,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"INS  ", 0x31,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"PUL A", 0x32,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"PUL B", 0x33,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"DES  ", 0x34,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"TXS  ", 0x35,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"PSH A", 0x36,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"PSH B", 0x37,  AM_INHERENT_6800,   1,        4,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x38,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"RTS  ", 0x39,  AM_INHERENT_6800,   1,        5,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x3A,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"RTI  ", 0x3B,  AM_INHERENT_6800,   1,       10,    10, 10, 10, 10, 10, 10},
    {"~~~~~", 0x3C,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x3D,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"WAI  ", 0x3E,  AM_INHERENT_6800,   1,        9,     0, 11,  0,  0,  0,  0},
    {"SWI  ", 0x3F,  AM_INHERENT_6800,   1,       12,     0, 13,  0,  0,  0,  0},
    {"NEG A", 0x40,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  1,  2},
    {"~~~~~", 0x41,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x42,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"COM A", 0x43,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14, 13},
    {"LSR A", 0x44,  AM_INHERENT_6800,   1,        2,     0,  0, 14, 15,  6, 15},
    {"~~~~~", 0x45,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"ROR A", 0x46,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ASR A", 0x47,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ASL A", 0x48,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ROL A", 0x49,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"DEC A", 0x4A,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  4,  0},
    {"~~~~~", 0x4B,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"INC A", 0x4C,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  5,  0},
    {"TST A", 0x4D,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14, 14},
    {"~~~~~", 0x4E,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"CLR A", 0x4F,  AM_INHERENT_6800,   1,        2,     0,  0, 14, 13, 14, 14},
    {"NEG B", 0x50,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  1,  2},
    {"~~~~~", 0x51,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x52,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"COM B", 0x53,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14, 13},
    {"LSR B", 0x54,  AM_INHERENT_6800,   1,        2,     0,  0, 14, 15,  6, 15},
    {"~~~~~", 0x55,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"ROR B", 0x56,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ASR B", 0x57,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ASL B", 0x58,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"ROL B", 0x59,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  6, 15},
    {"DEC B", 0x5A,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  4,  0},
    {"~~~~~", 0x5B,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"INC B", 0x5C,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15,  5,  0},
    {"TST B", 0x5D,  AM_INHERENT_6800,   1,        2,     0,  0, 15, 15, 14, 14},
    {"~~~~~", 0x5E,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"CLR B", 0x5F,  AM_INHERENT_6800,   1,        2,     0,  0, 14, 13, 14, 14},
    {"NEG  ", 0x60,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  1,  2},
    {"~~~~~", 0x61,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x62,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"COM  ", 0x63,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15, 14, 13},
    {"LSR  ", 0x64,  AM_INDEXED_6800,    2,        7,     0,  0, 14, 15,  6, 15},
    {"~~~~~", 0x65,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"ROR  ", 0x66,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  6, 15},
    {"ASR  ", 0x67,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  6, 15},
    {"ASL  ", 0x68,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  6, 15},
    {"ROL  ", 0x69,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  6, 15},
    {"DEC  ", 0x6A,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  4,  0},
    {"~~~~~", 0x6B,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"INC  ", 0x6C,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15,  5,  0},
    {"TST  ", 0x6D,  AM_INDEXED_6800,    2,        7,     0,  0, 15, 15, 14, 14},
    {"JMP  ", 0x6E,  AM_INDEXED_6800,    2,        4,     0,  0,  0,  0,  0,  0},
    {"CLR  ", 0x6F,  AM_INDEXED_6800,    2,        7,     0,  0, 14, 13, 14, 14},
    {"NEG  ", 0x70,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  1,  2},
    {"~~~~~", 0x71,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0x72,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"COM  ", 0x73,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15, 14, 13},
    {"LSR  ", 0x74,  AM_EXTENDED_6800,   3,        6,     0,  0, 14, 15,  6, 15},
    {"~~~~~", 0x75,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"ROR  ", 0x76,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  6, 15},
    {"ASR  ", 0x77,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  6, 15},
    {"ASL  ", 0x78,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  6, 15},
    {"ROL  ", 0x79,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  6, 15},
    {"DEC  ", 0x7A,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  4,  0},
    {"~~~~~", 0x7B,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"INC  ", 0x7C,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15,  5,  0},
    {"TST  ", 0x7D,  AM_EXTENDED_6800,   3,        6,     0,  0, 15, 15, 14, 14},
    {"JMP  ", 0x7E,  AM_EXTENDED_6800,   3,        3,     0,  0,  0,  0,  0,  0},
    {"CLR  ", 0x7F,  AM_EXTENDED_6800,   3,        6,     0,  0, 14, 13, 14, 14},
    {"SUB A", 0x80,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"CMP A", 0x81,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"SBC A", 0x82,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0x83,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND A", 0x84,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"BIT A", 0x85,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"LDA A", 0x86,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"~~~~~", 0x87,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"EOR A", 0x88,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0,},
    {"ADC A", 0x89,  AM_IMM8_6800,       2,        2,    15,  0, 15, 15, 15, 15,},
    {"ORA A", 0x8A,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0,},
    {"ADD A", 0x8B,  AM_IMM8_6800,       2,        2,    15,  0, 15, 15, 15, 15,},
    {"CPX  ", 0x8C,  AM_IMM16_6800,      3,        3,     0,  0,  7, 15,  8,  0,},
    {"BSR  ", 0x8D,  AM_RELATIVE_6800,   2,        6,     0,  0,  0,  0,  0,  0,},
    {"LDS  ", 0x8E,  AM_IMM16_6800,      3,        3,     0,  0,  9, 15, 14,  0},
    {"~~~~~", 0x8F,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"SUB A", 0x90,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"CMP A", 0x91,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"SBC A", 0x92,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0x93,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND A", 0x94,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"BIT A", 0x95,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"LDA A", 0x96,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"STA A", 0x97,  AM_DIRECT_6800,     2,        4,     0,  0, 15, 15, 14,  0},
    {"EOR A", 0x98,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"ADC A", 0x99,  AM_DIRECT_6800,     2,        3,    15,  0, 15, 15, 15, 15},
    {"ORA A", 0x9A,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"ADD A", 0x9B,  AM_DIRECT_6800,     2,        3,    15,  0, 15, 15, 15, 15},
    {"CPX  ", 0x9C,  AM_DIRECT_6800,     2,        4,     0,  0,  7, 15,  8,  0},
    {"~~~~~", 0x9D,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"LDS  ", 0x9E,  AM_DIRECT_6800,     2,        4,     0,  0,  9, 15, 14,  0},
    {"STS  ", 0x9F,  AM_DIRECT_6800,     2,        5,     0,  0,  9, 15, 14,  0},
    {"SUB A", 0xA0,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"CMP A", 0xA1,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"SBC A", 0xA2,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xA3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND A", 0xA4,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"BIT A", 0xA5,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"LDA A", 0xA6,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"STA A", 0xA7,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"EOR A", 0xA8,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"ADC A", 0xA9,  AM_INDEXED_6800,    2,        5,    15,  0, 15, 15, 15, 15},
    {"ORA A", 0xAA,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"ADD A", 0xAB,  AM_INDEXED_6800,    2,        5,    15,  0, 15, 15, 15, 15},
    {"CPX  ", 0xAC,  AM_INDEXED_6800,    2,        6,     0,  0,  7, 15,  8,  0},
    {"JSR  ", 0xAD,  AM_INDEXED_6800,    2,        8,     0,  0,  0,  0,  0,  0},
    {"LDS  ", 0xAE,  AM_INDEXED_6800,    2,        6,     0,  0,  9, 15, 14,  0},
    {"STS  ", 0xAF,  AM_INDEXED_6800,    2,        7,     0,  0,  9, 15, 14,  0},
    {"SUB A", 0xB0,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"CMP A", 0xB1,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"SBC A", 0xB2,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xB3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND A", 0xB4,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"BIT A", 0xB5,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"LDA A", 0xB6,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"STA A", 0xB7,  AM_EXTENDED_6800,   3,        5,     0,  0, 15, 15, 14,  0},
    {"EOR A", 0xB8,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"ADC A", 0xB9,  AM_EXTENDED_6800,   3,        4,    15,  0, 15, 15, 15, 15},
    {"ORA A", 0xBA,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"ADD A", 0xBB,  AM_EXTENDED_6800,   3,        4,    15,  0, 15, 15, 15, 15},
    {"CPX  ", 0xBC,  AM_EXTENDED_6800,   3,        5,     0,  0,  7, 15,  8,  0},
    {"JSR  ", 0xBD,  AM_EXTENDED_6800,   3,        9,     0,  0,  0,  0,  0,  0},
    {"LDS  ", 0xBE,  AM_EXTENDED_6800,   3,        5,     0,  0,  9, 15, 14,  0},
    {"STS  ", 0xBF,  AM_EXTENDED_6800,   3,        6,     0,  0,  9, 15, 14,  0},
    {"SUB B", 0xC0,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"CMP B", 0xC1,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"SBC B", 0xC2,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xC3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND B", 0xC4,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"BIT B", 0xC5,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"LDA B", 0xC6,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"~~~~~", 0xC7,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"EOR B", 0xC8,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"ADC B", 0xC9,  AM_IMM8_6800,       2,        2,    15,  0, 15, 15, 15, 15},
    {"ORA B", 0xCA,  AM_IMM8_6800,       2,        2,     0,  0, 15, 15, 14,  0},
    {"ADD B", 0xCB,  AM_IMM8_6800,       2,        2,    15,  0, 15, 15, 15, 15},
    {"~~~~~", 0xCC,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0xCD,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"LDX  ", 0xCE,  AM_IMM16_6800,      3,        3,     0,  0,  9, 15, 14,  0},
    {"~~~~~", 0xCF,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"SUB B", 0xD0,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"CMP B", 0xD1,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"SBC B", 0xD2,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xD3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND B", 0xD4,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"BIT B", 0xD5,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"LDA B", 0xD6,  AM_DIRECT_6800,     2,        4,     0,  0, 15, 15, 14,  0},
    {"STA B", 0xD7,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"EOR B", 0xD8,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"ADC B", 0xD9,  AM_DIRECT_6800,     2,        3,    15,  0, 15, 15, 15, 15},
    {"ORA B", 0xDA,  AM_DIRECT_6800,     2,        3,     0,  0, 15, 15, 14,  0},
    {"ADD B", 0xDB,  AM_DIRECT_6800,     2,        3,    15,  0, 15, 15, 15, 15},
    {"~~~~~", 0xDC,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0xDD,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"LDX  ", 0xDE,  AM_DIRECT_6800,     2,        4,     0,  0,  9, 15, 14,  0},
    {"STX  ", 0xDF,  AM_DIRECT_6800,     2,        5,     0,  0,  9, 15, 14,  0},
    {"SUB B", 0xE0,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"CMP B", 0xE1,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"SBC B", 0xE2,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xE3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND B", 0xE4,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"BIT B", 0xE5,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"LDA B", 0xE6,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"STA B", 0xE7,  AM_INDEXED_6800,    2,        6,     0,  0, 15, 15, 14,  0},
    {"EOR B", 0xE8,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"ADC B", 0xE9,  AM_INDEXED_6800,    2,        5,    15,  0, 15, 15, 15, 15},
    {"ORA B", 0xEA,  AM_INDEXED_6800,    2,        5,     0,  0, 15, 15, 14,  0},
    {"ADD B", 0xEB,  AM_INDEXED_6800,    2,        5,    15,  0, 15, 15, 15, 15},
    {"~~~~~", 0xEC,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0xED,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"LDX  ", 0xEE,  AM_INDEXED_6800,    2,        6,     0,  0,  9, 15, 14,  0},
    {"STX  ", 0xEF,  AM_INDEXED_6800,    2,        7,     0,  0,  9, 15, 14,  0},
    {"SUB B", 0xF0,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"CMP B", 0xF1,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"SBC B", 0xF2,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 15, 15},
    {"~~~~~", 0xF3,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"AND B", 0xF4,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"BIT B", 0xF5,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"LDA B", 0xF6,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"STA B", 0xF7,  AM_EXTENDED_6800,   3,        5,     0,  0, 15, 15, 14,  0},
    {"EOR B", 0xF8,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"ADC B", 0xF9,  AM_EXTENDED_6800,   3,        4,    15,  0, 15, 15, 15, 15},
    {"ORA B", 0xFA,  AM_EXTENDED_6800,   3,        4,     0,  0, 15, 15, 14,  0},
    {"ADD B", 0xFB,  AM_EXTENDED_6800,   3,        4,    15,  0, 15, 15, 15, 15},
    {"~~~~~", 0xFC,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"~~~~~", 0xFD,  AM_ILLEGAL,         1,        1,     0,  0,  0,  0,  0,  0},
    {"LDX  ", 0xFE,  AM_EXTENDED_6800,   3,        5,     0,  0,  9, 15, 14,  0},
    {"STX  ", 0xFF,  AM_EXTENDED_6800,   3,        6,     0,  0,  9, 15, 14,  0}
};

// The RP2040 UART registers are mapped as follows:
// 
//  Register	Function
//  UARTDR	    Holds TX and RX data (read to receive, write to transmit)
//  UARTFR	    Status flags: TXFF (TX FIFO Full), RXFE (RX FIFO Empty)
//  UARTIBRD	Integer part of baud rate divisor
//  UARTFBRD	Fractional part of baud rate divisor
//  UARTLCR_H	Line control (parity, stop bits, data length)
//  UARTCR	    UART Control Register (enable TX/RX)

bool uart_can_write(uart_inst_t *uart) 
{
    // Returns true if UART can accept new data for transmission.
    // Returns false if the TX FIFO is full.

    return uart_is_writable(uart);
}

bool uart_can_read(uart_inst_t *uart) 
{
    // Returns true if data is available to read.
    // Returns false if RX FIFO is empty.

    return uart_is_readable(uart);
}

// UART_UARTFR_BUSY_BITS: TX is still transmitting.
// UART_UARTFR_RXFE_BITS: RX FIFO is empty.

bool uart_tx_busy(uart_inst_t *uart) 
{
    return uart_get_hw(uart)->fr & UART_UARTFR_BUSY_BITS; // Check if TX is busy
}

bool uart_rx_available(uart_inst_t *uart) 
{
    return !(uart_get_hw(uart)->fr & UART_UARTFR_RXFE_BITS); // RX FIFO not empty
}

// Waits until the TX FIFO is not full.
// Writes the byte to the UARTDR register.

void uart_send_raw(uart_inst_t *uart, uint8_t data) 
{
    while (uart_get_hw(uart)->fr & UART_UARTFR_TXFF_BITS) {
        // Wait until TX FIFO is not full (TXFF = Transmit FIFO Full)
    }
    uart_get_hw(uart)->dr = data;  // Write data to the Data Register (UARTDR)
}

// Waits until the RX FIFO has data.
// Reads the byte from the UARTDR register.
// Masks the value with 0xFF to ensure only the lowest 8 bits are used.

uint8_t uart_receive_raw(uart_inst_t *uart) 
{
    while (uart_get_hw(uart)->fr & UART_UARTFR_RXFE_BITS) {
        // Wait until RX FIFO is not empty (RXFE = Receive FIFO Empty)
    }
    return (uint8_t)(uart_get_hw(uart)->dr & 0xFF); // Read received data
}

// function to write to the floppy controller registers
void writeFloppyControllerRegister(uint8_t b, uint16_t m)
{
    FloppyRegisterWrite (m, b);
}

// function to read from the floppy controller registers
uint8_t readFloppyControllerRegister (uint16_t m)
{
    uint8_t floppyRegister = 0xFF;
    floppyRegister = FloppyRegisterRead(m);
    return (floppyRegister);
}

// function to write to the SD Card controller registers
void writeSDCardControllerRegister(uint8_t b, uint16_t m)
{
    SDCARDRegisterWrite (m, b);
}

// function to read from the SC Card controller registers
uint8_t readSDCardControllerRegister (uint16_t m)
{
    uint8_t sdCardRegister = 0xFF;
    sdCardRegister = SDCARDRegisterRead(m);
    return (sdCardRegister);

}

// function to write to the ACIA UART registers
void writeUartRegister(uint8_t b, uint16_t m)
{
    // do not write to the uart control port.
    if (m != 0x8000 && m != 0x8002 && m != 0x8004 && m != 0x8006)
    {
        switch (m & 0x007)
        {
            case 0x01:      // aux data port
            case 0x03:      
                while (uart_get_hw(AUXPORT)->fr & UART_UARTFR_TXFF_BITS);
                uart_get_hw(AUXPORT)->dr = b;
                break;
            case 0x05:      
            case 0x07:      
                while (uart_get_hw(CONSOLE)->fr & UART_UARTFR_TXFF_BITS);
                uart_get_hw(CONSOLE)->dr = b;
                break;
        }
    }
}

// function to read from the ACIA UART registers
uint8_t readUartRegister (uint16_t m)
{
    uint8_t uartRegister = 0x00;

    switch (m & 0x007)
    {
        // handle the ACIA in slot 0
        case 0x00:          // aux rs232 status
            uartRegister  = uart_tx_busy(AUXPORT) == 1 ? 0x00 : 0x02;
            uartRegister |= uart_rx_available(AUXPORT) == 1 ? 0x01 : 0x00;
            break;
        case 0x01:          // aux rs232 data
            uartRegister = (uint8_t)(uart_get_hw(AUXPORT)->dr & 0xFF); // Read received data
            break;
        case 0x02:          // aux rs232 status
            uartRegister  = uart_tx_busy(AUXPORT) == 1 ? 0x00 : 0x02;
            uartRegister |= uart_rx_available(AUXPORT) == 1 ? 0x01 : 0x00;
            break;
        case 0x03:          // aux rs232 data
            uartRegister = (uint8_t)(uart_get_hw(AUXPORT)->dr & 0xFF); // Read received data
            break;

        // handle the ACIA in slot 1
        case 0x04:          // conole rs232 status
            uartRegister  = uart_tx_busy(CONSOLE) == 1 ? 0x00 : 0x02;
            uartRegister |= uart_rx_available(CONSOLE) == 1 ? 0x01 : 0x00;
            break;
        case 0x05:          // console rs232 data
            uartRegister = (uint8_t)(uart_get_hw(CONSOLE)->dr & 0xFF); // Read received data
            break;
        case 0x06:          // conole rs232 status
            uartRegister  = uart_tx_busy(CONSOLE) == 1 ? 0x00 : 0x02;
            uartRegister |= uart_rx_available(CONSOLE) == 1 ? 0x01 : 0x00;
            break;
        case 0x07:          // console rs232 data
            uartRegister = (uint8_t)(uart_get_hw(CONSOLE)->dr & 0xFF); // Read received data
        break;
    }

    return (uartRegister);
}

// function to read a byte from memory honoring the I/O port addresses
uint8_t LoadMemoryByte(uint16_t m)
{
    uint8_t d = 0xff;

    // reserve 4K bytes of I/O space
    if (m < 0x8000 || m >= 0x9000)
        d = cpu.memory[m];
    else
    {
        // This is where we handle I/O read acceess one byte at a time
        //      for now - just the console and auz rs232 ports

        switch (m & 0x001F)     // there are 32 I/O addresses
        {
            // slot 0
            case 0x00:        // aux serial port status register
            case 0x01:        // aux serial port data register
            case 0x02:        // still aux serial port status register
            case 0x03:        // still aux serial port data register

            // slot 1
            case 0x04:        // console serial port status register
            case 0x05:        // console  serial port data register
            case 0x06:        // still console serial port status register
            case 0x07:        // still console  serial port data register
                d = readUartRegister(m);
                break;

            // slot 2 & 3 are the SD Card
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0D:
            case 0x0E:
            case 0x0F:
                d = readSDCardControllerRegister(m);
                break;

            // slot 4
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
                break;

            // slot 5 and 6 are the floppy controller
            case 0x14:        // this is the drive select register on the flppy controller
            case 0x15:        
            case 0x16:        
            case 0x17:        
            case 0x18:        
            case 0x19:        
            case 0x1A:        
            case 0x1B:        
                d = readFloppyControllerRegister(m);
                break;

            // slot 7
            case 0x1C:        
            case 0x1D:        
            case 0x1E:        
            case 0x1F:        
                d = readMC146818(m);
                break;

            default:
                break;
        }
    }

    return (d);
}

uint16_t LoadMemoryWord(uint16_t sAddr)
{
    uint16_t d;

    d  = (uint16_t)(LoadMemoryByte(sAddr) * 256);
    d += (uint16_t)(LoadMemoryByte((uint16_t)(sAddr + 1)));

    return (d);
}

// function to write a byte from memory honoring the I/O port addresses
void StoreMemoryByte(uint8_t b, uint16_t m)
{
    // reserve 4K bytes of I/O space
    if (m < 0x8000 || m >= 0x9000)
    {
        cpu.memory[m] = b;
    }
    else
    {
        // This is where we handle I/O write acceess one byte at a time
        //      for now - just the console and auz rs232 ports

        switch (m & 0x001F)     // there are 32 I/O addresses
        {
            // slot 0
            case 0x00:        // aux serial port status register
            case 0x02:        // still aux serial port status register
            case 0x01:        // aux serial port data register
            case 0x03:        // still aux serial port data register

            // slot 1
            case 0x04:        // console serial port status register
            case 0x06:        // still console serial port status register
            case 0x05:        // console  serial port data register
            case 0x07:        // still console  serial port data register
                writeUartRegister(b, m);
                break;

            // slot 2 and 3 are the SD Card
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0D:
            case 0x0E:
            case 0x0F:
                writeSDCardControllerRegister(b, m);
                break;

            // slot 4
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
                break;

            // slot 5 and 6 are the floppy controller
            case 0x14:        // this is the drive select register on the flppy controller
            case 0x15:        
            case 0x16:        
            case 0x17:        
            case 0x18:        
            case 0x19:        
            case 0x1A:        
            case 0x1B:        
                writeFloppyControllerRegister(b, m);
                break;

            // slot 7
            case 0x1C:        
            case 0x1D:        
            case 0x1E:        
            case 0x1F:        
                writeMC146818(m, b);
                break;

            default:
                break;
        }
    }
}

void StoreMemoryWord(uint16_t w, uint16_t m)
{
    StoreMemoryByte((uint8_t)(w / 256), m);
    StoreMemoryByte((uint8_t)(w % 256), (uint16_t)(m + 1));
}

// HINZVC values:
//
//   0 No Affect
//   1 (Bit V) Test: Result = 10000000?
//   2 (Bit C) Test: Result = 00000000?
//   3 (Bit C) Test: Decimal value of most significant BCD character greater than 9?
//                   (Not cleared if previously set.)
//   4 (Bit V) Test: Operand = 10000000 prior to execution?
//   5 (Bit V) Test: Operand = 00000001 prior to execution?
//   6 (Bit V) Test: Set equal of result of N exclusive or C after shift has occurred.
//   7 (Bit N) Test: Sign bit of most significant byte of result = 1?
//   8 (Bit V) Test: 2's complement overflow from subtraction od LS bytes?
//   9 (Bit N) Test: Result less than zero? (Bit 15 = 1)
//  10 (All)   Load condition code register from stack
//  11 (Bit I) Set when interrupt occurrs. If prviously set, a Non-maskable interrupt is
//             required to exit the wait state.
//  12 (All)   Set according to the contents of Accumlator A
//  13 (All)   Set always
//  14 (All)   Reset always
//  15 (All)   Test and set if true, cleared otherwise.
//  16 (All)   same as 15 for 16 bit

void SetCCR_BeforeAndAfter(uint16_t after, uint16_t before)
{
    int i;
    uint8_t b;
    uint16_t s;
    int nRules[6];

    for (i = 0; i < 6; i++)
    {
        nRules[i] = opctbl[_opCode].ccr_rules[i];
    }
    
    for (i = 0; i < 6; i++)
    {
        switch (nRules[i])
        {
            case  0: // No Affect
                break;
            case  1: // (Bit V) Test: Result = 10000000?
                     //               ONLY USED FOR NEG     00 - M -> M
                     //                             NEG A   00 - A -> A
                     //                             NEG B   00 - B -> B
                if (after == 0x80)
                    cpu.CCR |= CCR_OVERFLOW;
                else
                    cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                break;
            case  2: // (Bit C) Test: Result = 00000000?
                     //               ONLY USED FOR NEG     00 - M -> M
                     //                             NEG A   00 - A -> A
                     //                             NEG B   00 - B -> B
                if (after == 0x00)
                    cpu.CCR &= (uint8_t)~CCR_CARRY;
                else
                    cpu.CCR |= CCR_CARRY;
                break;
            case  3: // (Bit C) Test: Decimal value of most significant BCD character > 9?
                     //               (Not cleared if previously set.)
                     //               ONLY USED FOR DAA
                if (_cf == 1)
                    cpu.CCR |= (uint8_t)CCR_CARRY;
                else
                    cpu.CCR &= (uint8_t)~CCR_CARRY;
                break;
            case  4: // (Bit V) Test: Operand = 10000000 prior to execution?
                     //               ONLY USED FOR DEC     M - 1 -> M
                     //                             DEC A   A - 1 -> A
                     //                             DEC B   B - 1 -> B
                if (before == 0x80)
                    cpu.CCR |= CCR_OVERFLOW;
                else
                    cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                break;
            case  5: // (Bit V) Test: Operand = 01111111 prior to execution?
                     //               ONLY USED FOR INC     M + 1 -> M
                     //                             INC A   A + 1 -> A
                     //                             INC B   B + 1 -> B
                if (before == 0x7F)
                    cpu.CCR |= CCR_OVERFLOW;
                else
                    cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                break;
            case  6: // (Bit V) Test: Set equal to result of N ^ C after shift has occurred.
                     //               ONLY USED FOR SHIFTS and ROTATES
                     //         The NEGATIVE bit in CCR will already be set
                     //         The Shifts and Rotates will have already set m_CF
                {
                    int nNegative = (cpu.CCR & CCR_NEGATIVE) == 0 ? 0 : 1;
                    if ((_cf ^ nNegative) == 1)
                        cpu.CCR |= CCR_OVERFLOW;
                    else
                        cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                }
                break;
            case  7: // (Bit N) Test: Sign bit of most significant byte of result = 1?
                     //               ONLY USED BY CPX
                if ((after & 0x8000) == 0x8000)
                    cpu.CCR |= CCR_NEGATIVE;
                else
                    cpu.CCR &= (uint8_t)~CCR_NEGATIVE;
                break;
            case  8: // (Bit V) Test: 2's complement overflow from subtraction od LS bytes?
                     //               ONLY USED BY CPX
                s = (uint16_t)((before & (uint16_t)0x00ff) - (_operand & (uint16_t)0x00ff));
                if ((s & 0x0100) == 0x0100)
                    cpu.CCR |= CCR_OVERFLOW;
                else
                    cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                break;
            case  9: // (Bit N) Test: Result less than zero? (Bit 15 = 1)
                if ((after & 0x8000) != 0)
                    cpu.CCR |= CCR_NEGATIVE;
                else
                    cpu.CCR &= (uint8_t)~CCR_NEGATIVE;
                break;
            case 10: // (All)   Load condition code register from stack
                     //         The instruction execution will have set CCR
                break;
            case 11: // (Bit I) Set when interrupt occurrs. If previously set, a Non-maskable interrupt is
                     //         required to exit the wait state.
                     //         The instruction execution will have set CCR
                break;
            case 12: // (All)   Set according to the contents of Accumlator A
                     //         The instruction execution will have set CCR (ONLY USED BY TAP)
                break;
            case 13: // (All)   Set always
                b = (uint8_t)(0x01 << (5 - i));
                cpu.CCR |= b;
                break;
            case 14: // (All)   Reset always
                b = (uint8_t)(0x01 << (5 - i));
                cpu.CCR &= (uint8_t)~b;
                break;
            case 15: // (All)   Test and set if true, cleared otherwise. 8 bit
                switch (i)
                {
                    case 0: // H    m_HF will be set by instruction execution
                        if (_hf == 1)
                            cpu.CCR |= CCR_HALFCARRY;
                        else
                            cpu.CCR &= (uint8_t)~CCR_HALFCARRY;
                        break;
                    case 1: // I    Only set by SWI in case 13
                        break;
                    case 2: // N    
                        if ((after & 0x0080) == 0x80)
                            cpu.CCR |= CCR_NEGATIVE;
                        else
                            cpu.CCR &= (uint8_t)~CCR_NEGATIVE;
                        break;
                    case 3: // Z
                        if (after == 0)
                            cpu.CCR |= CCR_ZERO;
                        else
                            cpu.CCR &= (uint8_t)~CCR_ZERO;
                        break;
                    case 4: // V    Only on Add's, Compare's, DAA, and Subtracts
                        if (_vf == 1)
                            cpu.CCR |= CCR_OVERFLOW;
                        else
                            cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                        break;
                    case 5: // C    m_CF will be set by instruction execution
                        if (_cf == 1)
                            cpu.CCR |= CCR_CARRY;
                        else
                            cpu.CCR &= (uint8_t)~CCR_CARRY;
                        break;
                }
                break;
            case 16: // (All)   Test and set if true, cleared otherwise. (16 bit)
                switch (i)
                {
                    case 0: // H    m_HF will be set by instruction execution
                        if (_hf == 1)
                            cpu.CCR |= CCR_HALFCARRY;
                        else
                            cpu.CCR &= (uint8_t)~CCR_HALFCARRY;
                        break;
                    case 1: // I    Only set by SWI in case 13
                        break;
                    case 2: // N    
                        if ((after & 0x8000) == 0x8000)
                            cpu.CCR |= CCR_NEGATIVE;
                        else
                            cpu.CCR &= (uint8_t)~CCR_NEGATIVE;
                        break;
                    case 3: // Z
                        if (after == 0)
                            cpu.CCR |= CCR_ZERO;
                        else
                            cpu.CCR &= (uint8_t)~CCR_ZERO;
                        break;
                    case 4: // V    Only on Add's, Compare's, DAA, and Subtracts
                        if (_vf == 1)
                            cpu.CCR |= CCR_OVERFLOW;
                        else
                            cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
                        break;
                    case 5: // C    m_CF will be set by instruction execution
                        if (_cf == 1)
                            cpu.CCR |= CCR_CARRY;
                        else
                            cpu.CCR &= (uint8_t)~CCR_CARRY;
                        break;
                }
                break;
        }
    }
}

void SetCCR_After(uint16_t after)
{
    SetCCR_BeforeAndAfter(after, 0);
}

void DecimalAdjustAccumulator ()
{
    uint8_t CF, UB, HF, LB;
    CF = (uint8_t)(cpu.CCR & CCR_CARRY);
    HF = (uint8_t)((cpu.CCR & CCR_HALFCARRY) == 0 ? 0 : 2);
    UB = (uint8_t)((cpu.A & 0xf0) >> 4);
    LB = (uint8_t)(cpu.A & 0x0f);
    _cf = 0;
    switch (CF + HF)
    {
        case 0:         // Carry clear - Halfcarry clear
            for(;;)
            {
                if ((UB >= 0 && UB <= 9) && (LB >= 0 && LB <= 9))
                {
                    _cf = 0;
                    SetCCR_After(cpu.A);
                    break;
                }
                if ((UB >= 0 && UB <= 8) && (LB >= 10 && LB <= 15))
                {
                    cpu.A += 0x06;
                    _cf = 0;
                    SetCCR_After(cpu.A);
                    break;
                }
                if ((UB >= 10 && UB <= 15) && (LB >= 0 && LB <= 9))
                {
                    cpu.A += 0x60;
                    _cf = 1;
                    SetCCR_After(cpu.A);
                    break;
                }
                if ((UB >= 9 && UB <= 15) && (LB >= 10 && LB <= 15))
                {
                    cpu.A += 0x66;
                    _cf = 1;
                    SetCCR_After(cpu.A);
                    break;
                }
                break;
            }
            break;
        case 1:         // Carry set - Halfcarry clear
            for (;;)
            {
                if ((UB >= 0 && UB <= 2) && (LB >= 0 && LB <= 9))
                {
                    cpu.A += 0x60;
                    _cf = 1;
                    SetCCR_After(cpu.A);
                    break;
                }
                if ((UB >= 0 && UB <= 2) && (LB >= 10 && LB <= 15))
                {
                    cpu.A += 0x66;
                    _cf = 1;
                    SetCCR_After(cpu.A);
                    break;
                }
                break;
            }
            break;
        case 2:         // Carry clear - Halfcarry set
        for (;;)
            {
                if ((UB >= 0 && UB <= 9) && (LB >= 0 && LB <= 3))
                {
                    cpu.A += 0x06;
                    _cf = 0;
                    SetCCR_After(cpu.A);
                    break;
                }
                if ((UB >= 10 && UB <= 15) && (LB >= 0 && LB <= 3))
                {
                    cpu.A += 0x66;
                    _cf = 1;
                    SetCCR_After(cpu.A);
                    break;
                }
                break;
            }
            break;
        case 3:         // Carry set - Halfcarry set
            if ((UB >= 0 && UB <= 3) && (LB >= 0 && LB <= 3))
            {
                cpu.A += 0x66;
                _cf = 1;
                SetCCR_After(cpu.A);
            }
            break;
    }
}
// region Arithmetic and Logical Register Operations
void BitRegister (uint8_t cReg, uint8_t cOperand)
{
    uint8_t c;
    c = (uint8_t)(cReg & cOperand);
    SetCCR_After (c);
}
uint8_t SubtractRegister (uint8_t cReg, uint8_t cOperand)
{
    //  R - M -> R
    uint8_t c;
    if (cReg < cOperand)
        _cf = 1;
    else
        _cf = 0;
    c = (uint8_t)(cReg - cOperand);
    if (
        ((cReg & 0x80) == 0x80 && (cOperand & 0x80) == 0x00 && (c & 0x80) == 0x00) ||
        ((cReg & 0x80) == 0x00 && (cOperand & 0x80) == 0x80 && (c & 0x80) == 0x80)
       )
        _vf = 1;
    else
        _vf = 0;
    SetCCR_After (c);
    return (c);
}
uint8_t CompareRegister (uint8_t cReg, uint8_t cOperand)
{
    //  R - M
    uint8_t c;
    if (cReg < cOperand)
        _cf = 1;
    else
        _cf = 0;

    c = (uint8_t)(cReg - cOperand);
    if (
        ((cReg & 0x80) == 0x80 && (cOperand & 0x80) == 0x00 && (c & 0x80) == 0x00) ||
        ((cReg & 0x80) == 0x00 && (cOperand & 0x80) == 0x80 && (c & 0x80) == 0x80)
       )
        _vf = 1;
    else
        _vf = 0;
    SetCCR_After (c);
    return (cReg);
}
uint8_t SubtractWithCarryRegister (uint8_t cReg, uint8_t cOperand)
{
    //  R - M - C -> R
    uint8_t c;
    if (cReg < (cOperand + (cpu.CCR & CCR_CARRY)))
        _cf = 1;
    else
        _cf = 0;
    c = (uint8_t)(cReg - cOperand - (cpu.CCR & CCR_CARRY));
    if (
        ((cReg & 0x80) == 0x80 && (cOperand & 0x80) == 0x00 && (c & 0x80) == 0x00) ||
        ((cReg & 0x80) == 0x00 && (cOperand & 0x80) == 0x80 && (c & 0x80) == 0x80)
       )
        _vf = 1;
    else
        _vf = 0;
    SetCCR_After (c);
    return (c);
}
uint8_t AndRegister (uint8_t cReg, uint8_t cOperand)
{
    cReg &= cOperand;
    SetCCR_After (cReg);
    return (cReg);
}
uint8_t ExclusiveOrRegister (uint8_t cReg, uint8_t cOperand)
{
    cReg ^= cOperand;
    SetCCR_After (cReg);
    return (cReg);
}
uint8_t AddWithCarryRegister (uint8_t cReg, uint8_t cOperand)
{
    //  R + M + C -> R
    uint8_t c;
    if ((cReg + cOperand + (cpu.CCR & CCR_CARRY)) > 255)
        _cf = 1;
    else
        _cf = 0;
    if (((cReg & 0x0f) + (cOperand & 0x0f) + (cpu.CCR & CCR_CARRY)) > 15)
        _hf = 1;
    else
        _hf = 0;
    c = (uint8_t)(cReg + cOperand + (cpu.CCR & CCR_CARRY));
    if (
        ((cReg & 0x80) == 0x80 && (cOperand & 0x80) == 0x80 && (c & 0x80) == 0x00) ||
        ((cReg & 0x80) == 0x00 && (cOperand & 0x80) == 0x00 && (c & 0x80) == 0x80)
       )
        _vf = 1;
    else
        _vf = 0;
    SetCCR_After (c);
    return (c);
}
uint8_t OrRegister (uint8_t cReg, uint8_t cOperand)
{
    cReg |= cOperand;
    SetCCR_After (cReg);
    return (cReg);
}
uint8_t AddRegister (uint8_t cReg, uint8_t cOperand)
{
    uint8_t c;
    if ((cReg + cOperand) > 255)
        _cf = 1;
    else
        _cf = 0;
    if (((cReg & 0x0f) + (cOperand & 0x0f)) > 15)
        _hf = 1;
    else
        _hf = 0;
    c = (uint8_t)(cReg + cOperand);
    if (
        ((cReg & 0x80) == 0x80 && (cOperand & 0x80) == 0x80 && (c & 0x80) == 0x00) ||
        ((cReg & 0x80) == 0x00 && (cOperand & 0x80) == 0x00 && (c & 0x80) == 0x80)
       )
        _vf = 1;
    else
        _vf = 0;
    SetCCR_After (c);
    return (c);
}
// #endregion
// #region Instruction Executors
void Execute6800Inherent ()
{
    uint8_t  cReg;
    switch (_opCode)
    {
        case 0x01:     //    "NOP  "
            break;
        case 0x06:     //    "TAP  "
            cpu.CCR = (uint8_t)(cpu.A | 0xC0);
            break;
        case 0x07:     //    "TPA  "
            cpu.A = cpu.CCR;
            break;
        case 0x08:     //    "INX  "
            cpu.X++;
            SetCCR_After(cpu.X);
            break;
        case 0x09:     //    "DEX  "
            cpu.X--;
            SetCCR_After(cpu.X);
            break;
        case 0x0A:     //    "CLV  "
            cpu.CCR &= (uint8_t)~CCR_OVERFLOW;
            break;
        case 0x0B:     //    "SEV  "
            cpu.CCR |= CCR_OVERFLOW;
            break;
        case 0x0C:     //    "CLC  "
            cpu.CCR &= (uint8_t)~CCR_CARRY;
            break;
        case 0x0D:     //    "SEC  "
            cpu.CCR |= CCR_CARRY;
            break;
        case 0x0E:     //    "CLI  "
            cpu.CCR &= (uint8_t)~CCR_INTERRUPT;
            break;
        case 0x0F:     //    "SEI  "
            cpu.CCR |= CCR_INTERRUPT;
            break;
        case 0x10:     //    "SBA  "
            if (cpu.A < cpu.B)
                _cf = 1;
            else
                _cf = 0;
            cReg = (uint8_t)(cpu.A - cpu.B);
            if (
                ((cpu.A & 0x80) == 0x80 && (cpu.B & 0x80) == 0x00 && (cReg & 0x80) == 0x00) ||
                ((cpu.A & 0x80) == 0x00 && (cpu.B & 0x80) == 0x80 && (cReg & 0x80) == 0x80)
               )
                _vf = 1;
            else
                _vf = 0;
            cpu.A = cReg;
            SetCCR_After(cpu.A);
            break;
        case 0x11:     //    "CBA  "
            if (cpu.A < cpu.B)
                _cf = 1;
            else
                _cf = 0;
            cReg = (uint8_t)(cpu.A - cpu.B);
            if (
                ((cpu.A & 0x80) == 0x80 && (cpu.B & 0x80) == 0x00 && (cReg & 0x80) == 0x00) ||
                ((cpu.A & 0x80) == 0x00 && (cpu.B & 0x80) == 0x80 && (cReg & 0x80) == 0x80)
               )
                _vf = 1;
            else
                _vf = 0;
            SetCCR_After (cReg);
            break;
        case 0x16:     //    "TAB  "
            cpu.B = cpu.A;
            SetCCR_After(cpu.B);
            break;
        case 0x17:     //    "TBA  "
            cpu.A = cpu.B;
            SetCCR_After(cpu.A);
            break;
        case 0x19:     //    "DAA  "
            DecimalAdjustAccumulator ();
            break;
        case 0x1B:     //    "ABA  "
            if ((cpu.A + cpu.B) > 255)
                _cf = 1;
            else
                _cf = 0;
            if (((cpu.A & 0x0f) + (cpu.B & 0x0f)) > 15)
                _hf = 1;
            else
                _hf = 0;
            cReg = (uint8_t)(cpu.A + cpu.B);
            if (
                ((cpu.A & 0x80) == 0x80 && (cpu.B & 0x80) == 0x80 && (cReg & 0x80) == 0x00) ||
                ((cpu.A & 0x80) == 0x00 && (cpu.B & 0x80) == 0x00 && (cReg & 0x80) == 0x80)
               )
                _vf = 1;
            else
                _vf = 0;
            cpu.A = cReg;
            SetCCR_After(cpu.A);
            break;
        case 0x30:     //    "TSX  "
            cpu.X = (uint16_t)(cpu.SP + 1);
            break;
        case 0x31:     //    "INS  "
            cpu.SP++;
            break;
        case 0x32:     //    "PUL A"
            cpu.A = LoadMemoryByte(++cpu.SP);
            break;
        case 0x33:     //    "PUL B"
            cpu.B = LoadMemoryByte(++cpu.SP);
            break;
        case 0x34:     //    "DES  "
            cpu.SP--;
            break;
        case 0x35:     //    "TXS  "
            cpu.SP = (uint16_t)(cpu.X - 1);
            break;
        case 0x36:     //    "PSH A"
            StoreMemoryByte(cpu.A, cpu.SP--);
            break;
        case 0x37:     //    "PSH B"
            StoreMemoryByte(cpu.B, cpu.SP--);
            break;
        case 0x39:     //    "RTS  "
            cpu.PC = (uint16_t)(cpu.memory[++cpu.SP] * 256);
            cpu.PC = (uint16_t)(cpu.PC + cpu.memory[++cpu.SP]);
            break;
        case 0x3B:     //    "RTI  "
            cpu.CCR = cpu.memory[++cpu.SP];
            cpu.B = cpu.memory[++cpu.SP];
            cpu.A = cpu.memory[++cpu.SP];
            cpu.X = (uint16_t)(cpu.memory[++cpu.SP] * 256);
            cpu.X += (uint16_t)(cpu.memory[++cpu.SP]);
            cpu.PC = (uint16_t)(cpu.memory[++cpu.SP] * 256);
            cpu.PC += (uint16_t)(cpu.memory[++cpu.SP]);
            break;
        case 0x3E:     //    "WAI  "
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC / 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.X % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.X / 256);
            cpu.memory[cpu.SP--] = cpu.A;
            cpu.memory[cpu.SP--] = cpu.B;
            cpu.memory[cpu.SP--] = cpu.CCR;
            inWait = 1;
            break;
        case 0x3F:     //    "SWI  "
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC / 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.X % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.X / 256);
            cpu.memory[cpu.SP--] = cpu.A;
            cpu.memory[cpu.SP--] = cpu.B;
            cpu.memory[cpu.SP--] = cpu.CCR;
            cpu.CCR |= CCR_INTERRUPT;
            cpu.PC = LoadMemoryWord(0xFFFA);
            break;
        case 0x40:     //    "NEG R"
            cpu.A = (uint8_t)(0x00 - cpu.A);
            SetCCR_After(cpu.A);
            break;
        case 0x43:     //    "COM R"
            cpu.A = (uint8_t)(0xFF - cpu.A);
            SetCCR_After(cpu.A);
            break;
        case 0x44:     //    "LSR R"
            cReg = cpu.A;
            _cf = (uint8_t)(cpu.A & 0x01);
            cpu.A = (uint8_t)((cpu.A >> 1) & 0x7F);        // always set bit 7 to 0
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x46:     //    "ROR R"
            cReg = cpu.A;
            _cf = (uint8_t)(cpu.A & 0x01);
            cpu.A = (uint8_t)(cpu.A >> 1);
            // set bit 7 = old carry flag
            if ((cpu.CCR & CCR_CARRY) == CCR_CARRY)
                cpu.A = (uint8_t)(cpu.A | 0x80);
            else
                cpu.A = (uint8_t)(cpu.A & 0x7F);
                SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x47:     //    "ASR R"
            cReg = cpu.A;
            _cf = (uint8_t)(cpu.A & 0x01);
            cpu.A = (uint8_t)(cpu.A >> 1);
            cpu.A = (uint8_t)(cpu.A | (cReg & 0x80));      // preserve the sign bit
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x48:     //    "ASL R"
            cReg = cpu.A;
            if ((cpu.A & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cpu.A = (uint8_t)(cpu.A << 1);
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x49:     //    "ROL R"
            cReg = cpu.A;
            if ((cpu.A & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cpu.A = (uint8_t)(cpu.A << 1);
            cpu.A |= (uint8_t)(cpu.CCR & CCR_CARRY);
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x4A:     //    "DEC R"
            cReg = cpu.A;
            cpu.A = (uint8_t)(cpu.A - 1);
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x4C:     //    "INC R"
            cReg = cpu.A;
            cpu.A = (uint8_t)(cpu.A + 1);
            SetCCR_BeforeAndAfter(cpu.A, cReg);
            break;
        case 0x4D:     //    "TST R"
            SetCCR_After(cpu.A);
            break;
        case 0x4F:     //    "CLR R"
            cpu.A = 0x00;
            SetCCR_After(cpu.A);
            break;
        case 0x50:     //    "NEG R"
            cpu.B = (uint8_t)(0x00 - cpu.B);
            SetCCR_After(cpu.B);
            break;
        case 0x53:     //    "COM R"
            cpu.B = (uint8_t)(0xFF - cpu.B);
            SetCCR_After(cpu.B);
            break;
        case 0x54:     //    "LSR R"
            cReg = cpu.B;
            _cf = (uint8_t)(cpu.B & 0x01);
            cpu.B = (uint8_t)((cpu.B >> 1) & 0x7F);        // always set bit 7 to 0
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x56:     //    "ROR R"
            cReg = cpu.A;
            _cf = (uint8_t)(cpu.B & 0x01);
            cpu.B = (uint8_t)(cpu.B >> 1);
            // set bit 7 = old carry flag
            if ((cpu.CCR & CCR_CARRY) == CCR_CARRY)
                cpu.B = (uint8_t)(cpu.B | 0x80);
            else
                cpu.B = (uint8_t)(cpu.B & 0x7F);
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x57:     //    "ASR R"
            cReg = cpu.B;
            _cf = (uint8_t)(cpu.B & 0x01);
            cpu.B = (uint8_t)(cpu.B >> 1);
            cpu.B = (uint8_t)(cpu.B | (cReg & 0x80));      // preserve the sign bit
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x58:     //    "ASL R"
            cReg = cpu.B;
            if ((cpu.B & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cpu.B = (uint8_t)(cpu.B << 1);
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x59:     //    "ROL R"
            cReg = cpu.B;
            if ((cpu.B & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cpu.B = (uint8_t)(cpu.B << 1);
            cpu.B |= (uint8_t)(cpu.CCR & CCR_CARRY);
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x5A:     //    "DEC R"
            cReg = cpu.B;
            cpu.B = (uint8_t)(cpu.B - 1);
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x5C:     //    "INC R"
            cReg = cpu.B;
            cpu.B = (uint8_t)(cpu.B + 1);
            SetCCR_BeforeAndAfter(cpu.B, cReg);
            break;
        case 0x5D:     //    "TST R"
            SetCCR_After(cpu.B);
            break;
        case 0x5F:     //    "CLR R"
            cpu.B = 0x00;
            SetCCR_After(cpu.B);
            break;
    }
}
void Execute6800Extended ()
{
    uint16_t sData;
    uint8_t  cData;
    uint8_t  cReg;
    uint16_t sBefore;
    uint16_t sResult;
    switch (_opCode)
    {
        case 0x70:     //    "NEG  "
            cData = LoadMemoryByte(_operand);
            cData = (uint8_t)(0x00 - cData);
            StoreMemoryByte (cData, _operand);
            SetCCR_After (cData);
            break;
        case 0x73:     //    "COM  "
            cData = LoadMemoryByte(_operand);
            cData = (uint8_t)(0xFF - cData);
            StoreMemoryByte (cData, _operand);
            SetCCR_After (cData);
            break;
        case 0x74:     //    "LSR  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData = (uint8_t)((cData >> 1) & 0x7F);        // always set bit 7 to 0
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x76:     //    "ROR  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData = (uint8_t)(cData >> 1);
            // set bit 7 = old carry flag
            if ((cpu.CCR & CCR_CARRY) == CCR_CARRY)
                cData = (uint8_t)(cData | 0x80);
            else
                cData = (uint8_t)(cData & 0x7F);
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x77:     //    "ASR  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData  = (uint8_t)(cData >> 1);
            cData = (uint8_t)(cData | (cReg & 0x80));      // preserve the sign bit
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x78:     //    "ASL  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            if ((cData & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cData  = (uint8_t)(cData << 1);
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x79:     //    "ROL  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            if ((cData & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cData  = (uint8_t)(cData << 1);
            cData |= (uint8_t)(cpu.CCR & CCR_CARRY);
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x7A:     //    "DEC  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            cData -= 1;
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x7C:     //    "INC  "
            cData = LoadMemoryByte(_operand);
            cReg = cData;
            cData += 1;
            StoreMemoryByte (cData, _operand);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x7D:     //    "TST  "
            cData = LoadMemoryByte(_operand);
            SetCCR_After (cData);
            break;
        case 0x7E:     //    "JMP  "
            cpu.PC = _operand;
            break;
        case 0x7F:     //    "CLR  "
            cData = 0x00;
            StoreMemoryByte (cData, _operand);
            SetCCR_After (cData);
            break;
        case 0xBC:     //    "CPX  "
            sData = LoadMemoryWord(_operand);
            sBefore = cpu.X;
            sResult = (uint16_t)(cpu.X - sData);
            SetCCR_BeforeAndAfter (sResult, sBefore);
            break;
        case 0xBD:     //    "JSR  "
            // First save current IP
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC / 256);
            // Then load new IP
            cpu.PC = _operand;
            break;
        case 0xBE:     //    "LDS  "
            cpu.SP = LoadMemoryWord(_operand);
            SetCCR_After (cpu.SP);
            break;
        case 0xBF:     //    "STS  "    
            StoreMemoryWord(cpu.SP, _operand);
            SetCCR_After (cpu.SP);
            break;
        case 0xFE:     //    "LDX  "
            cpu.X = LoadMemoryWord(_operand);
            SetCCR_After(cpu.X);
            break;
        case 0xFF:     //    "STX  "
            StoreMemoryWord(cpu.X, _operand);
            SetCCR_After(cpu.X);
            break;
        // Register A and B Extended Instructions
        case 0xB0:     //    "SUB R"
            cData = LoadMemoryByte(_operand);
            cpu.A = SubtractRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB1:     //    "CMP R"
            cData = LoadMemoryByte(_operand);
            CompareRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB2:     //    "SBC R"
            cData = LoadMemoryByte(_operand);
            cpu.A = SubtractWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB4:     //    "AND R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AndRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB5:     //    "BIT R"
            cData = LoadMemoryByte(_operand);
            BitRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB6:     //    "LDA R"
            cpu.A = LoadMemoryByte(_operand);
            SetCCR_After(cpu.A);
            break;
        case 0xB7:     //    "STA R"
            StoreMemoryByte(cpu.A, _operand);
            SetCCR_After(cpu.A);
            break;
        case 0xB8:     //    "EOR R"
            cData = LoadMemoryByte(_operand);
            cpu.A = ExclusiveOrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xB9:     //    "ADC R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AddWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xBA:     //    "ORA R"
            cData = LoadMemoryByte(_operand);
            cpu.A = OrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xBB:     //    "ADD R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AddRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xF0:     //    "SUB R"
            cData = LoadMemoryByte(_operand);
            cpu.B = SubtractRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF1:     //    "CMP R"
            cData = LoadMemoryByte(_operand);
            CompareRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF2:     //    "SBC R"
            cData = LoadMemoryByte(_operand);
            cpu.B = SubtractWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF4:     //    "AND R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AndRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF5:     //    "BIT R"
            cData = LoadMemoryByte(_operand);
            BitRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF6:     //    "LDA R"
            cpu.B = LoadMemoryByte(_operand);
            SetCCR_After(cpu.B);
            break;
        case 0xF7:     //    "STA R"
            StoreMemoryByte(cpu.B, _operand);
            SetCCR_After(cpu.B);
            break;
        case 0xF8:     //    "EOR R"
            cData = LoadMemoryByte(_operand);
            cpu.B = ExclusiveOrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xF9:     //    "ADC R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AddWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xFA:     //    "ORA R"
            cData = LoadMemoryByte(_operand);
            cpu.B = OrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xFB:     //    "ADD R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AddRegister(cpu.B, (uint8_t)cData);
            break;
    }
}
void Execute6800Indexed ()
{
    uint16_t sOperandPtr;
    uint16_t sData;
    uint8_t  cData;
    uint8_t  cReg;
    uint16_t sBefore;
    uint16_t sResult;
    sOperandPtr = (uint16_t)(_operand + cpu.X);
    switch (_opCode)
    {
        case 0x60:     //    "NEG  "
            cData = LoadMemoryByte(sOperandPtr);
            cData = (uint8_t)(0x00 - cData);
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_After (cData);
            break;
        case 0x63:     //    "COM  "
            cData = LoadMemoryByte(sOperandPtr);
            cData = (uint8_t)(0xFF - cData);
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_After (cData);
            break;
        case 0x64:     //    "LSR  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData = (uint8_t)((cData >> 1) & 0x7F);        // always set bit 7 to 0
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x66:     //    "ROR  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData = (uint8_t)(cData >> 1);
            // set bit 7 = old carry flag
            if ((cpu.CCR & CCR_CARRY) == CCR_CARRY)
                cData = (uint8_t)(cData | 0x80);
            else
                cData = (uint8_t)(cData & 0x7F);
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x67:     //    "ASR  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            _cf = (uint8_t)(cData & 0x01);
            cData = (uint8_t)(cData >> 1);
            cData = (uint8_t)(cData | (cReg & 0x80));      // preserve the sign bit
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x68:     //    "ASL  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            if ((cData & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cData  = (uint8_t)(cData << 1);
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x69:     //    "ROL  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            if ((cData & 0x80) == 0x80)
                _cf = 1;
            else
                _cf = 0;
            cData  = (uint8_t)(cData << 1);
            cData |= (uint8_t)(cpu.CCR & CCR_CARRY);
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x6A:     //    "DEC  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            cData -= 1;
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x6C:     //    "INC  "
            cData = LoadMemoryByte(sOperandPtr);
            cReg = cData;
            cData += 1;
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_BeforeAndAfter (cData, cReg);
            break;
        case 0x6D:     //    "TST  "
            cData = LoadMemoryByte(sOperandPtr);
            SetCCR_After (cData);
            break;
        case 0x6E:     //    "JMP  "
            cpu.PC = sOperandPtr;
            break;
        case 0x6F:     //    "CLR  "
            cData = 0x00;
            StoreMemoryByte (cData, sOperandPtr);
            SetCCR_After (cData);
            break;
        case 0xAC:     //    "CPX  "
            sData = LoadMemoryWord(sOperandPtr);
            sBefore = cpu.X;
            sResult = (uint16_t)(cpu.X - sData);
            SetCCR_BeforeAndAfter (sResult, sBefore);
            break;
        case 0xAD:     //    "JSR  "
            // First save current IP
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC % 256);
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC / 256);
            // Then load new IP
            cpu.PC = sOperandPtr;
            break;
        case 0xAE:     //    "LDS  "
            sData = LoadMemoryWord(sOperandPtr);
            cpu.SP = sData;
            SetCCR_After (cpu.SP);
            break;
        case 0xAF:     //    "STS  "
            StoreMemoryWord(cpu.SP, sOperandPtr);
            SetCCR_After (cpu.SP);
            break;
        case 0xEE:     //    "LDX  "
            sData = LoadMemoryWord(sOperandPtr);
            cpu.X = sData;
            SetCCR_After(cpu.X);
            break;
        case 0xEF:     //    "STX  "
            StoreMemoryWord(cpu.X, sOperandPtr);
            SetCCR_After(cpu.X);
            break;
        case 0xA0:     //    "SUB R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = SubtractRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA1:     //    "CMP R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = CompareRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA2:     //    "SBC R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = SubtractWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA4:     //    "AND R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = AndRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA5:     //    "BIT R"
            cData = LoadMemoryByte(sOperandPtr);
            BitRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA6:     //    "LDA R"
            cpu.A = LoadMemoryByte(sOperandPtr);
            SetCCR_After(cpu.A);
            break;
        case 0xA7:     //    "STA R"
            StoreMemoryByte(cpu.A, sOperandPtr);
            SetCCR_After(cpu.A);
            break;
        case 0xA8:     //    "EOR R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = ExclusiveOrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xA9:     //    "ADC R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = AddWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xAA:     //    "ORA R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = OrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xAB:     //    "ADD R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.A = AddRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xE0:     //    "SUB R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = SubtractRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE1:     //    "CMP R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = CompareRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE2:     //    "SBC R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = SubtractWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE4:     //    "AND R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = AndRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE5:     //    "BIT R"
            cData = LoadMemoryByte(sOperandPtr);
            BitRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE6:     //    "LDA R"
            cpu.B = LoadMemoryByte(sOperandPtr);
            SetCCR_After(cpu.B);
            break;
        case 0xE7:     //    "STA R"
            StoreMemoryByte(cpu.B, sOperandPtr);
            SetCCR_After(cpu.B);
            break;
        case 0xE8:     //    "EOR R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = ExclusiveOrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xE9:     //    "ADC R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = AddWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xEA:     //    "ORA R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = OrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xEB:     //    "ADD R"
            cData = LoadMemoryByte(sOperandPtr);
            cpu.B = AddRegister(cpu.B, (uint8_t)cData);
            break;
    }
}
void Execute6800Direct ()
{
    uint8_t  cData;
    uint16_t sData;
    uint16_t sResult;
    uint16_t sBefore;
    switch (_opCode)
    {
        case 0x9C:  // CPX
            sData = LoadMemoryWord(_operand);
            sBefore = cpu.X;
            sResult = (uint16_t)(cpu.X - sData);
            SetCCR_BeforeAndAfter (sResult, sBefore);
            break;
        case 0x9E:  // LDS
            sData = LoadMemoryWord(_operand);
            cpu.SP = sData;
            SetCCR_After (cpu.SP);
            break;
        case 0x9F:  // STS
            StoreMemoryWord(cpu.SP, _operand);
            SetCCR_After (cpu.SP);
            break;
        case 0xDE:  // LDX
            sData = LoadMemoryWord(_operand);
            cpu.X = sData;
            SetCCR_After(cpu.X);
            break;
        case 0xDF:  // STX
            StoreMemoryWord(cpu.X, _operand);
            SetCCR_After(cpu.X);
            break;
        case 0x90: //    "SUB R"
            cData = LoadMemoryByte(_operand);
            cpu.A = SubtractRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x91: //    "CMP R"
            cData = LoadMemoryByte(_operand);
            cpu.A = CompareRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x92: //    "SBC R"
            cData = LoadMemoryByte(_operand);
            cpu.A = SubtractWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x94: //    "AND R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AndRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x95: //    "BIT R"
            cData = LoadMemoryByte(_operand);
            BitRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x96: //    "LDA R"
            cpu.A = LoadMemoryByte(_operand);
            SetCCR_After(cpu.A);
            break;
        case 0x97: //    "STA R"
            StoreMemoryByte(cpu.A, _operand);
            SetCCR_After(cpu.A);
            break;
        case 0x98: //    "EOR R"
            cData = LoadMemoryByte(_operand);
            cpu.A = ExclusiveOrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x99: //    "ADC R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AddWithCarryRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x9A: //    "ORA R"
            cData = LoadMemoryByte(_operand);
            cpu.A = OrRegister(cpu.A, (uint8_t)cData);
            break;
        case 0x9B: //    "ADD R"
            cData = LoadMemoryByte(_operand);
            cpu.A = AddRegister(cpu.A, (uint8_t)cData);
            break;
        case 0xD0: //    "SUB R"
            cData = LoadMemoryByte(_operand);
            cpu.B = SubtractRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD1: //    "CMP R"
            cData = LoadMemoryByte(_operand);
            cpu.B = CompareRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD2: //    "SBC R"
            cData = LoadMemoryByte(_operand);
            cpu.B = SubtractWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD4: //    "AND R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AndRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD5: //    "BIT R"
            cData = LoadMemoryByte(_operand);
            BitRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD6: //    "LDA R"
            cpu.B = LoadMemoryByte(_operand);
            SetCCR_After(cpu.B);
            break;
        case 0xD7: //    "STA R"
            StoreMemoryByte(cpu.B, _operand);
            SetCCR_After(cpu.B);
            break;
        case 0xD8: //    "EOR R"
            cData = LoadMemoryByte(_operand);
            cpu.B = ExclusiveOrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xD9: //    "ADC R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AddWithCarryRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xDA: //    "ORA R"
            cData = LoadMemoryByte(_operand);
            cpu.B = OrRegister(cpu.B, (uint8_t)cData);
            break;
        case 0xDB: //    "ADD R"
            cData = LoadMemoryByte(_operand);
            cpu.B = AddRegister(cpu.B, (uint8_t)cData);
            break;
    }
}
void Execute6800Immediate8 ()
{
    switch (_opCode)
    {
        case 0x80: //    "SUB R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.A = SubtractRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x81: //    "CMP R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.A = CompareRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x82: //    "SBC R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.A = SubtractWithCarryRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x84: //    "AND R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.A = AndRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x85: //    "BIT R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            BitRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x86: //    "LDA R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.A = (uint8_t)_operand;
            SetCCR_After(cpu.A);
            break;
        case 0x88: //    "EOR R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.A = ExclusiveOrRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x89: //    "ADC R", AM_IMM8_6800,       15,  0, 15, 15, 15, 15, 
            cpu.A = AddWithCarryRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x8A: //    "ORA R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.A = OrRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0x8B: //    "ADD R", AM_IMM8_6800,       15,  0, 15, 15, 15, 15, 
            cpu.A = AddRegister(cpu.A, (uint8_t)_operand);
            break;
        case 0xC0: //    "SUB R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.B = SubtractRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC1: //    "CMP R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.B = CompareRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC2: //    "SBC R", AM_IMM8_6800,       0,  0, 15, 15, 15, 15, 
            cpu.B = SubtractWithCarryRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC4: //    "AND R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.B = AndRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC5: //    "BIT R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            BitRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC6: //    "LDA R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.B = (uint8_t)_operand;
            SetCCR_After(cpu.B);
            break;
        case 0xC8: //    "EOR R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.B = ExclusiveOrRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xC9: //    "ADC R", AM_IMM8_6800,       15,  0, 15, 15, 15, 15, 
            cpu.B = AddWithCarryRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xCA: //    "ORA R", AM_IMM8_6800,       0,  0, 15, 15, 14,  0, 
            cpu.B = OrRegister(cpu.B, (uint8_t)_operand);
            break;
        case 0xCB: //    "ADD R", AM_IMM8_6800,       15,  0, 15, 15, 15, 15, 
            cpu.B = AddRegister(cpu.B, (uint8_t)_operand);
            break;
    }
}
void Execute6800Immediate16 ()
{
    uint16_t sResult;
    uint16_t sBefore;
    switch (_opCode)
    {
        case 0x8C:  // CPX
            sBefore = cpu.X;
            sResult = (uint16_t)(cpu.X - _operand);
            SetCCR_BeforeAndAfter (sResult, sBefore);
            break;
        case 0x8E:  // LDS
            cpu.SP = _operand;
            SetCCR_After (cpu.SP);
            break;
        case 0xCE:  // LDX
            cpu.X = _operand;
            SetCCR_After(cpu.X);
            break;
    }
}
void Execute6800Relative ()
{
    uint8_t bDoBranch = 0;
    int nNegative = (cpu.CCR & CCR_NEGATIVE) == 0 ? 0 : 1;
    int nZero     = (cpu.CCR & CCR_ZERO)     == 0 ? 0 : 1;;
    int nOverflow = (cpu.CCR & CCR_OVERFLOW) == 0 ? 0 : 1; ;
    int nCarry    = (cpu.CCR & CCR_CARRY)    == 0 ? 0 : 1;;
    switch (_opCode)
    {
        case 0x8D:      // BSR
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC % 256);      // push return address on the stack
            cpu.memory[cpu.SP--] = (uint8_t)(cpu.PC / 256);
            bDoBranch = 1;
            break;
        case 0x20:      // BRA
            bDoBranch = 1;
            break;
        case 0x22:      // BHI
            if ((nCarry | nZero) == 0)
                bDoBranch = 1;
            break;
        case 0x23:      // BLS
            if ((nCarry | nZero) == 1)
                bDoBranch = 1;
            break;
        case 0x24:      // BCC
            if (nCarry == 0)
                bDoBranch = 1;
            break;
        case 0x25:      // BCS
            if (nCarry == 1)
                bDoBranch = 1;
            break;
        case 0x26:      // BNE
            if (nZero == 0)
                bDoBranch = 1;
            break;
        case 0x27:      // BEQ
            if (nZero == 1)
                bDoBranch = 1;
            break;
        case 0x28:      // BVC
            if (nOverflow == 0)
                bDoBranch = 1;
            break;
        case 0x29:      // BVS
            if (nOverflow == 1)
                bDoBranch = 1;
            break;
        case 0x2A:      // BPL
            if (nNegative == 0)
                bDoBranch = 1;
            break;
        case 0x2B:      // BMI
            if (nNegative == 1)
                bDoBranch = 1;
            break;
        case 0x2C:      // BGE
            if ((nNegative ^ nOverflow) == 0)
                bDoBranch = 1;
            break;
        case 0x2D:      // BLT
            if ((nNegative ^ nOverflow) == 1)
                bDoBranch = 1;
            break;
        case 0x2E:      // BGT
            if ((nZero | (nNegative ^ nOverflow)) == 0)
                bDoBranch = 1;
            break;
        case 0x2F:      // BLE
            if ((nZero | (nNegative ^ nOverflow)) == 1)
                bDoBranch = 1;
            break;
    }
    if (bDoBranch == 1)
    {
        if (_operand > 127)
            _operand += 0xFF00;
        cpu.PC = (uint16_t)(cpu.PC + _operand);
    }
}
// #endregion

//  Fetch-Decode-Execute Loop
//      The main CPU execution loop:

void execute_instruction() 
{
    _opCode = cpu.memory[cpu.PC++];
    _cycles = opctbl[_opCode].cycles;
    _attribute = opctbl[_opCode].attribute;
    _numBytes  = opctbl[_opCode].numbytes;

    switch (opctbl[_opCode].numbytes)
    {
        case 1:
            _operand = 0;
            break;
        case 2:
            _operand = cpu.memory[cpu.PC++];
            break;
        case 3:
            _operand = (uint16_t)(cpu.memory[cpu.PC++] * 256);
            _operand += cpu.memory[cpu.PC++];
            break;
    }

    _cf = 0;
    _hf = 0;
    _vf = 0;

    switch (_attribute)
    {
        case AM_INHERENT_6800:  Execute6800Inherent();                                    break;
        case AM_DIRECT_6800:    Execute6800Direct();                                      break;
        case AM_RELATIVE_6800:  Execute6800Relative();                                    break;
        case AM_EXTENDED_6800:  Execute6800Extended();                                    break;
        case AM_IMM16_6800:     Execute6800Immediate16();                                 break;
        case AM_IMM8_6800:      Execute6800Immediate8();                                  break;
        case AM_INDEXED_6800:   Execute6800Indexed();                                     break;
        case AM_ILLEGAL:        printf("Illegal Addressing Mode detected - aborting\n");  break;
    }

    cyclesExecuted += _cycles;
}

//  Initializing and Running the Emulator
//      We need a function to load a ROM into memory and start execution:

void load_rom(const uint8_t rom[], uint16_t sizeOfSTX) 
{
    uint8_t  state   = gettingSTX;
    uint16_t address = 0;
    uint8_t  size    = 0;

    for (size_t i = 0; i < sizeOfSTX; i++) 
    {
        switch (state)
        {
            case gettingSTX:
                if (rom[i] == 0x02)
                    state = gettingAddressHi;
                break;
            case gettingAddressHi:
                address = rom[i] * 256;
                state = gettingAddressLo;
                break;
            case gettingAddressLo:
                address += rom[i];
                state = gettingSize;
                break;
            case gettingSize:
                size = rom[i];
                state = gettingData;
                break;
            case gettingData:
                if (size > 0)
                {
                    cpu.memory[address++] = rom[i];
                    size--;
                    if (size == 0)
                        state = gettingSTX;
                }
                else
                    state = gettingSTX;
                break;
        }
    }        

    // we will do this in the main loop now
    // cpu.PC = cpu.memory[0xFFFE] * 256 + cpu.memory[0xFFFF];
}

//    The 6800 Condition Code Register (CCR) is an 8-bit register that holds various flags reflecting the state of the 
//    processor after arithmetic and logical operations. The bit definitions are as follows:
//
//    6800 Condition Code Register (CCR) Bits
//    ---------------------------------------
//    Bit Position	Name	        Meaning
//    Bit 7	        S (Sign)	    Set if the result is negative (bit 7 of result = 1).
//    Bit 6	        X (Half Carry)	Set if there is a carry from bit 3 to bit 4 (used for BCD operations).
//    Bit 5	        Reserved	    Unused (always 0 on the 6800).
//    Bit 4	        H (Half Carry)	Duplicate of bit 6 (not separately used in the 6800).
//    Bit 3	        I (Interrupt)	Set to disable IRQ interrupts.
//    Bit 2	        N (Negative)	Duplicate of bit 7 (not separately used in the 6800).
//    Bit 1	        Z (Zero)	    Set if the result is zero.
//    Bit 0	        V (Overflow)	Set if signed overflow occurs.
//
//    Bit Breakdown
//    -------------
//    S (Sign) – Reflects the MSB (Most Significant Bit) of the result.
//        1 if the result is negative.
//        0 if the result is positive.
//
//    X/H (Half Carry) – Used in BCD (Binary-Coded Decimal) arithmetic.
//        Set when there is a carry from bit 3 to bit 4.
//
//    I (Interrupt Disable) – Controls IRQ interrupts.
//        1 = IRQs disabled.
//        0 = IRQs enabled.
//
//    Z (Zero) – Indicates a zero result.
//        1 = The result is zero.
//        0 = The result is non-zero.
//    
//    V (Overflow) – Indicates signed arithmetic overflow.
//        Set when a signed result is out of range (> 127 or < -128).
//
//    Example: Condition Code Register After Operations
//    -------------------------------------------------
//    Operation	                    Binary Result	CCR (S X - H I - Z V)	Meaning
//    LDAA #$FF	                    1111 1111	         1 0 - 0 0 - 0 0	Sign flag set (negative result).
//    LDAA #$00	                    0000 0000	         0 0 - 0 0 - 1 0	Zero flag set.
//    ADDA #$45 (Overflow occurs)	0110 0000	         0 0 - 0 0 - 0 1	Overflow flag set.

BREAKPOINT breakpoints [] =
{
    // 0xBED5, "BED5  B6 8014 READ3      LDA A    >DRVREG                READ FAST STATUS REGISTER"    , false, 
    // 0xBEDA, "BEDA  27 F9              BEQ      READ3                  TEST FOR DISK BUSY"           , true,
    // 0xBEE7, "BEE7  F6 8018 ERTST      LDA B    >COMREG                READ STATUS"                  , false, 
    // 0xBEEE, "BEEE  C5 1C   ER2        BIT B    #$1C                   ONLY TEST  ERROR CONDITIONS"  , true,
    // 0xBF8E, "BF8E  BD BEE7            JSR      ERTST                  NOT BUSY CHECK FOR ERRORS"    , false,
    // 0xBF8A, "BF8A  2B 08              BMI      WRIT5                  TEST FOR DRQ"                 , false
};

// ALL this code got moved to the main emu6800.c file so we could implement a reset and power cycle button
// void run_emulator() 
// {
//     int numberOfBreakPointEntries = sizeof(breakpoints) / sizeof(BREAKPOINT);    // there are 8 bytes per entry - 4 for 

//     cpu.A   = 0;
//     cpu.B   = 0;
//     cpu.X   = 0;
//     cpu.SP  = 0;
//     cpu.PC  = 0;
//     cpu.CCR = 0;

//     inWait = 0;

//     // we are going to pick the rom to load based on the configuration set by GPIO18 through GPIO21.
//     //
//     //      using the following logic:
//     //
//     //      configuraiton   0   = swtbuga_v1_303        this is for emulating the PT68-1 (has SD Card)
//     //                      1   = newbug                this is for emulating SWTPC without SD Card
//     //                      2   = swtbug_justtherom     this is for emulating CP68 without SD Card

//     switch (selectedConfiguration)
//     {
//         case 0:     printf("loading swtbuga_v1_303\n");     load_rom(swtbuga_v1_303,    sizeof(swtbuga_v1_303));    break;
//         case 1:     printf("loading newbug\n");             load_rom(newbug,            sizeof(newbug));            break;
//         case 2:     printf("loading swtbug_justtherom\n");  load_rom(swtbug_justtherom, sizeof(swtbug_justtherom)); break;

//         default:    printf("loading swtbuga_v1_303\n");     load_rom(swtbuga_v1_303,    sizeof(swtbuga_v1_303));    break;
//     }

//     while (1) 
//     {
//         for (int i = 0; i < numberOfBreakPointEntries; i++)
//         {
//             // before we execute the next instruciton - see if it is in the list of breakpoints
//             if (breakpoints[i].address == cpu.PC)
//             {
//                 // if it is - see if we should show the registers and the instruction line from the listing file
//                 if (breakpoints[i].printLine)
//                 {
//                     // show the registers and the instruction line from the listing file
//                     printf("\n0x%04X: X=%04X A=%02X B=%02X CCR=%02x %s", cpu.PC, cpu.X, cpu.A, cpu.B, cpu.CCR, breakpoints[i].description);
//                 }
//                 break;
//             }
//         }
//         execute_instruction();
//         if (sendCycles)
//         {
//             tcp_request(cyclesPacketData, cyclesResponseBuffer, sizeof(cyclesPacketData));
//             sendCycles = false;
//         }
//     }
// }
