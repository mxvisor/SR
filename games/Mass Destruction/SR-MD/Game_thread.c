/**
 *
 *  Copyright (C) 2016-2026 Roman Pauer
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Game_defs.h"
#include "Game_vars.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"

static int md_saved_argc;
static char **md_saved_argv;

void Game_SetMainArgs(int argc, char *argv[])
{
    md_saved_argc = argc;
    md_saved_argv = argv;
}

/* main_ gets argv as DOS/4GW passed it: an array of 32-bit pointers to strings
 * the game can address (its switch parser at 0x14B34 reads [argv+i*4] from
 * argv[1] on).  The host's argv is 64-bit and outside guest memory, so it is
 * copied, as Albion does.  argv[0] is the program's DOS name; the game does
 * not read it. */
int Game_Main(void)
{
    static const char main_filename[] = "MASSD.EXE";
    unsigned int size;
    uint8_t *argv_local;
    char *str;
    int argc, index, ret;

    if (Thread_Exit)
    {
        return 1;
    }

    argc = (md_saved_argc > 1) ? md_saved_argc : 1;

    size = (argc + 1) * sizeof(uint32_t) + sizeof(main_filename);
    for (index = 1; index < argc; index++)
    {
        size += strlen(md_saved_argv[index]) + 1;
    }

    argv_local = (uint8_t *)x86_malloc(size);
    if (argv_local == NULL)
    {
        fprintf(stderr, "Error: Not enough memory\n");
        return 1;
    }

    str = (char *)(argv_local + (argc + 1) * sizeof(uint32_t));
    for (index = 0; index < argc; index++)
    {
        const char *arg = (index == 0) ? main_filename : md_saved_argv[index];

        ((PTR32(char) *)argv_local)[index] = str;
        strcpy(str, arg);
        str += strlen(arg) + 1;
    }
    ((PTR32(char) *)argv_local)[argc] = NULL;

    ret = Game_Main_Asm(argc, (char **)argv_local);

    x86_free(argv_local);

    return ret;
}

void Game_StopMain(void)
{
    Game_TimerTick += 2;
    Game_StopMain_Asm();
}

int Game_MainThread(void *data)
{
    (void)data;

    Game_CleanState(Thread_Exit);

    {
        int ret;

        ret = Game_Main();
        Game_ExitCode = ret;
    }

    Thread_Exited = 1;

    {
        SDL_Event event;

        event.type = SDL_USEREVENT;
        event.user.code = EC_PROGRAM_QUIT;
        event.user.data1 = NULL;
        event.user.data2 = NULL;

        SDL_PushEvent(&event);
    }

    return 0;
}

/* Nothing of the game to time here: channel 0 interrupts are scheduled in
 * the game thread (SR_CheckTimer, MD-runtime.c), which the retrace-locked tick
 * needs to sub-millisecond precision.  The thread is kept for main.c's
 * lifecycle and for the input frontend's periodic work (a controller's mouse
 * motion in the other games; input/pc.c has none). */
int Game_TimerThread(void *data)
{
    (void)data;

    while (!(Thread_Exit && Thread_Exited))
    {
        Handle_Timer_Input_Event();
        SDL_Delay(10);
    }

    return 0;
}

int Game_FlipThread(void *data)
{
    SDL_Event event;

    (void)data;

    for (;;)
    {
        SDL_SemWait(Game_FlipSem);
        if (Thread_Exit) break;

        Display_Flip_Procedure(Game_FrameBuffer, Game_TextureData);

        event.type = SDL_USEREVENT;
        event.user.code = EC_DISPLAY_FLIP_FINISH;
        event.user.data1 = NULL;
        event.user.data2 = NULL;

        SDL_PushEvent(&event);

        if (Thread_Exit) break;
    }

    return 0;
}
