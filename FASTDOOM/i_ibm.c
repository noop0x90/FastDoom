//
// Copyright (C) 1993-1996 Id Software, Inc.
// Copyright (C) 1993-2008 Raven Software
// Copyright (C) 2016-2017 Alexey Khokholov (Nuke.YKT)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//  IBM DOS VGA graphics and key/mouse.
//

#include <string.h>
#include <dos.h>
#include <conio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "d_main.h"
#include "doomstat.h"
#include "r_local.h"
#include "sounds.h"
#include "i_system.h"
#include "i_sound.h"
#include "g_game.h"
#include "m_misc.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "ns_dpmi.h"
#include "ns_task.h"
#include "doomdef.h"
#include "doomstat.h"

#include "std_func.h"

#if defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT)
#include "i_vesa.h"
#endif

//
// Macros
//

#define DPMI_INT 0x31
#define SBARHEIGHT 32

//
// Code
//

void I_StartupNet(void);
void I_ShutdownNet(void);
void I_ReadExternDriver(void);

typedef struct
{
    unsigned edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    unsigned short flags, es, ds, fs, gs, ip, cs, sp, ss;
} dpmiregs_t;

extern dpmiregs_t dpmiregs;

void I_ReadMouse(void);

extern int usemouse;

//
// Constants
//

#define SC_INDEX 0x3C4
#define SC_DATA 0x3C5
#define SC_RESET 0
#define SC_CLOCK 1
#define SC_MAPMASK 2
#define SC_CHARMAP 3
#define SC_MEMMODE 4

#define CRTC_INDEX 0x3D4
#define CRTC_DATA 0x3D5
#define CRTC_H_TOTAL 0
#define CRTC_H_DISPEND 1
#define CRTC_H_BLANK 2
#define CRTC_H_ENDBLANK 3
#define CRTC_H_RETRACE 4
#define CRTC_H_ENDRETRACE 5
#define CRTC_V_TOTAL 6
#define CRTC_OVERFLOW 7
#define CRTC_ROWSCAN 8
#define CRTC_MAXSCANLINE 9
#define CRTC_CURSORSTART 10
#define CRTC_CURSOREND 11
#define CRTC_STARTHIGH 12
#define CRTC_STARTLOW 13
#define CRTC_CURSORHIGH 14
#define CRTC_CURSORLOW 15
#define CRTC_V_RETRACE 16
#define CRTC_V_ENDRETRACE 17
#define CRTC_V_DISPEND 18
#define CRTC_OFFSET 19
#define CRTC_UNDERLINE 20
#define CRTC_V_BLANK 21
#define CRTC_V_ENDBLANK 22
#define CRTC_MODE 23
#define CRTC_LINECOMPARE 24

#define GC_INDEX 0x3CE
#define GC_DATA 0x3CF
#define GC_SETRESET 0
#define GC_ENABLESETRESET 1
#define GC_COLORCOMPARE 2
#define GC_DATAROTATE 3
#define GC_READMAP 4
#define GC_MODE 5
#define GC_MISCELLANEOUS 6
#define GC_COLORDONTCARE 7
#define GC_BITMASK 8

#define ATR_INDEX 0x3c0
#define ATR_MODE 16
#define ATR_OVERSCAN 17
#define ATR_COLORPLANEENABLE 18
#define ATR_PELPAN 19
#define ATR_COLORSELECT 20

#define STATUS_REGISTER_1 0x3da

#define PEL_WRITE_ADR 0x3c8
#define PEL_READ_ADR 0x3c7
#define PEL_DATA 0x3c9
#define PEL_MASK 0x3c6

#define SYNC_RESET 0
#define MAP_MASK 2
#define MEMORY_MODE 4

#define READ_MAP 4
#define GRAPHICS_MODE 5
#define MISCELLANOUS 6

#define MISC_OUTPUT 0x3C2

#define MAX_SCAN_LINE 9
#define UNDERLINE 0x14
#define MODE_CONTROL 0x17

#define VBLCOUNTER 34000 // hardware tics to a frame

#define TIMERINT 8
#define KEYBOARDINT 9

#define CRTCOFF (_inbyte(STATUS_REGISTER_1) & 1)
#define CLI _disable()
#define STI _enable()

#define _outbyte(x, y) (outp(x, y))
#define _outhword(x, y) (outpw(x, y))

#define _inbyte(x) (inp(x))
#define _inhword(x) (inpw(x))

#define MOUSEB1 1
#define MOUSEB2 2
#define MOUSEB3 4

byte mousepresent;

int ticcount;
fixed_t fps;

// REGS stuff used for int calls
union REGS regs;
struct SREGS segregs;

#define KBDQUESIZE 32
byte keyboardque[KBDQUESIZE];
int kbdtail, kbdhead;

#define KEY_LSHIFT 0xfe

#define KEY_INS (0x80 + 0x52)
#define KEY_DEL (0x80 + 0x53)
#define KEY_PGUP (0x80 + 0x49)
#define KEY_PGDN (0x80 + 0x51)
#define KEY_HOME (0x80 + 0x47)
#define KEY_END (0x80 + 0x4f)

#define SC_RSHIFT 0x36
#define SC_LSHIFT 0x2a
void DPMIInt(int i);
void I_StartupSound(void);
void I_ShutdownSound(void);
void I_ShutdownTimer(void);

#if defined(MODE_VGA16) || defined(MODE_CGA16) || defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_PCP) || defined(MODE_CVB)
byte lut16colors[14 * 256];
byte *ptrlut16colors;
#endif

#if defined(MODE_EGA)
byte lutRcolor[14 * 256];
byte lutGcolor[14 * 256];
byte lutBcolor[14 * 256];
byte lutIcolor[14 * 256];
byte *ptrlutRcolor;
byte *ptrlutGcolor;
byte *ptrlutBcolor;
byte *ptrlutIcolor;
#endif

#if defined(MODE_CGA)
byte lut4colors[14 * 256];
byte *ptrlut4colors;
#endif

#if defined(MODE_CGA_BW) || defined(MODE_HERC) || defined(MODE_EGA640) || defined(MODE_ATI640)
byte lutcolors00[14 * 256];
byte lutcolors01[14 * 256];
byte lutcolors10[14 * 256];
byte lutcolors11[14 * 256];
byte *ptrlutcolors00;
byte *ptrlutcolors01;
byte *ptrlutcolors10;
byte *ptrlutcolors11;
#endif

#if defined(MODE_V) || defined(MODE_V2)
int lutplane0[(320 * 350) / 4];
int lutplane1[(320 * 350) / 4];
int lutplane2[(320 * 350) / 4];
int lutplane3[(320 * 350) / 4];
#endif

byte gammatable[5][256] =
    {
        {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49, 49, 50, 50, 50, 50, 51, 51, 51, 51, 52, 52, 52, 52, 53, 53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 55, 56, 56, 56, 56, 57, 57, 57, 57, 58, 58, 58, 58, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 62, 62, 62, 62, 63, 63, 63, 63},
        {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49, 49, 49, 50, 50, 50, 50, 51, 51, 51, 51, 51, 52, 52, 52, 52, 53, 53, 53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 55, 55, 56, 56, 56, 56, 57, 57, 57, 57, 57, 58, 58, 58, 58, 59, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 61, 62, 62, 62, 62, 63, 63, 63, 63, 63},
        {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 46, 46, 46, 46, 47, 47, 47, 47, 47, 48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63},
        {2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 48, 48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 50, 51, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58, 58, 59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63},
        {4, 5, 7, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 15, 15, 16, 16, 17, 17, 17, 18, 18, 19, 19, 20, 20, 20, 21, 21, 21, 22, 22, 23, 23, 23, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 32, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 48, 48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 50, 50, 51, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58, 58, 58, 58, 59, 59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63}};

#if defined(MODE_Y) || defined(MODE_13H) || defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT) || defined(MODE_V) || defined(MODE_V2)
byte processedpalette[14 * 768];
#endif

byte scantokey[128] =
    {
        //  0           1       2       3       4       5       6       7
        //  8           9       A       B       C       D       E       F
        0, 27, '1', '2', '3', '4', '5', '6',
        '7', '8', '9', '0', '-', '=', KEY_BACKSPACE, 9, // 0
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', '[', ']', 13, KEY_RCTRL, 'a', 's', // 1
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
        39, '`', KEY_LSHIFT, 92, 'z', 'x', 'c', 'v', // 2
        'b', 'n', 'm', ',', '.', '/', KEY_RSHIFT, '*',
        KEY_RALT, ' ', 0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, // 3
        KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, KEY_HOME,
        KEY_UPARROW, KEY_PGUP, '-', KEY_LEFTARROW, '5', KEY_RIGHTARROW, '+', KEY_END, //4
        KEY_DOWNARROW, KEY_PGDN, KEY_INS, KEY_DEL, 0, 0, 0, KEY_F11,
        KEY_F12, 0, 0, 0, 0, 0, 0, 0, // 5
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, // 6
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0 // 7
};

#if defined(MODE_VGA16)
const byte vga16palette[672] = {
    0x00,0x00,0x11,0x09,0x09,0x05,0x07,0x05,0x35,0x27,0x00,0x27,0x1e,0x06,0x06,0x2c,0x0c,0x00,0x10,0x17,0x0b,0x1a,0x15,0x10,0x3b,0x1a,0x07,0x30,0x1f,0x3f,0x27,0x26,0x25,0x38,0x24,0x1d,0x16,0x31,0x14,0x2f,0x2a,0x21,0x3c,0x39,0x12,0x3a,0x37,0x36,0x0f,0x00,0x14,0x0e,0x07,0x05,0x0b,0x05,0x2f,0x22,0x08,0x05,0x2d,0x0a,0x00,0x1c,0x12,0x0e,0x12,0x17,0x08,0x3c,0x0e,0x35,0x3b,0x16,0x06,0x2b,0x21,0x20,0x39,0x20,0x1a,0x2a,0x22,0x38,0x1b,0x2c,0x12,0x30,0x25,0x1c,0x3c,0x32,0x10,0x3c,0x31,0x30,0x12,0x00,0x13,0x1e,0x02,0x0c,0x14,0x06,0x05,0x12,0x04,0x2b,0x27,0x08,0x04,0x20,0x10,0x0d,0x19,0x16,0x08,0x3c,0x0c,0x2e,0x3b,0x13,0x05,0x38,0x16,0x16,0x2c,0x1e,0x31,0x37,0x1e,0x18,0x20,0x28,0x11,0x35,0x27,0x27,0x38,0x2b,0x1f,0x3e,0x2e,0x0e,0x19,0x00,0x12,0x19,0x05,0x04,0x1a,0x06,0x25,0x29,0x07,0x04,0x32,0x06,0x00,0x25,0x0b,0x09,0x24,0x11,0x0d,0x3b,0x0c,0x02,0x3d,0x0a,0x28,0x39,0x17,0x12,0x32,0x19,0x17,0x3a,0x1a,0x0a,0x25,0x24,0x0f,0x34,0x1f,0x2a,0x3e,0x24,0x22,0x3f,0x29,0x10,0x1f,0x04,0x05,0x1e,0x02,0x21,0x20,0x04,0x15,0x32,0x05,0x00,0x2b,0x07,0x04,0x23,0x0a,0x03,0x2b,0x0d,0x0d,0x3d,0x09,0x01,0x3d,0x07,0x21,0x39,0x12,0x04,0x3a,0x13,0x10,0x34,0x17,0x16,0x34,0x17,0x23,0x2a,0x1e,0x0c,0x3e,0x1f,0x1e,0x3f,0x22,0x0d,0x23,0x00,0x09,0x27,0x04,0x02,0x27,0x03,0x19,0x32,0x03,0x00,0x30,0x07,0x05,0x2c,0x0b,0x06,0x2f,0x0a,0x0c,0x3c,0x07,0x01,0x3a,0x0f,0x0c,0x3a,0x10,0x1b,0x36,0x12,0x12,0x3c,0x13,0x05,0x2f,0x18,0x0a,0x3e,0x19,0x18,0x3f,0x1c,0x02,0x3f,0x1c,0x0f,0x2a,0x00,0x05,0x2d,0x00,0x14,0x2d,0x03,0x02,0x2c,0x03,0x0e,0x34,0x02,0x01,0x34,0x07,0x07,0x3d,0x05,0x01,0x31,0x0b,0x06,0x3c,0x0b,0x09,0x3c,0x0c,0x05,0x39,0x0d,0x0d,0x3b,0x0c,0x14,0x33,0x14,0x08,0x3f,0x12,0x11,0x3f,0x14,0x0a,0x3f,0x15,0x04,0x32,0x00,0x03,0x34,0x00,0x00,0x33,0x00,0x0c,0x39,0x00,0x01,0x37,0x03,0x02,0x3f,0x00,0x0d,0x37,0x03,0x06,0x3c,0x05,0x05,0x3e,0x05,0x00,0x35,0x08,0x02,0x3d,0x07,0x02,0x39,0x0a,0x05,0x3a,0x09,0x09,0x3f,0x09,0x08,0x3e,0x0c,0x0e,0x3f,0x0c,0x0b,0x38,0x00,0x01,0x39,0x00,0x04,0x39,0x00,0x07,0x3c,0x00,0x00,0x3b,0x00,0x01,0x39,0x01,0x00,0x3c,0x00,0x04,0x3f,0x00,0x01,0x3f,0x01,0x04,0x3d,0x03,0x02,0x3f,0x02,0x07,0x3d,0x04,0x07,0x3b,0x05,0x01,0x3d,0x05,0x05,0x3f,0x05,0x00,0x3e,0x05,0x00,0x12,0x07,0x19,0x0d,0x0c,0x06,0x0c,0x0a,0x33,0x21,0x0b,0x06,0x2f,0x0e,0x01,0x1c,0x17,0x0f,0x15,0x1a,0x0d,0x3e,0x16,0x39,0x3b,0x1c,0x09,0x29,0x26,0x23,0x37,0x23,0x1a,0x29,0x28,0x3a,0x1b,0x31,0x13,0x31,0x2b,0x1f,0x3b,0x37,0x11,0x3a,0x36,0x32,0x0d,0x0b,0x15,0x14,0x11,0x08,0x11,0x0f,0x2e,0x2b,0x0b,0x21,0x35,0x0c,0x04,0x26,0x11,0x08,0x22,0x1a,0x0d,0x1c,0x1c,0x11,0x3d,0x18,0x33,0x38,0x22,0x0a,0x28,0x26,0x34,0x38,0x26,0x1a,0x1e,0x30,0x14,0x31,0x2b,0x1c,0x34,0x30,0x29,0x3b,0x38,0x11,0x16,0x14,0x19,0x19,0x15,0x09,0x25,0x11,0x16,0x17,0x14,0x29,0x36,0x12,0x06,0x35,0x11,0x26,0x2a,0x17,0x09,0x26,0x1e,0x0e,0x20,0x20,0x12,0x37,0x25,0x1c,0x2d,0x28,0x1c,0x38,0x27,0x14,0x2c,0x2a,0x2e,0x22,0x30,0x13,0x38,0x34,0x14,0x39,0x33,0x28,0x24,0x17,0x09,0x1c,0x19,0x18,0x1f,0x1b,0x0b,0x28,0x17,0x15,0x1e,0x1a,0x25,0x37,0x17,0x09,0x34,0x17,0x22,0x2d,0x1c,0x0b,0x29,0x21,0x0f,0x25,0x23,0x12,0x2e,0x2a,0x1b,0x37,0x27,0x1a,0x37,0x29,0x12,0x26,0x30,0x13,0x38,0x32,0x23,0x3a,0x35,0x14,0x10,0x08,0x00,0x0e,0x09,0x18,0x07,0x0e,0x05,0x06,0x0d,0x31,0x2f,0x09,0x00,0x1f,0x10,0x06,0x16,0x1a,0x0c,0x38,0x18,0x38,0x35,0x23,0x08,0x1f,0x27,0x37,0x23,0x28,0x20,0x12,0x30,0x10,0x32,0x27,0x19,0x2a,0x2d,0x1d,0x32,0x3a,0x10,0x33,0x38,0x30};

