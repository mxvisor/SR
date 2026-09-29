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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "Game_defs.h"

#define DEFINE_VARIABLES
    #include "Game_vars.h"
#undef DEFINE_VARIABLES

#include "audio.h"
#include "display.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "input.h"
#include "main.h"
#include "MD-runtime.h"

#if defined(_WIN32)
#include <windows.h>
#endif

void Game_SetMainArgs(int argc, char *argv[]);

/* Albion/Warcraft Game_CleanState minus display/semaphore pieces MD does not own. */
void Game_CleanState(int imm)
{
    int i;

    (void)imm;

    for (i = 0; i < 256; i++)
    {
        if (Game_AllocatedMemory[i] != NULL)
        {
            x86_free(Game_AllocatedMemory[i]);
            Game_AllocatedMemory[i] = NULL;
        }
    }

    Game_ScreenWindow = Game_FrameBuffer;
    Game_ScreenWindowNum = 0;
    Game_DisplayStart = 0;
    Game_NextMemory = 0;

    Game_MQueueWrite = 0;
    Game_MQueueRead = 0;
    Game_KQueueWrite = 0;
    Game_KQueueRead = 0;
    Game_KBufferWrite = 0;
    Game_KBufferRead = 0;
    Game_LastKeyStroke = 0;

    memset(&Game_InterruptTable, 0, sizeof(Game_InterruptTable));
    memset(&Game_MouseTable, 0, sizeof(Game_MouseTable));
    memset(&Game_Palette_Or, 0, sizeof(Game_Palette_Or));
}

/* Game_Texture[0] scaled up by whole pixels; see Game_Display_Flip. */
static int ScaledWidth, ScaledHeight;

/* The window, sized by display/pc.c's Init_Display2, and what the flip thread
 * draws into: Game_TextureData, Render_Width x Render_Height 32-bit pixels. */
