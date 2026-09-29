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

/* The game's CD as drive D:.
 *
 * The game needs the disc for three things: its CD check opens
 * "%c:\tank.res" on the drive MSCDEX reports; the movies come from
 * FLIC_PATH, which TANK.INI sets to D:\FLIC\; and the music is Red Book
 * audio, tracks 2-12.
 *
 * The disc is one of:
 *  1. an image: a cue sheet with the disc's data track, the way DOSBox's
 *     IMGMOUNT takes it, in the directory CD inside the game directory, or
 *     $MD_CD_IMAGE;
 *  2. without an image, the game directory itself, when it is a copy of the
 *     disc (portable, run without installing): its FLIC\ tells it from an
 *     installed game, since the installer leaves the movies on the disc.
 *     The audio tracks are then files of their own, listed by a cue sheet
 *     in the directory AUDIO -- the disc's AUDIO\, which holds their \*.RB
 *     extents; with no cue sheet there is no music.
 *
 * An image's data track is an ISO9660 volume.  Its sectors are raw in the
 * image (MODE2/2352: 24 bytes of sync, header and subheader, then the 2048
 * data bytes), so files are read sector by sector (MD-file.c), not through
 * a host FILE.  Audio tracks are raw CDDA in a BINARY/MOTOROLA file, or
 * files of their own (ogg, wav, ... -- how DOSBox takes a soundtrack in
 * another format); MD-music.c plays them.  The disc's AUDIO\*.RB files are
 * no data of their own: their ISO9660 extents are those audio tracks. */

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "MD-cdrom.h"
#include "MD-file.h"

extern "C" {

#define CD_RAW_SECTOR 2352
#define ISO_PVD_LBA 16

typedef struct {
    int present;
    int audio;
    int compressed;
    int swap;
    char path[PATH_MAX];
    int64_t offset;         /* raw files: byte offset of INDEX 01 */
    int32_t start_frames;   /* compressed files: INDEX 01 */
    int sector_bytes;
    int data_header;        /* bytes before the 2048 data bytes of a sector */
} cd_track;

static char cue_path[PATH_MAX];
static char disc_dir[PATH_MAX];     /* the disc's files as a directory, or "" */
static cd_track tracks[MD_CD_MAX_TRACKS + 1];
static FILE *data_file;
static int64_t data_offset;
static int data_sector_bytes;
static int data_header;
static uint32_t root_lba;
static uint32_t root_size;

static int trace_cd(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("MD_TRACE_FILE") != NULL || getenv("MD_TRACE_SOUND") != NULL;
    return enabled;
}

static int64_t file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (int64_t)st.st_size;
}

/* `name` in `dir`, any case: copies of a disc often change the case. */
/* want_dir: 1 a directory, 0 a regular file, -1 either. */
static int find_in_dir(const char *dir, const char *name, char *out, size_t out_size, int want_dir)
{
    DIR *handle = opendir(dir);
    if (handle == NULL) return 0;
    int found = 0;
    for (struct dirent *entry = readdir(handle); entry != NULL && !found; entry = readdir(handle)) {
        if (strcasecmp(entry->d_name, name) != 0) continue;
        int length = snprintf(out, out_size, "%s/%s", dir, entry->d_name);
        struct stat st;
        found = length > 0 && (size_t)length < out_size && stat(out, &st) == 0 &&
                (want_dir < 0 ? (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)) :
                 want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
    }
    closedir(handle);
    return found;
}