#endif

#if defined(MODE_Y) || defined(MODE_13H) || defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT) || defined(MODE_V) || defined(MODE_V2)
void I_ProcessPalette(byte *palette)
{
    int i;

    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 768; i += 4, palette += 4)
    {
        processedpalette[i] = ptr[*palette];
        processedpalette[i + 1] = ptr[*(palette + 1)];
        processedpalette[i + 2] = ptr[*(palette + 2)];
        processedpalette[i + 3] = ptr[*(palette + 3)];
    }
}
#endif

#if defined(MODE_CGA_BW) || defined(MODE_HERC)
void I_ProcessPalette(byte *palette)
{
    int i;

    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++, palette += 3)
    {
        byte r, g, b;

        r = ptr[*palette];
        g = ptr[*(palette + 1)];
        b = ptr[*(palette + 2)];

        if (r + g + b > 38)
        {
            lutcolors00[i] = 0xFF;
        }
        else
        {
            lutcolors00[i] = 0x00;
        }

        if (r + g + b > 115)
        {
            lutcolors01[i] = 0xFF;
        }
        else
        {
            lutcolors01[i] = 0x00;
        }

        if (r + g + b > 155)
        {
            lutcolors10[i] = 0xFF;
        }
        else
        {
            lutcolors10[i] = 0x00;
        }

        if (r + g + b > 77)
        {
            lutcolors11[i] = 0xFF;
        }
        else
        {
            lutcolors11[i] = 0x00;
        }
    }
}
#endif

#if defined(MODE_CGA16) || defined(MODE_EGA640) || defined(MODE_EGA) || defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100)
const byte colors[48] = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x2A,
    0x00, 0x2A, 0x00,
    0x00, 0x2A, 0x2A,
    0x2A, 0x00, 0x00,
    0x2A, 0x00, 0x2A,
    0x2A, 0x15, 0x00,
    0x2A, 0x2A, 0x2A,
    0x15, 0x15, 0x15,
    0x15, 0x15, 0x3F,
    0x15, 0x3F, 0x15,
    0x15, 0x3F, 0x3F,
    0x3F, 0x15, 0x15,
    0x3F, 0x15, 0x3F,
    0x3F, 0x3F, 0x15,
    0x3F, 0x3F, 0x3F};
#endif

#if defined(MODE_ATI640)
const byte colors[48] = { // Color      R G B I     G R I B
    0x00, 0x00, 0x00,     // Black      0 0 0 0     0 0 0 0
    0x00, 0x2A, 0x00,     // Green      0 1 0 0     1 0 0 0
    0x2A, 0x00, 0x00,     // Red        1 0 0 0     0 1 0 0
    0x2A, 0x15, 0x00,     // Brown      1 1 0 0     1 1 0 0
    0x15, 0x15, 0x15,     // Gray       0 0 0 1     0 0 1 0
    0x15, 0x3F, 0x15,     // L green    0 1 0 1     1 0 1 0
    0x3F, 0x15, 0x15,     // L red      1 0 0 1     0 1 1 0
    0x3F, 0x3F, 0x15,     // Yellow     1 1 0 1     1 1 1 0
    0x00, 0x00, 0x2A,     // Blue       0 0 1 0     0 0 0 1
    0x00, 0x2A, 0x2A,     // Cyan       0 1 1 0     1 0 0 1
    0x2A, 0x00, 0x2A,     // Magenta    1 0 1 0     0 1 0 1
    0x2A, 0x2A, 0x2A,     // L Gray     1 1 1 0     1 1 0 1
    0x15, 0x15, 0x3F,     // L blue     0 0 1 1     0 0 1 1
    0x15, 0x3F, 0x3F,     // L cyan     0 1 1 1     1 0 1 1
    0x3F, 0x15, 0x3F,     // L magenta  1 0 1 1     0 1 1 1
    0x3F, 0x3F, 0x3F};    // White      1 1 1 1     1 1 1 1
#endif

#ifdef MODE_CVB
const byte colors[48] = { // standard IBM CGA
    0x00, 0x00, 0x00,
    0x00, 0x18, 0x06,
    0x09, 0x0a, 0x2f,
    0x04, 0x26, 0x37,
    0x20, 0x03, 0x15,
    0x1c, 0x1c, 0x1c,
    0x2c, 0x0e, 0x3f,
    0x26, 0x2a, 0x3f,
    0x12, 0x12, 0x00,
    0x0f, 0x2e, 0x00,
    0x1c, 0x1c, 0x1c,
    0x18, 0x3b, 0x21,
    0x37, 0x15, 0x04,
    0x35, 0x31, 0x07,
    0x3f, 0x20, 0x3a,
    0x3f, 0x3f, 0x3f};
/*const byte colors[48] = {  // ATi Small Wonder
    0x00, 0x00, 0x00,
    0x22, 0x04, 0x00,
    0x06, 0x15, 0x00,
    0x26, 0x1f, 0x00,
    0x03, 0x0f, 0x23,
    0x16, 0x17, 0x15,
    0x00, 0x2b, 0x00,
    0x1a, 0x38, 0x00,
    0x18, 0x01, 0x36,
    0x3f, 0x07, 0x31,
    0x14, 0x15, 0x13,
    0x3f, 0x22, 0x0e,
    0x0a, 0x13, 0x3f,
    0x39, 0x1e, 0x3f,
    0x07, 0x32, 0x3f,
    0x3f, 0x3f, 0x3f};*/
#endif

#ifdef MODE_PCP
const byte colors[48] = {
    0x00, 0x00, 0x00,
    0x00, 0x2A, 0x00,
    0x2A, 0x00, 0x00,
    0x2A, 0x15, 0x00,
    0x15, 0x15, 0x15,
    0x15, 0x3F, 0x15,
    0x3F, 0x15, 0x15,
    0x3F, 0x3F, 0x15,
    0x00, 0x00, 0x2A,
    0x00, 0x2A, 0x2A,
    0x2A, 0x00, 0x2A,
    0x2A, 0x2A, 0x2A,
    0x15, 0x15, 0x3F,
    0x15, 0x3F, 0x3F,
    0x3F, 0x15, 0x3F,
    0x3F, 0x3F, 0x3F};
#endif

#ifdef MODE_CGA
const byte colors[12] = {
    0x00, 0x00, 0x00,
    0x00, 0x2A, 0x00,
    0x2A, 0x00, 0x00,
    0x2A, 0x15, 0x00};
#endif

#if defined(MODE_VGA16) || defined(MODE_CGA16) || defined(MODE_CGA) || defined(MODE_EGA640) || defined(MODE_EGA) || defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_PCP) || defined(MODE_CVB) || defined(MODE_ATI640)

int I_SQRT(int x)
{
    int start = 1, end = x / 2, ans;

    if (x == 0 || x == 1)
        return x;

    while (start <= end)
    {
        int mid = (start + end) / 2;

        if (mid * mid == x)
            return mid;

        if (mid <= x / mid)
        {
            start = mid + 1;
            ans = mid;
        }
        else
            end = mid - 1;
    }
    return ans;
}

#endif

#if defined(MODE_EGA640) || defined(MODE_ATI640)

int I_GetClosestColor(int r1, int g1, int b1)
{
    int i;

    int result;

    int distance;
    int best_difference = MAXINT;

    for (i = 0; i < 16; i++)
    {
        int r2, g2, b2;
        int cR, cG, cB;
        int pos = i * 3;

        r2 = (int)colors[pos];
        cR = abs(r2 - r1);

        g2 = (int)colors[pos + 1];
        cG = abs(g2 - g1);

        b2 = (int)colors[pos + 2];
        cB = abs(b2 - b1);

        distance = cR + cG + cB;

        if (distance == 0)
        {
            return i;
        }

        distance = I_SQRT(distance);

        if (best_difference > distance)
        {
            best_difference = distance;
            result = i;
        }
    }

    return result;
}

void I_ProcessPalette(byte *palette)
{
    int i;

    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++, palette += 3)
    {
        unsigned char color;
        int r, g, b;
        int r2, g2, b2;

        r = (int)ptr[*palette];
        g = (int)ptr[*(palette + 1)];
        b = (int)ptr[*(palette + 2)];

        r2 = (r * 0) / 4 + r;
        g2 = (g * 0) / 4 + g;
        b2 = (b * 0) / 4 + b;

        color = I_GetClosestColor(r2, g2, b2);
        color |= color << 4;

        lutcolors00[i] = color;

        r2 = (r * 2) / 4 + r;
        g2 = (g * 2) / 4 + g;
        b2 = (b * 2) / 4 + b;

        color = I_GetClosestColor(r2, g2, b2);
        color |= color << 4;

        lutcolors01[i] = color;

        r2 = (r * 3) / 4 + r;
        g2 = (g * 3) / 4 + g;
        b2 = (b * 3) / 4 + b;

        color = I_GetClosestColor(r2, g2, b2);
        color |= color << 4;

        lutcolors10[i] = color;

        r2 = (r * 1) / 4 + r;
        g2 = (g * 1) / 4 + g;
        b2 = (b * 1) / 4 + b;

        color = I_GetClosestColor(r2, g2, b2);
        color |= color << 4;

        lutcolors11[i] = color;
    }
}
#endif

#if defined(MODE_VGA16)
void I_ProcessPalette(byte *palette)
{
    int i, j, k;
    byte *ptr = gammatable[usegamma];

    // PLAYPAL
    for (i = 0; i < 14 * 256; i++)
    {
        int distance;

        int r1, g1, b1;

        int best_difference = MAXINT;

        k = i / 256;

        r1 = (int)ptr[*palette++];
        g1 = (int)ptr[*palette++];
        b1 = (int)ptr[*palette++];

        for (j = 0; j < 16; j++)
        {
            int r2, g2, b2;
            int cR, cG, cB;
            int pos = (k * 48) + (j * 3);

            r2 = (int)(vga16palette[pos]);
            cR = abs(r2 - r1);

            g2 = (int)(vga16palette[pos + 1]);
            cG = abs(g2 - g1);

            b2 = (int)(vga16palette[pos + 2]);
            cB = abs(b2 - b1);

            distance = cR + cG + cB;

            if (distance == 0)
            {
                lut16colors[i] = j;
                break;
            }

            distance = I_SQRT(distance);

            if (best_difference > distance)
            {
                best_difference = distance;
                lut16colors[i] = j;
            }
        }
    }
}
#endif

#if defined(MODE_CGA16) || defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_CVB)
void I_ProcessPalette(byte *palette)
{
    int i, j;
    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++)
    {
        int distance;

        int r1, g1, b1;

        int best_difference = MAXINT;

        r1 = (int)ptr[*palette++];
        g1 = (int)ptr[*palette++];
        b1 = (int)ptr[*palette++];

        for (j = 0; j < 16; j++)
        {
            int r2, g2, b2;
            int cR, cG, cB;
            int pos = j * 3;

            r2 = (int)colors[pos];
            cR = abs(r2 - r1);

            g2 = (int)colors[pos + 1];
            cG = abs(g2 - g1);

            b2 = (int)colors[pos + 2];
            cB = abs(b2 - b1);

            distance = cR + cG + cB;

            if (distance == 0)
            {
                lut16colors[i] = j;
                break;
            }

            distance = I_SQRT(distance);

            if (best_difference > distance)
            {
                best_difference = distance;
                lut16colors[i] = j;
            }
        }
    }
}
#endif

