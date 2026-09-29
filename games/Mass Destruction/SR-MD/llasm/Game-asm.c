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

/* Minimal C<->llasm glue: just enough to call into the recompiled entry point.
   Unlike Albion's Game-asm.c (which this is modelled on), there is no timer-ISR reentry
   point or function-pointer-table call bridge (Game_RunTimer_Asm, Game_MouseMove, etc.):
   nothing calls into the game that way, and each would need its own alias in
   SR-games/Mass Destruction/SR/llasm/global_aliases.sci (which has only main_). */

#include <stdlib.h>
#if !(defined(__GNUC__) && defined(_WIN64))
#include <setjmp.h>
#if defined(_MSC_VER) && defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif
#include "llasm_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

extern _cpu *x86_initialize_cpu(void);
extern void x86_deinitialize_cpu(void);

extern void CCALL c_main_(CPU);

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) && defined(_WIN64)
static intptr_t exit_env[5];
#else
static jmp_buf exit_env;
#if defined(_MSC_VER) && defined(_WIN64)
static int (*dyn_intrinsic_setjmp)(jmp_buf, void *);
static void (*dyn_longjmp)(jmp_buf, int);
#endif
#endif

static int main_return_value;


EXTERNC void CCALL Game_ExitMain_Asm(int32_t status)
{
    main_return_value = status;
#if defined(__GNUC__) && defined(_WIN64)
    __builtin_longjmp(exit_env, 1);
#elif defined(_MSC_VER) && defined(_WIN64)
    (dyn_longjmp != NULL) ? dyn_longjmp(exit_env, 1) : longjmp(exit_env, 1);
#else
    longjmp(exit_env, 1);
#endif
}

void CCALL Game_StopMain_Asm(void)
{
    main_return_value = 1;
#if defined(__GNUC__) && defined(_WIN64)
    __builtin_longjmp(exit_env, 1);
#elif defined(_MSC_VER) && defined(_WIN64)
    (dyn_longjmp != NULL) ? dyn_longjmp(exit_env, 1) : longjmp(exit_env, 1);
#else
    longjmp(exit_env, 1);
#endif
}

int CCALL Game_Main_Asm(int argc, char *argv[])
{
#if defined(__GNUC__) && defined(_WIN64)
    if (__builtin_setjmp(exit_env) == 0)
#elif defined(_MSC_VER) && defined(_WIN64)
    HMODULE hLib;

    hLib = GetModuleHandleW(L"ucrtbase.dll");
    if (hLib != NULL)
    {
        dyn_intrinsic_setjmp = (int (*)(jmp_buf, void *))GetProcAddress(hLib, "__intrinsic_setjmp");
        dyn_longjmp = (void (*)(jmp_buf, int))GetProcAddress(hLib, "longjmp");
    }
    if (hLib == NULL || dyn_intrinsic_setjmp == NULL || dyn_longjmp == NULL)
    {
        dyn_intrinsic_setjmp = NULL;
        dyn_longjmp = NULL;
    }

    if (((dyn_intrinsic_setjmp != NULL) ? dyn_intrinsic_setjmp(exit_env, NULL) : setjmp(exit_env)) == 0)
#else
    if (setjmp(exit_env) == 0)
#endif
    {
        _cpu *cpu;
        int retval;

        cpu = x86_initialize_cpu();

        eax = argc;
        edx = PTR2REG(argv);

        c_main_(cpu);

        retval = eax;

        x86_deinitialize_cpu();

        return retval;
    }
    else
    {
        x86_deinitialize_cpu();

        return main_return_value;
    }
}
