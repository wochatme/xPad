#pragma once

#include "ztlib.h"


extern const unsigned int xbmpLOpenFileN[64 * 64];
extern const unsigned int xbmpLOpenFileP[64 * 64];
extern const unsigned int xbmpLSubmitN[32 * 32];

int ScreenFillColor(U32* dst, U32 size, U32 color, bool round = false);
int ScreenDrawRect(U32* dst, int w, int h, U32* src, int sw, int sh, int dx, int dy);