#if defined(MODE_PCP)
void I_ProcessPalette(byte *palette)
{
    int i, j;
    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++)
    {
        int distance;

        int r1, g1, b1;

        int best_difference = MAXINT;

        r1 = (int)ptr[*palette++];
        g1 = (int)ptr[*palette++];
        b1 = (int)ptr[*palette++];

        for (j = 0; j < 16; j++)
        {
            int r2, g2, b2;
            int cR, cG, cB;
            int pos = j * 3;

            r2 = (int)colors[pos];
            cR = abs(r2 - r1);

            g2 = (int)colors[pos + 1];
            cG = abs(g2 - g1);

            b2 = (int)colors[pos + 2];
            cB = abs(b2 - b1);

            distance = cR + cG + cB;

            if (distance == 0)
            {
                lut16colors[i] = j | j << 4;
                break;
            }

            distance = I_SQRT(distance);

            if (best_difference > distance)
            {
                best_difference = distance;
                lut16colors[i] = j | j << 4;
            }
        }
    }
}
#endif

#if defined(MODE_EGA)
void I_ProcessPalette(byte *palette)
{
    int i, j;
    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++)
    {
        int distance;

        int r1, g1, b1;

        int best_difference = MAXINT;

        r1 = (int)ptr[*palette++];
        g1 = (int)ptr[*palette++];
        b1 = (int)ptr[*palette++];

        for (j = 0; j < 16; j++)
        {
            int r2, g2, b2;
            int cR, cG, cB;
            int pos = j * 3;

            r2 = (int)colors[pos];
            cR = abs(r2 - r1);

            g2 = (int)colors[pos + 1];
            cG = abs(g2 - g1);

            b2 = (int)colors[pos + 2];
            cB = abs(b2 - b1);

            distance = cR + cG + cB;

            if (distance == 0)
            {
                // R
                if (j & 8)
                {
                    lutRcolor[i] = 0xFF;
                }
                else
                {
                    lutRcolor[i] = 0x00;
                }

                // G
                if (j & 4)
                {
                    lutGcolor[i] = 0xFF;
                }
                else
                {
                    lutGcolor[i] = 0x00;
                }

                // B
                if (j & 2)
                {
                    lutBcolor[i] = 0xFF;
                }
                else
                {
                    lutBcolor[i] = 0x00;
                }

                // I
                if (j & 1)
                {
                    lutIcolor[i] = 0xFF;
                }
                else
                {
                    lutIcolor[i] = 0x00;
                }

                break;
            }

            distance = I_SQRT(distance);

            if (best_difference > distance)
            {
                best_difference = distance;

                // R
                if (j & 8)
                {
                    lutRcolor[i] = 0xFF;
                }
                else
                {
                    lutRcolor[i] = 0x00;
                }

                // G
                if (j & 4)
                {
                    lutGcolor[i] = 0xFF;
                }
                else
                {
                    lutGcolor[i] = 0x00;
                }

                // B
                if (j & 2)
                {
                    lutBcolor[i] = 0xFF;
                }
                else
                {
                    lutBcolor[i] = 0x00;
                }

                // I
                if (j & 1)
                {
                    lutIcolor[i] = 0xFF;
                }
                else
                {
                    lutIcolor[i] = 0x00;
                }
            }
        }
    }
}
#endif

#if defined(MODE_CGA)
void I_ProcessPalette(byte *palette)
{
    int i, j;
    byte *ptr = gammatable[usegamma];

    for (i = 0; i < 14 * 256; i++)
    {
        int distance;

        int r1, g1, b1;

        int best_difference = MAXINT;

        r1 = (int)ptr[*palette++];
        g1 = (int)ptr[*palette++];
        b1 = (int)ptr[*palette++];

        for (j = 0; j < 4; j++)
        {
            int r2, g2, b2;
            int cR, cG, cB;
            int pos = j * 3;

            r2 = (int)colors[pos];
            cR = abs(r2 - r1);

            g2 = (int)colors[pos + 1];
            cG = abs(g2 - g1);

            b2 = (int)colors[pos + 2];
            cB = abs(b2 - b1);

            distance = cR + cG + cB;

            if (distance == 0)
            {
                lut4colors[i] = j | j << 2 | j << 4 | j << 6;
                break;
            }

            distance = I_SQRT(distance);

            if (best_difference > distance)
            {
                best_difference = distance;
                lut4colors[i] = j | j << 2 | j << 4 | j << 6;
            }
        }
    }
}
#endif

//
// I_SetPalette
// Palette source must use 8 bit RGB elements.
//
void I_SetPalette(int numpalette)
{

#if defined(MODE_CGA_BW) || defined(MODE_HERC) || defined(MODE_EGA640) || defined(MODE_ATI640)
    ptrlutcolors00 = lutcolors00 + numpalette * 256;
    ptrlutcolors01 = lutcolors01 + numpalette * 256;
    ptrlutcolors10 = lutcolors10 + numpalette * 256;
    ptrlutcolors11 = lutcolors11 + numpalette * 256;
#endif

#if defined(MODE_VGA16) || defined(MODE_CGA16) || defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100) || defined(MODE_PCP) || defined(MODE_CVB)
    ptrlut16colors = lut16colors + numpalette * 256;
#endif

#if defined(MODE_CGA)
    ptrlut4colors = lut4colors + numpalette * 256;
#endif

#if defined(MODE_EGA)
    ptrlutRcolor = lutRcolor + numpalette * 256;
    ptrlutGcolor = lutGcolor + numpalette * 256;
    ptrlutBcolor = lutBcolor + numpalette * 256;
    ptrlutIcolor = lutIcolor + numpalette * 256;
#endif

#if defined(MODE_VGA16)
    {
        /*int i;
        byte *ptrvga16palette;

        ptrvga16palette = vga16palette + numpalette * 48;

        for (i = 0; i < 16; i++, ptrvga16palette += 3)
        {
            int j;
            // reset Attribute Controller (AC) flip-flop so 0x3C0 is AC address
            (void)inp(0x3DA);
            // set AC address = i. You must clear bit b5 here to modify the palettes.
            // Clearing this bit will also blank the display.
            outp(0x3C0, i);
            // read AC register #i
            //THIS is the index into the 256-color palette, not 'i'
            j = inp(0x3C1);

            // set the 256-color palette (DAC) entry. Only the bottom 6 bits of each
            // R, G, and B entry are significant, so we divide by 4. Change RED, GREEN,
            // and BLUE to whatever is appropriate for your code.
            outp(0x3C8, j);
            outp(0x3C9, *(ptrvga16palette));
            outp(0x3C9, *(ptrvga16palette + 1));
            outp(0x3C9, *(ptrvga16palette + 2));
        }
        // set bit b5 in AC address register to lock palette and unblank display
        (void)inp(0x3DA);
        outp(0x3C0, 0x20);*/

        int i;
        byte *ptrvga16palette;
        const byte palposition[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };

        ptrvga16palette = vga16palette + numpalette * 48;

        for (i = 0; i < 16; i++, ptrvga16palette += 3)
        {
            outp(0x3C8, palposition[i]);
            outp(0x3C9, *(ptrvga16palette));
            outp(0x3C9, *(ptrvga16palette + 1));
            outp(0x3C9, *(ptrvga16palette + 2));
        }
    }
#endif

#if defined(MODE_Y) || defined(MODE_13H) || defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT) || defined(MODE_V) || defined(MODE_V2)
    {
        int pos = Mul768(numpalette);

        _outbyte(PEL_WRITE_ADR, 0);

        OutString(PEL_DATA, ((unsigned char *)processedpalette) + pos, 768);
    }
#endif
}

//
// Graphics mode
//

#if defined(USE_BACKBUFFER)
int updatestate;
#endif
byte *pcscreen, *currentscreen, *destscreen, *destview;

#if defined(MODE_EGA) || defined(MODE_VBE2_DIRECT) || defined(MODE_EGA640)
byte page = 0;
#endif

#if defined(MODE_T8025) || defined(MODE_T8050) || defined(MODE_T4025) || defined(MODE_T4050) || defined(MODE_T80100)
unsigned short *textdestscreen = (unsigned short *)0xB8000;
byte textpage = 0;
#endif

//
// I_UpdateBox
//
#ifdef MODE_VBE2_DIRECT
void I_UpdateBox(int x, int y, int w, int h)
{
    byte *dest;
    byte *source;
    int i;

    dest = destscreen + Mul320(y) + x;
    source = screen0 + Mul320(y) + x;

    for (i = y; i < y + h; i++)
    {
        CopyBytes(source, dest, w);
        dest += 320;
        source += 320;
    }
}
#endif

#ifdef MODE_Y
void I_UpdateBox(int x, int y, int w, int h)
{
    int i, j, k, count;
    int sp_x1, sp_x2;
    int poffset;
    int offset;
    int pstep;
    int step;
    byte *dest, *source;

    outp(SC_INDEX, SC_MAPMASK);

    sp_x1 = x / 8;
    sp_x2 = (x + w) / 8;
    count = sp_x2 - sp_x1 + 1;
    step = SCREENWIDTH - count * 8;
    offset = Mul320(y) + sp_x1 * 8;
    poffset = offset / 4;
    pstep = step / 4;

    if (count & 1)
    {
        // 16-bit copy

        count = 2 * count;

        for (i = 0; i < 4; i++)
        {
            outp(SC_INDEX + 1, 1 << i);
            source = &screen0[offset + i];
            dest = destscreen + poffset;

            for (j = 0; j < h; j++)
            {
                k = dest + count;

                while (dest < k)
                {
                    *(unsigned short *)dest = (unsigned short)((*source) + ((*(source + 4)) << 8));
                    dest += 2;
                    source += 8;
                }

                source += step;
                dest += pstep;
            }
        }
    }
    else
    {
        // 32-bit copy

        count = 2 * count;

        for (i = 0; i < 4; i++)
        {
            outp(SC_INDEX + 1, 1 << i);
            source = &screen0[offset + i];
            dest = destscreen + poffset;

            for (j = 0; j < h; j++)
            {
                k = dest + count;

                while (dest < k)
                {
                    *(unsigned int *)dest = (unsigned int)((*source) + ((*(source + 4)) << 8) + ((*(source + 8)) << 16) + ((*(source + 12)) << 24));
                    dest += 4;
                    source += 16;
                }

                source += step;
                dest += pstep;
            }
        }
    }
}
#endif

//
// I_UpdateNoBlit
//
#if defined(MODE_Y) || defined(MODE_VBE2_DIRECT)
int olddb[2][4];
void I_UpdateNoBlit(void)
{
    int realdr[4];

    // Set current screen
    currentscreen = destscreen;

    // Update dirtybox size
    realdr[BOXTOP] = dirtybox[BOXTOP];
    if (realdr[BOXTOP] < olddb[0][BOXTOP])
    {
        realdr[BOXTOP] = olddb[0][BOXTOP];
    }
    if (realdr[BOXTOP] < olddb[1][BOXTOP])
    {
        realdr[BOXTOP] = olddb[1][BOXTOP];
    }

    realdr[BOXRIGHT] = dirtybox[BOXRIGHT];
    if (realdr[BOXRIGHT] < olddb[0][BOXRIGHT])
    {
        realdr[BOXRIGHT] = olddb[0][BOXRIGHT];
    }
    if (realdr[BOXRIGHT] < olddb[1][BOXRIGHT])
    {
        realdr[BOXRIGHT] = olddb[1][BOXRIGHT];
    }

    realdr[BOXBOTTOM] = dirtybox[BOXBOTTOM];
    if (realdr[BOXBOTTOM] > olddb[0][BOXBOTTOM])
    {
        realdr[BOXBOTTOM] = olddb[0][BOXBOTTOM];
    }
    if (realdr[BOXBOTTOM] > olddb[1][BOXBOTTOM])
    {
        realdr[BOXBOTTOM] = olddb[1][BOXBOTTOM];
    }

    realdr[BOXLEFT] = dirtybox[BOXLEFT];
    if (realdr[BOXLEFT] > olddb[0][BOXLEFT])
    {
        realdr[BOXLEFT] = olddb[0][BOXLEFT];
    }
    if (realdr[BOXLEFT] > olddb[1][BOXLEFT])
    {
        realdr[BOXLEFT] = olddb[1][BOXLEFT];
    }

    // Leave current box for next update
    //CopyDWords(olddb[1], olddb[0], 4);
    //CopyDWords(dirtybox, olddb[1], 4);
    //memcpy(olddb[0], olddb[1], 16);
    //memcpy(olddb[1], dirtybox, 16);

    olddb[0][0] = olddb[1][0];
    olddb[0][1] = olddb[1][1];
    olddb[0][2] = olddb[1][2];
    olddb[0][3] = olddb[1][3];
    olddb[1][0] = dirtybox[0];
    olddb[1][1] = dirtybox[1];
    olddb[1][2] = dirtybox[2];
    olddb[1][3] = dirtybox[3];

    // Update screen
    if (realdr[BOXBOTTOM] <= realdr[BOXTOP])
    {
        int x, y, w, h;

        x = realdr[BOXLEFT];
        w = realdr[BOXRIGHT] - x + 1;

        y = realdr[BOXBOTTOM];
        h = realdr[BOXTOP] - y + 1;

        I_UpdateBox(x, y, w, h);
    }
    // Clear box
    dirtybox[BOXTOP] = dirtybox[BOXRIGHT] = MININT;
    dirtybox[BOXBOTTOM] = dirtybox[BOXLEFT] = MAXINT;
}
#endif

