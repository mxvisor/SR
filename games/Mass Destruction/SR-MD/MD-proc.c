/**
 *
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

#include <stdint.h>
#include <stdio.h>

#include "printf_x86.h"
#include "Game_memory.h"

extern "C"
{
    int32_t key_available(void);
    uint8_t dos_blocking_getch(void);

    /* Replacement for WATCOM's kbhit (INT 21h AH=0Bh) via external_procedures.sci
     * at loc_6A34E. Returns -1 (all bits set, 32-bit 0xFFFFFFFF) if a key is
     * available in the queue, or 0 if not -- matching the behavior the game
     * expects after calling through the loc_6A34E/loc_6AA96 dispatch pair. */
    int32_t Game_checkch(void)
    {
        return key_available() ? -1 : 0;
    }

    /* Replacement for WATCOM's getch (INT 21h AH=08h) via external_procedures.sci
     * at loc_6AA96. Blocks until a key is available and returns its ASCII code.
     * Uses dos_blocking_getch() which already implements this behavior. */
    int32_t Game_getch(void)
    {
        return dos_blocking_getch();
    }

    static void md_stdout_char(char character, void *arg)
    {
        fputc(character, (FILE *)arg);
    }

    /* Replacement for WATCOM's printf (INT 21h AH=40h through the guest's own
     * stdio) via external_procedures.sci at loc_691AB.
     *
     * The point is not speed but flushing. WATCOM line-buffers stdout, and the
     * startup banner is printf("\nLoading") followed by one printf(".") per
     * resource -- no trailing newline anywhere. The leading "\n" flushes, then
     * "Loading" and its dots sit in the guest's buffer with nothing to push
     * them out, so they were still there, unprinted, when the game switched to
     * mode 13h. DOSBox shows the banner because DOS writes reach the screen as
     * they are made. Formatting host-side and flushing per call restores that.
     *
     * `ap` points at the caller's varargs on the guest stack; printf_x86.c
     * reads them 32-bit-at-a-time, which is what the guest pushed. */
    int32_t MD_vprintf(const char *format, uint32_t *ap)
    {
        int res;

        res = vfctprintf_x86(md_stdout_char, stdout, format, ap);
        fflush(stdout);

        return res;
    }

    /* Replacement for WATCOM's sprintf via external_procedures.sci at
     * loc_6998A. That one is __prtf with a store-to-memory output routine and
     * a terminating NUL written at buf[result]; vsprintf_x86 does the same. */
    int32_t MD_vsprintf(char *buf, const char *format, uint32_t *ap)
    {
        return vsprintf_x86(buf, format, ap);
    }

    /* Replacement for WATCOM's _nmalloc via external_procedures.sci at
     * loc_69894 (and the AIL_mem_use_malloc hook at loc_B899C, which holds
     * its address). Its callers are the game's MemInit pool (loc_543C8),
     * Miles AIL, and the C runtime's own stdio/argv/environment setup. The
     * two range checks are _nmalloc's own: 0 and anything above 0xFFFFFFD4
     * return NULL without allocating. */
    void *Game_malloc(uint32_t size)
    {
        if (size == 0 || size > 0xFFFFFFD4u) return NULL;
        return x86_malloc(size);
    }

    /* Replacement for WATCOM's _nfree at loc_6AE62 (and the AIL_mem_use_free
     * hook at loc_B89A0). Every free site in MASSD.EXE releases a pointer
     * that came from one of the _nmalloc sites above, so both move together. */
    void Game_free(void *ptr)
    {
        x86_free(ptr);
    }
}
