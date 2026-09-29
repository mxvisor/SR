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

/* Digital sound: the Miles AIL 3.x calls MASSD.EXE makes, replaced natively.
 *
 * The game drives AIL from a small sound module (loc_10910 init, loc_118A0
 * onwards play/stop) through 18 cdecl entry points, located in the binary by
 * the AIL debug-trace format strings each public wrapper prints
 * ("AIL_start_sample(0x%X)" at 0x927F1 is pushed from loc_65041, and so on).
 * external_procedures.sci routes them to SR-asm-calls.llasm, which calls the
 * Game_AIL_* functions here.  The real AIL talks to a 16-bit real-mode .DIG
 * driver through INT 66h, which nothing here can run; SDL_mixer plays the
 * samples instead.  Albion-sound.c does the same for Albion's AIL.
 *
 * What the game uses: 8 sample handles, 8-bit unsigned mono PCM out of
 * TANK.RES, per-sound playback rates, volume (also changed while a looped
 * sound plays), loop count 0 for looped sounds, AIL_stop_sample to silence
 * them, and AIL_sample_status == SMP_PLAYING to find a free handle.
 *
 * AIL's timer service matters more than the sound: with a DIG driver
 * installed, loc_150AD skips installing the game's INT 8 handler (loc_53F1D)
 * and runs the per-frame tick loc_53752 as an AIL timer at 12500 us instead.
 * That callback waits for vertical retrace, then reprograms the PIT itself
 * (AIL's loc_68924) to AIL's current period minus 320 us, which it reads out
 * of AIL's own data (loc_BCAA6) -- so the native timer keeps that word
 * current too. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_vars.h"
#include "MD-runtime.h"
#include "MD-sound.h"

extern "C" {

/* AIL 3.x sample status (AIL.H) */
#define SMP_DONE 0x0002u
#define SMP_PLAYING 0x0004u
#define SMP_STOPPED 0x0008u

/* AIL_set_sample_type: format bit 0 = 16-bit, bit 1 = stereo; flags bit 0 =
 * DIG_PCM_SIGN */
#define DIG_F_16BITS_MASK 1
#define DIG_F_STEREO_MASK 2
#define DIG_PCM_SIGN 1u

#define DIG_DEFAULT_VOLUME 5
#define AIL_PREFERENCE_COUNT 19

/* AIL's timer table: 16 slots, the last one reserved for chaining to the
 * BIOS handler (AIL's ISR at loc_687CE only calls back slots 0-14). */
#define AIL_TIMERS 16
#define AIL_USER_TIMERS 15
#define AIL_BIOS_TIMER_US 54925u
/* The DIG driver's own service timer. The real AIL registers one when the
 * driver is installed; with SB16.DIG under DOSBox, AIL's period at 0xBCAA6
 * reads 5000 us and the game's timer gets handle 4. That period is what the
 * game's frame tick reprograms the PIT from (loc_10A15: 5000 - 320 us), so
 * it sets how the tick meets vertical retrace, and with it the pace of the
 * FLIC player (loc_5C930 waits 4 ticks a frame). Nothing is called back. */
#define AIL_DRIVER_SERVICE_US 5000u
#define AILT_FREE 0u
#define AILT_STOPPED 1u
#define AILT_RUNNING 2u
/* AIL's current PIT period in microseconds (0x2CAA6 in obj2 terms): what
 * loc_68957 programs the PIT to and what loc_10A15 reads back. */
#define AIL_TIMER_PERIOD_FLAT 0xBCAA6u

/* The game's sound table, loaded from sounds\samples.txt by loc_10B4E:
 * 37 records of 0x25 bytes (+0 data address, +4 length, +0x11 name).
 * loc_11EA0 rejects numbers >= 0x25. */
#define MD_SOUND_TABLE_FLAT 0xBFE78u
#define MD_SOUND_TABLE_ENTRIES 37
#define MD_SOUND_RECORD_SIZE 0x25
#define MD_SOUND_NAME_OFFSET 0x11

#define MD_MIXER_CHANNELS 16
#define MD_SOUND_CACHE_SIZE 64

typedef struct {
    uint32_t status;
    uint32_t callback;  /* guest code address */
    uint32_t user;
    uint32_t period;    /* microseconds */
    uint32_t elapsed;
    uint32_t pending;
} ail_timer;

