/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

/* The pc display: a resizable window, 4:3 in shape, showing mode 13h.
 *
 * The flip thread expands the 320x200 frame through the palette into 32-bit
 * pixels here (Display_Flip_Procedure); main.c scales that up by whole pixels
 * and keeps the picture 4:3 inside the window (Game_Display_Flip).  The
 * palette is the game's DAC in 8-bit levels, set by MD-runtime.c's port 3C9h
 * through Set_Palette_Value. */

#include "../Game_defs.h"
#include "../Game_vars.h"
#include "../display.h"
#include "palette32bgra.h"

static void Set_Palette_Value2(uint32_t index, uint32_t r, uint32_t g, uint32_t b)
{
    (void)r;
    (void)g;
    (void)b;

    /* opaque: the texture is ARGB8888 */
    Game_Palette[index].s.a = 255;
}

static void Flip_320x200x8_to_320x200x32(const uint8_t *src, uint32_t *dst)
{
    int counter;

    for (counter = 320*200; counter != 0; counter-=8)
    {
        dst[0] = Game_Palette[src[0]].pix;
        dst[1] = Game_Palette[src[1]].pix;
        dst[2] = Game_Palette[src[2]].pix;
        dst[3] = Game_Palette[src[3]].pix;
        dst[4] = Game_Palette[src[4]].pix;
        dst[5] = Game_Palette[src[5]].pix;
        dst[6] = Game_Palette[src[6]].pix;
        dst[7] = Game_Palette[src[7]].pix;

        src+=8;
        dst+=8;
    }
}

void Init_Display(void)
{
    Display_FSType = 1;
    Display_Fullscreen = 0;
}

void Init_Display2(void)
{
    int index, scale;
    SDL_Rect usable;

    Init_Palette();
    for (index = 0; index < 256; index++)
    {
        Game_Palette[index].s.a = 255;
    }

    /* 4:3, the shape Game_Display_Flip gives mode 13h: the largest whole
     * multiple of 320x240 that fits in 4/5 of the desktop, at least 640x480. */
    scale = 2;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
    {
        while (320 * (scale + 1) <= usable.w * 4 / 5 && 240 * (scale + 1) <= usable.h * 4 / 5) scale++;
    }

    Display_Width = 320 * scale;
    Display_Height = 240 * scale;
    Display_Bitsperpixel = 32;
    Display_MouseLocked = 0;
    Render_Width = 320;
    Render_Height = 200;
    Display_Flip_Procedure = (Game_Flip_Procedure) &Flip_320x200x8_to_320x200x32;
}

int Config_Display(char *str, char *param)
{
    (void)str;
    (void)param;

    return 0;
}

void Cleanup_Display(void)
{
}
