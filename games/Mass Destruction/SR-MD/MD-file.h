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

#ifndef MD_FILE_H
#define MD_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* An open DOS file: a host file (the game directory and the write root), or
 * a file on the CD (MD-cdrom.c), which is read out of the image sector by
 * sector.  The calls mirror the stdio ones the file I/O replacements need. */
typedef struct md_file md_file;

#ifdef __cplusplus
extern "C" {
#endif

md_file *md_file_host(FILE *file);
md_file *md_file_cd(uint32_t lba, uint32_t size);

size_t md_fread(void *buffer, size_t length, md_file *file);
size_t md_fwrite(const void *buffer, size_t length, md_file *file);
int md_fseek(md_file *file, long offset, int origin);
long md_ftell(md_file *file);
int md_fgetc(md_file *file);
int md_fungetc(int character, md_file *file);
int md_fputc(int character, md_file *file);
/* Cuts or extends the file to its current position, as DOS's zero-byte
 * write does; -1 on a CD file or a failure. */
int md_ftruncate(md_file *file);
int md_feof(md_file *file);
int md_ferror(md_file *file);
void md_clearerr(md_file *file);
void md_setbuf(md_file *file, int buffered);
int md_fclose(md_file *file);

#ifdef __cplusplus
}
#endif

#endif