typedef struct {
    const uint8_t *start;
    uint32_t len;
    int32_t format;
    uint32_t flags;
    int32_t rate;
    uint32_t hash;
    uint32_t last_used;
    Uint8 *buffer;
    Mix_Chunk *chunk;
} cached_sound;

/* The HSAMPLE the game holds: allocated in 32-bit guest memory because the
 * game stores the pointer in a dword table (loc_C12B8); it never reads the
 * fields. */
typedef struct {
    int channel;
    uint32_t status;
    int32_t format;
    uint32_t flags;
    const uint8_t *start;
    uint32_t len;
    int32_t playback_rate;
    int32_t volume;         /* 0-127 */
    int32_t pan;            /* 0 = left, 127 = right */
    int32_t loop_count;     /* 0 = forever */
} ail_sample;

/* AIL 3.x defaults, the same table Albion-AIL.c keeps; only
 * AIL_set_preference's return value and DIG_DEFAULT_VOLUME read it. */
static int32_t ail_preference[AIL_PREFERENCE_COUNT] = {200, 1, 32768, 100, 16, 100, 655, 0, 0, 1, 0, 120, 8, 127, 1, 0, 2, 1, 1};
static ail_timer ail_timers[AIL_TIMERS];
static uint32_t ail_timer_period_us = AIL_BIOS_TIMER_US;
static int ail_timers_running;

static int audio_open;
static int sound_enabled;
static int audio_rate;
static Uint16 audio_format;
static int audio_channels;
static int mixer_channels_used;
static cached_sound sound_cache[MD_SOUND_CACHE_SIZE];
static uint32_t sound_cache_clock;

static int trace_sound(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("MD_TRACE_SOUND") != NULL;
    return enabled;
}

int MD_AudioInit(void)
{
    const char *nosound = getenv("MD_NOSOUND");

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "Mass Destruction: no audio: %s\n", SDL_GetError());
        return -1;
    }
    /* the format audio/pc.c's Init_Audio chose */
    if (Mix_OpenAudio(Game_AudioRate, (Uint16)Game_AudioFormat, (int)Game_AudioChannels, Game_AudioBufferSize) != 0) {
        fprintf(stderr, "Mass Destruction: no audio: %s\n", Mix_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
    Mix_AllocateChannels(MD_MIXER_CHANNELS);
    audio_open = 1;
    sound_enabled = nosound == NULL || nosound[0] == 0 || nosound[0] == '0';
    if (trace_sound()) {
        fprintf(stderr, "SOUND device %d Hz format %04x channels %d, digital sound %s\n",
                audio_rate, audio_format, audio_channels, sound_enabled ? "on" : "off (MD_NOSOUND)");
    }
    return 0;
}

void MD_AudioDeinit(void)
{
    if (!audio_open) return;
    Mix_HaltChannel(-1);
    for (int index = 0; index < MD_SOUND_CACHE_SIZE; index++) {
        if (sound_cache[index].chunk != NULL) Mix_FreeChunk(sound_cache[index].chunk);
        SDL_free(sound_cache[index].buffer);
        memset(&sound_cache[index], 0, sizeof(sound_cache[index]));
    }
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_open = 0;
    sound_enabled = 0;
}

int MD_AudioIsOpen(void)
{
    return audio_open;
}

/* ---- timers ---- */

/* loc_68924: AIL's microseconds-to-PIT-reload conversion. */
static uint32_t pit_reload_for(uint32_t microseconds)
{
    if (microseconds >= 0xD68Du) return 0;
    return (uint32_t)(((uint64_t)microseconds * 10000u) / 8380u);
}

/* loc_68957: the PIT runs at the shortest period of any registered timer,
 * the BIOS chain slot included. */
static void update_timer_period(void)
{
    uint32_t period = 0xFFFFFFFFu;
    for (int index = 0; index < AIL_TIMERS; index++) {
        if (ail_timers[index].status != AILT_FREE && ail_timers[index].period != 0 &&
            ail_timers[index].period < period) {
            period = ail_timers[index].period;
        }
    }
    if (period == ail_timer_period_us) return;
    ail_timer_period_us = period;
    memcpy(MD_Obj2Pointer(AIL_TIMER_PERIOD_FLAT), &period, sizeof(period));
    MD_ProgramPit(pit_reload_for(period));
    if (trace_sound()) fprintf(stderr, "SOUND timer period %u us\n", period);
}

static void reset_timers(void)
{
    memset(ail_timers, 0, sizeof(ail_timers));
    ail_timers[AIL_USER_TIMERS].status = AILT_RUNNING;
    ail_timers[AIL_USER_TIMERS].period = AIL_BIOS_TIMER_US;
    ail_timer_period_us = AIL_BIOS_TIMER_US;
    ail_timers_running = 0;
}

/* loc_687CE, minus the BIOS chain (SR_CheckTimer delivers INT 8 right after
 * this): every running timer gains the programmed period, and one that
 * reaches its own period is called back once. */
void MD_AIL_TimerInterrupt(void)
{
    /* an interrupt that cannot be taken now is not counted either: AIL's ISR
     * returns at once when it is re-entered */
    if (!ail_timers_running || !MD_GuestInterruptsEnabled()) return;
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        ail_timer *timer = &ail_timers[index];
        if (timer->status != AILT_RUNNING) continue;
        timer->elapsed += ail_timer_period_us;
        if (timer->elapsed >= timer->period) {
            timer->elapsed -= timer->period;
            timer->pending++;
        }
    }
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        ail_timer *timer = &ail_timers[index];
        if (timer->callback == 0) {
            timer->pending = 0;     /* the driver's service timer */
            continue;
        }
        while (timer->pending != 0) {
            if (!MD_CallGuestTimerCallback(timer->callback, timer->user)) return;
            timer->pending--;
        }
    }
}

