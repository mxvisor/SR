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

/* Host-side replacements for the WATCOM C library's file I/O, wired through
 * external_procedures.sci. Two families, both of them everything the game
 * (and the Miles AIL code linked into MASSD.EXE) calls:
 *
 *   streams:   fopen loc_694DC, fclose loc_69634, fread loc_6A51A,
 *              fwrite loc_6A6F5, fseek loc_6A41E, fgetc loc_6B1E2,
 *              setbuf loc_6BEE1, fprintf loc_69873, fscanf loc_69613
 *   handles:   open loc_6AAB3, read loc_6AD62, close loc_6AE5D,
 *              filelength loc_6AD25
 *
 * They replace a guest stdio that worked, through INT 21h. Every file the
 * game writes (ERROR.LOG, TANK.SAV, ...) goes to MD_WriteRoot(): the game
 * directory itself, as in DOS and in the other SR ports, unless MD_WRITE_ROOT
 * moves it; there the write root is an overlay, and reads look into it
 * first.
 *
 * Handles are MD-runtime.c's DOS handle table, shared with the INT 21h path,
 * so a FILE's _handle is a real DOS handle to both. */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Game_memory.h"
#include "MD-runtime.h"
#include "printf_x86.h"

/* WATCOM's FILE, #pragma pack(1), 26 bytes. The game reads two fields of it
 * inline, without a call: _flag through the feof() macro (eleven sites, e.g.
 * loc_10D4E `test byte [ebp+0xc], 0x10`; AIL's INI reader also tests _SFERR),
 * and _handle through fileno() (loc_5C212, loc_5C50E, into filelength). Only
 * those two are kept up to date; nothing reaches the rest. */
#pragma pack(push, 1)
typedef struct {
    uint32_t ptr;
    int32_t cnt;
    uint32_t base;
    uint32_t flag;
    int32_t handle;
    uint32_t bufsize;
    uint8_t ungotten;
    uint8_t tmpfchar;
} watcom_file;
#pragma pack(pop)

#define WATCOM_READ 0x0001u
#define WATCOM_WRITE 0x0002u
#define WATCOM_EOF 0x0010u
#define WATCOM_SFERR 0x0020u
#define WATCOM_BINARY 0x0040u

#define WATCOM_O_ACCMODE 0x0003
#define WATCOM_O_APPEND 0x0010
#define WATCOM_O_CREAT 0x0020
#define WATCOM_O_TRUNC 0x0040
#define WATCOM_O_BINARY 0x0200

enum { LAST_NONE, LAST_READ, LAST_WRITE };

typedef struct {
    watcom_file *guest;
    int text;
    int last;
} md_stream;

/* Indexed by DOS handle. */
static md_stream streams[MD_MAX_HANDLES];

static int host_file_exists(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

/* "C:\MAPS\level30.tb1" -> "MAPS/LEVEL30.TB1". Every DOS drive is the same
 * directory (see resolve_dos_path), and nothing changes the DOS current
 * directory from the root, so the drive and leading separators just go. */
static int write_root_path(const char *dos_path, char *path, size_t size)
{
    const char *source = dos_path;
    size_t length;

    if (source[0] != 0 && source[1] == ':') source += 2;
    while (*source == '\\' || *source == '/') source++;
    if (*source == 0) return -1;

    length = (size_t)snprintf(path, size, "%s/", MD_WriteRoot());
    for (; *source != 0; source++) {
        if (length + 1 >= size) return -1;
        path[length++] = (*source == '\\') ? '/' : (char)toupper((unsigned char)*source);
    }
    path[length] = 0;
    return 0;
}

static int is_host_separator(char character)
{
#if defined(_WIN32)
    return character == '/' || character == '\\';
#else
    return character == '/';
#endif
}

static int make_parent_directories(char *path)
{
    char *start = path + 1;
#if defined(_WIN32)
    if (path[0] != 0 && path[1] == ':' && path[2] != 0) start = path + 3;   /* not "C:" itself */
#endif
    for (char *separator = start; *separator != 0; separator++) {
        if (!is_host_separator(*separator)) continue;
        char saved = *separator;
        *separator = 0;
#if defined(_WIN32)
        int result = mkdir(path);
#else
        int result = mkdir(path, 0755);
#endif
        *separator = saved;
        if (result != 0 && errno != EEXIST) return -1;
    }
    return 0;
}

static int copy_host_file(const char *from, const char *to)
{
    char buffer[16384];
    size_t length;
    FILE *source = fopen(from, "rb");
    if (source == NULL) return -1;
    FILE *target = fopen(to, "wb");
    if (target == NULL) {
        fclose(source);
        return -1;
    }
    while ((length = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, length, target) != length) break;
    }
    int failed = ferror(source) || ferror(target);
    fclose(source);
    if (fclose(target) != 0) failed = 1;
    return failed ? -1 : 0;
}