/* The directory of a file's path, into out ("." for a bare name). */
static void directory_of(const char *path, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", path);
    char *slash = strrchr(out, '/');
#if defined(_WIN32)
    /* "C:\CD\MD.cue" as well as "C:/CD/MD.cue" */
    char *backslash = strrchr(out, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
#endif
    if (slash != NULL) *slash = 0; else snprintf(out, out_size, ".");
}

/* ---- cue sheet ---- */

static int parse_msf(const char *text, int32_t *frames)
{
    int m, s, f;
    if (sscanf(text, "%d:%d:%d", &m, &s, &f) != 3) return 0;
    *frames = (m * 60 + s) * 75 + f;
    return 1;
}

typedef struct {
    int number;
    int file;
    int sector_bytes;
    int data_header;
    int audio;
    int32_t index0;
    int32_t index1;
} cue_track;

typedef struct {
    char path[PATH_MAX];
    int swap;               /* MOTOROLA: big-endian samples */
    int compressed;         /* WAVE, MP3, OGG, FLAC, AIFF: decoded by SDL_mixer */
} cue_file;

static int load_cue(const char *path)
{
    FILE *cue = fopen(path, "rt");
    if (cue == NULL) return -1;

    char cue_dir[PATH_MAX];
    directory_of(path, cue_dir, sizeof(cue_dir));

    static cue_file files[MD_CD_MAX_TRACKS];
    static cue_track parsed[MD_CD_MAX_TRACKS];
    int file_count = 0, track_count = 0;
    char line[1024];

    while (fgets(line, sizeof(line), cue) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE", 4) == 0) {
            char *open = strchr(p, '"');
            char *close = open != NULL ? strchr(open + 1, '"') : NULL;
            if (close == NULL || file_count == MD_CD_MAX_TRACKS) continue;
            *close = 0;
            const char *name = open + 1;
            const char *base = strrchr(name, '\\');
            if (base == NULL) base = strrchr(name, '/');
            base = base != NULL ? base + 1 : name;
            cue_file *file = &files[file_count++];
            if (!find_in_dir(cue_dir, base, file->path, sizeof(file->path), 0)) {
                snprintf(file->path, sizeof(file->path), "%s/%s", cue_dir, base);
            }
            char type[32] = "";
            sscanf(close + 1, "%31s", type);
            file->swap = strcasecmp(type, "MOTOROLA") == 0;
            file->compressed = strcasecmp(type, "BINARY") != 0 && !file->swap;
        } else if (strncasecmp(p, "TRACK", 5) == 0 && file_count > 0 && track_count < MD_CD_MAX_TRACKS) {
            cue_track *track = &parsed[track_count++];
            char type[32] = "";
            track->number = 0;
            sscanf(p + 5, "%d %31s", &track->number, type);
            track->file = file_count - 1;
            track->audio = strcasecmp(type, "AUDIO") == 0;
            track->sector_bytes = strstr(type, "/2048") != NULL ? 2048 :
                                  strstr(type, "/2336") != NULL ? 2336 : CD_RAW_SECTOR;
            track->data_header = track->sector_bytes == 2048 ? 0 :
                                 track->sector_bytes == 2336 ? 8 :
                                 strncasecmp(type, "MODE2", 5) == 0 ? 24 : 16;
            track->index0 = -1;
            track->index1 = -1;
        } else if (strncasecmp(p, "INDEX", 5) == 0 && track_count > 0) {
            int index = -1;
            char msf[32] = "";
            int32_t frames;
            if (sscanf(p + 5, "%d %31s", &index, msf) == 2 && parse_msf(msf, &frames)) {
                if (index == 0) parsed[track_count - 1].index0 = frames;
                else if (index == 1) parsed[track_count - 1].index1 = frames;
            }
        }
    }
    fclose(cue);

    /* Walk each raw file's tracks in order: a track's sectors run from its
     * INDEX 00 (or 01) to the next track's, at its own sector size. */
    int64_t base = 0;
    int32_t start = 0;
    int size = CD_RAW_SECTOR;
    for (int index = 0; index < track_count; index++) {
        cue_track *track = &parsed[index];
        const cue_file *file = &files[track->file];
        if (track->index1 < 0) continue;
        int32_t track_start = track->index0 >= 0 ? track->index0 : track->index1;
        if (index == 0 || parsed[index - 1].file != track->file) {
            base = (int64_t)track_start * track->sector_bytes;
        } else {
            base += (int64_t)(track_start - start) * size;
        }
        start = track_start;
        size = track->sector_bytes;
        if (track->number < 1 || track->number > MD_CD_MAX_TRACKS) continue;
        cd_track *out = &tracks[track->number];
        out->present = 1;
        out->audio = track->audio;
        out->compressed = file->compressed;
        out->swap = file->swap;
        out->offset = file->compressed ? 0 : base + (int64_t)(track->index1 - track_start) * track->sector_bytes;
        out->start_frames = file->compressed ? track->index1 : 0;
        out->sector_bytes = track->sector_bytes;
        out->data_header = track->data_header;
        snprintf(out->path, sizeof(out->path), "%s", file->path);
    }
    return 0;
}