void Game_AIL_startup(void)
{
    if (trace_sound()) fprintf(stderr, "SOUND AIL_startup\n");
    reset_timers();
    memcpy(MD_Obj2Pointer(AIL_TIMER_PERIOD_FLAT), &ail_timer_period_us, sizeof(ail_timer_period_us));
}

void Game_AIL_shutdown(void)
{
    if (trace_sound()) fprintf(stderr, "SOUND AIL_shutdown\n");
    if (audio_open) Mix_HaltChannel(-1);
    /* AIL restores the BIOS rate along with its INT 8 vector */
    if (ail_timer_period_us != AIL_BIOS_TIMER_US) MD_ProgramPit(0);
    reset_timers();
}

int32_t Game_AIL_set_preference(uint32_t number, int32_t value)
{
    if (number >= AIL_PREFERENCE_COUNT) return 0;
    int32_t old_value = ail_preference[number];
    ail_preference[number] = value;
    return old_value;
}

int32_t Game_AIL_register_timer(uint32_t callback)
{
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        if (ail_timers[index].status == AILT_FREE) {
            memset(&ail_timers[index], 0, sizeof(ail_timers[index]));
            ail_timers[index].status = AILT_STOPPED;
            ail_timers[index].callback = callback;
            if (trace_sound()) fprintf(stderr, "SOUND AIL_register_timer(%08x) = %d\n", callback, index);
            return index;
        }
    }
    return -1;
}

void Game_AIL_set_timer_period(int32_t timer, uint32_t microseconds)
{
    if (timer < 0 || timer >= AIL_USER_TIMERS || ail_timers[timer].status == AILT_FREE) return;
    ail_timers[timer].period = microseconds;
    ail_timers[timer].elapsed = 0;
    update_timer_period();
}

void Game_AIL_start_timer(int32_t timer)
{
    if (timer < 0 || timer >= AIL_USER_TIMERS || ail_timers[timer].status == AILT_FREE) return;
    ail_timers[timer].status = AILT_RUNNING;
    ail_timers_running = 1;
}

/* ---- digital driver and samples ---- */

int32_t Game_AIL_install_DIG_INI(uint32_t *dig)
{
    if (!audio_open || !sound_enabled) {
        if (trace_sound()) fprintf(stderr, "SOUND AIL_install_DIG_INI: no device\n");
        return 1;   /* AIL_NO_INI_FILE: the game takes its no-sound branch */
    }
    /* The game only hands this back to AIL_allocate_sample_handle. */
    void *driver = x86_malloc(256);
    if (driver == NULL) return 1;
    memset(driver, 0, 256);
    *dig = (uint32_t)(uintptr_t)driver;
    for (int index = 0; index < AIL_USER_TIMERS; index++) {
        if (ail_timers[index].status == AILT_FREE) {
            memset(&ail_timers[index], 0, sizeof(ail_timers[index]));
            ail_timers[index].status = AILT_RUNNING;
            ail_timers[index].period = AIL_DRIVER_SERVICE_US;
            ail_timers_running = 1;
            update_timer_period();
            break;
        }
    }
    if (trace_sound()) fprintf(stderr, "SOUND AIL_install_DIG_INI = %p\n", driver);
    return 0;
}

