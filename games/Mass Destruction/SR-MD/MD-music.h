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

#ifndef MD_MUSIC_H
#define MD_MUSIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CD audio for MSCDEX PLAY/STOP/RESUME AUDIO (MD-runtime.c's handle_mscdex).
 * data_root is the directory with MASSD.EXE; the music sources are looked up
 * relative to it (MD-music.c). */
void MD_MusicInit(void);
void MD_MusicDeinit(void);
/* Play `sectors` sectors (1/75 s each) of Red Book track `track`, starting
 * `offset` sectors into it. */
void MD_MusicPlay(int track, uint32_t offset, uint32_t sectors);
/* MSCDEX semantics: the first STOP pauses, a second one ends play. */
void MD_MusicStop(void);
void MD_MusicResume(void);

#ifdef __cplusplus
}
#endif

#endif
