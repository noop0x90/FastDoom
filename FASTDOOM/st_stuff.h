//
// Copyright (C) 1993-1996 Id Software, Inc.
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
//	Status bar code.
//	Does the face/direction indicator animatin.
//	Does palette indicators as well (red pain/berserk, bright pickup)
//

#ifndef __STSTUFF_H__
#define __STSTUFF_H__

#include "doomtype.h"
#include "d_event.h"

// Size of statusbar.
// Now sensitive for scaling.
#define ST_HEIGHT 32 * SCREEN_MUL
#define ST_WIDTH SCREENWIDTH
#define ST_Y (SCREENHEIGHT - ST_HEIGHT)
#define ST_BACKGROUND_COLOR 108

//
// STATUS BAR
//

// Called by main loop.
void ST_Responder(event_t *ev);

// Called by main loop.
void ST_Ticker(void);

// Called by main loop.
void ST_Drawer(byte screenblocks, byte refresh);
void ST_DrawerMini();
void ST_DrawerText4025();
void ST_DrawerText8025();
void ST_DrawerText8050();

// Called when the console player is spawned on each level.
void ST_Start(void);

// Called by startup code.
void ST_Init(void);

void ST_createWidgets(void);
void ST_createWidgets_mini(void);

void ST_doPaletteStuff(void);

// States for status bar code.
typedef enum
{
    AutomapState,
    FirstPersonState

} st_stateenum_t;

#endif