//
// I_FinishUpdate
//

extern int screenblocks;

#ifdef MODE_CGA_BW
void CGA_BW_DrawBackbuffer(void)
{
    int x;
    unsigned char *vram = (unsigned char *)0xB8000;
    byte *ptrbackbuffer;

    for (ptrbackbuffer = backbuffer; ptrbackbuffer < backbuffer + 200 * 640 / 2; ptrbackbuffer += 640, vram += 80)
    {
        for (x = 0; x < 320; x += 4)
        {
            byte color;
            byte finalcolor;

            color = ptrbackbuffer[x];
            finalcolor = (ptrlutcolors00[color]) & 0x80 | (ptrlutcolors10[color]) & 0x40;
            color = ptrbackbuffer[x + 1];
            finalcolor |= (ptrlutcolors00[color]) & 0x20 | (ptrlutcolors10[color]) & 0x10;
            color = ptrbackbuffer[x + 2];
            finalcolor |= (ptrlutcolors00[color]) & 0x08 | (ptrlutcolors10[color]) & 0x04;
            color = ptrbackbuffer[x + 3];
            finalcolor |= (ptrlutcolors00[color]) & 0x02 | (ptrlutcolors10[color]) & 0x01;

            *(vram + (x / 4)) = finalcolor;

            color = ptrbackbuffer[x + 320];
            finalcolor = (ptrlutcolors01[color]) & 0x80 | (ptrlutcolors11[color]) & 0x40;
            color = ptrbackbuffer[x + 321];
            finalcolor |= (ptrlutcolors01[color]) & 0x20 | (ptrlutcolors11[color]) & 0x10;
            color = ptrbackbuffer[x + 322];
            finalcolor |= (ptrlutcolors01[color]) & 0x08 | (ptrlutcolors11[color]) & 0x04;
            color = ptrbackbuffer[x + 323];
            finalcolor |= (ptrlutcolors01[color]) & 0x02 | (ptrlutcolors11[color]) & 0x01;

            *(vram + 0x2000 + (x / 4)) = finalcolor;
        }
    }
}
#endif

#ifdef MODE_HERC
void HERC_DrawBackbuffer(void)
{
    int x;
    unsigned char *vram = (unsigned char *)0xB0000;
    byte *ptrbackbuffer;

    for (ptrbackbuffer = backbuffer; ptrbackbuffer < backbuffer + 400 * 640 / 4; ptrbackbuffer += 640, vram += 80)
    {
        for (x = 0; x < 320; x += 4)
        {
            byte color;
            byte finalcolor0;
            byte finalcolor1;

            color = ptrbackbuffer[x];
            finalcolor0 = (ptrlutcolors00[color]) & 0x80 | (ptrlutcolors10[color]) & 0x40;
            finalcolor1 = (ptrlutcolors01[color]) & 0x80 | (ptrlutcolors11[color]) & 0x40;
            color = ptrbackbuffer[x + 1];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x20 | (ptrlutcolors10[color]) & 0x10;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x20 | (ptrlutcolors11[color]) & 0x10;
            color = ptrbackbuffer[x + 2];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x08 | (ptrlutcolors10[color]) & 0x04;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x08 | (ptrlutcolors11[color]) & 0x04;
            color = ptrbackbuffer[x + 3];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x02 | (ptrlutcolors10[color]) & 0x01;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x02 | (ptrlutcolors11[color]) & 0x01;

            *(vram + (x / 4)) = finalcolor0;
            *(vram + 0x2000 + (x / 4)) = finalcolor1;

            color = ptrbackbuffer[x + 320];
            finalcolor0 = (ptrlutcolors00[color]) & 0x80 | (ptrlutcolors10[color]) & 0x40;
            finalcolor1 = (ptrlutcolors01[color]) & 0x80 | (ptrlutcolors11[color]) & 0x40;
            color = ptrbackbuffer[x + 321];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x20 | (ptrlutcolors10[color]) & 0x10;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x20 | (ptrlutcolors11[color]) & 0x10;
            color = ptrbackbuffer[x + 322];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x08 | (ptrlutcolors10[color]) & 0x04;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x08 | (ptrlutcolors11[color]) & 0x04;
            color = ptrbackbuffer[x + 323];
            finalcolor0 |= (ptrlutcolors00[color]) & 0x02 | (ptrlutcolors10[color]) & 0x01;
            finalcolor1 |= (ptrlutcolors01[color]) & 0x02 | (ptrlutcolors11[color]) & 0x01;

            *(vram + 0x4000 + (x / 4)) = finalcolor0;
            *(vram + 0x6000 + (x / 4)) = finalcolor1;
        }
    }
}
#endif

#ifdef MODE_CGA16
void CGA16_DrawBackbuffer(void)
{
    unsigned char *vram = (unsigned char *)0xB8000;
    int i;
    unsigned int base = 0;
    unsigned int line = 0;

    for (i = 1; i < 16000; i += 2)
    {
        unsigned char color0 = ptrlut16colors[backbuffer[base]];
        unsigned char color1 = ptrlut16colors[backbuffer[base + 2]];

        vram[i] = color0 << 4 | color1;

        line++;
        if (line == 80)
        {
            line = 0;
            base += 324;
        }
        else
        {
            base += 4;
        }
    }
}
#endif

#ifdef MODE_VGA16
void VGA16_DrawBackbuffer(void)
{
    unsigned char *vram = (unsigned char *)0xB8000;
    int i;
    unsigned int base = 0;
    unsigned int line = 0;

    for (i = 1; i < 32000; i += 2, base += 4)
    {
        unsigned char color0 = ptrlut16colors[backbuffer[base]];
        unsigned char color1 = ptrlut16colors[backbuffer[base + 2]];

        vram[i] = color0 << 4 | color1;
    }
}
#endif

