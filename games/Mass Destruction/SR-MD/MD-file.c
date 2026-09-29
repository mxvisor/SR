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

/* DOS files over host stdio or over the CD image (see MD-file.h). */

#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "MD-cdrom.h"
#include "MD-file.h"

extern "C" {

struct md_file {
    FILE *host;             /* NULL for a CD file */
    /* a CD file: read-only, `size` bytes from sector `lba` on */
    uint32_t lba;
    uint32_t size;
    uint32_t position;
    int eof;
    int error;
    int pushed;             /* md_fungetc's character, or EOF; `position` is
                               already back on it */
    uint32_t cached;        /* sector in `sector`, or UINT32_MAX */
    uint8_t sector[MD_CD_SECTOR_DATA];
};

md_file *md_file_host(FILE *host)
{
    if (host == NULL) return NULL;
    md_file *file = (md_file *)calloc(1, sizeof(md_file));
    if (file == NULL) return NULL;
    file->host = host;
    file->pushed = EOF;
    return file;
}

md_file *md_file_cd(uint32_t lba, uint32_t size)
{
    md_file *file = (md_file *)calloc(1, sizeof(md_file));
    if (file == NULL) return NULL;
    file->lba = lba;
    file->size = size;
    file->pushed = EOF;
    file->cached = UINT32_MAX;
    return file;
}

size_t md_fread(void *buffer, size_t length, md_file *file)
{
    if (file->host != NULL) return fread(buffer, 1, length, file->host);
    uint8_t *target = (uint8_t *)buffer;
    size_t done = 0;
    if (length != 0 && file->pushed != EOF) {
        target[done++] = (uint8_t)file->pushed;
        file->pushed = EOF;
        file->position++;
    }
    while (done < length) {
        if (file->position >= file->size) {
            file->eof = 1;
            break;
        }
        uint32_t sector = file->position / MD_CD_SECTOR_DATA;
        uint32_t within = file->position % MD_CD_SECTOR_DATA;
        size_t part = MD_CD_SECTOR_DATA - within;
        if (part > length - done) part = length - done;
        if (part > file->size - file->position) part = file->size - file->position;
        if (within == 0 && part == MD_CD_SECTOR_DATA) {
            /* whole sectors go straight to the caller */
            if (MD_CdReadSector(file->lba + sector, target + done) != 0) {
                file->error = 1;
                break;
            }
        } else {
            if (file->cached != sector) {
                if (MD_CdReadSector(file->lba + sector, file->sector) != 0) {
                    file->error = 1;
                    break;
                }
                file->cached = sector;
            }
            memcpy(target + done, file->sector + within, part);
        }
        done += part;
        file->position += (uint32_t)part;
    }
    return done;
}

size_t md_fwrite(const void *buffer, size_t length, md_file *file)
{
    if (file->host != NULL) return fwrite(buffer, 1, length, file->host);
    file->error = 1;        /* the CD is read-only */
    return 0;
}

int md_fseek(md_file *file, long offset, int origin)
{
    if (file->host != NULL) return fseek(file->host, offset, origin);
    long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)file->position :
                origin == SEEK_END ? (long)file->size : -1;
    if (base < 0 || base + offset < 0) return -1;
    file->position = (uint32_t)(base + offset);
    file->pushed = EOF;
    file->eof = 0;
    return 0;
}

long md_ftell(md_file *file)
{
    if (file->host != NULL) return ftell(file->host);
    return (long)file->position;
}

int md_fgetc(md_file *file)
{
    if (file->host != NULL) return getc(file->host);
    uint8_t character;
    return md_fread(&character, 1, file) == 1 ? character : EOF;
}

int md_fungetc(int character, md_file *file)
{
    if (file->host != NULL) return ungetc(character, file->host);
    if (character == EOF || file->pushed != EOF || file->position == 0) return EOF;
    file->pushed = character & 0xff;
    file->position--;
    file->eof = 0;
    return file->pushed;
}

int md_fputc(int character, md_file *file)
{
    if (file->host != NULL) return putc(character, file->host);
    file->error = 1;
    return EOF;
}

int md_ftruncate(md_file *file)
{
    if (file->host == NULL) {
        file->error = 1;
        return -1;
    }
    long position = ftell(file->host);
    if (position < 0 || fflush(file->host) != 0) return -1;
#if defined(_WIN32)
    return _chsize_s(_fileno(file->host), position) == 0 ? 0 : -1;
#else
    return ftruncate(fileno(file->host), (off_t)position);
#endif
}

int md_feof(md_file *file)
{
    return file->host != NULL ? feof(file->host) : file->eof;
}

int md_ferror(md_file *file)
{
    return file->host != NULL ? ferror(file->host) : file->error;
}

void md_clearerr(md_file *file)
{
    if (file->host != NULL) clearerr(file->host);
    file->eof = 0;
    file->error = 0;
}

void md_setbuf(md_file *file, int buffered)
{
    if (file->host != NULL) setvbuf(file->host, NULL, buffered ? _IOFBF : _IONBF, BUFSIZ);
}

int md_fclose(md_file *file)
{
    int result = file->host != NULL ? fclose(file->host) : 0;
    free(file);
    return result;
}

}