/* ---- ISO9660 ---- */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int MD_CdReadSector(uint32_t lba, uint8_t *data)
{
    if (data_file == NULL) return -1;
    int64_t at = data_offset + (int64_t)lba * data_sector_bytes + data_header;
    if (fseeko(data_file, (off_t)at, SEEK_SET) != 0) return -1;
    return fread(data, 1, MD_CD_SECTOR_DATA, data_file) == MD_CD_SECTOR_DATA ? 0 : -1;
}

/* A directory record's name as DOS sees it: no ";1", no trailing '.', upper
 * case, 8.3 or it is skipped. */
static int record_name(const uint8_t *record, char *name)
{
    int length = record[32];
    const uint8_t *text = record + 33;
    if (length == 1 && (text[0] == 0 || text[0] == 1)) return -1;   /* '.' and '..' */
    int out = 0;
    for (int index = 0; index < length && text[index] != ';'; index++) {
        if (out == 12) return -1;
        name[out++] = (char)toupper(text[index]);
    }
    while (out > 0 && name[out - 1] == '.') out--;
    name[out] = 0;
    return out > 0 ? 0 : -1;
}

typedef int (*record_visitor)(const md_cd_entry *entry, void *arg);   /* nonzero stops */

static int walk_directory(uint32_t lba, uint32_t size, record_visitor visit, void *arg)
{
    uint8_t sector[MD_CD_SECTOR_DATA];
    for (uint32_t done = 0; done < size; done += MD_CD_SECTOR_DATA) {
        if (MD_CdReadSector(lba + done / MD_CD_SECTOR_DATA, sector) != 0) return -1;
        for (uint32_t at = 0; at + 34 <= MD_CD_SECTOR_DATA;) {
            const uint8_t *record = sector + at;
            if (record[0] == 0) break;          /* the rest of the sector is padding */
            if (at + record[0] > MD_CD_SECTOR_DATA || record[0] < 34) break;
            md_cd_entry entry;
            if (record_name(record, entry.dos_name) == 0) {
                entry.lba = le32(record + 2);
                entry.size = le32(record + 10);
                entry.directory = (record[25] & 0x02) != 0;
                if (visit(&entry, arg)) return 1;
            }
            at += record[0];
        }
    }
    return 0;
}

typedef struct {
    const char *name;
    md_cd_entry *found;
} name_search;

static int match_name(const md_cd_entry *entry, void *arg)
{
    name_search *search = (name_search *)arg;
    if (strcasecmp(entry->dos_name, search->name) != 0) return 0;
    *search->found = *entry;
    return 1;
}

static int image_find(const char *path, md_cd_entry *entry)
{
    md_cd_entry current;
    memset(&current, 0, sizeof(current));
    current.lba = root_lba;
    current.size = root_size;
    current.directory = 1;
    const char *p = path;
    while (*p != 0) {
        while (*p == '\\' || *p == '/') p++;
        if (*p == 0) break;
        char component[16];
        size_t length = 0;
        while (p[length] != 0 && p[length] != '\\' && p[length] != '/') length++;
        if (length >= sizeof(component)) return -1;
        memcpy(component, p, length);
        component[length] = 0;
        p += length;
        if (!current.directory) return -1;
        md_cd_entry next;
        name_search search = { component, &next };
        if (walk_directory(current.lba, current.size, match_name, &search) != 1) return -1;
        current = next;
    }
    *entry = current;
    return 0;
}

/* ---- the disc as a directory ---- */

/* A DOS name as the disc has it: 8.3, upper case. */
static int dos_name_of(const char *host_name, char *dos_name)
{
    size_t length = strlen(host_name);
    const char *dot = strchr(host_name, '.');
    if (length == 0 || length > 12 || host_name[0] == '.') return -1;
    if (dot == NULL ? length > 8 : (dot - host_name > 8 || strchr(dot + 1, '.') != NULL || strlen(dot + 1) > 3)) return -1;
    for (size_t index = 0; index <= length; index++) dos_name[index] = (char)toupper((unsigned char)host_name[index]);
    return 0;
}

