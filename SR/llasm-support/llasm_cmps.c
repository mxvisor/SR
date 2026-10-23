//part of static recompiler -- do not edit

/**
 *
 *  Copyright (C) 2021-2026 Roman Pauer
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

#include "llasm_cpu.h"

EXTERNC uint32_t CCALL x86_repe_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 != srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}

EXTERNC uint32_t CCALL x86_repne_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 == srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}

/* the word and doubleword forms leave the last pair of elements behind esi
 * and edi: the caller takes them from there to set the flags, because two
 * values do not fit in the return value */

#define X86_REP_CMPS(NAME, TYPE, STEP, STOP_WHEN_EQUAL) \
EXTERNC uint32_t CCALL NAME(CPU) \
{ \
    int32_t dir; \
    uint32_t srcptr1, srcptr2, counter; \
    TYPE srcvalue1, srcvalue2; \
\
    dir = (eflags & DF)?-(STEP):(STEP); \
\
    counter = ecx; \
    srcptr1 = esi; \
    srcptr2 = edi; \
\
    do { \
        srcvalue1 = *((TYPE *)REG2PTR(srcptr1)); \
        srcvalue2 = *((TYPE *)REG2PTR(srcptr2)); \
        srcptr1 += dir; \
        srcptr2 += dir; \
\
        counter--; \
\
        if ((srcvalue1 == srcvalue2) == (STOP_WHEN_EQUAL)) break; \
    } while (counter != 0); \
\
    ecx = counter; \
    esi = srcptr1; \
    edi = srcptr2; \
\
    return 0; \
}

X86_REP_CMPS(x86_repe_cmpsw, uint16_t, 2, 0)
X86_REP_CMPS(x86_repne_cmpsw, uint16_t, 2, 1)
X86_REP_CMPS(x86_repe_cmpsd, uint32_t, 4, 0)
X86_REP_CMPS(x86_repne_cmpsd, uint32_t, 4, 1)