/* Opens dos_path for the game. `kind` is 'r', 'w' or 'a' and `update` is
 * the '+'; the host file is always binary, text mode is done by the callers.
 * With the write root in the game directory, every open goes to the game
 * directory's own file (MD_GameDirWritePath), as in DOS. Otherwise the write
 * root is an overlay: a read-only open finds the write-root copy first, then
 * the data directory, and anything that can write opens the write-root copy,
 * made from the data directory's file first when the mode keeps existing
 * contents. */
static md_file *open_game_file(const char *dos_path, char kind, int update)
{
    char overlay[PATH_MAX];
    char data[PATH_MAX];
    char mode[4];
    const char *opened;
    void *new_in = NULL;
    md_file *file;
    int on_cd;
    int error_log;

    if (dos_path == NULL) return NULL;
    /* the CD drive is read-only and has no write-root copy */
    file = MD_OpenCdFile(dos_path, kind != 'r' || update, &on_cd);
    if (on_cd) {
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE %s '%s' (CD)\n", file != NULL ? "CDOPEN" : "CDOPENFAIL", dos_path);
        return file;
    }
    if (write_root_path(dos_path, overlay, sizeof(overlay)) != 0) return NULL;
    error_log = strcmp(overlay + strlen(MD_WriteRoot()), "/ERROR.LOG") == 0;
    snprintf(mode, sizeof(mode), "%c%sb", kind, update ? "+" : "");

    if (MD_WriteRootIsDataRoot()) {
        if (kind == 'r' && !update) {
            opened = MD_ResolveDataPath(dos_path, data, sizeof(data)) == 0 ? data : NULL;
        } else {
            opened = MD_GameDirWritePath(dos_path, data, sizeof(data), &new_in) == 0 ? data : NULL;
        }
    } else if (kind == 'r' && !update) {
        if (host_file_exists(overlay)) {
            opened = overlay;
        } else if (MD_ResolveDataPath(dos_path, data, sizeof(data)) == 0) {
            opened = data;
        } else {
            opened = NULL;
        }
    } else {
        opened = overlay;
        if (make_parent_directories(overlay) != 0) {
            opened = NULL;
        } else if (kind != 'w' && !host_file_exists(overlay) &&
                   MD_ResolveDataPath(dos_path, data, sizeof(data)) == 0 &&
                   copy_host_file(data, overlay) != 0) {
            opened = NULL;
        }
    }

    file = opened != NULL ? md_file_host(fopen(opened, mode)) : NULL;
    if (file != NULL) MD_GameDirAdded(new_in, opened);
    if (MD_TraceFileEnabled()) {
        if (file != NULL) fprintf(stderr, "FILE HOSTOPEN %s '%s' -> '%s'\n", mode, dos_path, opened);
        else fprintf(stderr, "FILE HOSTOPENFAIL %s '%s'\n", mode, dos_path);
    }
    if (file != NULL && error_log) MD_DumpGuestMemory();
    return file;
}

static md_stream *stream_of(void *pointer, const char *function)
{
    watcom_file *guest = (watcom_file *)pointer;
    if (guest != NULL && guest->handle >= 0 && guest->handle < MD_MAX_HANDLES &&
        streams[guest->handle].guest == guest) {
        return &streams[guest->handle];
    }
    fprintf(stderr, "Mass Destruction: %s: FILE %p did not come from Game_fopen\n", function, pointer);
    return NULL;
}

static md_file *host_of(md_stream *stream)
{
    return MD_HandleFile(stream->guest->handle);
}

/* C stdio needs a seek between reading and writing an update stream; the
 * WATCOM library the game was written against did that itself. */
static void switch_direction(md_stream *stream, md_file *file, int direction)
{
    if (stream->last != LAST_NONE && stream->last != direction) md_fseek(file, 0, SEEK_CUR);
    stream->last = direction;
}

static void note_host_state(md_stream *stream, md_file *file)
{
    if (md_feof(file)) stream->guest->flag |= WATCOM_EOF;
    if (md_ferror(file)) stream->guest->flag |= WATCOM_SFERR;
}

/* WATCOM text-mode input (fgetc loc_6B1E2, fread's text loop at
 * loc_6A686): a CR is dropped and the character after it taken as is --
 * only one, even if it is another CR -- and ^Z is end of file. */