/* `path` under disc_dir, each component matched in any case. */
static int folder_resolve(const char *path, char *out, size_t out_size)
{
    char current[PATH_MAX];
    snprintf(current, sizeof(current), "%s", disc_dir);
    const char *p = path;
    while (*p != 0) {
        while (*p == '\\' || *p == '/') p++;
        if (*p == 0) break;
        char component[16];
        size_t length = 0;
        while (p[length] != 0 && p[length] != '\\' && p[length] != '/') length++;
        if (length >= sizeof(component)) return -1;
        memcpy(component, p, length);
        component[length] = 0;
        p += length;
        char next[PATH_MAX];
        if (!find_in_dir(current, component, next, sizeof(next), -1)) return -1;
        snprintf(current, sizeof(current), "%s", next);
    }
    if (strlen(current) >= out_size) return -1;
    snprintf(out, out_size, "%s", current);
    return 0;
}

static int folder_entry(const char *host_path, const char *dos_name, md_cd_entry *entry)
{
    struct stat st;
    if (stat(host_path, &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) return -1;
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->dos_name, sizeof(entry->dos_name), "%s", dos_name);
    entry->size = S_ISREG(st.st_mode) ? (uint32_t)st.st_size : 0;
    entry->directory = S_ISDIR(st.st_mode);
    return 0;
}

/* ---- either ---- */

int MD_CdFind(const char *path, md_cd_entry *entry)
{
    if (data_file != NULL) return image_find(path, entry);
    if (disc_dir[0] == 0) return -1;
    char host[PATH_MAX];
    if (folder_resolve(path, host, sizeof(host)) != 0) return -1;
    const char *base = strrchr(host, '/');
    char dos_name[13] = "";
    if (base != NULL && dos_name_of(base + 1, dos_name) != 0) dos_name[0] = 0;
    return folder_entry(host, dos_name, entry);
}

md_file *MD_CdOpen(const char *path)
{
    md_cd_entry entry;
    if (MD_CdFind(path, &entry) != 0 || entry.directory) return NULL;
    if (data_file != NULL) return md_file_cd(entry.lba, entry.size);
    char host[PATH_MAX];
    if (folder_resolve(path, host, sizeof(host)) != 0) return NULL;
    return md_file_host(fopen(host, "rb"));
}

typedef struct {
    void (*visit)(const md_cd_entry *entry, void *arg);
    void *arg;
} list_state;

static int list_entry(const md_cd_entry *entry, void *arg)
{
    list_state *state = (list_state *)arg;
    state->visit(entry, state->arg);
    return 0;
}

int MD_CdList(const char *path, void (*visit)(const md_cd_entry *entry, void *arg), void *arg)
{
    md_cd_entry dir;
    if (MD_CdFind(path, &dir) != 0 || !dir.directory) return -1;
    if (data_file != NULL) {
        list_state state = { visit, arg };
        return walk_directory(dir.lba, dir.size, list_entry, &state) < 0 ? -1 : 0;
    }
    char host[PATH_MAX];
    if (folder_resolve(path, host, sizeof(host)) != 0) return -1;
    DIR *handle = opendir(host);
    if (handle == NULL) return -1;
    for (struct dirent *item = readdir(handle); item != NULL; item = readdir(handle)) {
        char dos_name[13], child[PATH_MAX];
        md_cd_entry entry;
        if (dos_name_of(item->d_name, dos_name) != 0) continue;
        int length = snprintf(child, sizeof(child), "%s/%s", host, item->d_name);
        if (length < 0 || length >= (int)sizeof(child) || folder_entry(child, dos_name, &entry) != 0) continue;
        visit(&entry, arg);
    }
    closedir(handle);
    return 0;
}

static int open_volume(void)
{
    const cd_track *data = NULL;
    for (int number = 1; number <= MD_CD_MAX_TRACKS && data == NULL; number++) {
        if (tracks[number].present && !tracks[number].audio && !tracks[number].compressed) data = &tracks[number];
    }
    if (data == NULL) return -1;
    data_file = fopen(data->path, "rb");
    if (data_file == NULL) return -1;
    data_offset = data->offset;
    data_sector_bytes = data->sector_bytes;
    data_header = data->data_header;

    uint8_t sector[MD_CD_SECTOR_DATA];
    for (uint32_t lba = ISO_PVD_LBA; lba < ISO_PVD_LBA + 16; lba++) {
        if (MD_CdReadSector(lba, sector) != 0 || memcmp(sector + 1, "CD001", 5) != 0) break;
        if (sector[0] == 1) {
            root_lba = le32(sector + 156 + 2);
            root_size = le32(sector + 156 + 10);
            return 0;
        }
        if (sector[0] == 255) break;
    }
    fclose(data_file);
    data_file = NULL;
    return -1;
}