static int Game_Display_Create(void)
{
    Game_Window = SDL_CreateWindow("Mass Destruction", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   Display_Width, Display_Height, SDL_WINDOW_RESIZABLE);
    if (Game_Window == NULL) return -1;
    /* resizable; the picture keeps 4:3 inside whatever size the window gets */
    SDL_SetWindowMinimumSize(Game_Window, 320, 240);

    Game_Renderer = SDL_CreateRenderer(Game_Window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (Game_Renderer == NULL) Game_Renderer = SDL_CreateRenderer(Game_Window, -1, SDL_RENDERER_SOFTWARE);
    if (Game_Renderer == NULL) return -1;

    Game_Texture[0] = SDL_CreateTexture(Game_Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        Render_Width, Render_Height);
    if (Game_Texture[0] == NULL) return -1;
    SDL_SetTextureScaleMode(Game_Texture[0], SDL_ScaleModeNearest);

    Game_TextureData = calloc(Render_Width * Render_Height, Display_Bitsperpixel / 8);
    if (Game_TextureData == NULL)
    {
        SDL_OutOfMemory();
        return -1;
    }

    return 0;
}

static void Game_Display_Destroy(void)
{
    if (Game_ScaledTexture[0] != NULL) SDL_DestroyTexture(Game_ScaledTexture[0]);
    if (Game_Texture[0] != NULL) SDL_DestroyTexture(Game_Texture[0]);
    if (Game_Renderer != NULL) SDL_DestroyRenderer(Game_Renderer);
    if (Game_Window != NULL) SDL_DestroyWindow(Game_Window);
    free(Game_TextureData);

    Game_ScaledTexture[0] = NULL;
    Game_Texture[0] = NULL;
    Game_Renderer = NULL;
    Game_Window = NULL;
    Game_TextureData = NULL;
}

/* Largest 4:3 rectangle centred in the output.  Mode 13h is 320x200 with
 * non-square pixels made for a 4:3 monitor; dosbox-staging's default
 * (aspect = true) shows it the same way. */
static void Display_PictureRect(int output_width, int output_height, SDL_Rect *rect)
{
    rect->w = output_width;
    rect->h = output_width * 3 / 4;
    if (rect->h > output_height)
    {
        rect->h = output_height;
        rect->w = output_height * 4 / 3;
    }
    rect->x = (output_width - rect->w) / 2;
    rect->y = (output_height - rect->h) / 2;
}

/* Main thread only, on EC_DISPLAY_FLIP_FINISH.  The frame goes
 * nearest-neighbour into Game_ScaledTexture[0] at the smallest whole multiple
 * that covers the picture, and that is filtered down to the picture: pixels
 * keep sharp edges and even rows at any window size.  This is dosbox-staging's
 * default 'sharp' shader, and X-Com's advanced scaling with the normal
 * scaler. */
static void Game_Display_Flip(void)
{
    SDL_Texture *source = Game_Texture[0];
    SDL_Rect picture;
    int output_width, output_height;

    if (Game_Renderer == NULL || Game_Texture[0] == NULL) return;
    SDL_UpdateTexture(Game_Texture[0], NULL, Game_TextureData, Render_Width * Display_Bitsperpixel / 8);
    if (SDL_GetRendererOutputSize(Game_Renderer, &output_width, &output_height) != 0) return;
    Display_PictureRect(output_width, output_height, &picture);
    if (picture.w <= 0 || picture.h <= 0) return;

    int width = Render_Width * ((picture.w + Render_Width - 1) / Render_Width);
    int height = Render_Height * ((picture.h + Render_Height - 1) / Render_Height);
    if (width != (int)Render_Width || height != (int)Render_Height)
    {
        if (Game_ScaledTexture[0] == NULL || ScaledWidth != width || ScaledHeight != height)
        {
            if (Game_ScaledTexture[0] != NULL) SDL_DestroyTexture(Game_ScaledTexture[0]);
            Game_ScaledTexture[0] = SDL_CreateTexture(Game_Renderer, SDL_PIXELFORMAT_ARGB8888,
                                                      SDL_TEXTUREACCESS_TARGET, width, height);
            if (Game_ScaledTexture[0] != NULL) SDL_SetTextureScaleMode(Game_ScaledTexture[0], SDL_ScaleModeLinear);
            ScaledWidth = width;
            ScaledHeight = height;
        }
        /* No target support: fall back to plain nearest-neighbour scaling. */
        if (Game_ScaledTexture[0] != NULL && SDL_SetRenderTarget(Game_Renderer, Game_ScaledTexture[0]) == 0)
        {
            SDL_RenderCopy(Game_Renderer, Game_Texture[0], NULL, NULL);
            SDL_SetRenderTarget(Game_Renderer, NULL);
            source = Game_ScaledTexture[0];
        }
    }
    SDL_SetRenderDrawColor(Game_Renderer, 0, 0, 0, 255);
    SDL_RenderClear(Game_Renderer);
    SDL_RenderCopy(Game_Renderer, source, NULL, &picture);
    SDL_RenderPresent(Game_Renderer);
}

/* the player closed the window: the game's stop is not an error to report */
static int Game_QuitRequested;

static void Game_Event_Loop(void)
{
    SDL_Thread *MainThread;
    SDL_Thread *FlipThread;
    SDL_Thread *TimerThread;
    SDL_Event event;
    uint32_t AppMouseFocus;
    uint32_t AppInputFocus;
    uint32_t AppActive;
    int FlipActive, NumEvents, PumpEvents;

    Thread_Exited = 0;
    Thread_Exit = 0;
    Game_ExitCode = 0;
    Game_TimerRunning = 0;
    Game_TimerTick = 0;
    Game_TimerRun = 0;
    Game_VSyncTick = 0;

    Game_FlipSem = SDL_CreateSemaphore(0);
    if (Game_FlipSem == NULL)
    {
        fprintf(stderr, "Error: Unable to create semaphore\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        return;
    }

    TimerThread = SDL_CreateThread(Game_TimerThread, "timer", NULL);
    if (TimerThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start timer thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    FlipThread = SDL_CreateThread(Game_FlipThread, "flip", NULL);
    if (FlipThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start flip thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_WaitThread(TimerThread, NULL);
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    MainThread = SDL_CreateThread(Game_MainThread, "main", NULL);
    if (MainThread == NULL)
    {
        fprintf(stderr, "Error: Unable to start main thread\n");
        Thread_Exited = 1;
        Thread_Exit = 1;
        SDL_SemPost(Game_FlipSem);
        SDL_WaitThread(FlipThread, NULL);
        SDL_WaitThread(TimerThread, NULL);
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
        return;
    }

    AppMouseFocus = 1;
    AppInputFocus = 1;
    AppActive = 1;
    FlipActive = 0;
    PumpEvents = 1;

    while (!Thread_Exited || !Thread_Exit)
    {
        NumEvents = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        if (NumEvents <= 0)
        {
            if (PumpEvents)
            {
                PumpEvents = 0;
                SDL_PumpEvents();
            }
            else
            {
                SDL_Delay(1);
                PumpEvents = 1;
            }
            continue;
        }

        if (Handle_Input_Event(&event)) continue;

        switch (event.type)
        {
            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                        /* Without a WM, xdotool windowclose / XDestroyWindow never
                         * becomes SDL_QUIT — only this event. Same shutdown path. */
                        if (Thread_Exit) exit(1);

                        Game_QuitRequested = 1;
                        Thread_Exit = 1;

                        SDL_SemPost(Game_FlipSem);

                        SDL_WaitThread(FlipThread, NULL);
                        SDL_WaitThread(MainThread, NULL);
                        SDL_WaitThread(TimerThread, NULL);
                        break;
                    case SDL_WINDOWEVENT_ENTER:
                        AppMouseFocus = 1;
                        break;
                    case SDL_WINDOWEVENT_LEAVE:
                        AppMouseFocus = 0;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        AppInputFocus = 1;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        AppInputFocus = 0;
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                        AppActive = 0;
                        break;
                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        AppActive = 1;
                        break;
                }
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (!event.key.repeat && AppActive && AppInputFocus)
                {
                    if (((Game_KQueueWrite + 1) & (GAME_KQUEUE_LENGTH - 1)) == Game_KQueueRead)
                    {
                        /* keyboard event queue overflow */
                    }
                    else
                    {
                        Game_EventKQueue[Game_KQueueWrite] = event;
                        Game_KQueueWrite = (Game_KQueueWrite + 1) & (GAME_KQUEUE_LENGTH - 1);
                    }
                }
                break;

            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEWHEEL:
                if (AppActive && AppInputFocus && AppMouseFocus)
                {
                    if (((Game_MQueueWrite + 1) & (GAME_MQUEUE_LENGTH - 1)) == Game_MQueueRead)
                    {
                        /* mouse event queue overflow */
                    }
                    else
                    {
                        Game_EventMQueue[Game_MQueueWrite] = event;
                        Game_MQueueWrite = (Game_MQueueWrite + 1) & (GAME_MQUEUE_LENGTH - 1);
                    }
                }
                break;

            case SDL_QUIT:
                if (Thread_Exit) exit(1);

                Game_QuitRequested = 1;
                Thread_Exit = 1;

                SDL_SemPost(Game_FlipSem);

                SDL_WaitThread(FlipThread, NULL);
                SDL_WaitThread(MainThread, NULL);
                SDL_WaitThread(TimerThread, NULL);
                break;

            case SDL_USEREVENT:
                switch (event.user.code)
                {
                    case EC_DISPLAY_FLIP_START:
                        if (!FlipActive && Game_FlipSem != NULL)
                        {
                            FlipActive = 1;
                            SDL_SemPost(Game_FlipSem);
                        }
                        break;

                    case EC_DISPLAY_FLIP_FINISH:
                        if (FlipActive)
                        {
                            Game_Display_Flip();
                            FlipActive = 0;
                        }
                        break;

                    case EC_PROGRAM_QUIT:
                        if (!Thread_Exit)
                        {
                            Thread_Exit = 1;

                            SDL_SemPost(Game_FlipSem);

                            SDL_WaitThread(FlipThread, NULL);
                            SDL_WaitThread(MainThread, NULL);
                            SDL_WaitThread(TimerThread, NULL);
                        }
                        break;
                }
                break;

            default:
                Handle_Input_Event2(&event);
                break;
        }
    }

    if (Game_FlipSem != NULL)
    {
        SDL_DestroySemaphore(Game_FlipSem);
        Game_FlipSem = NULL;
    }
}

#if defined(_WIN32)
/* A GUI program (-mwindows) starts without a console.  Unless the caller
 * gave it somewhere to write, the game's messages (stdout) and the port's
 * (stderr) go to mass-destruction.log in the write root; the game's own
 * listing of C:\ skips the name, which is not 8.3. */
static char Log_Path[MAX_PATH];
static int Log_Has_Stdout;

static int Has_Std_Handle(DWORD which)
{
    HANDLE handle = GetStdHandle(which);
    return handle != NULL && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

static void Log_Open(void)
{
    int has_stdout = Has_Std_Handle(STD_OUTPUT_HANDLE);
    int has_stderr = Has_Std_Handle(STD_ERROR_HANDLE);
    FILE *log;
    int length;

    if ((has_stdout && has_stderr) || MD_SelectWriteRoot() != 0) return;
    CreateDirectoryA(MD_WriteRoot(), NULL);
    length = snprintf(Log_Path, sizeof(Log_Path), "%s\\mass-destruction.log", MD_WriteRoot());
    if (length < 0 || length >= (int)sizeof(Log_Path) || (log = fopen(Log_Path, "w")) == NULL)
    {
        Log_Path[0] = 0;
        return;
    }
    fclose(log);
    for (char *separator = Log_Path; *separator != 0; separator++)
    {
        if (*separator == '/') *separator = '\\';
    }

    /* both in append mode and unbuffered, so their lines keep their order */
    if (!has_stdout && freopen(Log_Path, "a", stdout) != NULL)
    {
        setvbuf(stdout, NULL, _IONBF, 0);
        Log_Has_Stdout = 1;
    }
    if (!has_stderr && freopen(Log_Path, "a", stderr) != NULL) setvbuf(stderr, NULL, _IONBF, 0);
}

/* The last lines of the log, which say why the game stopped: its message
 * ("Insert Mass Destruction CD-ROM.") and the port's. */
static void Log_Show(void)
{
    char buffer[4096];
    char text[sizeof(buffer) + MAX_PATH + 64];
    char *lines[256];
    int count = 0, first;
    size_t length;
    long size;
    FILE *log = fopen(Log_Path, "rb");

    if (log == NULL) return;
    fseek(log, 0, SEEK_END);
    size = ftell(log);
    fseek(log, (size >= (long)sizeof(buffer)) ? size - (long)(sizeof(buffer) - 1) : 0, SEEK_SET);
    length = fread(buffer, 1, sizeof(buffer) - 1, log);
    fclose(log);
    buffer[length] = 0;

    for (char *line = strtok(buffer, "\r\n"); line != NULL && count < 256; line = strtok(NULL, "\r\n"))
    {
        lines[count++] = line;
    }
    first = (size >= (long)sizeof(buffer)) ? 1 : 0;   /* a cut line */
    if (count - first > 12) first = count - 12;

    text[0] = 0;
    for (int index = first; index < count; index++)
    {
        strncat(text, lines[index], sizeof(text) - strlen(text) - 3);
        strcat(text, "\r\n");
    }
    snprintf(text + strlen(text), sizeof(text) - strlen(text), "\r\nThe whole output is in %s", Log_Path);
    MessageBoxA(NULL, text, "Mass Destruction", MB_OK | MB_ICONERROR);
}
#endif

static int Game_Run(int argc, char *argv[])
{
    Init_Display();
    Init_Audio();
    Init_Input();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    Init_Display2();
    Init_Audio2();
    Init_Input2();

    if (Game_Display_Create() != 0)
    {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

    if (x86_init_malloc() != 0)
    {
        fprintf(stderr, "Could not initialize the 32-bit guest allocator\n");
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

#ifdef PTROFS_64BIT
    if (initialize_pointer_offset() != 0)
    {
        fprintf(stderr, "Could not initialize the guest pointer offset\n");
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }
#endif

    if (MD_RuntimeInit() != 0)
    {
        fprintf(stderr, "Could not initialize the Mass Destruction runtime: %s\n", SDL_GetError());
        x86_deinit_malloc();
        Game_Display_Destroy();
        SDL_Quit();
        return 1;
    }

    Game_SetMainArgs(argc, argv);
    Game_Event_Loop();

    MD_RuntimeDeinit();
    x86_deinit_malloc();
    Cleanup_Input();
    Cleanup_Audio();
    Cleanup_Display();
    Game_Display_Destroy();
    SDL_Quit();
    return Game_ExitCode;
}

int main(int argc, char *argv[])
{
    int code;

#if defined(_WIN32)
    Log_Open();
#endif
    code = Game_Run(argc, argv);
#if defined(_WIN32)
    /* The game stops on an error with exit(-1), 255, or exit(1) after "File
     * Error"; the port returns 1 when it cannot start.  A normal exit returns
     * what the game's last printf returned, 38.  Closing the window stops the
     * game with 1 or 255 too, and says nothing. */
    if (Log_Has_Stdout && !Game_QuitRequested && (code == 1 || code == 255)) Log_Show();
#endif
    return code;
}