static int stream_getc(md_stream *stream, md_file *file)
{
    int character = md_fgetc(file);
    if (stream->text) {
        if (character == '\r') character = md_fgetc(file);
        if (character == 0x1a) {
            stream->guest->flag |= WATCOM_EOF;
            return EOF;
        }
    }
    if (character == EOF) note_host_state(stream, file);
    return character;
}

/* WATCOM text-mode output (fputc loc_752AA, which fwrite and fprintf go
 * through): '\n' is written as CR LF. */
static int stream_putc(md_stream *stream, md_file *file, int character)
{
    if ((stream->text && character == '\n' && md_fputc('\r', file) == EOF) || md_fputc(character, file) == EOF) {
        stream->guest->flag |= WATCOM_SFERR;
        return EOF;
    }
    return character;
}

static int can_read(md_stream *stream)
{
    if (stream->guest->flag & WATCOM_READ) return 1;
    stream->guest->flag |= WATCOM_SFERR;
    return 0;
}

static int can_write(md_stream *stream)
{
    if (stream->guest->flag & WATCOM_WRITE) return 1;
    stream->guest->flag |= WATCOM_SFERR;
    return 0;
}

typedef struct {
    md_stream *stream;
    md_file *file;
} stream_output;

static void stream_output_char(char character, void *arg)
{
    stream_output *output = (stream_output *)arg;
    stream_putc(output->stream, output->file, (unsigned char)character);
}

