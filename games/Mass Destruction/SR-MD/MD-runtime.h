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

#ifndef MD_RUNTIME_H
#define MD_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "Game_defs.h"     /* CCALL */
#include "MD-file.h"

/* DOS file handles: one table for both the INT 21h path (MD-runtime.c) and
 * the host-side C library replacements (MD-proc-vfs.c), so a handle from
 * either side means the same file to the other -- the game reads fileno()
 * out of a FILE and hands it to filelength(). */
#define MD_MAX_HANDLES 64

#ifdef __cplusplus
extern "C" {
#endif

int MD_ResolveDataPath(const char *dos_path, char *host_path, size_t host_path_size);
/* MD_WRITE_ROOT, else the game directory (mdfiles/ in the current directory
 * when the game data is not found); MD_RuntimeInit calls it, and so may main
 * before it. */
int MD_SelectWriteRoot(void);
const char *MD_WriteRoot(void);
/* The write root is the game directory: no overlay, one copy of each file. */
int MD_WriteRootIsDataRoot(void);
int MD_GameDirWritePath(const char *dos_path, char *host_path, size_t host_path_size, void **new_in);
void MD_GameDirAdded(void *new_in, const char *host_path);
int MD_AttachHandle(md_file *file);
md_file *MD_HandleFile(int handle);
/* A path on the CD drive: *on_cd = 1 and the file opened (NULL, errno set,
 * when it is missing or `write` is asked for -- the CD is read-only).
 * Otherwise *on_cd = 0. */
md_file *MD_OpenCdFile(const char *dos_path, int write, int *on_cd);
/* Opens dos_path as the game's fopen does (MD-proc-vfs.c), with `kind` 'r',
 * 'w' or 'a' and `update` the '+': the CD, the write root or the game
 * directory.  NULL on failure; errno is EACCES for a write to the CD. */
md_file *MD_OpenGameFile(const char *dos_path, char kind, int update);
void MD_ReleaseHandle(int handle);
int MD_TraceFileEnabled(void);
void MD_DumpGuestMemory(void);

int key_available(void);
uint8_t dos_blocking_getch(void);
uint8_t dos_scan_to_ascii(uint8_t scan_code);
int MD_RuntimeInit(void);
void MD_RuntimeDeinit(void);
void MD_RequestPresent(void);
void MD_DrainEventKeys(void);
/* Reprogram channel 0 as a guest `out 43h/40h` sequence would (MD-sound.c's
 * AIL timers). */
void MD_ProgramPit(uint32_t divisor);
/* Call a guest timer callback as AIL's ISR does; 0 if it could not run now
 * (guest IF clear, or inside another guest ISR). */
int MD_CallGuestTimerCallback(uint32_t target, uint32_t user);
/* Guest IF set and no guest ISR running: a hardware interrupt could be taken. */
int MD_GuestInterruptsEnabled(void);
/* Host address of a flat address in obj2 (the game's data, 0x90000-0x110000). */
void *MD_Obj2Pointer(uint32_t flat);

#ifdef __cplusplus
}
#endif

#endif
