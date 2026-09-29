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

/* The pc input: the keyboard as it is.  main.c queues SDL's key events and
 * MD-runtime.c turns their scan codes into the PC keyboard's; nothing is
 * remapped here, and there is no controller support. */

#include "../Game_defs.h"
#include "../Game_vars.h"
#include "../input.h"

/* A key event as SDL would send it, for Game_virtualkeyboard.c.  The scan code
 * is set too: that is what MD-runtime.c reads. */
void EmulateKey(int type, int key)
{
    SDL_Event pump_event;

    pump_event.type = type;
    pump_event.key.state = (type == SDL_KEYUP)?SDL_RELEASED:SDL_PRESSED;
    pump_event.key.repeat = 0;
    pump_event.key.keysym.sym = (SDL_Keycode) key;
    pump_event.key.keysym.scancode = SDL_GetScancodeFromKey((SDL_Keycode) key);
    pump_event.key.keysym.mod = KMOD_NONE;

    SDL_PushEvent(&pump_event);
}

void Init_Input(void)
{
}

void Init_Input2(void)
{
}

int Config_Input(char *str, char *param)
{
    (void)str;
    (void)param;

    return 0;
}

void Cleanup_Input(void)
{
}

int Handle_Input_Event(SDL_Event *_event)
{
    (void)_event;

    return 0;
}

int Handle_Input_Event2(SDL_Event *_event)
{
    (void)_event;

    return 0;
}

void Handle_Timer_Input_Event(void)
{
}