extern "C"
{
    /* For INT 21h AH=3Ch (MD-runtime.c): a raw DOS create lands where fopen
     * puts a new file. */
    md_file *MD_OpenGameFile(const char *dos_path, char kind, int update)
    {
        return open_game_file(dos_path, kind, update);
    }

    /* WATCOM fopen: eax = name, edx = mode. The mode is r/w/a, then any of
     * '+', 'b', 't'; without 'b' the stream is text. */
    void *Game_fopen(const char *filename, const char *mode)
    {
        char kind = mode != NULL ? mode[0] : 0;
        int update, text;
        md_file *file;
        int handle;
        watcom_file *guest;

        if (kind != 'r' && kind != 'w' && kind != 'a') return NULL;
        update = strchr(mode + 1, '+') != NULL;
        text = strchr(mode + 1, 'b') == NULL;

        file = open_game_file(filename, kind, update);
        if (file == NULL) return NULL;
        handle = MD_AttachHandle(file);
        guest = handle >= 0 ? (watcom_file *)x86_malloc(sizeof(watcom_file)) : NULL;
        if (guest == NULL) {
            if (handle >= 0) MD_ReleaseHandle(handle);
            md_fclose(file);
            return NULL;
        }
        memset(guest, 0, sizeof(*guest));
        guest->flag = ((kind == 'r' || update) ? WATCOM_READ : 0) |
                      ((kind != 'r' || update) ? WATCOM_WRITE : 0) |
                      (text ? 0 : WATCOM_BINARY);
        guest->handle = handle;
        streams[handle].guest = guest;
        streams[handle].text = text;
        streams[handle].last = LAST_NONE;
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE FOPEN %s h=%d '%s'\n", mode, handle, filename);
        return guest;
    }

    /* WATCOM fclose (loc_69634) looks the stream up among the open ones and
     * returns -1 when it is not there. main_ closes ipx.log on exit
     * (loc_1200A) whether or not a network game opened it, so NULL comes
     * on every exit and is not reported. */
    int32_t Game_fclose(void *stream_pointer)
    {
        if (stream_pointer == NULL) return EOF;
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL) return EOF;
        int handle = stream->guest->handle;
        md_file *file = MD_HandleFile(handle);
        int result = file != NULL ? md_fclose(file) : EOF;
        MD_ReleaseHandle(handle);
        x86_free(stream->guest);
        memset(stream, 0, sizeof(*stream));
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE FCLOSE h=%d\n", handle);
        return result == 0 ? 0 : EOF;
    }

    /* WATCOM fread: eax = buffer, edx = size, ebx = count, ecx = stream.
     * Like the original it returns bytes transferred / size, so a partial
     * last element is still copied. */
    uint32_t Game_fread(void *buffer, uint32_t size, uint32_t count, void *stream_pointer)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL || !can_read(stream)) return 0;
        uint32_t total = size * count;
        if (total == 0) return 0;
        md_file *file = host_of(stream);
        switch_direction(stream, file, LAST_READ);

        uint32_t done;
        if (!stream->text) {
            done = (uint32_t)md_fread(buffer, total, file);
            if (done < total) note_host_state(stream, file);
        } else {
            uint8_t *target = (uint8_t *)buffer;
            int character;
            for (done = 0; done < total && (character = stream_getc(stream, file)) != EOF; done++)
                target[done] = (uint8_t)character;
        }
        return done / size;
    }

    /* WATCOM fwrite: eax = buffer, edx = size, ebx = count, ecx = stream. */
    uint32_t Game_fwrite(const void *buffer, uint32_t size, uint32_t count, void *stream_pointer)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL || !can_write(stream)) return 0;
        uint32_t total = size * count;
        if (total == 0) return 0;
        md_file *file = host_of(stream);
        switch_direction(stream, file, LAST_WRITE);

        uint32_t done;
        if (!stream->text) {
            done = (uint32_t)md_fwrite(buffer, total, file);
            if (done < total) note_host_state(stream, file);
        } else {
            const uint8_t *source = (const uint8_t *)buffer;
            for (done = 0; done < total && stream_putc(stream, file, source[done]) != EOF; done++) {
            }
        }
        return done / size;
    }

    /* WATCOM fseek: eax = stream, edx = offset, ebx = whence (0/1/2). A
     * successful seek clears end-of-file, as in the original. */
    int32_t Game_fseek(void *stream_pointer, int32_t offset, int32_t whence)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL) return -1;
        int origin = whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : -1;
        if (origin < 0 || md_fseek(host_of(stream), offset, origin) != 0) return -1;
        stream->guest->flag &= ~WATCOM_EOF;
        stream->last = LAST_NONE;
        return 0;
    }

    /* WATCOM fgetc: eax = stream. */
    int32_t Game_fgetc(void *stream_pointer)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL || !can_read(stream)) return EOF;
        md_file *file = host_of(stream);
        switch_direction(stream, file, LAST_READ);
        return stream_getc(stream, file);
    }

    /* WATCOM setbuf: eax = stream, edx = buffer. Its only caller is AIL's
     * debug log (loc_63649), which asks for no buffering. A buffer, when
     * given, is not used: the host stream keeps its own. */
    void Game_setbuf(void *stream_pointer, char *buffer)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL) return;
        md_setbuf(host_of(stream), buffer != NULL);
    }

    /* WATCOM fprintf, cdecl: `ap` is the guest's varargs, as for MD_vprintf. */
    int32_t MD_vfprintf(void *stream_pointer, const char *format, uint32_t *ap)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL || !can_write(stream)) return -1;
        stream_output output = { stream, host_of(stream) };
        switch_direction(stream, output.file, LAST_WRITE);
        return vfctprintf_x86(stream_output_char, &output, format, ap);
    }

    /* WATCOM fscanf, cdecl. Its 65 call sites use two formats, "%s" and "%d"
     * (the game's and AIL's text files are keyword/number token streams), so
     * this is ANSI scanf for exactly the directives those need: white space,
     * ordinary characters, %%, %s and %d with '*' and a width. Anything else
     * is reported and ends the scan. Input goes through stream_getc, so it
     * sees the same text-mode stream the guest getter at loc_69590 saw. */
    int32_t MD_vfscanf(void *stream_pointer, const char *format, uint32_t *ap)
    {
        md_stream *stream = stream_of(stream_pointer, __func__);
        if (stream == NULL || !can_read(stream)) return EOF;
        md_file *file = host_of(stream);
        switch_direction(stream, file, LAST_READ);

        int assigned = 0, converted = 0, character = 0;
        while (*format != 0) {
            if (isspace((unsigned char)*format)) {
                while (isspace((unsigned char)*format)) format++;
                while ((character = stream_getc(stream, file)) != EOF && isspace(character)) {
                }
                if (character == EOF) break;
                md_fungetc(character, file);
                continue;
            }
            if (*format != '%' || format[1] == '%') {
                if (*format == '%') format++;
                character = stream_getc(stream, file);
                if (character != (unsigned char)*format) {
                    if (character != EOF) md_fungetc(character, file);
                    goto input_failure_or_mismatch;
                }
                format++;
                continue;
            }

            format++;
            int suppress = *format == '*';
            if (suppress) format++;
            uint32_t width = 0;
            while (isdigit((unsigned char)*format)) width = width * 10 + (uint32_t)(*format++ - '0');
            if (width == 0) width = UINT32_MAX;
            char conversion = *format++;
            if (conversion != 's' && conversion != 'd') {
                fprintf(stderr, "Mass Destruction: fscanf conversion %%%c not implemented\n", conversion);
                break;
            }

            while ((character = stream_getc(stream, file)) != EOF && isspace(character)) {
            }
            if (character == EOF) break;

            if (conversion == 's') {
                char *target = suppress ? NULL : (char *)(uintptr_t)*ap++;
                uint32_t length = 0;
                do {
                    if (target != NULL) target[length] = (char)character;
                    length++;
                } while (length < width && (character = stream_getc(stream, file)) != EOF && !isspace(character));
                if (character != EOF && length < width) md_fungetc(character, file);
                if (target != NULL) {
                    target[length] = 0;
                    assigned++;
                }
            } else {
                int negative = 0;
                uint32_t length = 0, digits = 0, value = 0;
                if (character == '-' || character == '+') {
                    negative = character == '-';
                    length++;
                    character = length < width ? stream_getc(stream, file) : EOF;
                }
                while (character != EOF && isdigit(character)) {
                    value = value * 10 + (uint32_t)(character - '0');
                    digits++;
                    length++;
                    character = length < width ? stream_getc(stream, file) : EOF;
                }
                if (character != EOF && length < width) md_fungetc(character, file);
                if (digits == 0) goto input_failure_or_mismatch;
                if (!suppress) {
                    *(int32_t *)(uintptr_t)*ap++ = negative ? -(int32_t)value : (int32_t)value;
                    assigned++;
                }
            }
            converted++;
        }
        return (converted == 0 && character == EOF) ? EOF : assigned;

    input_failure_or_mismatch:
        return (converted == 0 && md_feof(file)) ? EOF : assigned;
    }

    /* WATCOM open, cdecl: path, oflag[, permission]. Every call site in the
     * game passes O_RDONLY | O_BINARY; the other access modes map onto the
     * same write-root rules as fopen. */
    int32_t Game_open(const char *path, int32_t oflag)
    {
        int access = oflag & WATCOM_O_ACCMODE;
        char kind;
        int update;
        md_file *file;
        int handle;

        if (!(oflag & WATCOM_O_BINARY)) {
            fprintf(stderr, "Mass Destruction: text-mode open('%s', 0x%x) is read as binary\n",
                    path != NULL ? path : "", (unsigned int)oflag);
        }
        if (access == 0) {
            kind = 'r';
            update = 0;
        } else {
            kind = (oflag & WATCOM_O_TRUNC) ? 'w' : (oflag & WATCOM_O_APPEND) ? 'a' : 'r';
            update = 1;
            if (kind == 'r' && (oflag & WATCOM_O_CREAT)) {
                char overlay[PATH_MAX], data[PATH_MAX];
                if (path != NULL && write_root_path(path, overlay, sizeof(overlay)) == 0 &&
                    !host_file_exists(overlay) && MD_ResolveDataPath(path, data, sizeof(data)) != 0) {
                    kind = 'w';
                }
            }
        }

        file = open_game_file(path, kind, update);
        if (file == NULL) return -1;
        handle = MD_AttachHandle(file);
        if (handle < 0) {
            md_fclose(file);
            return -1;
        }
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE HOPEN 0x%x h=%d '%s'\n", (unsigned int)oflag, handle, path);
        return handle;
    }

    /* WATCOM read: eax = handle, edx = buffer, ebx = length. */
    int32_t Game_read(int32_t handle, void *buffer, uint32_t length)
    {
        md_file *file = MD_HandleFile(handle);
        if (file == NULL) return -1;
        size_t done = md_fread(buffer, length, file);
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE HREAD h=%d n=%x got=%zx\n", handle, length, done);
        if (done < length && md_ferror(file)) {
            md_clearerr(file);
            return -1;
        }
        return (int32_t)done;
    }

    /* WATCOM close: eax = handle. */
    int32_t Game_close(int32_t handle)
    {
        md_file *file = MD_HandleFile(handle);
        if (file == NULL) return -1;
        if (streams[handle].guest != NULL) {
            fprintf(stderr, "Mass Destruction: close(%d) under an open FILE\n", handle);
        }
        int result = md_fclose(file);
        MD_ReleaseHandle(handle);
        if (MD_TraceFileEnabled()) fprintf(stderr, "FILE HCLOSE h=%d\n", handle);
        return result == 0 ? 0 : -1;
    }

    /* WATCOM filelength: eax = handle. Also reached with fileno() of a
     * stream (loc_5C212, loc_5C50E). */
    int32_t Game_filelength(int32_t handle)
    {
        md_file *file = MD_HandleFile(handle);
        if (file == NULL) return -1;
        long position = md_ftell(file);
        if (position < 0 || md_fseek(file, 0, SEEK_END) != 0) return -1;
        long length = md_ftell(file);
        md_fseek(file, position, SEEK_SET);
        if (handle < MD_MAX_HANDLES && streams[handle].guest != NULL) streams[handle].last = LAST_NONE;
        return (int32_t)length;
    }
}