#ifdef MODE_ATI640
void ATI640_DrawBackbuffer(void)
{
    int x;

    unsigned char *vram = (unsigned char *)0xB0000;

    unsigned int base = 0;

    for (base = 0; base < SCREENHEIGHT * 320;)
    {
        for (x = 0; x < 160; x++, base += 2, vram++)
        {
            unsigned char color;
            unsigned char tmpColor0;
            unsigned char tmpColor1;

            color = ptrlutcolors00[backbuffer[base]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlutcolors01[backbuffer[base]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlutcolors00[backbuffer[base + 1]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlutcolors01[backbuffer[base + 1]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram) = tmpColor0;
            *(vram + 0x8000) = tmpColor1;

            color = ptrlutcolors10[backbuffer[base + 320]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlutcolors11[backbuffer[base + 320]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlutcolors10[backbuffer[base + 321]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlutcolors11[backbuffer[base + 321]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram + 0x2000) = tmpColor0;
            *(vram + 0xA000) = tmpColor1;

            color = ptrlutcolors00[backbuffer[base + 640]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlutcolors01[backbuffer[base + 640]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlutcolors00[backbuffer[base + 641]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlutcolors01[backbuffer[base + 641]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram + 0x4000) = tmpColor0;
            *(vram + 0xC000) = tmpColor1;

            color = ptrlutcolors10[backbuffer[base + 960]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlutcolors11[backbuffer[base + 960]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlutcolors10[backbuffer[base + 961]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlutcolors11[backbuffer[base + 961]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram + 0x6000) = tmpColor0;
            *(vram + 0xE000) = tmpColor1;
        }
        base += 960;
    }
}
#endif

#ifdef MODE_EGA640
void EGA640_DrawBackbuffer(void)
{
    byte plane_red[SCREENWIDTH * SCREENHEIGHT / 4];
    byte plane_green[SCREENWIDTH * SCREENHEIGHT / 4];
    byte plane_blue[SCREENWIDTH * SCREENHEIGHT / 4];
    byte plane_intensity[SCREENWIDTH * SCREENHEIGHT / 4];

    int x, y;
    unsigned int base = 0;
    unsigned int plane_position = 0;
    int line = 0;
    int odd = 0;

    // Chunky 2 planar conversion (hi Amiga fans!)

    for (base = 0; base < SCREENHEIGHT * SCREENWIDTH; base += 8)
    {
        unsigned char color;

        line++;
        if (line == 40)
        {
            line = 0;
            odd = !odd;
        }

        if (odd)
        {
            // ODD
            color = ptrlutcolors10[backbuffer[base]] & 0x80 | (ptrlutcolors11[backbuffer[base]] & 0x80) >> 1 | (ptrlutcolors10[backbuffer[base + 1]] & 0x80) >> 2 | (ptrlutcolors11[backbuffer[base + 1]] & 0x80) >> 3 | ptrlutcolors10[backbuffer[base + 2]] & 0x08 | (ptrlutcolors11[backbuffer[base + 2]] & 0x08) >> 1 | (ptrlutcolors10[backbuffer[base + 3]] & 0x08) >> 2 | (ptrlutcolors11[backbuffer[base + 3]] & 0x08) >> 3;
            plane_red[plane_position] = color;

            color = ptrlutcolors10[backbuffer[base + 4]] & 0x80 | (ptrlutcolors11[backbuffer[base + 4]] & 0x80) >> 1 | (ptrlutcolors10[backbuffer[base + 5]] & 0x80) >> 2 | (ptrlutcolors11[backbuffer[base + 5]] & 0x80) >> 3 | ptrlutcolors10[backbuffer[base + 6]] & 0x08 | (ptrlutcolors11[backbuffer[base + 6]] & 0x08) >> 1 | (ptrlutcolors10[backbuffer[base + 7]] & 0x08) >> 2 | (ptrlutcolors11[backbuffer[base + 7]] & 0x08) >> 3;
            plane_red[plane_position + 1] = color;

            color = (ptrlutcolors10[backbuffer[base]] & 0x40) << 1 | ptrlutcolors11[backbuffer[base]] & 0x40 | (ptrlutcolors10[backbuffer[base + 1]] & 0x40) >> 1 | (ptrlutcolors11[backbuffer[base + 1]] & 0x40) >> 2 | (ptrlutcolors10[backbuffer[base + 2]] & 0x04) << 1 | ptrlutcolors11[backbuffer[base + 2]] & 0x04 | (ptrlutcolors10[backbuffer[base + 3]] & 0x04) >> 1 | (ptrlutcolors11[backbuffer[base + 3]] & 0x04) >> 2;
            plane_green[plane_position] = color;

            color = (ptrlutcolors10[backbuffer[base + 4]] & 0x40) << 1 | ptrlutcolors11[backbuffer[base + 4]] & 0x40 | (ptrlutcolors10[backbuffer[base + 5]] & 0x40) >> 1 | (ptrlutcolors11[backbuffer[base + 5]] & 0x40) >> 2 | (ptrlutcolors10[backbuffer[base + 6]] & 0x04) << 1 | ptrlutcolors11[backbuffer[base + 6]] & 0x04 | (ptrlutcolors10[backbuffer[base + 7]] & 0x04) >> 1 | (ptrlutcolors11[backbuffer[base + 7]] & 0x04) >> 2;
            plane_green[plane_position + 1] = color;

            color = (ptrlutcolors10[backbuffer[base]] & 0x20) << 2 | (ptrlutcolors11[backbuffer[base]] & 0x20) << 1 | ptrlutcolors10[backbuffer[base + 1]] & 0x20 | (ptrlutcolors11[backbuffer[base + 1]] & 0x20) >> 1 | (ptrlutcolors10[backbuffer[base + 2]] & 0x02) << 2 | (ptrlutcolors11[backbuffer[base + 2]] & 0x02) << 1 | ptrlutcolors10[backbuffer[base + 3]] & 0x02 | (ptrlutcolors11[backbuffer[base + 3]] & 0x02) >> 1;
            plane_blue[plane_position] = color;

            color = (ptrlutcolors10[backbuffer[base + 4]] & 0x20) << 2 | (ptrlutcolors11[backbuffer[base + 4]] & 0x20) << 1 | ptrlutcolors10[backbuffer[base + 5]] & 0x20 | (ptrlutcolors11[backbuffer[base + 5]] & 0x20) >> 1 | (ptrlutcolors10[backbuffer[base + 6]] & 0x02) << 2 | (ptrlutcolors11[backbuffer[base + 6]] & 0x02) << 1 | ptrlutcolors10[backbuffer[base + 7]] & 0x02 | (ptrlutcolors11[backbuffer[base + 7]] & 0x02) >> 1;
            plane_blue[plane_position + 1] = color;

            color = (ptrlutcolors10[backbuffer[base]] & 0x10) << 3 | (ptrlutcolors11[backbuffer[base]] & 0x10) << 2 | (ptrlutcolors10[backbuffer[base + 1]] & 0x10) << 1 | ptrlutcolors11[backbuffer[base + 1]] & 0x10 | (ptrlutcolors10[backbuffer[base + 2]] & 0x01) << 3 | (ptrlutcolors11[backbuffer[base + 2]] & 0x01) << 2 | (ptrlutcolors10[backbuffer[base + 3]] & 0x01) << 1 | ptrlutcolors11[backbuffer[base + 3]] & 0x01;
            plane_intensity[plane_position] = color;

            color = (ptrlutcolors10[backbuffer[base + 4]] & 0x10) << 3 | (ptrlutcolors11[backbuffer[base + 4]] & 0x10) << 2 | (ptrlutcolors10[backbuffer[base + 5]] & 0x10) << 1 | ptrlutcolors11[backbuffer[base + 5]] & 0x10 | (ptrlutcolors10[backbuffer[base + 6]] & 0x01) << 3 | (ptrlutcolors11[backbuffer[base + 6]] & 0x01) << 2 | (ptrlutcolors10[backbuffer[base + 7]] & 0x01) << 1 | ptrlutcolors11[backbuffer[base + 7]] & 0x01;
            plane_intensity[plane_position + 1] = color;
        }
        else
        {
            // EVEN
            color = ptrlutcolors00[backbuffer[base]] & 0x80 | (ptrlutcolors01[backbuffer[base]] & 0x80) >> 1 | (ptrlutcolors00[backbuffer[base + 1]] & 0x80) >> 2 | (ptrlutcolors01[backbuffer[base + 1]] & 0x80) >> 3 | ptrlutcolors00[backbuffer[base + 2]] & 0x08 | (ptrlutcolors01[backbuffer[base + 2]] & 0x08) >> 1 | (ptrlutcolors00[backbuffer[base + 3]] & 0x08) >> 2 | (ptrlutcolors01[backbuffer[base + 3]] & 0x08) >> 3;
            plane_red[plane_position] = color;

            color = ptrlutcolors00[backbuffer[base + 4]] & 0x80 | (ptrlutcolors01[backbuffer[base + 4]] & 0x80) >> 1 | (ptrlutcolors00[backbuffer[base + 5]] & 0x80) >> 2 | (ptrlutcolors01[backbuffer[base + 5]] & 0x80) >> 3 | ptrlutcolors00[backbuffer[base + 6]] & 0x08 | (ptrlutcolors01[backbuffer[base + 6]] & 0x08) >> 1 | (ptrlutcolors00[backbuffer[base + 7]] & 0x08) >> 2 | (ptrlutcolors01[backbuffer[base + 7]] & 0x08) >> 3;
            plane_red[plane_position + 1] = color;

            color = (ptrlutcolors00[backbuffer[base]] & 0x40) << 1 | ptrlutcolors01[backbuffer[base]] & 0x40 | (ptrlutcolors00[backbuffer[base + 1]] & 0x40) >> 1 | (ptrlutcolors01[backbuffer[base + 1]] & 0x40) >> 2 | (ptrlutcolors00[backbuffer[base + 2]] & 0x04) << 1 | ptrlutcolors01[backbuffer[base + 2]] & 0x04 | (ptrlutcolors00[backbuffer[base + 3]] & 0x04) >> 1 | (ptrlutcolors01[backbuffer[base + 3]] & 0x04) >> 2;
            plane_green[plane_position] = color;

            color = (ptrlutcolors00[backbuffer[base + 4]] & 0x40) << 1 | ptrlutcolors01[backbuffer[base + 4]] & 0x40 | (ptrlutcolors00[backbuffer[base + 5]] & 0x40) >> 1 | (ptrlutcolors01[backbuffer[base + 5]] & 0x40) >> 2 | (ptrlutcolors00[backbuffer[base + 6]] & 0x04) << 1 | ptrlutcolors01[backbuffer[base + 6]] & 0x04 | (ptrlutcolors00[backbuffer[base + 7]] & 0x04) >> 1 | (ptrlutcolors01[backbuffer[base + 7]] & 0x04) >> 2;
            plane_green[plane_position + 1] = color;

            color = (ptrlutcolors00[backbuffer[base]] & 0x20) << 2 | (ptrlutcolors01[backbuffer[base]] & 0x20) << 1 | ptrlutcolors00[backbuffer[base + 1]] & 0x20 | (ptrlutcolors01[backbuffer[base + 1]] & 0x20) >> 1 | (ptrlutcolors00[backbuffer[base + 2]] & 0x02) << 2 | (ptrlutcolors01[backbuffer[base + 2]] & 0x02) << 1 | ptrlutcolors00[backbuffer[base + 3]] & 0x02 | (ptrlutcolors01[backbuffer[base + 3]] & 0x02) >> 1;
            plane_blue[plane_position] = color;

            color = (ptrlutcolors00[backbuffer[base + 4]] & 0x20) << 2 | (ptrlutcolors01[backbuffer[base + 4]] & 0x20) << 1 | ptrlutcolors00[backbuffer[base + 5]] & 0x20 | (ptrlutcolors01[backbuffer[base + 5]] & 0x20) >> 1 | (ptrlutcolors00[backbuffer[base + 6]] & 0x02) << 2 | (ptrlutcolors01[backbuffer[base + 6]] & 0x02) << 1 | ptrlutcolors00[backbuffer[base + 7]] & 0x02 | (ptrlutcolors01[backbuffer[base + 7]] & 0x02) >> 1;
            plane_blue[plane_position + 1] = color;

            color = (ptrlutcolors00[backbuffer[base]] & 0x10) << 3 | (ptrlutcolors01[backbuffer[base]] & 0x10) << 2 | (ptrlutcolors00[backbuffer[base + 1]] & 0x10) << 1 | ptrlutcolors01[backbuffer[base + 1]] & 0x10 | (ptrlutcolors00[backbuffer[base + 2]] & 0x01) << 3 | (ptrlutcolors01[backbuffer[base + 2]] & 0x01) << 2 | (ptrlutcolors00[backbuffer[base + 3]] & 0x01) << 1 | ptrlutcolors01[backbuffer[base + 3]] & 0x01;
            plane_intensity[plane_position] = color;

            color = (ptrlutcolors00[backbuffer[base + 4]] & 0x10) << 3 | (ptrlutcolors01[backbuffer[base + 4]] & 0x10) << 2 | (ptrlutcolors00[backbuffer[base + 5]] & 0x10) << 1 | ptrlutcolors01[backbuffer[base + 5]] & 0x10 | (ptrlutcolors00[backbuffer[base + 6]] & 0x01) << 3 | (ptrlutcolors01[backbuffer[base + 6]] & 0x01) << 2 | (ptrlutcolors00[backbuffer[base + 7]] & 0x01) << 1 | ptrlutcolors01[backbuffer[base + 7]] & 0x01;
            plane_intensity[plane_position + 1] = color;
        }

        plane_position += 2;
    }

    // Copy each bitplane
    outp(0x3C5, 1 << (3 & 0x03));
    CopyDWords(plane_red, destscreen, SCREENWIDTH * SCREENHEIGHT / 16);

    outp(0x3C5, 1 << (2 & 0x03));
    CopyDWords(plane_green, destscreen, SCREENWIDTH * SCREENHEIGHT / 16);

    outp(0x3C5, 1 << (1 & 0x03));
    CopyDWords(plane_blue, destscreen, SCREENWIDTH * SCREENHEIGHT / 16);

    outp(0x3C5, 1 << (0 & 0x03));
    CopyDWords(plane_intensity, destscreen, SCREENWIDTH * SCREENHEIGHT / 16);

    // Change video page
    regs.h.ah = 0x05;
    regs.h.al = page;
    regs.h.bh = 0x00;
    regs.h.bl = 0x00;
    int386(0x10, &regs, &regs);

    //Next plane
    destscreen += 0x4000;

    page++;
    if (page == 3)
    {
        destscreen = (byte *)0xa0000;
        page = 0;
    }
}
#endif

#ifdef MODE_EGA
void EGA_DrawBackbuffer(void)
{
    int i;
    byte *backbufferptr;

    // Red
    outp(0x3C5, 1 << (3 & 0x03));

    for (i = 0, backbufferptr = backbuffer; i < SCREENWIDTH * SCREENHEIGHT / 8; i++, backbufferptr += 8)
    {
        unsigned char color;
        unsigned char tmpColor;

        color = ptrlutRcolor[*(backbufferptr)];
        tmpColor = color & 0x80;

        color = ptrlutRcolor[*(backbufferptr + 1)];
        tmpColor |= color & 0x40;

        color = ptrlutRcolor[*(backbufferptr + 2)];
        tmpColor |= color & 0x20;

        color = ptrlutRcolor[*(backbufferptr + 3)];
        tmpColor |= color & 0x10;

        color = ptrlutRcolor[*(backbufferptr + 4)];
        tmpColor |= color & 0x08;

        color = ptrlutRcolor[*(backbufferptr + 5)];
        tmpColor |= color & 0x04;

        color = ptrlutRcolor[*(backbufferptr + 6)];
        tmpColor |= color & 0x02;

        color = ptrlutRcolor[*(backbufferptr + 7)];
        tmpColor |= color & 0x01;

        destscreen[i] = tmpColor;
    }

    // Green
    outp(0x3C5, 1 << (2 & 0x03));

    for (i = 0, backbufferptr = backbuffer; i < SCREENWIDTH * SCREENHEIGHT / 8; i++, backbufferptr += 8)
    {
        unsigned char color;
        unsigned char tmpColor;

        color = ptrlutGcolor[*(backbufferptr)];
        tmpColor = color & 0x80;

        color = ptrlutGcolor[*(backbufferptr + 1)];
        tmpColor |= color & 0x40;

        color = ptrlutGcolor[*(backbufferptr + 2)];
        tmpColor |= color & 0x20;

        color = ptrlutGcolor[*(backbufferptr + 3)];
        tmpColor |= color & 0x10;

        color = ptrlutGcolor[*(backbufferptr + 4)];
        tmpColor |= color & 0x08;

        color = ptrlutGcolor[*(backbufferptr + 5)];
        tmpColor |= color & 0x04;

        color = ptrlutGcolor[*(backbufferptr + 6)];
        tmpColor |= color & 0x02;

        color = ptrlutGcolor[*(backbufferptr + 7)];
        tmpColor |= color & 0x01;

        destscreen[i] = tmpColor;
    }

    // Blue
    outp(0x3C5, 1 << (1 & 0x03));

    for (i = 0, backbufferptr = backbuffer; i < SCREENWIDTH * SCREENHEIGHT / 8; i++, backbufferptr += 8)
    {
        unsigned char color;
        unsigned char tmpColor;

        color = ptrlutBcolor[*(backbufferptr)];
        tmpColor = color & 0x80;

        color = ptrlutBcolor[*(backbufferptr + 1)];
        tmpColor |= color & 0x40;

        color = ptrlutBcolor[*(backbufferptr + 2)];
        tmpColor |= color & 0x20;

        color = ptrlutBcolor[*(backbufferptr + 3)];
        tmpColor |= color & 0x10;

        color = ptrlutBcolor[*(backbufferptr + 4)];
        tmpColor |= color & 0x08;

        color = ptrlutBcolor[*(backbufferptr + 5)];
        tmpColor |= color & 0x04;

        color = ptrlutBcolor[*(backbufferptr + 6)];
        tmpColor |= color & 0x02;

        color = ptrlutBcolor[*(backbufferptr + 7)];
        tmpColor |= color & 0x01;

        destscreen[i] = tmpColor;
    }

    // Intensity
    outp(0x3C5, 1 << (0 & 0x03));

    for (i = 0, backbufferptr = backbuffer; i < SCREENWIDTH * SCREENHEIGHT / 8; i++, backbufferptr += 8)
    {
        unsigned char color;
        unsigned char tmpColor;

        color = ptrlutIcolor[*(backbufferptr)];
        tmpColor = color & 0x80;

        color = ptrlutIcolor[*(backbufferptr + 1)];
        tmpColor |= color & 0x40;

        color = ptrlutIcolor[*(backbufferptr + 2)];
        tmpColor |= color & 0x20;

        color = ptrlutIcolor[*(backbufferptr + 3)];
        tmpColor |= color & 0x10;

        color = ptrlutIcolor[*(backbufferptr + 4)];
        tmpColor |= color & 0x08;

        color = ptrlutIcolor[*(backbufferptr + 5)];
        tmpColor |= color & 0x04;

        color = ptrlutIcolor[*(backbufferptr + 6)];
        tmpColor |= color & 0x02;

        color = ptrlutIcolor[*(backbufferptr + 7)];
        tmpColor |= color & 0x01;

        destscreen[i] = tmpColor;
    }

    // Change video page
    regs.h.ah = 0x05;
    regs.h.al = page;
    regs.h.bh = 0x00;
    regs.h.bl = 0x00;
    int386(0x10, &regs, &regs);

    //Next plane
    destscreen += 0x2000;

    page++;
    if (page == 3)
    {
        destscreen = (byte *)0xa0000;
        page = 0;
    }
}
#endif

#ifdef MODE_CVB
void CVBS_DrawBackbuffer(void)
{
    int x;
    unsigned char *vram = (unsigned char *)0xB8000;
    unsigned int base = 0;
    unsigned char color0, color1;

    for (base = 0; base < SCREENHEIGHT * 320;)
    {
        for (x = 0; x < SCREENWIDTH / 4; x++, base += 4, vram++)
        {
            color0 = ptrlut16colors[backbuffer[base]];
            color1 = ptrlut16colors[backbuffer[base + 2]];
            *(vram) = color0 << 4 | color1;
        }
        vram += 0x1FB0;
        for (x = 0; x < SCREENWIDTH / 4; x++, base += 4, vram++)
        {
            color0 = ptrlut16colors[backbuffer[base]];
            color1 = ptrlut16colors[backbuffer[base + 2]];
            *(vram) = color0 << 4 | color1;
        }
        vram -= 0x2000;
    }
}
#endif

#ifdef MODE_PCP
void PCP_DrawBackbuffer(void)
{
    int x;
    unsigned char *vram = (unsigned char *)0xB8000;
    unsigned int base = 0;

    for (base = 0; base < SCREENHEIGHT * 320;)
    {
        for (x = 0; x < SCREENWIDTH / 4; x++, base += 4, vram++)
        {
            unsigned char color;

            unsigned char tmpColor0;
            unsigned char tmpColor1;

            color = ptrlut16colors[backbuffer[base]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlut16colors[backbuffer[base + 1]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlut16colors[backbuffer[base + 2]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlut16colors[backbuffer[base + 3]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram) = tmpColor0;
            *(vram + 0x4000) = tmpColor1;

            color = ptrlut16colors[backbuffer[base + 320]];
            tmpColor0 = (color & 3) << 6;
            tmpColor1 = (color & 0xC0);

            color = ptrlut16colors[backbuffer[base + 321]];
            tmpColor0 |= (color & 0x30);
            tmpColor1 |= (color & 12) << 2;

            color = ptrlut16colors[backbuffer[base + 322]];
            tmpColor0 |= (color & 3) << 2;
            tmpColor1 |= (color & 12);

            color = ptrlut16colors[backbuffer[base + 323]];
            tmpColor0 |= (color & 3);
            tmpColor1 |= (color & 12) >> 2;

            *(vram + 0x2000) = tmpColor0;
            *(vram + 0x6000) = tmpColor1;
        }
        base += 320;
    }
}
#endif

#ifdef MODE_CGA
void CGA_DrawBackbuffer(void)
{
    int x, y;
    unsigned char *vram = (unsigned char *)0xB8000;
    unsigned int base = 0;

    for (base = 0; base < SCREENHEIGHT * 320; base += 320)
    {
        for (x = 0; x < SCREENWIDTH / 4; x++, base += 4, vram++)
        {
            unsigned char color;

            color = (ptrlut4colors[backbuffer[base]]) & 0xC0 | (ptrlut4colors[backbuffer[base + 1]]) & 0x30 | (ptrlut4colors[backbuffer[base + 2]]) & 0x0C | (ptrlut4colors[backbuffer[base + 3]]) & 0x03;
            *(vram) = color;

            color = (ptrlut4colors[backbuffer[base + 320]]) & 0xC0 | (ptrlut4colors[backbuffer[base + 321]]) & 0x30 | (ptrlut4colors[backbuffer[base + 322]]) & 0x0C | (ptrlut4colors[backbuffer[base + 323]]) & 0x03;
            *(vram + 0x2000) = color;
        }
    }
}
#endif

#if defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT)
static struct VBE_VbeInfoBlock vbeinfo;
static struct VBE_ModeInfoBlock vbemode;
char *Types[8] = {"Text", "CGA", "Hercules", "Planar", "Packed Pixel", "Unchained", "Directcolor", "YUV"};
char *MemoryType[2] = {"Banked", "Linear"};
unsigned short vesavideomode = 0xFFFF;
int vesalinear = -1;
char *vesavideoptr;
#endif

void I_FinishUpdate(void)
{
    static int fps_counter, fps_starttime, fps_nextcalculation;
    int opt1, opt2;

#ifndef MODE_HERC
    if (waitVsync)
    {
        I_WaitSingleVBL();
    }
#endif

#if defined(MODE_T4025) || defined(MODE_T4050)
    // Change video page
    regs.h.ah = 0x05;
    regs.h.al = textpage;
    regs.h.bh = 0x00;
    regs.h.bl = 0x00;
    int386(0x10, &regs, &regs);

    textdestscreen += 1024;
    textpage++;
    if (textpage == 3)
    {
        textdestscreen = (unsigned short *)0xB8000;
        textpage = 0;
    }
#endif

#ifdef MODE_T8025
    // Change video page
    regs.h.ah = 0x05;
    regs.h.al = textpage;
    regs.h.bh = 0x00;
    regs.h.bl = 0x00;
    int386(0x10, &regs, &regs);

    textdestscreen += 2048;
    textpage++;
    if (textpage == 3)
    {
        textdestscreen = (unsigned short *)0xB8000;
        textpage = 0;
    }
#endif
#if defined(MODE_T8050) || defined(MODE_T80100)
    // Change video page
    regs.h.ah = 0x05;
    regs.h.al = textpage;
    regs.h.bh = 0x00;
    regs.h.bl = 0x00;
    int386(0x10, &regs, &regs);

    textdestscreen += 4128;
    textpage++;
    if (textpage == 3)
    {
        textdestscreen = (unsigned short *)0xB8000;
        textpage = 0;
    }
#endif
#ifdef MODE_Y
    outpw(CRTC_INDEX, ((int)destscreen & 0xff00) + 0xc);

    //Next plane
    destscreen += 0x4000;
    if (destscreen == (byte *)0xac000)
    {
        destscreen = (byte *)0xa0000;
    }
#endif
#ifdef MODE_VBE2_DIRECT
    VBE_SetDisplayStart(0, 200 * page);

    page++;

    if (page == 3)
    {
        page = 0;
        destscreen -= 2 * 320 * 200;
    }
    else
    {
        destscreen += 320 * 200;
    }
#endif
#if defined(MODE_13H) || defined(MODE_VBE2)

    if (updatestate & I_FULLSCRN)
    {
        CopyDWords(backbuffer, pcscreen, SCREENHEIGHT * SCREENWIDTH / 4);
        updatestate = I_NOUPDATE; // clear out all draw types
    }
    if (updatestate & I_FULLVIEW)
    {
        if (updatestate & I_MESSAGES && screenblocks > 7)
        {
            int i;
            for (i = 0; i < Mul320(viewwindowy + viewheight); i += SCREENWIDTH)
            {
                CopyDWords(backbuffer + i, pcscreen + i, SCREENWIDTH / 4);
            }
            updatestate &= ~(I_FULLVIEW | I_MESSAGES);
        }
        else
        {
            int i;
            for (i = Mul320(viewwindowy) + viewwindowx; i < Mul320(viewwindowy + viewheight); i += SCREENWIDTH)
            {
                CopyDWords(backbuffer + i, pcscreen + i, SCREENWIDTH / 4);
            }
            updatestate &= ~I_FULLVIEW;
        }
    }
    if (updatestate & I_STATBAR)
    {
        CopyDWords(backbuffer + SCREENWIDTH * (SCREENHEIGHT - SBARHEIGHT), pcscreen + SCREENWIDTH * (SCREENHEIGHT - SBARHEIGHT), SCREENWIDTH * SBARHEIGHT / 4);
        updatestate &= ~I_STATBAR;
    }
    if (updatestate & I_MESSAGES)
    {
        CopyDWords(backbuffer, pcscreen, (SCREENWIDTH * 28) / 4);
        updatestate &= ~I_MESSAGES;
    }
#endif
#ifdef MODE_HERC
    HERC_DrawBackbuffer();
#endif
#ifdef MODE_CGA
    CGA_DrawBackbuffer();
#endif
#ifdef MODE_CGA_BW
    CGA_BW_DrawBackbuffer();
#endif
#ifdef MODE_CGA16
    CGA16_DrawBackbuffer();
#endif
#ifdef MODE_VGA16
    VGA16_DrawBackbuffer();
#endif
#ifdef MODE_EGA
    EGA_DrawBackbuffer();
#endif
#ifdef MODE_EGA640
    EGA640_DrawBackbuffer();
#endif
#ifdef MODE_ATI640
    ATI640_DrawBackbuffer();
#endif
#ifdef MODE_PCP
    PCP_DrawBackbuffer();
#endif
#ifdef MODE_CVB
    CVBS_DrawBackbuffer();
#endif
#if defined(MODE_V)
    {
        int x, y;

        outp(SC_INDEX + 1, 1 << 0);
        for (x = 0; x < (320 * 350) / 4; x += 4)
        {
            destscreen[x] = backbuffer[lutplane0[x]];
            destscreen[x + 1] = backbuffer[lutplane0[x + 1]];
            destscreen[x + 2] = backbuffer[lutplane0[x + 2]];
            destscreen[x + 3] = backbuffer[lutplane0[x + 3]];
        }

        outp(SC_INDEX + 1, 1 << 1);
        for (x = 0; x < (320 * 350) / 4; x += 4)
        {
            destscreen[x] = backbuffer[lutplane1[x]];
            destscreen[x + 1] = backbuffer[lutplane1[x + 1]];
            destscreen[x + 2] = backbuffer[lutplane1[x + 2]];
            destscreen[x + 3] = backbuffer[lutplane1[x + 3]];
        }

        outp(SC_INDEX + 1, 1 << 2);
        for (x = 0; x < (320 * 350) / 4; x += 4)
        {
            destscreen[x] = backbuffer[lutplane2[x]];
            destscreen[x + 1] = backbuffer[lutplane2[x + 1]];
            destscreen[x + 2] = backbuffer[lutplane2[x + 2]];
            destscreen[x + 3] = backbuffer[lutplane2[x + 3]];
        }

        outp(SC_INDEX + 1, 1 << 3);
        for (x = 0; x < (320 * 350) / 4; x += 4)
        {
            destscreen[x] = backbuffer[lutplane3[x]];
            destscreen[x + 1] = backbuffer[lutplane3[x + 1]];
            destscreen[x + 2] = backbuffer[lutplane3[x + 2]];
            destscreen[x + 3] = backbuffer[lutplane3[x + 3]];
        }

        outpw(CRTC_INDEX, ((int)destscreen & 0xff00) + 0xc);

        //Next plane
        destscreen += 0x7000;
        if (destscreen == (byte *)0xae000)
        {
            destscreen = (byte *)0xa0000;
        }
    }
#endif
#if defined(MODE_V2)
    {
        int x, y;

        outp(SC_INDEX + 1, 1 << 0);
        for (y = 15 * 80; y < 335 * 80; y += 80)
        {
            for (x = 15; x < 65; x += 5)
            {
                destscreen[y + x] = backbuffer[lutplane0[y + x]];
                destscreen[y + x + 1] = backbuffer[lutplane0[y + x + 1]];
                destscreen[y + x + 2] = backbuffer[lutplane0[y + x + 2]];
                destscreen[y + x + 3] = backbuffer[lutplane0[y + x + 3]];
                destscreen[y + x + 4] = backbuffer[lutplane0[y + x + 4]];
            }
        }

        outp(SC_INDEX + 1, 1 << 1);
        for (y = 15 * 80; y < 335 * 80; y += 80)
        {
            for (x = 15; x < 65; x += 5)
            {
                destscreen[y + x] = backbuffer[lutplane1[y + x]];
                destscreen[y + x + 1] = backbuffer[lutplane1[y + x + 1]];
                destscreen[y + x + 2] = backbuffer[lutplane1[y + x + 2]];
                destscreen[y + x + 3] = backbuffer[lutplane1[y + x + 3]];
                destscreen[y + x + 4] = backbuffer[lutplane1[y + x + 4]];
            }
        }

        outp(SC_INDEX + 1, 1 << 2);
        for (y = 15 * 80; y < 335 * 80; y += 80)
        {
            for (x = 15; x < 65; x += 5)
            {
                destscreen[y + x] = backbuffer[lutplane2[y + x]];
                destscreen[y + x + 1] = backbuffer[lutplane2[y + x + 1]];
                destscreen[y + x + 2] = backbuffer[lutplane2[y + x + 2]];
                destscreen[y + x + 3] = backbuffer[lutplane2[y + x + 3]];
                destscreen[y + x + 4] = backbuffer[lutplane2[y + x + 4]];
            }
        }

        outp(SC_INDEX + 1, 1 << 3);
        for (y = 15 * 80; y < 335 * 80; y += 80)
        {
            for (x = 15; x < 65; x += 5)
            {
                destscreen[y + x] = backbuffer[lutplane3[y + x]];
                destscreen[y + x + 1] = backbuffer[lutplane3[y + x + 1]];
                destscreen[y + x + 2] = backbuffer[lutplane3[y + x + 2]];
                destscreen[y + x + 3] = backbuffer[lutplane3[y + x + 3]];
                destscreen[y + x + 4] = backbuffer[lutplane3[y + x + 4]];
            }
        }

        outpw(CRTC_INDEX, ((int)destscreen & 0xff00) + 0xc);

        //Next plane
        destscreen += 0x7000;
        if (destscreen == (byte *)0xae000)
        {
            destscreen = (byte *)0xa0000;
        }
    }
#endif

    if (showFPS)
    {
        if (fps_counter == 0)
        {
            fps_starttime = ticcount;
        }

        fps_counter++;

        // store a value and/or draw when data is ok:
        if (fps_counter > (TICRATE * 2) && fps_nextcalculation < ticcount)
        {
            // in case of a very fast system, this will limit the sampling
            // minus 1!, exactly 35 FPS when measeraring for a longer time.
            opt1 = Mul35(fps_counter - 1) << FRACBITS;
            opt2 = (ticcount - fps_starttime) << FRACBITS;
            fps = (opt1 >> 14 >= opt2) ? ((opt1 ^ opt2) >> 31) ^ MAXINT : FixedDiv2(opt1, opt2);
            fps_nextcalculation = ticcount + 12;
            fps_counter = 0; // flush old data
        }
    }
}

//
// I_InitGraphics
//
void I_InitGraphics(void)
{
#if defined(MODE_T4025) || defined(MODE_T4050)
    // Set 40x25 color mode
    regs.h.ah = 0x00;
    regs.h.al = 0x01;
    int386(0x10, &regs, &regs);

    // Disable cursor
    regs.h.ah = 0x01;
    regs.h.ch = 0x3F;
    int386(0x10, &regs, &regs);

    // CGA Disable blink
    if (CGAcard)
    {
        I_DisableCGABlink();
    }
    else
    {
        // Disable blinking
        regs.h.ah = 0x10;
        regs.h.al = 0x03;
        regs.h.bl = 0x00;
        regs.h.bh = 0x00;
        int386(0x10, &regs, &regs);
    }

    textdestscreen = (unsigned short *)0xB8000;
    textpage = 0;
#endif
#ifdef MODE_T8025
    // Set 80x25 color mode
    regs.h.ah = 0x00;
    regs.h.al = 0x03;
    int386(0x10, &regs, &regs);

    // Disable cursor
    regs.h.ah = 0x01;
    regs.h.ch = 0x3F;
    int386(0x10, &regs, &regs);

    // CGA Disable blink
    if (CGAcard)
    {
        I_DisableCGABlink();
    }
    else
    {
        // Disable blinking
        regs.h.ah = 0x10;
        regs.h.al = 0x03;
        regs.h.bl = 0x00;
        regs.h.bh = 0x00;
        int386(0x10, &regs, &regs);
    }

    textdestscreen = (unsigned short *)0xB8000;
    textpage = 0;
#endif
#if defined(MODE_T8050) || defined(MODE_T80100)
    // Set 80x25 color mode
    regs.h.ah = 0x00;
    regs.h.al = 0x03;
    int386(0x10, &regs, &regs);

    // Change font size to 8x8 (only VGA cards)
    regs.h.ah = 0x11;
    regs.h.al = 0x12;
    regs.h.bh = 0;
    regs.h.bl = 0;
    int386(0x10, &regs, &regs);

    // Disable cursor
    regs.h.ah = 0x01;
    regs.h.ch = 0x3F;
    int386(0x10, &regs, &regs);

    // Disable blinking
    regs.h.ah = 0x10;
    regs.h.al = 0x03;
    regs.h.bl = 0x00;
    regs.h.bh = 0x00;
    int386(0x10, &regs, &regs);

    textdestscreen = (unsigned short *)0xB8000;
    textpage = 0;
#endif
#ifdef MODE_Y
    regs.w.ax = 0x13;
    int386(0x10, (union REGS *)&regs, &regs);
    pcscreen = currentscreen = (byte *)0xA0000;
    destscreen = (byte *)0xA4000;

    outp(SC_INDEX, SC_MEMMODE);
    outp(SC_INDEX + 1, (inp(SC_INDEX + 1) & ~8) | 4);
    outp(GC_INDEX, GC_MODE);
    outp(GC_INDEX + 1, inp(GC_INDEX + 1) & ~0x13);
    outp(GC_INDEX, GC_MISCELLANEOUS);
    outp(GC_INDEX + 1, inp(GC_INDEX + 1) & ~2);
    outpw(SC_INDEX, 0xf02);
    SetDWords(pcscreen, 0, 0x4000);
    outp(CRTC_INDEX, CRTC_UNDERLINE);
    outp(CRTC_INDEX + 1, inp(CRTC_INDEX + 1) & ~0x40);
    outp(CRTC_INDEX, CRTC_MODE);
    outp(CRTC_INDEX + 1, inp(CRTC_INDEX + 1) | 0x40);
    outp(GC_INDEX, GC_READMAP);
#endif
#if defined(MODE_V)

    {
        int x, y;

        // Initialize LUT
        for (x = 0; x < 320; x += 4)
        {
            int yp = 0;
            int xp = x / 4;
            for (y = 0; y < 350; y++, yp += 80)
            {
                int pos_x_backbuffer;
                int pos_y_backbuffer;

                pos_x_backbuffer = ((x * 200) / 320);
                pos_y_backbuffer = 320 - ((y * 320) / 350);

                lutplane0[yp + xp] = pos_x_backbuffer * 320 + pos_y_backbuffer;
            }
        }

        for (x = 1; x < 320; x += 4)
        {
            int yp = 0;
            int xp = x / 4;
            for (y = 0; y < 350; y++, yp += 80)
            {
                int pos_x_backbuffer;
                int pos_y_backbuffer;

                pos_x_backbuffer = ((x * 200) / 320);
                pos_y_backbuffer = 320 - ((y * 320) / 350);

                lutplane1[yp + xp] = pos_x_backbuffer * 320 + pos_y_backbuffer;
            }
        }

        for (x = 2; x < 320; x += 4)
        {
            int yp = 0;
            int xp = x / 4;
            for (y = 0; y < 350; y++, yp += 80)
            {
                int pos_x_backbuffer;
                int pos_y_backbuffer;

                pos_x_backbuffer = ((x * 200) / 320);
                pos_y_backbuffer = 320 - ((y * 320) / 350);

                lutplane2[yp + xp] = pos_x_backbuffer * 320 + pos_y_backbuffer;
            }
        }

        for (x = 3; x < 320; x += 4)
        {
            int yp = 0;
            int xp = x / 4;
            for (y = 0; y < 350; y++, yp += 80)
            {
                int pos_x_backbuffer;
                int pos_y_backbuffer;

                pos_x_backbuffer = ((x * 200) / 320);
                pos_y_backbuffer = 320 - ((y * 320) / 350);

                lutplane3[yp + xp] = pos_x_backbuffer * 320 + pos_y_backbuffer;
            }
        }
    }
#endif
#if defined(MODE_V2)
    {
        int x, y;

        for (x = 0; x < 200; x++)
        {
            int xp = x / 4;
            for (y = 15; y < 320 + 15; y++)
            {
                int pos_x_backbuffer;
                int pos_y_backbuffer;

                pos_x_backbuffer = x;
                pos_y_backbuffer = 319 - y;

                switch (x % 4)
                {
                case 0:
                    lutplane0[y * 80 + xp + 15] = pos_x_backbuffer * 320 + pos_y_backbuffer + 15;
                    break;
                case 1:
                    lutplane1[y * 80 + xp + 15] = pos_x_backbuffer * 320 + pos_y_backbuffer + 15;
                    break;
                case 2:
                    lutplane2[y * 80 + xp + 15] = pos_x_backbuffer * 320 + pos_y_backbuffer + 15;
                    break;
                case 3:
                    lutplane3[y * 80 + xp + 15] = pos_x_backbuffer * 320 + pos_y_backbuffer + 15;
                    break;
                }
            }
        }
    }

#endif

#if defined(MODE_V) || defined(MODE_V2)

    regs.w.ax = 0x13;
    int386(0x10, (union REGS *)&regs, &regs);
    pcscreen = currentscreen = (byte *)0xA0000;
    destscreen = (byte *)0xA7000;

    //
    // switch to linear, non-chain4 mode
    //
    outp(SC_INDEX, SYNC_RESET);
    outp(SC_DATA, 1);

    outp(SC_INDEX, MEMORY_MODE);
    outp(SC_DATA, (inp(SC_DATA) & ~0x08) | 0x04);
    outp(GC_INDEX, GRAPHICS_MODE);
    outp(GC_DATA, (inp(GC_DATA) & ~0x10) | 0x00);
    outp(GC_INDEX, MISCELLANOUS);
    outp(GC_DATA, (inp(GC_DATA) & ~0x02) | 0x00);

    outpw(SC_INDEX, 0xf02);
    SetDWords(pcscreen, 0, 0x4000);

    outp(MISC_OUTPUT, 0xA3); // 350-scan-line scan rate

    outp(SC_INDEX, SYNC_RESET);
    outp(SC_DATA, 3);

    //
    // unprotect CRTC0 through CRTC0
    //
    outp(CRTC_INDEX, 0x11);
    outp(CRTC_DATA, (inp(CRTC_DATA) & ~0x80) | 0x00);

    //
    // stop scanning each line twice
    //
    outp(CRTC_INDEX, MAX_SCAN_LINE);
    outp(CRTC_DATA, (inp(CRTC_DATA) & ~0x1F) | 0x00);

    //
    // change the CRTC from doubleword to byte mode
    //
    outp(CRTC_INDEX, UNDERLINE);
    outp(CRTC_DATA, (inp(CRTC_DATA) & ~0x40) | 0x00);
    outp(CRTC_INDEX, MODE_CONTROL);
    outp(CRTC_DATA, (inp(CRTC_DATA) & ~0x00) | 0x40);

    //
    // set the vertical counts for 350-scan-line mode
    //
    outp(CRTC_INDEX, 0x06);
    outp(CRTC_INDEX + 1, 0xBF);
    outp(CRTC_INDEX, 0x07);
    outp(CRTC_INDEX + 1, 0x1F);
    outp(CRTC_INDEX, 0x10);
    outp(CRTC_INDEX + 1, 0x83);
    outp(CRTC_INDEX, 0x11);
    outp(CRTC_INDEX + 1, 0x85);
    outp(CRTC_INDEX, 0x12);
    outp(CRTC_INDEX + 1, 0x5D);
    outp(CRTC_INDEX, 0x15);
    outp(CRTC_INDEX + 1, 0x63);
    outp(CRTC_INDEX, 0x16);
    outp(CRTC_INDEX + 1, 0xBA);

    outp(SC_INDEX, MAP_MASK);
    outp(GC_INDEX, READ_MAP);
#endif
#ifdef MODE_13H
    regs.w.ax = 0x13;
    int386(0x10, (union REGS *)&regs, &regs);
    pcscreen = destscreen = (byte *)0xA0000;
#endif
#ifdef MODE_CGA
    // Set video mode 4
    regs.w.ax = 0x04;
    int386(0x10, (union REGS *)&regs, &regs);

    // Set palette and intensity (CGA)
    regs.w.ax = 0x0B00;
    regs.w.bx = 0x0100;
    int386(0x10, (union REGS *)&regs, &regs);
    regs.w.ax = 0x0B00;
    regs.w.bx = 0x0000;
    int386(0x10, (union REGS *)&regs, &regs);

    // Fix EGA/VGA wrong colors
    regs.w.ax = 0x1000;
    regs.w.bx = 0x0000;
    int386(0x10, (union REGS *)&regs, &regs);

    regs.w.ax = 0x1000;
    regs.w.bx = 0x0201;
    int386(0x10, (union REGS *)&regs, &regs);

    regs.w.ax = 0x1000;
    regs.w.bx = 0x0402;
    int386(0x10, (union REGS *)&regs, &regs);

    regs.w.ax = 0x1000;
    regs.w.bx = 0x0603;
    int386(0x10, (union REGS *)&regs, &regs);

    pcscreen = destscreen = (byte *)0xB8000;
#endif
#if defined(MODE_CGA16) || defined(MODE_VGA16)
    unsigned char *vram = (unsigned char *)0xB8000;
    int i;

    // Set 80x25 color mode
    regs.h.ah = 0x00;
    regs.h.al = 0x03;
    int386(0x10, &regs, &regs);

    // Disable cursor
    regs.h.ah = 0x01;
    regs.h.ch = 0x3F;
    int386(0x10, &regs, &regs);

    // Disable blinking
    regs.h.ah = 0x10;
    regs.h.al = 0x03;
    regs.h.bl = 0x00;
    regs.h.bh = 0x00;
    int386(0x10, &regs, &regs);

    /* set mode control register for 80x25 text mode and disable video output */
    outp(0x3D8, 1);

    /*
        These settings put the 6845 into "graphics" mode without actually
        switching the CGA controller into graphics mode.  The register
        values are directly copied from CGA graphics mode register
        settings.  The 6845 does not directly display graphics, the
        6845 only generates addresses and sync signals, the CGA
        attribute controller either displays character ROM data or color
        pixel data, this is external to the 6845 and keeps the CGA card
        in text mode.
        ref: HELPPC
    */

    /* set vert total lines to 127 */
    outp(0x3D4, 0x04);
    outp(0x3D5, 0x7F);
    /* set vert displayed char rows to 100 */
    outp(0x3D4, 0x06);
    outp(0x3D5, 0x64);
    /* set vert sync position to 112 */
    outp(0x3D4, 0x07);
    outp(0x3D5, 0x70);
    /* set char scan line count to 1 */
    outp(0x3D4, 0x09);
    outp(0x3D5, 0x01);

    /* re-enable the video output in 80x25 text mode */
    outp(0x3D8, 9);

    /* init screen */
#endif

#if defined(MODE_CGA16)
    for (i = 0; i < 16000; i += 2)
    {
        vram[i] = 0xDE;
    }
#endif

#if defined(MODE_VGA16)
    for (i = 0; i < 32000; i += 2)
    {
        vram[i] = 0xDE;
    }
#endif

#ifdef MODE_CGA_BW
    regs.w.ax = 0x06;
    int386(0x10, (union REGS *)&regs, &regs);
    pcscreen = destscreen = (byte *)0xB8000;
#endif
#ifdef MODE_PCP
    regs.w.ax = 0x04;
    int386(0x10, (union REGS *)&regs, &regs);
    outp(0x3DD, 0x10);
    pcscreen = destscreen = (byte *)0xB8000;
#endif
#ifdef MODE_EGA
    regs.w.ax = 0x0D;
    int386(0x10, (union REGS *)&regs, &regs);
    outp(0x3C4, 0x2);
    pcscreen = destscreen = (byte *)0xA0000;
#endif
#ifdef MODE_EGA640
    regs.w.ax = 0x0E;
    int386(0x10, (union REGS *)&regs, &regs);
    outp(0x3C4, 0x2);
    pcscreen = destscreen = (byte *)0xA0000;
#endif
#ifdef MODE_ATI640

    static int parms[16] = {0x70, 0x50, 0x58, 0x0a,
                            0x40, 0x06, 0x32, 0x38,
                            0x02, 0x03, 0x06, 0x07,
                            0x00, 0x00, 0x00, 0x00};
    int i;

    /* Set the Graphics Solution to 640 x 200 with 16 colors in 
	   Color Mode */
    outp(0x3D8, 0x2);

    /* Set extended graphics registers */
    outp(0x3DD, 0x80);

    outp(0x03D8, 0x2);
    /* Program 6845 crt controlller */
    for (i = 0; i < 16; ++i)
    {
        outp(0x3D4, i);
        outp(0x3D5, parms[i]);
    }
    outp(0x3D8, 0x0A);
    outp(0x3D9, 0x30);

    outp(0x3dd, 0x80);

    pcscreen = destscreen = (byte *)0xB0000;
#endif
#ifdef MODE_CVB
    regs.w.ax = 0x06;
    int386(0x10, (union REGS *)&regs, &regs);
    outp(0x3D8, 0x1A); // Enable color burst
    pcscreen = destscreen = (byte *)0xB8000;
#endif
#ifdef MODE_HERC
    //byte Graph_720x348[12] = {0x03, 0x36, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x0A};
    byte Graph_640x400[12] = {0x03, 0x34, 0x28, 0x2A, 0x47, 0x69, 0x00, 0x64, 0x65, 0x02, 0x03, 0x0A};
    //byte Graph_640x200[12] = {0x03, 0x6E, 0x28, 0x2E, 0x07, 0x67, 0x0A, 0x64, 0x65, 0x02, 0x01, 0x0A}; --> NOT WORKING ON REAL HARDWARE
    int i;

    outp(0x03BF, Graph_640x400[0]);
    for (i = 0; i < 10; i++)
    {
        outp(0x03B4, i);
        outp(0x03B5, Graph_640x400[i + 1]);
    }
    outp(0x03B8, Graph_640x400[11]);
    pcscreen = destscreen = (byte *)0xB0000;
#endif

#if defined(MODE_VBE2) || defined(MODE_VBE2_DIRECT)

    int mode;

    VBE_Init();

    // Get VBE info
    VBE_Controller_Information(&vbeinfo);
    printf("%s %hi.%hi\n", vbeinfo.vbeSignature, vbeinfo.vbeVersion.hi, vbeinfo.vbeVersion.lo);
    printf("%s\n", vbeinfo.OemStringPtr);
    printf("%s\n", vbeinfo.OemVendorNamePtr);
    printf("%s\n", vbeinfo.OemProductNamePtr);
    printf("%hi kb\n", vbeinfo.TotalMemory * 64);

    // Get VBE modes
    for (mode = 0; vbeinfo.VideoModePtr[mode] != 0xffff; mode++)
    {
        VBE_Mode_Information(vbeinfo.VideoModePtr[mode], &vbemode);
        if (vbemode.XResolution == 320 && vbemode.YResolution == 200 && vbemode.BitsPerPixel == 8)
        {
            vesavideomode = vbeinfo.VideoModePtr[mode];
            vesalinear = VBE_IsModeLinear(vesavideomode);

            printf("Mode 0x%3x: %3ix%3i %1i bpp (%-12s, %s) %2hi pages\n",
                   vesavideomode,
                   vbemode.XResolution,
                   vbemode.YResolution,
                   vbemode.BitsPerPixel,
                   Types[vbemode.MemoryModel],
                   MemoryType[vesalinear],
                   vbemode.NumberOfImagePages);

            break;
        }
    }

    // If a VESA compatible 320x200 8bpp mode is found, use it!
    if (vesavideomode != 0xFFFF)
    {
        VBE_SetMode(vesavideomode, vesalinear, 1);

        if (vesalinear == 1)
        {
            pcscreen = destscreen = VBE_GetVideoPtr(vesavideomode);
        }
        else
        {
            pcscreen = destscreen = (char *)0xA0000;
        }

        // Force 6 bits resolution per color
        VBE_SetDACWidth(6);
    }
    else
    {
        I_Error("Compatible VESA 2.0 video mode not found! (320x200 8bpp required)");
    }
#endif

    I_ProcessPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));
    I_SetPalette(0);
}

//
// I_ShutdownGraphics
//
void I_ShutdownGraphics(void)
{
#ifdef MODE_HERC
    byte Text_80x25[12] = {0x00, 0x61, 0x50, 0x52, 0x0F, 0x19, 0x06, 0x19, 0x19, 0x02, 0x0D, 0x08};
    int i;

    outp(0x03BF, Text_80x25[0]);
    for (i = 0; i < 10; i++)
    {
        outp(0x03B4, i);
        outp(0x03B5, Text_80x25[i + 1]);
    }
    outp(0x03B8, Text_80x25[11]);
#endif

#ifdef MODE_VBE2
    VBE_Done();
#endif

    regs.w.ax = 3;
    int386(0x10, &regs, &regs); // back to text mode
}

//
// I_StartTic
//
// called by D_DoomLoop
// called before processing each tic in a frame
// can call D_PostEvent
// asyncronous interrupt functions should maintain private ques that are
// read by the syncronous functions to be converted into events
//

#define SC_UPARROW 0x48
#define SC_DOWNARROW 0x50
#define SC_LEFTARROW 0x4b
#define SC_RIGHTARROW 0x4d

void I_StartTic(void)
{
    int k;
    event_t ev;

    if (mousepresent)
        I_ReadMouse();

    //
    // keyboard events
    //
    while (kbdtail < kbdhead)
    {
        k = keyboardque[kbdtail & (KBDQUESIZE - 1)];
        kbdtail++;

        // extended keyboard shift key bullshit
        if ((k & 0x7f) == SC_LSHIFT || (k & 0x7f) == SC_RSHIFT)
        {
            if (keyboardque[(kbdtail - 2) & (KBDQUESIZE - 1)] == 0xe0)
            {
                continue;
            }
            k &= 0x80;
            k |= SC_RSHIFT;
        }

        if (k == 0xe0 || keyboardque[(kbdtail - 2) & (KBDQUESIZE - 1)] == 0xe1)
        {
            continue; // special / pause keys, pause key bullshit
        }

        if (k == 0xc5 && keyboardque[(kbdtail - 2) & (KBDQUESIZE - 1)] == 0x9d)
        {
            ev.type = ev_keydown;
            ev.data1 = KEY_PAUSE;
            D_PostEvent(&ev);
            continue;
        }

        ev.type = (k & 0x80) != 0;
        k &= 0x7f;

        switch (k)
        {
        case SC_UPARROW:
            ev.data1 = KEY_UPARROW;
            break;
        case SC_DOWNARROW:
            ev.data1 = KEY_DOWNARROW;
            break;
        case SC_LEFTARROW:
            ev.data1 = KEY_LEFTARROW;
            break;
        case SC_RIGHTARROW:
            ev.data1 = KEY_RIGHTARROW;
            break;
        default:
            ev.data1 = scantokey[k];
            break;
        }
        D_PostEvent(&ev);
    }
}

//
// Timer interrupt
//

//
// I_TimerISR
//
void I_TimerISR(task *task)
{
    ticcount++;
}

//
// Keyboard
//

void(__interrupt __far *oldkeyboardisr)() = NULL;

//
// I_KeyboardISR
//

void __interrupt I_KeyboardISR(void)
{
    // Get the scan code

    keyboardque[kbdhead & (KBDQUESIZE - 1)] = _inbyte(0x60);
    kbdhead++;

    // acknowledge the interrupt

    _outbyte(0x20, 0x20);
}

//
// I_StartupKeyboard
//
void I_StartupKeyboard(void)
{
    oldkeyboardisr = _dos_getvect(KEYBOARDINT);
    _dos_setvect(0x8000 | KEYBOARDINT, I_KeyboardISR);
}

void I_ShutdownKeyboard(void)
{
    if (oldkeyboardisr)
        _dos_setvect(KEYBOARDINT, oldkeyboardisr);
    *(short *)0x41c = *(short *)0x41a; // clear bios key buffer
}

//
// Mouse
//

int I_ResetMouse(void)
{
    regs.w.ax = 0; // reset
    int386(0x33, &regs, &regs);
    return regs.w.ax;
}

//
// StartupMouse
//

void I_StartupMouse(void)
{
    //
    // General mouse detection
    //
    mousepresent = 0;
    if (M_CheckParm("-nomouse") || !usemouse)
    {
        return;
    }

    if (I_ResetMouse() != 0xffff)
    {
        printf("Mouse: not present\n", 0);
        return;
    }
    printf("Mouse: detected\n", 0);

    mousepresent = 1;
}

//
// ShutdownMouse
//
void I_ShutdownMouse(void)
{
    if (!mousepresent)
    {
        return;
    }

    I_ResetMouse();
}

//
// I_ReadMouse
//
void I_ReadMouse(void)
{
    event_t ev;

    //
    // mouse events
    //

    ev.type = ev_mouse;

    SetBytes(&dpmiregs, 0, sizeof(dpmiregs));
    dpmiregs.eax = 3; // read buttons / position
    DPMIInt(0x33);
    ev.data1 = dpmiregs.ebx;

    dpmiregs.eax = 11; // read counters
    DPMIInt(0x33);
    ev.data2 = (short)dpmiregs.ecx;

    D_PostEvent(&ev);
}

//
// DPMI stuff
//

#define REALSTACKSIZE 1024

dpmiregs_t dpmiregs;

unsigned realstackseg;

//
// I_StartupDPMI
//
byte *I_AllocLow(int length);

void I_StartupDPMI(void)
{
    extern char __begtext;
    extern char ___Argc;

    //
    // allocate a decent stack for real mode ISRs
    //
    realstackseg = (int)I_AllocLow(1024) >> 4;

    //
    // lock the entire program down
    //

    DPMI_LockMemory(&__begtext, &___Argc - &__begtext);
}

//
// I_Init
// hook interrupts and set graphics mode
//
void I_Init(void)
{
    int p;
    printf("I_StartupDPMI\n");
    I_StartupDPMI();
    printf("I_StartupMouse\n");
    I_StartupMouse();
    printf("I_StartupKeyboard\n");
    I_StartupKeyboard();
    printf("I_StartupSound\n");
    I_StartupSound();
}

//
// I_Shutdown
// return to default system state
//
void I_Shutdown(void)
{
    I_ShutdownGraphics();
    I_ShutdownSound();
    I_ShutdownTimer();
    I_ShutdownMouse();
    I_ShutdownKeyboard();
}

//
// I_Error
//
void I_Error(char *error, ...)
{
    va_list argptr;

    I_Shutdown();
    va_start(argptr, error);
    vprintf(error, argptr);
    va_end(argptr);
    printf("\n");
    exit(1);
}

//
// I_Quit
//
// Shuts down net game, saves defaults, prints the exit text message,
// goes to text mode, and exits.
//
void I_Quit(void)
{
    byte *scr;

    if (demorecording)
    {
        G_CheckDemoStatus();
    }

    M_SaveDefaults();
    scr = (byte *)W_CacheLumpName("ENDOOM", PU_CACHE);
    I_Shutdown();
#ifdef MODE_HERC
    CopyDWords(scr, (void *)0xb0000, (80 * 25 * 2) / 4);
#else
    CopyDWords(scr, (void *)0xb8000, (80 * 25 * 2) / 4);
#endif
    regs.w.ax = 0x0200;
    regs.h.bh = 0;
    regs.h.dl = 0;
    regs.h.dh = 23;
    int386(0x10, (union REGS *)&regs, &regs); // Set text pos
    printf("\n");

    exit(0);
}

//
// I_ZoneBase
//
byte *I_ZoneBase(int *size)
{
    int meminfo[32];
    int heap;
    byte *ptr;

    SetDWords(meminfo, 0, sizeof(meminfo) / 4);
    segread(&segregs);
    segregs.es = segregs.ds;
    regs.w.ax = 0x500; // get memory info
    regs.x.edi = (int)&meminfo;
    int386x(0x31, &regs, &regs, &segregs);

    heap = meminfo[0];
    printf("DPMI memory: %d Kb", heap >> 10);

    do
    {
        heap -= 0x60000; // leave 384kb alone
        if (heap > 0x800000 && !unlimitedRAM)
        {
            heap = 0x800000;
        }
        ptr = malloc(heap);
    } while (!ptr);

    printf(", %d Kb allocated for zone\n", heap >> 10);

    *size = heap;
    return ptr;
}

//
// I_AllocLow
//
byte *I_AllocLow(int length)
{
    byte *mem;

    // DPMI call 100h allocates DOS memory
    segread(&segregs);
    regs.w.ax = 0x0100; // DPMI allocate DOS memory
    regs.w.bx = (length + 15) / 16;
    int386(DPMI_INT, &regs, &regs);
    //segment = regs.w.ax;
    //selector = regs.w.dx;

    mem = (void *)((regs.x.eax & 0xFFFF) << 4);

    memset(mem, 0, length);
    return mem;
}

//
// DPMIInt
//
void DPMIInt(int i)
{
    dpmiregs.ss = realstackseg;
    dpmiregs.sp = REALSTACKSIZE - 4;

    segread(&segregs);
    regs.w.ax = 0x300;
    regs.w.bx = i;
    regs.w.cx = 0;
    regs.x.edi = (unsigned)&dpmiregs;
    segregs.es = segregs.ds;
    int386x(DPMI_INT, &regs, &regs, &segregs);
}