/* The first *.cue in `dir`, into cue_path. */
static int find_cue(const char *dir)
{
    DIR *handle = opendir(dir);
    if (handle == NULL) return 0;
    for (struct dirent *entry = readdir(handle); entry != NULL; entry = readdir(handle)) {
        size_t length = strlen(entry->d_name);
        if (length > 4 && strcasecmp(entry->d_name + length - 4, ".cue") == 0) {
            int written = snprintf(cue_path, sizeof(cue_path), "%s/%s", dir, entry->d_name);
            if (written < 0 || written >= (int)sizeof(cue_path)) cue_path[0] = 0;
            break;
        }
    }
    closedir(handle);
    return cue_path[0] != 0;
}

int MD_CdInit(const char *game_dir)
{
    MD_CdDeinit();
    const char *configured = getenv("MD_CD_IMAGE");
    char dir[PATH_MAX];
    const char *data = NULL;
    if (configured != NULL && configured[0] != 0) {
        snprintf(cue_path, sizeof(cue_path), "%s", configured);
    } else if (find_in_dir(game_dir, "CD", dir, sizeof(dir), 1)) {
        find_cue(dir);
    }
    if (cue_path[0] != 0) {
        if (load_cue(cue_path) != 0) {
            fprintf(stderr, "Mass Destruction: cannot read %s\n", cue_path);
        } else if (open_volume() == 0) {
            data = "an ISO9660 image";
        } else {
            fprintf(stderr, "Mass Destruction: no readable data track in %s\n", cue_path);
        }
    } else if (find_in_dir(game_dir, "FLIC", dir, sizeof(dir), 1)) {
        snprintf(disc_dir, sizeof(disc_dir), "%s", game_dir);
        data = "the disc's files";
        if (find_in_dir(game_dir, "AUDIO", dir, sizeof(dir), 1) && find_cue(dir) && load_cue(cue_path) != 0) {
            fprintf(stderr, "Mass Destruction: cannot read %s\n", cue_path);
            cue_path[0] = 0;
        }
    }
    int audio = 0;
    for (int number = 1; number <= MD_CD_MAX_TRACKS; number++) {
        if (tracks[number].present && tracks[number].audio &&
            (tracks[number].compressed || file_size(tracks[number].path) > tracks[number].offset)) {
            audio++;
        }
    }
    if (data == NULL) {
        fprintf(stderr, "Mass Destruction: no CD: no image in %s/CD or $MD_CD_IMAGE, and no copy of the disc (FLIC) in %s\n",
                game_dir, game_dir);
    } else if (trace_cd()) {
        fprintf(stderr, "Mass Destruction: CD: %s from %s, %d audio tracks%s%s\n", data,
                disc_dir[0] != 0 ? disc_dir : cue_path, audio, cue_path[0] != 0 ? " in " : "",
                cue_path);
    }
    return data != NULL ? 0 : -1;
}

void MD_CdDeinit(void)
{
    if (data_file != NULL) fclose(data_file);
    data_file = NULL;
    memset(tracks, 0, sizeof(tracks));
    cue_path[0] = 0;
    disc_dir[0] = 0;
}

int MD_CdPresent(void)
{
    return data_file != NULL || disc_dir[0] != 0;
}

const char *MD_CdCue(void)
{
    return cue_path;
}

int MD_CdAudioTrack(int track, md_cd_audio *audio)
{
    if (track < 1 || track > MD_CD_MAX_TRACKS || !tracks[track].present || !tracks[track].audio) return -1;
    const cd_track *source = &tracks[track];
    audio->path = source->path;
    audio->compressed = source->compressed;
    audio->swap = source->swap;
    audio->offset = source->offset;
    audio->start_frames = source->start_frames;
    return 0;
}

}