void Game_AIL_init_sample(void *S)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    if (sample->status != SMP_DONE) Mix_HaltChannel(sample->channel);
    sample->status = SMP_DONE;
    sample->format = 0;
    sample->flags = 0;
    sample->start = NULL;
    sample->len = 0;
    sample->playback_rate = 11025;
    sample->volume = ail_preference[DIG_DEFAULT_VOLUME];
    sample->pan = 64;
    sample->loop_count = 1;
}

void *Game_AIL_allocate_sample_handle(void *dig)
{
    (void)dig;
    if (!audio_open) return NULL;
    ail_sample *sample = (ail_sample *)x86_malloc(sizeof(ail_sample));
    if (sample == NULL) return NULL;
    memset(sample, 0, sizeof(*sample));
    if (mixer_channels_used >= Mix_AllocateChannels(-1)) Mix_AllocateChannels(mixer_channels_used + 1);
    sample->channel = mixer_channels_used++;
    sample->status = SMP_DONE;
    Game_AIL_init_sample(sample);
    return sample;
}

void Game_AIL_set_sample_address(void *S, void *start, uint32_t len)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    sample->start = (const uint8_t *)start;
    sample->len = len;
}

void Game_AIL_set_sample_type(void *S, int32_t format, uint32_t flags)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    sample->format = format;
    sample->flags = flags;
}

static uint32_t fnv1a(const uint8_t *data, uint32_t len)
{
    uint32_t hash = 2166136261u;
    for (uint32_t index = 0; index < len; index++) hash = (hash ^ data[index]) * 16777619u;
    return hash;
}

/* A looped sound can stay on its channel for a whole mission; evicting its
 * chunk would cut it off. */
static int chunk_is_playing(const Mix_Chunk *chunk)
{
    for (int channel = 0; channel < mixer_channels_used; channel++) {
        if (Mix_Playing(channel) && Mix_GetChunk(channel) == chunk) return 1;
    }
    return 0;
}

/* The sound converted to the device format.  Sounds live in the game's
 * resource buffers, which are reloaded between missions, so the key includes
 * a hash of the data, not just its address. */
static Mix_Chunk *converted_sound(const ail_sample *sample)
{
    uint32_t hash = fnv1a(sample->start, sample->len);
    int victim = -1;
    uint32_t oldest = 0xFFFFFFFFu;

    for (int index = 0; index < MD_SOUND_CACHE_SIZE; index++) {
        cached_sound *entry = &sound_cache[index];
        if (entry->chunk != NULL && entry->start == sample->start && entry->len == sample->len &&
            entry->format == sample->format && entry->flags == sample->flags &&
            entry->rate == sample->playback_rate && entry->hash == hash) {
            entry->last_used = ++sound_cache_clock;
            return entry->chunk;
        }
        if (entry->chunk == NULL) {
            if (oldest != 0) { victim = index; oldest = 0; }
        } else if (entry->last_used < oldest && !chunk_is_playing(entry->chunk)) {
            victim = index;
            oldest = entry->last_used;
        }
    }
    if (victim < 0) return NULL;

    SDL_AudioFormat format;
    if (sample->format & DIG_F_16BITS_MASK) format = (sample->flags & DIG_PCM_SIGN) ? AUDIO_S16LSB : AUDIO_U16LSB;
    else format = (sample->flags & DIG_PCM_SIGN) ? AUDIO_S8 : AUDIO_U8;
    int channels = (sample->format & DIG_F_STEREO_MASK) ? 2 : 1;
    SDL_AudioStream *stream = SDL_NewAudioStream(format, channels, sample->playback_rate,
                                                 audio_format, audio_channels, audio_rate);
    if (stream == NULL) return NULL;
    Uint8 *buffer = NULL;
    int length = 0;
    if (SDL_AudioStreamPut(stream, sample->start, (int)sample->len) == 0 && SDL_AudioStreamFlush(stream) == 0) {
        length = SDL_AudioStreamAvailable(stream);
        buffer = (Uint8 *)SDL_malloc(length > 0 ? length : 1);
        if (buffer != NULL) length = SDL_AudioStreamGet(stream, buffer, length);
    }
    SDL_FreeAudioStream(stream);
    if (buffer == NULL || length <= 0) {
        SDL_free(buffer);
        return NULL;
    }
    Mix_Chunk *chunk = Mix_QuickLoad_RAW(buffer, (Uint32)length);
    if (chunk == NULL) {
        SDL_free(buffer);
        return NULL;
    }

    cached_sound *entry = &sound_cache[victim];
    /* Mix_FreeChunk halts any channel still playing it */
    if (entry->chunk != NULL) Mix_FreeChunk(entry->chunk);
    SDL_free(entry->buffer);
    entry->start = sample->start;
    entry->len = sample->len;
    entry->format = sample->format;
    entry->flags = sample->flags;
    entry->rate = sample->playback_rate;
    entry->hash = hash;
    entry->last_used = ++sound_cache_clock;
    entry->buffer = buffer;
    entry->chunk = chunk;
    return chunk;
}

static void apply_volume_and_pan(const ail_sample *sample)
{
    int volume = sample->volume < 0 ? 0 : sample->volume > 127 ? 127 : sample->volume;
    Mix_Volume(sample->channel, (volume * MIX_MAX_VOLUME + 63) / 127);
    int pan = sample->pan < 0 ? 0 : sample->pan > 127 ? 127 : sample->pan;
    Uint8 left = pan <= 64 ? 255 : (Uint8)((127 - pan) * 255 / 63);
    Uint8 right = pan >= 64 ? 255 : (Uint8)(pan * 255 / 64);
    Mix_SetPanning(sample->channel, left, right);
}

/* The samples.txt number of the sound at this address, for the trace. */
static int sound_number(const uint8_t *start, const char **name)
{
    for (int number = 0; number < MD_SOUND_TABLE_ENTRIES; number++) {
        const uint8_t *record = (const uint8_t *)MD_Obj2Pointer(MD_SOUND_TABLE_FLAT + number * MD_SOUND_RECORD_SIZE);
        uint32_t address;
        memcpy(&address, record, sizeof(address));
        if (address != 0 && address == (uint32_t)(uintptr_t)start) {
            *name = (const char *)record + MD_SOUND_NAME_OFFSET;
            return number;
        }
    }
    *name = "?";
    return -1;
}

void Game_AIL_start_sample(void *S)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    if (sample->start == NULL || sample->len == 0 || sample->playback_rate <= 0) return;
    Mix_Chunk *chunk = converted_sound(sample);
    if (chunk == NULL) {
        if (trace_sound()) fprintf(stderr, "SOUND convert failed: %s\n", SDL_GetError());
        return;
    }
    apply_volume_and_pan(sample);
    int loops = sample->loop_count == 0 ? -1 : sample->loop_count - 1;
    if (Mix_PlayChannel(sample->channel, chunk, loops) < 0) return;
    sample->status = SMP_PLAYING;
    if (trace_sound()) {
        const char *name;
        int number = sound_number(sample->start, &name);
        fprintf(stderr, "SOUND start ch %d #%d %.20s %p len %u rate %d vol %d loop %d\n", sample->channel,
                number, name, (const void *)sample->start, sample->len, sample->playback_rate,
                sample->volume, sample->loop_count);
    }
}

/* Like AIL's own, every sample function ignores a NULL handle: the game's
 * sound shutdown (loc_10A9C) ends all eight handles even when none were
 * allocated. */

/* AIL_stop_sample pauses; AIL_resume_sample would continue from there. */
void Game_AIL_stop_sample(void *S)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    if (sample->status != SMP_PLAYING) return;
    Mix_Pause(sample->channel);
    sample->status = SMP_STOPPED;
}

void Game_AIL_end_sample(void *S)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    Mix_HaltChannel(sample->channel);
    sample->status = SMP_DONE;
}

void Game_AIL_set_sample_playback_rate(void *S, int32_t playback_rate)
{
    if (S == NULL) return;
    ((ail_sample *)S)->playback_rate = playback_rate;
}

void Game_AIL_set_sample_volume(void *S, int32_t volume)
{
    if (S == NULL) return;
    ail_sample *sample = (ail_sample *)S;
    sample->volume = volume;
    if (sample->status != SMP_DONE) apply_volume_and_pan(sample);
}

void Game_AIL_set_sample_loop_count(void *S, int32_t loop_count)
{
    if (S == NULL) return;
    ((ail_sample *)S)->loop_count = loop_count;
}

uint32_t Game_AIL_sample_status(void *S)
{
    if (S == NULL) return 0;
    ail_sample *sample = (ail_sample *)S;
    if (sample->status == SMP_PLAYING && !Mix_Playing(sample->channel)) sample->status = SMP_DONE;
    return sample->status;
}

}
