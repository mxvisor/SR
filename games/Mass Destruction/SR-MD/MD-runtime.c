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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/timeb.h>
#else
#include <execinfo.h>
#include <sys/time.h>
#endif
#include <SDL.h>

#include "Game_memory.h"
#include "Game_defs.h"
#include "Game_vars.h"
#include "display.h"
#include "virtualfs.h"
#include "MD-cdrom.h"
#include "MD-file.h"
#include "MD-runtime.h"
#include "MD-sound.h"
#include "MD-music.h"
#include "Game_thread.h"
#include "llasm/llasm_cpu.h"

#undef eax
#undef ebx
#undef ecx
#undef edx
#undef esi
#undef edi
#undef ebp
#undef esp
#undef eflags

#define MD_CF 0x0001u
#define MD_ZF 0x0040u
/* Mode 13h's window in DOS.  The game's own code does not see it (its
 * `mov reg, 0xa0000` loads Game_ScreenWindow instead, see
 * instruction_replacements.sci), only the segment-override accesses below
 * that land in [0xA0000, +64000). */
#define MD_A0000 0x000a0000u
#define MD_FRAMEBUFFER_SIZE (320u * 200u)
#define MD_FRAMEBUFFER_MAP_SIZE 0x10000u
#define MD_MAX_VECTORS 256
#define MD_KEY_QUEUE_SIZE 64

/* Segment-override accesses arrive with the segment dropped (SR keeps only the
 * offset), and before any DPMI real-mode block exists the only thing WATCOM's
 * c0 startup addresses this way is its own PSP: it reads the environment
 * paragraph at +0x2C and, when INT 21h AH=30h reports the DOS4GW/PharLap
 * extender, the initial break address at +0x5C. Real DOS/DPMI offsets never
 * reach this high, so "address < MD_PSP_SIZE" is an unambiguous way to route
 * these small offsets into a dedicated buffer instead of flat guest memory. */
#define MD_PSP_SIZE 0x100u
/* Backing for plain INT 21h AH=48h/49h DOS memory allocation (unrelated to
 * the WATCOM near-heap above). Not known to be on any path the game takes,
 * but cheap to back for real instead of leaving it a no-op. */
#define MD_DOSMEM_SIZE (16u * 1024u * 1024u)
#define MD_DOSMEM_MAX_BLOCKS 64
/* DOS conventional memory, handed out by DPMI 0100h.  A real-mode segment is
 * 16 bits, so the segments are those of [MD_LOWMEM_BASE, +MD_LOWMEM_SIZE), as
 * DOS would give them.  The pool itself is mapped below 4 GB anywhere (Windows
 * does not leave the low megabyte free) and a segment's address is
 * lowmem_flat(segment << 4).  The game converts a segment itself in two places,
 * which add md_lowmem_bias too (instruction_replacements.sci).  The range ends at 0x90000, below mode 13h's window at 0xA0000,
 * which the segment-override accesses still address. */
#define MD_LOWMEM_BASE 0x10000u
#define MD_LOWMEM_SIZE 0x80000u
/* Free memory DPMI 0500h reports.  The game's heap is the host's (x86_malloc)
 * and has no fixed size, so this is a figure, not a pool: the game only
 * prints it, as "mem:%dk" (0x1223B).  The original under DOSBox with 16 MB
 * prints mem:13516k. */
#define MD_DPMI_FREE_MEMORY (64u * 1024u * 1024u)

/* CD-ROM TOC, from the disc's cue sheet: 215512 Mode2 sectors, one data track followed by eleven Red Book audio tracks.  MSCDEX reports absolute
 * ("Red Book") MSF = cue offset + 150 frames, packed frame:second:minute from
 * the low byte up. */
#define MD_MSF(m, s, f) ((uint32_t)((f) | ((s) << 8) | ((m) << 16)))
/* Drive letter MSCDEX reports for the CD, 0-based (3 = D:), MD-cdrom.c's
 * disc.  The install check (INT 2Fh AX=1500h) returns it in CX, and the
 * device requests (AX=1510h) name it in CX. */
#define MD_CD_DRIVE_INDEX 3
#define MD_CD_FIRST_TRACK 1
#define MD_CD_LAST_TRACK 12
#define MD_CD_VOLUME_SIZE 215512u
#define MD_CD_LEADOUT_MSF MD_MSF(47, 55, 37)

extern "C" {

extern void Game_ExitMain_Asm(int32_t status);
extern void X86_InterruptProcedure(uint8_t interrupt_number, void *registers);
extern _cpu *x86_initialize_cpu(void);
/* Generic guest-code call bridge (Game-llasm.llasm): tail-calls whatever
 * function pointer is in ecx. See call_guest_interrupt_vector. */
extern void CCALL c_RunProcECX(CPU);
/* Default "nothing registered" ISR (Game-llasm.llasm): pops a return address
 * and does the same iret-equivalent every other handler in this game uses.
 * interrupt_vectors[] is seeded with its address so a chain-to-the-old-vector
 * pattern (see call_guest_interrupt_vector) lands somewhere safe instead of
 * jumping to guest address 0, an unset vector. */
extern void CCALL c_md_null_isr(CPU);

/* The generated object defines this label at guest address loc_BF342 (see
 * SR-games/Mass Destruction/SR/llasm/global_aliases.sci). WATCOM's c0 startup
 * normally fills it in from its DOS-extender probe (INT 21h AH=30h, BL = ASCII
 * major version); we call main_ directly and skip c0, so MD_RuntimeInit seeds
 * it instead. Its value selects which allocator loc_738E0/loc_73990 use:
 * 0 = legacy INT 21h AH=ED/4A path, 1 = DOS/4GW 1.x, 9 = special, 2..8 = the
 * DOS/4GW 2.x paged path that loc_74773's near-heap grower expects. */

/* X86_InterruptFlag lives in main.c via Game_vars.h DEFINE_VARIABLES
 * (EXTERNAL_CVAR, C linkage) - same storage asm-cpu.c/llasm_pushx.c declare. */

extern "C" uint8_t md_heap_flag;
/* TEMP: language byte loc_BC8D0 shares dseg03 base with loc_BF342 (md_heap_flag). */
static uint8_t md_debug_lang(void)
{
    return *(const uint8_t *)((uintptr_t)&md_heap_flag - 0x2A72u);
}
/* obj2 is two blocks here: its file-backed part [0x90000, 0xF4000) is dseg03,
 * one contiguous piece of the linked .data, and the rest (BSS and stack, up
 * to 0x100E00, bssborder.csv) is useg05 in .bss. A flat address is its
 * distance from a label SR exports in the same block: loc_BF342
 * (md_heap_flag) in dseg03, stack_start (flat 0x100E00, the end of useg05)
 * in useg05. */
extern "C" uint8_t stack_start;
#define MD_OBJ2_FLAT 0x90000u
#define MD_OBJ2_BSS_FLAT 0xF4000u
#define MD_OBJ2_STACK_END_FLAT 0x100E00u
extern "C" void *MD_Obj2Pointer(uint32_t flat)
{
    /* integer arithmetic: the result lies outside the label's own object */
    if (flat >= MD_OBJ2_BSS_FLAT) return (void *)((uintptr_t)&stack_start - MD_OBJ2_STACK_END_FLAT + flat);
    return (void *)((uintptr_t)&md_heap_flag - 0xBF342u + flat);
}
uint32_t md_cr0 = 0x13;
/* `lsl eax, ds` (instruction_replacements.sci).  Its one reader is in WATCOM's
 * sbrk (loc_74759), which never gets that far: it needs a near heap, and only
 * the heap growers make one, dead since _nmalloc and _nfree are the host's;
 * nothing sets it. */
uint32_t md_ds_limit;
static uint8_t md_idt[2048];
uint32_t md_idt_base;
uint32_t md_smc_3f5d7;
uint32_t md_smc_3f5e2;
uint32_t md_smc_41532;
uint32_t md_smc_4153b;
uint32_t md_smc_4154f;
/* Patched fields of the span fillers at loc_40B40/loc_41060 and loc_41F52,
 * one per field, named after its address (instruction_replacements.sci). */
uint32_t md_smc_40b42;
uint32_t md_smc_40b4b;
uint32_t md_smc_40b57;
uint32_t md_smc_40b62;
uint32_t md_smc_40b6b;
uint32_t md_smc_40b73;
uint32_t md_smc_40b81;
uint32_t md_smc_40b8a;
uint32_t md_smc_40b92;
uint32_t md_smc_40ba0;
uint32_t md_smc_40ba9;
uint32_t md_smc_40bb1;
uint32_t md_smc_40bbf;
uint32_t md_smc_40bc8;
uint32_t md_smc_40bd0;
uint32_t md_smc_41062;
uint32_t md_smc_4106b;
uint32_t md_smc_41077;
uint32_t md_smc_4107f;
uint32_t md_smc_410a5;
uint32_t md_smc_410ae;
uint32_t md_smc_410b6;
uint32_t md_smc_410c0;
uint32_t md_smc_410d7;
uint32_t md_smc_410e0;
uint32_t md_smc_410e8;
uint32_t md_smc_410f2;
uint32_t md_smc_41109;
uint32_t md_smc_41112;
uint32_t md_smc_4111a;
uint32_t md_smc_41124;
uint32_t md_smc_4113b;
uint32_t md_smc_41144;
uint32_t md_smc_4114c;
uint32_t md_smc_41154;
uint32_t md_smc_41f54;
/* WATCOM int386() dispatches a software interrupt by returning into a raw
 * `int N / ret` stub; loc_74928 stashes N here and returns into
 * md_int86_thunk instead (see Game-llasm.llasm). */
uint32_t md_int86_no;

typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t ignored_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eflags;
} interrupt_frame;

static int quit_requested;
static uint8_t *framebuffer;
/* VGA DAC as the game programs it through ports 3C7h/3C8h/3C9h: 6-bit R, G, B
 * per entry.  Kept apart from the display's 8-bit copy (Set_Palette_Value,
 * display/pc.c) because the game reads colours back -- loc_3A1D8 saves the
 * whole DAC before a fade (callers loc_125B8, loc_12814, loc_12942). */
static uint8_t dac[256][3];
static uint8_t dac_write_index;
static uint8_t dac_write_component;
static uint8_t dac_read_index;
static uint8_t dac_read_component;
static uint8_t dac_state; /* port 3C7h read: 0 after 3C8h (writing), 3 after 3C7h (reading) */
static md_file *dos_handles[MD_MAX_HANDLES];
static uint32_t interrupt_vectors[MD_MAX_VECTORS];
static uint8_t key_queue[MD_KEY_QUEUE_SIZE];
static unsigned int key_queue_read;
static unsigned int key_queue_write;
/* BIOS/kbhit side (INT 21h AH=0Bh/08h via SR_checkch/SR_getch). Real hardware
 * keeps this separate from the controller byte the ISR reads on port 60h; the
 * game's loc_10480 never chains to a BIOS buffer, so we mirror makes here
 * whenever they enter the port-60h queue. Without this, deliver_keyboard_irq
 * drains key_queue before the menu's SR_checkch can ever see the press. */
static uint8_t bios_key_queue[MD_KEY_QUEUE_SIZE];
static unsigned int bios_key_read;
static unsigned int bios_key_write;
/* Set when a new make/break was queued since the last guest keyboard ISR, so
 * SR_CheckTimer can deliver INT 9 once the guest is between ACTION points.
 * Separate from the queue itself: the ISR drains it via port 60h. */
static int kbd_irq_pending;
/* Headless/key-test schedule from MD_AUTO_KEYS: "delay_ms:Key[~hold_ms],...".
 * Keys are pushed straight into key_queue + kbd_irq_pending (no X11 focus), which
 * is the only reliable way to feed input under Xvfb on this box.  Each key is a
 * make at delay_ms and a break hold_ms later (default MD_AUTO_KEY_HOLD_MS): the
 * menus poll the INT 9 key-state table once a frame, so a make and break back to
 * back are never seen there.  auto_key_* holds both events, sorted by time;
 * bit 7 of the scan code marks the break. */
#define MD_MAX_AUTO_KEYS 32
/* 120 ms, not the 300 ms first used: a menu that accepts on key-down hands over
 * to the next menu, which sees the same key still down and accepts too (the
 * campaign menu skipped its "Part 1" submenu that way). */
#define MD_AUTO_KEY_HOLD_MS 120
static uint32_t auto_key_at[2 * MD_MAX_AUTO_KEYS];
static uint8_t auto_key_scan[2 * MD_MAX_AUTO_KEYS];
static unsigned int auto_key_count;
static unsigned int auto_key_fired;
static char data_root[PATH_MAX];
/* Where files the game writes go (MD_SelectWriteRoot): MD_WRITE_ROOT, else
 * the game directory, as Albion's, Warcraft's and X-Com's ports write into
 * theirs; mdfiles/ under the start directory when there is no game data.
 * Anywhere but data_root it is an overlay, see MD-proc-vfs.c. */
static char write_root[PATH_MAX];
static int write_root_is_data_root;
/* Disk Transfer Area for INT 21h AH=4E/4F.  One active search (matches
 * DOS's single-DTA model); FindFirst collects the matches on the host, and
 * the guest DTA only ever carries the current result record. */
static uint8_t md_dta_default[0x80];
static uint32_t dta_addr;
#define MD_MAX_FIND_RESULTS 256
typedef struct {
    char dos_name[13];
    char *host_path;        /* or "CD:<path>" */
    uint8_t attr;
    int64_t cd_size;        /* a CD entry's size, else -1 */
} find_result;
static find_result find_results[MD_MAX_FIND_RESULTS];
static unsigned int find_count;
static unsigned int find_next;
static char find_pattern[16];


static uint8_t *md_psp;
static uint8_t *md_dosmem_base;
static uint8_t *md_lowmem;
static int watch_next;
/* Guards call_guest_interrupt_vector against reentrancy: ACTION_* checkpoints
 * are inserted at every backward jump in translated code, including inside
 * the guest's own timer ISR (its slot-dispatch loop has one), so running
 * that ISR can trigger SR_CheckTimer again before the outer call returns.
 * Matches Albion's Game_TimerRunning guard (Albion-timer.c). */
static int guest_timer_isr_running;
/* Bounds of the recompiled code's executable mapping (fixed at link time via
 * -Ttext-segment,0x10000000). Not every value that ends up in
 * interrupt_vectors[] is a proc address the game intends to be called as
 * translated code: WATCOM's startup installs placeholder vectors (seen as
 * low, sub-0x1000 values for INT 08h before the game's real handler is
 * registered) that were never meant to be dereferenced as flat DOS pointers
 * in the first place, only compared/round-tripped through _dos_getvect.
 * Calling one of those via the RunProcECX bridge is a jump to garbage, so
 * validate against this range first. */
static uintptr_t guest_code_lo;
static uintptr_t guest_code_hi;
static uint32_t md_lowmem_next;
/* Most recent DPMI 0100h real-mode block.  The CD chain addresses it by bare
 * offset (the segment load is dropped by SR), so seg_target() routes small
 * offsets here once it exists. */
static uint32_t md_dpmi_last_block;
/* Segment bases of the DPMI 0300h real-mode call structure, flattened so the
 * nested interrupt handlers get usable addresses. */
static uint32_t md_rm_es;
static uint32_t md_rm_ds;
/* md_lowmem - MD_LOWMEM_BASE, mod 2^32: a DOS address plus this is the pool's. */
extern "C" uint32_t md_lowmem_bias;
uint32_t md_lowmem_bias;

/* The host address of a real-mode (segment << 4) + offset address: in the
 * pool when it is one of the pool's, else itself. */
static uint32_t lowmem_flat(uint32_t address)
{
    if (address >= MD_LOWMEM_BASE && address < MD_LOWMEM_BASE + MD_LOWMEM_SIZE) return address + md_lowmem_bias;
    return address;
}

static const uint32_t md_cd_track_msf[MD_CD_LAST_TRACK + 1] = {
    0,                  /* tracks are 1-based; [0] is never read */
    MD_MSF( 0,  2,  0), /* 1  data   (cue 00:00:00) */
    MD_MSF(12, 44, 11), /* 2  audio  (cue 12:42:11) */
    MD_MSF(13, 52,  3), /* 3         (cue 13:50:03) */
    MD_MSF(14,  0, 43), /* 4         (cue 13:58:43) */
    MD_MSF(18, 15, 60), /* 5         (cue 18:13:60) */
    MD_MSF(22, 19,  1), /* 6         (cue 22:17:01) */
    MD_MSF(26, 13, 15), /* 7         (cue 26:11:15) */
    MD_MSF(30, 35, 12), /* 8         (cue 30:33:12) */
    MD_MSF(34, 26, 46), /* 9         (cue 34:24:46) */
    MD_MSF(38, 36, 37), /* 10        (cue 38:34:37) */
    MD_MSF(42, 27, 72), /* 11        (cue 42:25:72) */
    MD_MSF(46,  1, 28), /* 12        (cue 45:59:28) */
};

/* Red Book MSF, packed as above, to an HSG sector number. */
static uint32_t md_msf_to_hsg(uint32_t msf)
{
    return ((msf >> 16) & 0xffu) * 4500u + ((msf >> 8) & 0xffu) * 75u + (msf & 0xffu) - 150u;
}

typedef struct {
    uint32_t start;
    uint32_t size;
    int used;
} md_dosmem_block;

static md_dosmem_block md_dosmem_blocks[MD_DOSMEM_MAX_BLOCKS];
static int md_dosmem_block_count;

static void dosmem_init(void)
{
    md_dosmem_blocks[0].start = (uint32_t)(uintptr_t)md_dosmem_base;
    md_dosmem_blocks[0].size = MD_DOSMEM_SIZE;
    md_dosmem_blocks[0].used = 0;
    md_dosmem_block_count = 1;
}

static uint32_t dosmem_alloc(uint32_t bytes)
{
    for (int index = 0; index < md_dosmem_block_count; index++) {
        md_dosmem_block *block = &md_dosmem_blocks[index];
        if (block->used || block->size < bytes) continue;
        uint32_t start = block->start;
        uint32_t remaining = block->size - bytes;
        if (remaining > 0x100 && md_dosmem_block_count < MD_DOSMEM_MAX_BLOCKS) {
            md_dosmem_block *split = &md_dosmem_blocks[md_dosmem_block_count++];
            split->start = start + bytes;
            split->size = remaining;
            split->used = 0;
            block->size = bytes;
        }
        block->used = 1;
        memset((void *)(uintptr_t)start, 0, bytes);
        return start;
    }
    return 0;
}

static uint32_t dosmem_largest(void)
{
    uint32_t best = 0;
    for (int index = 0; index < md_dosmem_block_count; index++) {
        if (!md_dosmem_blocks[index].used && md_dosmem_blocks[index].size > best) best = md_dosmem_blocks[index].size;
    }
    return best;
}

static uint8_t frame_al(const interrupt_frame *frame)
{
    return (uint8_t)frame->eax;
}

static uint8_t frame_ah(const interrupt_frame *frame)
{
    return (uint8_t)(frame->eax >> 8);
}

static uint16_t frame_cx(const interrupt_frame *frame)
{
    return (uint16_t)frame->ecx;
}

static uint16_t frame_dx(const interrupt_frame *frame)
{
    return (uint16_t)frame->edx;
}

static uint16_t frame_bx(const interrupt_frame *frame)
{
    return (uint16_t)frame->ebx;
}

static void set_carry(interrupt_frame *frame, int value)
{
    if (value) frame->eflags |= MD_CF;
    else frame->eflags &= ~MD_CF;
}

static void set_ax(interrupt_frame *frame, uint16_t value)
{
    frame->eax = (frame->eax & 0xffff0000u) | value;
}

static void set_bx(interrupt_frame *frame, uint16_t value)
{
    frame->ebx = (frame->ebx & 0xffff0000u) | value;
}

static void set_cx(interrupt_frame *frame, uint16_t value)
{
    frame->ecx = (frame->ecx & 0xffff0000u) | value;
}

static void set_dx(interrupt_frame *frame, uint16_t value)
{
    frame->edx = (frame->edx & 0xffff0000u) | value;
}

static void dos_error(interrupt_frame *frame, uint16_t error)
{
    set_ax(frame, error);
    set_carry(frame, 1);
}

static void *guest_pointer(uint32_t address)
{
    return (void *)(uintptr_t)address;
}

/* MD_TRACE_* switches, read once.  getenv() on every port access, key
 * delivery and interrupt took a fifth of the main menu's CPU time (perf). */
enum { TRACE_CALLS, TRACE_FILE, TRACE_INT, TRACE_KEYS, TRACE_LANG, TRACE_PORTS, TRACE_REGS, TRACE_COUNT };
static int trace_enabled(int which)
{
    static int enabled[TRACE_COUNT] = { -1, -1, -1, -1, -1, -1, -1 };
    static const char *const names[TRACE_COUNT] = {
        "MD_TRACE_CALLS", "MD_TRACE_FILE", "MD_TRACE_INT", "MD_TRACE_KEYS",
        "MD_TRACE_LANG", "MD_TRACE_PORTS", "MD_TRACE_REGS"
    };
    if (enabled[which] < 0) enabled[which] = getenv(names[which]) != NULL;
    return enabled[which];
}

static int trace_file_enabled(void)
{
    return trace_enabled(TRACE_FILE);
}

/* The host call stack, for the debug traces; there is no backtrace() on
 * Windows. */
static void trace_backtrace(void)
{
#if !defined(_WIN32)
    void *bt[24];
    int count = backtrace(bt, 24);
    backtrace_symbols_fd(bt, count, STDERR_FILENO);
#endif
}

static void init_guest_code_bounds(void)
{
#if defined(_WIN32)
    /* The pages around a translated proc, all of the same protection: the
     * executable's code section. */
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery((const void *)&c_RunProcECX, &info, sizeof(info)) != 0 && (info.Protect & 0xf0) != 0) {
        guest_code_lo = (uintptr_t)info.BaseAddress;
        guest_code_hi = guest_code_lo + info.RegionSize;
    }
#else
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[256];

    if (maps == NULL) return;
    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long start, end;
        char perms[8];

        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        if (start == 0x10000000ul && strchr(perms, 'x') != NULL) {
            guest_code_lo = start;
            guest_code_hi = end;
            break;
        }
    }
    fclose(maps);
#endif
}

static int guest_code_address_valid(uint32_t address)
{
    return guest_code_lo != 0 && address >= guest_code_lo && address < guest_code_hi;
}

static uint16_t load_guest16(const uint8_t *pointer)
{
    uint16_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static uint32_t load_guest32(const uint8_t *pointer)
{
    uint32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void store_guest16(uint8_t *pointer, uint16_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

static void store_guest32(uint8_t *pointer, uint32_t value)
{
    memcpy(pointer, &value, sizeof(value));
}

/* An absolute host path: "/..." here, "C:\..." or "\\server\..." on Windows. */
static int host_path_is_absolute(const char *path)
{
#if defined(_WIN32)
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return 1;
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

/* The absolute form of a host path, into out (PATH_MAX bytes); 0 on success. */
static int host_full_path(const char *path, char *out)
{
#if defined(_WIN32)
    return _fullpath(out, path, PATH_MAX) != NULL ? 0 : -1;
#else
    return realpath(path, out) != NULL ? 0 : -1;
#endif
}

/* Whether two host paths are one existing directory, however spelled. */
static int same_directory(const char *first, const char *second)
{
#if defined(_WIN32)
    char full_first[PATH_MAX], full_second[PATH_MAX];
    size_t length_first, length_second;
    if (host_full_path(first, full_first) != 0 || host_full_path(second, full_second) != 0) return 0;
    length_first = strlen(full_first);
    length_second = strlen(full_second);
    while (length_first > 3 && (full_first[length_first - 1] == '\\' || full_first[length_first - 1] == '/')) length_first--;
    while (length_second > 3 && (full_second[length_second - 1] == '\\' || full_second[length_second - 1] == '/')) length_second--;
    if (length_first != length_second) return 0;
    for (size_t index = 0; index < length_first; index++) {
        char a = full_first[index] == '/' ? '\\' : (char)toupper((unsigned char)full_first[index]);
        char b = full_second[index] == '/' ? '\\' : (char)toupper((unsigned char)full_second[index]);
        if (a != b) return 0;
    }
    return 1;
#else
    struct stat status_first, status_second;
    return stat(first, &status_first) == 0 && stat(second, &status_second) == 0 &&
           S_ISDIR(status_first.st_mode) && status_first.st_dev == status_second.st_dev &&
           status_first.st_ino == status_second.st_ino;
#endif
}

/* A regular file `name` in `dir`, any case: copies of the game often change it. */
static int dir_has_file(const char *dir, const char *name)
{
    DIR *handle = opendir(dir);
    if (handle == NULL) return 0;
    int found = 0;
    for (struct dirent *entry = readdir(handle); entry != NULL && !found; entry = readdir(handle)) {
        char path[PATH_MAX];
        struct stat status;
        if (strcasecmp(entry->d_name, name) != 0) continue;
        int length = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        found = length > 0 && length < (int)sizeof(path) && stat(path, &status) == 0 && S_ISREG(status.st_mode);
    }
    closedir(handle);
    return found;
}

/* DOS path -> host path, via the virtualfs file_entry tree.
 * vfs_get_real_name walks the tree vfs_visit_dir built at MD_RuntimeInit
 * (case-insensitive through the uppercase dos_name), the one FindFirst/FindNext
 * iterate too. Returns 0 with host_path filled on success (entry present in
 * the tree). */
static int resolve_dos_path(const char *dos_path, char *host_path, size_t host_path_size)
{
    char rewritten[PATH_MAX];
    const char *source = dos_path;
    file_entry *realdir;
    int vfs_err;

    if (source == NULL || *source == 0) return -1;
    if (host_path_size < MAX_PATH) return -1;

    /* C: is the vfs tree (Game_CDir); the CD drive is MD-cdrom.c's, and no
     * other drive exists. */
    if (source[1] == ':') {
        if (toupper((unsigned char)source[0]) != 'C') return -1;
        if (snprintf(rewritten, sizeof(rewritten), "C:%s", source + 2) >= (int)sizeof(rewritten))
            return -1;
        source = rewritten;
    }

    vfs_err = vfs_get_real_name(source, host_path, &realdir);
    /* 0 = found. 1 = final component missing (buf holds the would-be path
     * under the resolved parent); 2 = parent missing. dos_open only ever
     * opens existing files (rb/rb+, never creates), so anything but a hit is
     * a failure. */
    if (vfs_err != 0) return -1;
    return 0;
}

static md_file *dos_file(uint16_t handle)
{
    return handle < MD_MAX_HANDLES ? dos_handles[handle] : NULL;
}

/* A path on the CD drive: its part after "D:", else NULL. */
static const char *cd_relative_path(const char *dos_path)
{
    if (dos_path == NULL || dos_path[0] == 0 || dos_path[1] != ':') return NULL;
    if (toupper((unsigned char)dos_path[0]) - 'A' != MD_CD_DRIVE_INDEX) return NULL;
    return dos_path + 2;
}

/* Opens a file on the CD, read-only as the medium is. NULL, errno set, when
 * it is not there (ENOENT) or a write is asked for (EACCES). */
static md_file *open_cd_file(const char *relative, int write)
{
    md_cd_entry entry;
    if (MD_CdFind(relative, &entry) != 0 || entry.directory) {
        errno = ENOENT;
        return NULL;
    }
    if (write) {
        errno = EACCES;
        return NULL;
    }
    md_file *file = MD_CdOpen(relative);
    if (file == NULL) errno = ENOENT;
    return file;
}

static int allocate_handle(md_file *file)
{
    for (int handle = 5; handle < MD_MAX_HANDLES; handle++) {
        if (dos_handles[handle] == NULL) {
            dos_handles[handle] = file;
            return handle;
        }
    }
    return -1;
}

/* The handle table and path resolution above, for MD-proc-vfs.c. */
extern "C" int MD_ResolveDataPath(const char *dos_path, char *host_path, size_t host_path_size)
{
    return resolve_dos_path(dos_path, host_path, host_path_size);
}

extern "C" const char *MD_WriteRoot(void)
{
    return write_root;
}

extern "C" int MD_WriteRootIsDataRoot(void)
{
    return write_root_is_data_root;
}

/* With the write root in the game directory: where a C: path is written. A
 * file that is there keeps its own name, whatever its case; a new one goes
 * into its directory under the name in upper case, as DOS makes it, and
 * *new_in is that directory, for MD_GameDirAdded once the file exists. */
extern "C" int MD_GameDirWritePath(const char *dos_path, char *host_path, size_t host_path_size, void **new_in)
{
    char rewritten[PATH_MAX];
    char found[MAX_PATH];
    const char *source = dos_path;
    const char *leaf;
    file_entry *realdir;
    size_t length;

    *new_in = NULL;
    if (source == NULL || *source == 0) return -1;
    if (source[1] == ':') {
        if (toupper((unsigned char)source[0]) != 'C') return -1;
        if (snprintf(rewritten, sizeof(rewritten), "C:%s", source + 2) >= (int)sizeof(rewritten)) return -1;
        source = rewritten;
    }
    switch (vfs_get_real_name(source, found, &realdir)) {
    case 0:
        return snprintf(host_path, host_path_size, "%s", found) < (int)host_path_size ? 0 : -1;
    case 1:
        /* realdir is the directory, found its would-be path in lower case */
        leaf = source + strlen(source);
        while (leaf > source && leaf[-1] != '\\' && leaf[-1] != '/' && leaf[-1] != ':') leaf--;
        if (*leaf == 0) return -1;
        length = (size_t)snprintf(host_path, host_path_size, "%s/", realdir->real_fullname);
        for (; *leaf != 0; leaf++) {
            if (length + 1 >= host_path_size) return -1;
            host_path[length++] = (char)toupper((unsigned char)*leaf);
        }
        host_path[length] = 0;
        *new_in = realdir;
        return 0;
    default:
        return -1;          /* a directory on the way is missing */
    }
}

/* A file MD_GameDirWritePath placed, now created: into the VFS, so opens by
 * MD_ResolveDataPath and FindFirst see it, as Albion's Game_fopen does. */
extern "C" void MD_GameDirAdded(void *new_in, const char *host_path)
{
    if (new_in != NULL) vfs_add_file((file_entry *)new_in, host_path, 0);
}

extern "C" int MD_AttachHandle(md_file *file)
{
    return allocate_handle(file);
}

extern "C" md_file *MD_OpenCdFile(const char *dos_path, int write, int *on_cd)
{
    const char *relative = cd_relative_path(dos_path);
    *on_cd = relative != NULL;
    return relative != NULL ? open_cd_file(relative, write) : NULL;
}

extern "C" md_file *MD_HandleFile(int handle)
{
    return handle >= 0 ? dos_file((uint16_t)handle) : NULL;
}

extern "C" void MD_ReleaseHandle(int handle)
{
    if (handle >= 0 && handle < MD_MAX_HANDLES) dos_handles[handle] = NULL;
}

extern "C" int MD_TraceFileEnabled(void)
{
    return trace_file_enabled();
}

static void queue_bios_key(uint8_t scan_code)
{
    unsigned int next = (bios_key_write + 1) % MD_KEY_QUEUE_SIZE;
    if (next == bios_key_read) return;
    bios_key_queue[bios_key_write] = scan_code;
    bios_key_write = next;
    if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY bios queue %02x\n", scan_code);
}

static void queue_key(uint8_t scan_code)
{
    unsigned int next = (key_queue_write + 1) % MD_KEY_QUEUE_SIZE;
    if (next == key_queue_read) return;
    key_queue[key_queue_write] = scan_code;
    key_queue_write = next;
    kbd_irq_pending = 1;
    if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY queue %02x\n", scan_code);
    /* Make codes also become visible to kbhit/getch. Break stays port-60h only. */
    if ((scan_code & 0x80) == 0) queue_bios_key(scan_code);
}

static int dequeue_key(uint8_t *scan_code)
{
    if (key_queue_read == key_queue_write) return 0;
    *scan_code = key_queue[key_queue_read];
    key_queue_read = (key_queue_read + 1) % MD_KEY_QUEUE_SIZE;
    return 1;
}

static int dequeue_bios_key(uint8_t *scan_code)
{
    if (bios_key_read == bios_key_write) return 0;
    *scan_code = bios_key_queue[bios_key_read];
    bios_key_read = (bios_key_read + 1) % MD_KEY_QUEUE_SIZE;
    return 1;
}

/* Non-destructive peek for INT 21h AH=0Bh (kbhit()): true if a real keypress
 * (not just a queued key-release) is waiting. Reads the BIOS-side queue so the
 * INT 9 ISR draining port 60h cannot starve SR_checkch. */
int key_available(void)
{
    return bios_key_read != bios_key_write;
}

static uint8_t dos_scan_code(SDL_Scancode scan_code)
{
    static const struct { SDL_Scancode sdl; uint8_t dos; } scan_codes[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03}, {SDL_SCANCODE_3, 0x04}, {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06}, {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08}, {SDL_SCANCODE_8, 0x09}, {SDL_SCANCODE_9, 0x0a}, {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_MINUS, 0x0c}, {SDL_SCANCODE_EQUALS, 0x0d}, {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12}, {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15}, {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18}, {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_LCTRL, 0x1d}, {SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_S, 0x1f}, {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25}, {SDL_SCANCODE_L, 0x26},
        {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28}, {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2a}, {SDL_SCANCODE_BACKSLASH, 0x2b}, {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_X, 0x2d}, {SDL_SCANCODE_C, 0x2e}, {SDL_SCANCODE_V, 0x2f}, {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_CAPSLOCK, 0x3a}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F2, 0x3c}, {SDL_SCANCODE_F3, 0x3d}, {SDL_SCANCODE_F4, 0x3e}, {SDL_SCANCODE_F5, 0x3f}, {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41}, {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_NUMLOCKCLEAR, 0x45}, {SDL_SCANCODE_SCROLLLOCK, 0x46}, {SDL_SCANCODE_HOME, 0x47}, {SDL_SCANCODE_UP, 0x48}, {SDL_SCANCODE_PAGEUP, 0x49}, {SDL_SCANCODE_LEFT, 0x4b}, {SDL_SCANCODE_RIGHT, 0x4d}, {SDL_SCANCODE_END, 0x4f}, {SDL_SCANCODE_DOWN, 0x50}, {SDL_SCANCODE_PAGEDOWN, 0x51}, {SDL_SCANCODE_INSERT, 0x52}, {SDL_SCANCODE_DELETE, 0x53}
    };
    for (unsigned int index = 0; index < sizeof(scan_codes) / sizeof(scan_codes[0]); index++) {
        if (scan_codes[index].sdl == scan_code) return scan_codes[index].dos;
    }
    return 0;
}

/* Any thread: push EC_DISPLAY_FLIP_START so the main loop arms FlipActive and
 * wakes the flip thread (same handoff as Albion's Game_RunTimer). */
void MD_RequestPresent(void)
{
    SDL_Event event;

    event.type = SDL_USEREVENT;
    event.user.code = EC_DISPLAY_FLIP_START;
    event.user.data1 = NULL;
    event.user.data2 = NULL;
    SDL_PushEvent(&event);
}

static uint8_t dos_scan_code(SDL_Scancode scan_code);

/* Game thread: move main-thread SDL key/mouse events into the DOS-side queues. */
void MD_DrainEventKeys(void)
{
    while (Game_KQueueRead != Game_KQueueWrite)
    {
        SDL_Event event = Game_EventKQueue[Game_KQueueRead];
        Game_KQueueRead = (Game_KQueueRead + 1) & (GAME_KQUEUE_LENGTH - 1);
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            uint8_t scan_code = dos_scan_code(event.key.keysym.scancode);
            if (scan_code != 0) queue_key(scan_code | (event.type == SDL_KEYUP ? 0x80 : 0));
        }
    }
}

/* DOS scan code (make code, bit 0x80 clear) -> unshifted ASCII, for INT 21h
 * AH=08/0Bh (the WATCOM runtime's getch()/kbhit() -- confirmed live via
 * dis.py against the real MASSD.EXE image: 0x6a34e/0x6a35d is kbhit, 0x6aa96
 * is getch, called from 0x3eb2d as "if kbhit() { if (getch()==ESC) ... }"
 * and from 0x58f52 as a flush-pending-keys loop). No shift-state tracking,
 * matching the same simplification the existing INT 16h case already makes. */
uint8_t dos_scan_to_ascii(uint8_t scan_code)
{
    switch (scan_code) {
    case 0x01: return 0x1b;
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3'; case 0x05: return '4'; case 0x06: return '5';
    case 0x07: return '6'; case 0x08: return '7'; case 0x09: return '8'; case 0x0a: return '9'; case 0x0b: return '0';
    case 0x0c: return '-'; case 0x0d: return '='; case 0x0e: return 0x08; case 0x0f: return 0x09;
    case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e'; case 0x13: return 'r'; case 0x14: return 't';
    case 0x15: return 'y'; case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o'; case 0x19: return 'p';
    case 0x1c: return 0x0d;
    case 0x1e: return 'a'; case 0x1f: return 's'; case 0x20: return 'd'; case 0x21: return 'f'; case 0x22: return 'g';
    case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
    case 0x27: return ';'; case 0x28: return '\''; case 0x29: return '`'; case 0x2b: return '\\';
    case 0x2c: return 'z'; case 0x2d: return 'x'; case 0x2e: return 'c'; case 0x2f: return 'v'; case 0x30: return 'b';
    case 0x31: return 'n'; case 0x32: return 'm'; case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/';
    case 0x39: return ' ';
    default: return 0;
    }
}

/* PIT channel 0.  The game's per-frame tick is locked to vertical retrace:
 * its INT 8 handler (loc_53F1D, sound off) or its AIL timer (loc_53752, sound
 * on) waits for retrace, then reprograms the PIT -- divisor 0x3FCC through
 * loc_3A45A, or AIL's period minus 320 us through loc_68924 -- so that the
 * next interrupt comes shortly before the following retrace.  Two things make
 * that work and are modelled: writing the reload value restarts the count,
 * as it does on the 8253 in modes 2 and 3, and the interrupt is scheduled
 * here in the game thread, where SR_CheckTimer runs often enough to deliver
 * it on time (the margin before retrace is 0.6 ms without sound).  Nothing
 * reads the counter back. */
static uint32_t pit_divisor = 0x10000u;
static uint8_t pit_access;          /* 1 = lo only, 2 = hi only, 3 = lo then hi */
static uint8_t pit_write_high;      /* access 3: next port-40h write is the high byte */
static uint8_t pit_low;
static uint64_t perf_frequency;
static uint64_t pit_next_tick;      /* performance counter */

static uint64_t pit_period(void)
{
    return perf_frequency * pit_divisor / 1193182u;
}

static void pit_restart(void)
{
    pit_next_tick = SDL_GetPerformanceCounter() + pit_period();
}

/* VGA Input Status #1 (3DAh) bit 3 for mode 13h: a 70.086 Hz frame of 449
 * lines at 800 dots of 25.175 MHz, with the vertical sync pulse on lines
 * 412-414 (CRTC 10h/11h = 9Ch/8Eh), timed as dosbox-staging times it.  The
 * retrace-locked tick above waits for it, so this is the game's frame rate. */
static uint8_t vga_input_status1(void)
{
    const uint64_t frame = perf_frequency * (449u * 800u) / 25175000u;
    const uint64_t position = SDL_GetPerformanceCounter() % frame;
    return (position >= frame * 412u / 449u && position < frame * 414u / 449u) ? 0x08 : 0x00;
}

static void fire_auto_keys(uint32_t now_ms);
static void deliver_keyboard_irq(void);
static int call_guest_interrupt_vector(uint8_t interrupt_number);

/* Blocking counterpart to dequeue_key() for INT 21h AH=08h: every call site
 * found in MASSD.EXE guards this with kbhit() (AH=0Bh) first, so in practice
 * this never actually spins, but real DOS AH=08 does block until a key is
 * available and nothing here should assume every future caller pre-checks. */
uint8_t dos_blocking_getch(void)
{
    uint8_t scan_code;
    for (;;) {
        while (dequeue_bios_key(&scan_code)) {
            if ((scan_code & 0x80) == 0) {
                if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY getch %02x\n", scan_code);
                return dos_scan_to_ascii(scan_code);
            }
        }
        if (Thread_Exit) return 0;
        MD_DrainEventKeys();
        fire_auto_keys(SDL_GetTicks());
        deliver_keyboard_irq();
        MD_RequestPresent();
        SDL_Delay(10);
    }
}

/* Run guest code at `target` from host code, as a hardware interrupt would:
 * `frame` is pushed first (frame[0] ends up at [esp]), eflags has IF set, and
 * guest_timer_isr_running keeps INT 8/9 and the AIL timer from nesting.  The
 * registers the guest may clobber are restored afterwards. */
static void run_guest_code(uint32_t target, const uint32_t *frame, unsigned int words, uint32_t eax)
{
    _cpu *cpu = x86_initialize_cpu();
    uint32_t old_eax = cpu->_eax;
    uint32_t old_ecx = cpu->_ecx;
    uint32_t old_ebp = cpu->_ebp;
    uint32_t old_esi = cpu->_esi;
    uint32_t old_edi = cpu->_edi;
    uint32_t old_eflags = cpu->_eflags;
    uint32_t old_flag = X86_InterruptFlag;
    uint32_t old_esp = cpu->_esp;

    cpu->_eflags = 0x3202;
    cpu->_eax = eax;
    cpu->_ecx = target;
    cpu->_esp -= 4 * words;
    memcpy((void *)(uintptr_t)cpu->_esp, frame, 4 * words);
    guest_timer_isr_running = 1;
    c_RunProcECX(cpu);
    guest_timer_isr_running = 0;
    cpu->_esp = old_esp;

    cpu->_eax = old_eax;
    cpu->_ecx = old_ecx;
    cpu->_ebp = old_ebp;
    cpu->_esi = old_esi;
    cpu->_edi = old_edi;
    cpu->_eflags = old_eflags;
    X86_InterruptFlag = old_flag;
}

/* A timer callback registered with AIL (MD-sound.c), called the way AIL's own
 * ISR at loc_687CE calls it once it has re-enabled interrupts: `push user` then
 * a near call, with eax = user too.  Skipped, like a hardware interrupt, while
 * the guest has IF clear or another guest ISR is running. */
extern "C" int MD_GuestInterruptsEnabled(void)
{
    return X86_InterruptFlag != 0 && !guest_timer_isr_running;
}

extern "C" int MD_CallGuestTimerCallback(uint32_t target, uint32_t user)
{
    if (!guest_code_address_valid(target) || !MD_GuestInterruptsEnabled()) return 0;
    /* [0] return address: the unwind sentinel (see call_guest_interrupt_vector);
     * [1] the cdecl argument, which the caller pops */
    const uint32_t frame[2] = { 0, user };
    run_guest_code(target, frame, 2, user);
    return 1;
}

/* Returns 1 when the handler ran, 0 when it could not (invalid vector, IF clear,
 * another ISR running). */
static int call_guest_interrupt_vector(uint8_t interrupt_number)
{
    int valid = guest_code_address_valid(interrupt_vectors[interrupt_number]);
    int iflag = X86_InterruptFlag != 0;
    int nested = guest_timer_isr_running;
    if (interrupt_number == 0x09 && trace_enabled(TRACE_KEYS)) {
        fprintf(stderr, "KEY isr enter? vec=%08x valid=%d IF=%d nested=%d\n",
                interrupt_vectors[interrupt_number], valid, iflag, nested);
    }
    if (valid && iflag && !nested) {
        if (interrupt_number == 0x09 && trace_enabled(TRACE_KEYS))
            fprintf(stderr, "KEY isr RUN vec=%08x\n", interrupt_vectors[interrupt_number]);
        /* Whatever this proc's eventual ret/retn/iret pops as the "return
         * address" must resolve back to us, not to guest code -- there is
         * none, we're calling in fresh. Albion's Game_MouseMove does the same
         * "emulate far call" push of a bare 0 before invoking a callback
         * through this same RunProcECX bridge; 0 is never a valid translated
         * proc address, so it's the sentinel this backend's pop-then-tcall
         * epilogues use to unwind back to their native caller instead of
         * jumping into guest address 0. The guest handler's own pushad/iret
         * (baked into its translated body) pops that sentinel and returns.
         * The rest of the frame is what iret pops after it: CS, then the
         * flags it restores (IF set, as on entry to a hardware interrupt).
         * With the sentinel alone, the translated iret popped CS and flags
         * from whatever lay above the stack, and loc_53F1D's _chain_intr path
         * rearranges exactly those slots. */
        const uint32_t iret_frame[3] = {
            0,          /* EIP: the unwind sentinel */
            0,          /* CS */
            0x3202u,    /* EFLAGS */
        };
        run_guest_code(interrupt_vectors[interrupt_number], iret_frame, 3, x86_initialize_cpu()->_eax);
        return 1;
    }
    return 0;
}

/* Deliver a pending keyboard IRQ to the guest's INT 9 hook (installed at startup
 * via INT 21h AH=25h). No-op until SETVECT 9 has run or if already inside an
 * ISR (guest_timer_isr_running is the shared reentrancy guard). */
static void deliver_keyboard_irq(void)
{
    /* The ISR body starts with ACTION_CALL -> SR_CheckTimer, which re-enters
     * here before IN 60h. Re-setting kbd_irq_pending from that nested call
     * (queue still holds the byte) caused a third delivery with an empty
     * port-60h (scancode 0). Bail out entirely while an ISR is running. */
    if (guest_timer_isr_running) return;
    /* One ISR invocation drains exactly one port-60h byte. A make+break pair
     * therefore needs two deliveries, or the break never reaches the ring. */
    int delivered = 0;
    while (kbd_irq_pending || key_queue_read != key_queue_write) {
        unsigned int read_before = key_queue_read;
        if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY irq deliver\n");
        kbd_irq_pending = 0;
        /* The handler cannot run now (IF clear, say): the keys stay queued for
         * the next checkpoint.  Looping on would never end. */
        if (!call_guest_interrupt_vector(0x09)) break;
        delivered = 1;
        if (guest_timer_isr_running) break;
        /* A handler that left port 60h alone, the null ISR that stands in for
         * the BIOS's before the game installs its own, would be called again
         * and again for the same byte, which only the BIOS handler of real
         * hardware takes (queue_key has put make codes in the BIOS queue). */
        if (key_queue_read == read_before) {
            uint8_t dropped;
            dequeue_key(&dropped);
        }
    }
    kbd_irq_pending = (key_queue_read != key_queue_write);
    if (delivered && trace_enabled(TRACE_KEYS)) {
        /* dseg03 lives in linked .data, not at the DOS flat address.
         * loc_E0700 (count), loc_E0704 (r), loc_E0708 (w) share the same
         * base as the exported loc_BF342 == md_heap_flag. */
        uint32_t *ring = (uint32_t *)((uintptr_t)&md_heap_flag + 0x213beu);
        fprintf(stderr, "KEY ring count=%u r=%u w=%u bios=%u port60=%u\n",
                ring[0], ring[1], ring[2],
                (bios_key_write + MD_KEY_QUEUE_SIZE - bios_key_read) % MD_KEY_QUEUE_SIZE,
                (key_queue_write + MD_KEY_QUEUE_SIZE - key_queue_read) % MD_KEY_QUEUE_SIZE);
    }
}

/* Insert one make/break event, keeping auto_key_* sorted by time (stable, so
 * a key's make stays ahead of its break even with ~0). */
static void add_auto_key_event(uint32_t at_ms, uint8_t scan)
{
    unsigned int i = auto_key_count;
    while (i > 0 && auto_key_at[i - 1] > at_ms) {
        auto_key_at[i] = auto_key_at[i - 1];
        auto_key_scan[i] = auto_key_scan[i - 1];
        i--;
    }
    auto_key_at[i] = at_ms;
    auto_key_scan[i] = scan;
    auto_key_count++;
}

/* MD_AUTO_KEYS=delay_ms:Key[~hold_ms],...  -- parse once at runtime init. */
static void parse_auto_keys(void)
{
    const char *spec = getenv("MD_AUTO_KEYS");
    if (spec == NULL || spec[0] == 0) return;
    while (*spec != 0 && auto_key_count + 2 <= 2 * MD_MAX_AUTO_KEYS) {
        char *end = NULL;
        unsigned long delay_ms = strtoul(spec, &end, 10);
        if (end == spec || *end != ':') {
            fprintf(stderr, "Mass Destruction: bad MD_AUTO_KEYS near '%s'\n", spec);
            return;
        }
        spec = end + 1;
        /* key token runs up to the next comma or NUL */
        char name[32];
        size_t n = 0;
        while (spec[n] != 0 && spec[n] != ',' && spec[n] != '~' && n + 1 < sizeof(name)) {
            name[n] = spec[n];
            n++;
        }
        name[n] = 0;
        spec += n;
        uint8_t scan = 0;
        if (strcmp(name, "Return") == 0) scan = 0x1c;
        else if (strcmp(name, "Escape") == 0) scan = 0x01;
        else if (strcmp(name, "Space") == 0) scan = 0x39;
        else if (strcmp(name, "Up") == 0) scan = 0x48;
        else if (strcmp(name, "Down") == 0) scan = 0x50;
        else if (strcmp(name, "Left") == 0) scan = 0x4b;
        else if (strcmp(name, "Right") == 0) scan = 0x4d;
        else {
            SDL_Scancode sc = SDL_GetScancodeFromName(name);
            if (sc != SDL_SCANCODE_UNKNOWN) scan = dos_scan_code(sc);
        }
        if (scan == 0) {
            fprintf(stderr, "Mass Destruction: MD_AUTO_KEYS unknown key '%s'\n", name);
            return;
        }
        unsigned long hold_ms = MD_AUTO_KEY_HOLD_MS;
        if (*spec == '~') {
            hold_ms = strtoul(spec + 1, &end, 10);
            if (end == spec + 1) {
                fprintf(stderr, "Mass Destruction: bad MD_AUTO_KEYS hold near '%s'\n", spec);
                return;
            }
            spec = end;
        }
        add_auto_key_event((uint32_t)delay_ms, scan);
        add_auto_key_event((uint32_t)(delay_ms + hold_ms), scan | 0x80);
        if (*spec == ',') spec++;
    }
    fprintf(stderr, "Mass Destruction: MD_AUTO_KEYS %u events\n", auto_key_count);
}

static void fire_auto_keys(uint32_t now_ms)
{
    while (auto_key_fired < auto_key_count && now_ms >= auto_key_at[auto_key_fired]) {
        /* loc_10480 sets the key-state table on make and enqueues into the
         * game ring only on break; SDL input sends both, and so does this. */
        queue_key(auto_key_scan[auto_key_fired]);
        auto_key_fired++;
    }
}

void SR_CheckTimer(void)
{
    static uint32_t last_present;
    uint32_t now;

    /* Albion/Warcraft: every ACTION point is an exit checkpoint. */
    if (Thread_Exit) Game_StopMain();

    MD_DrainEventKeys();
    now = SDL_GetTicks();
    fire_auto_keys(now);

    {
        const uint64_t counter = SDL_GetPerformanceCounter();
        if (counter >= pit_next_tick)
        {
            const uint64_t period = pit_period();
            /* A stall (debugger, suspended VM) is not caught up tick by tick. */
            if (counter - pit_next_tick > 8 * period) pit_next_tick = counter;
            while (counter >= pit_next_tick)
            {
                pit_next_tick += period;
                /* AIL's ISR owns INT 8 while its timers run (the game
                 * installs loc_53F1D only when sound is off); it chains to
                 * the BIOS handler, which call_guest_interrupt_vector's tick
                 * models.  Either handler may reprogram the PIT, which moves
                 * pit_next_tick past `counter` and ends this loop. */
                MD_AIL_TimerInterrupt();
                call_guest_interrupt_vector(0x08);
                if (Thread_Exit) Game_StopMain();
            }
        }
    }

    deliver_keyboard_irq();

    if (now - last_present >= 33)
    {
        MD_RequestPresent();
        last_present = now;
    }
}

/* 6-bit DAC level -> 8 bits, linear and rounded to nearest (63 -> 255).  This
 * is dosbox-staging's mapping, so port frames match the reference pixel for
 * pixel; (level << 2) | (level >> 4) is up to one step off it. */
static uint32_t dac_level_to_8bit(uint8_t level)
{
    return ((uint32_t)level * 255u + 31u) / 63u;
}

uint32_t X86_InPortProcedure(uint16_t port, uint32_t size)
{
    (void)size;
    uint8_t scan_code;
    if (trace_enabled(TRACE_PORTS)) fprintf(stderr, "PORT IN %04x\n", port);
    if (port == 0x60) {
        if (dequeue_key(&scan_code)) return scan_code;
        if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY port60 empty\n");
        return 0;
    }
    if (port == 0x64) return key_queue_read == key_queue_write ? 0 : 1;
    if (port == 0x3c7) return dac_state;
    if (port == 0x3c8) return dac_write_index;
    if (port == 0x3c9) {
        uint8_t level = dac[dac_read_index][dac_read_component];
        if (++dac_read_component == 3) {
            dac_read_component = 0;
            dac_read_index++;
        }
        return level;
    }
    if (port == 0x3da) { uint8_t v = vga_input_status1(); if (trace_enabled(TRACE_PORTS)) fprintf(stderr, "PORT IN 03da = %02x @%p\n", v, __builtin_return_address(0)); return v; }
    return 0;
}

void X86_OutPortProcedure(uint16_t port, uint32_t size, uint32_t value)
{
    (void)size;
    if (port == 0x43) {
        /* bits 7-6 channel, 5-4 access (0 = latch), 3-1 mode */
        if ((value & 0xc0u) == 0 && (value & 0x30u) != 0) {
            pit_access = (uint8_t)((value >> 4) & 3u);
            pit_write_high = 0;
        }
        return;
    }
    if (port == 0x40) {
        uint32_t reload;
        if (pit_access == 3 && !pit_write_high) {
            pit_low = (uint8_t)value;
            pit_write_high = 1;
            return;
        }
        if (pit_access == 3) reload = pit_low | ((value & 0xffu) << 8);
        else if (pit_access == 2) reload = (value & 0xffu) << 8;
        else reload = value & 0xffu;
        pit_write_high = 0;
        pit_divisor = reload == 0 ? 0x10000u : reload;
        pit_restart();
        return;
    }
    if (port == 0x3c7) {
        dac_read_index = (uint8_t)value;
        dac_read_component = 0;
        dac_state = 3;
        return;
    }
    if (port == 0x3c8) {
        dac_write_index = (uint8_t)value;
        dac_write_component = 0;
        dac_state = 0;
        return;
    }
    if (port == 0x3c9) {
        dac[dac_write_index][dac_write_component] = (uint8_t)value & 0x3f;
        if (++dac_write_component == 3) {
            const uint8_t *colour = dac[dac_write_index];
            Set_Palette_Value(dac_write_index, dac_level_to_8bit(colour[0]),
                              dac_level_to_8bit(colour[1]), dac_level_to_8bit(colour[2]));
            dac_write_component = 0;
            dac_write_index++;
        }
    }
}

/* Program channel 0 the way AIL's loc_688F4 does -- control word 36h, then the
 * reload value low byte first -- through the model the game's own writes use. */
void MD_ProgramPit(uint32_t divisor)
{
    X86_OutPortProcedure(0x43, 1, 0x36);
    X86_OutPortProcedure(0x40, 1, divisor & 0xffu);
    X86_OutPortProcedure(0x40, 1, (divisor >> 8) & 0xffu);
}

/* SR turns a segment-override access into a helper call carrying only the
 * offset -- the segment register is dropped.  Two segments are used this way
 * and they are cleanly separated in time: before any real-mode block exists it
 * is the PSP (c0 startup reads the environment paragraph at +0x2C and the
 * memory top at +0x5C), afterwards it is the most recent DPMI 0100h block, which
 * the CD chain addresses at offsets 0x00/0x02/0x0E/0x10/0x12 to build MSCDEX
 * request headers. */
static uint32_t seg_target(uint32_t address)
{
    if (address >= MD_PSP_SIZE) return 0;
    if (md_dpmi_last_block != 0 && address != 0x2cu && address != 0x5cu) return md_dpmi_last_block + address;
    return (uint32_t)(uintptr_t)md_psp + address;
}

uint32_t X86_ReadMemProcedure(uint32_t address, uint32_t size)
{
    uint32_t target = seg_target(address);
    if (target != 0) {
        uint32_t value = 0;
        memcpy(&value, (void *)(uintptr_t)target, size);
        return value;
    }
    if (address < MD_A0000 || address + size > MD_A0000 + MD_FRAMEBUFFER_SIZE) return 0;
    uint32_t value = 0;
    memcpy(&value, framebuffer + address - MD_A0000, size);
    return value;
}

void X86_WriteMemProcedure(uint32_t address, uint32_t size, uint32_t value)
{
    uint32_t target = seg_target(address);
    if (target != 0) {
        memcpy((void *)(uintptr_t)target, &value, size);
        return;
    }
    if (address < MD_A0000 || address + size > MD_A0000 + MD_FRAMEBUFFER_SIZE) return;
    memcpy(framebuffer + address - MD_A0000, &value, size);
}

static void dump_guest_dir_dump(void)
{
    const char *dir = getenv("MD_DUMP");
    if (dir == NULL) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/lowmem.bin", dir);
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(md_lowmem, 1, MD_LOWMEM_SIZE, f);
        fclose(f);
    }
    fprintf(stderr, "MD_DUMP: dumped\n");
}

/* The game opens ERROR.LOG only on its fatal-error path; that open is the
 * MD_DUMP trigger, from dos_open below or from Game_fopen (MD-proc-vfs.c). */
extern "C" void MD_DumpGuestMemory(void)
{
    dump_guest_dir_dump();
}

static void dos_open(interrupt_frame *frame)
{
    char resolved_path[PATH_MAX];
    const char *mode = (frame_al(frame) & 3) == 0 ? "rb" : "rb+";
    const char *dos_filename = (const char *)guest_pointer(frame->edx);
    const char *cd_path = cd_relative_path(dos_filename);
    md_file *file;
    if (cd_path != NULL) {
        file = open_cd_file(cd_path, (frame_al(frame) & 3) != 0);
        snprintf(resolved_path, sizeof(resolved_path), "CD:%s", cd_path);
    } else if (resolve_dos_path(dos_filename, resolved_path, sizeof(resolved_path)) != 0) {
        file = NULL;
        errno = ENOENT;
    } else {
        if (strstr(resolved_path, "ERROR.LOG") != NULL) dump_guest_dir_dump();
        file = md_file_host(fopen(resolved_path, mode));
    }
    if (file == NULL) {
        if (trace_file_enabled()) fprintf(stderr, "FILE OPENFAIL '%s'\n", dos_filename);
        dos_error(frame, errno == EACCES ? 5 : 2);
        return;
    }
    int handle = allocate_handle(file);
    if (handle < 0) {
        md_fclose(file);
        dos_error(frame, 4);
        return;
    }
    if (trace_file_enabled()) {
        fprintf(stderr, "FILE OPEN %02x h=%d '%s'\n", frame_al(frame) & 3, handle, resolved_path);
    }
    set_ax(frame, (uint16_t)handle);
    set_carry(frame, 0);
}

/* AH=3Ch: create a file, or cut an existing one to 0 bytes, and open it for
 * reading and writing.  The /D screenshot writer (0x51F60) is the caller.
 * The file goes where the game's own fopen puts a new one (MD-proc-vfs.c):
 * the write root, or the game directory.  CX, the attributes, is not used. */
static void dos_create(interrupt_frame *frame)
{
    const char *dos_filename = (const char *)guest_pointer(frame->edx);
    errno = 0;
    md_file *file = MD_OpenGameFile(dos_filename, 'w', 1);
    if (file == NULL) {
        if (trace_file_enabled()) fprintf(stderr, "FILE CREATEFAIL '%s'\n", dos_filename);
        dos_error(frame, errno == EACCES ? 5 : 3);
        return;
    }
    int handle = allocate_handle(file);
    if (handle < 0) {
        md_fclose(file);
        dos_error(frame, 4);
        return;
    }
    if (trace_file_enabled()) fprintf(stderr, "FILE CREATE h=%d '%s'\n", handle, dos_filename);
    set_ax(frame, (uint16_t)handle);
    set_carry(frame, 0);
}

/* AH=09h: write the '$'-terminated string at DS:EDX to standard output; AL
 * comes back as '$'.  The game's only caller is the screenshot writer's
 * "Could not open file." after a failed create (0x51F84), and it sets only DX
 * (`lea dx`): EDX keeps the high half of the file name's stack address, so in
 * DOS as well the text comes from somewhere else.  Here that address can be
 * past the end of the mapped image, so a string is taken only from the obj2
 * block it starts in (MD_Obj2Pointer), where the game's strings are, and no
 * further than 64 KiB, the size of the real-mode segment DS:DX addresses. */
static void dos_print_string(interrupt_frame *frame)
{
    uintptr_t text = (uintptr_t)guest_pointer(frame->edx);
    uintptr_t data = (uintptr_t)MD_Obj2Pointer(MD_OBJ2_FLAT);
    uintptr_t bss = (uintptr_t)MD_Obj2Pointer(MD_OBJ2_BSS_FLAT);
    uintptr_t end = 0;
    if (text >= data && text < data + (MD_OBJ2_BSS_FLAT - MD_OBJ2_FLAT)) end = data + (MD_OBJ2_BSS_FLAT - MD_OBJ2_FLAT);
    else if (text >= bss && text < (uintptr_t)&stack_start) end = (uintptr_t)&stack_start;
    if (end != 0 && end - text > 0x10000) end = text + 0x10000;
    uintptr_t at = text;
    while (at < end && *(const char *)at != '$') at++;
    if (at < end) {
        fwrite((const char *)text, 1, at - text, stdout);
        fflush(stdout);
    } else {
        fprintf(stderr, "Mass Destruction: INT 21h AH=09h: no string at %08x in the game's data\n", frame->edx);
    }
    frame->eax = (frame->eax & 0xffffff00u) | '$';
}

/* AH=2Ch: the local time, CH hour, CL minute, DH second, DL hundredths, read
 * as Warcraft's _dos_gettime does (Warcraft-proc.c).  The live caller is
 * WATCOM's _dos_gettime (0x74691) in sleep() (0x69A71), which the switch
 * parser (0x14B34) calls for one second after "Invalid Option." (an unknown
 * switch, or /I above 7).  sleep() polls until the second changes; with the
 * registers left as they were, it spun for good.  The
 * other sites are dead here: the delay calibration at 0x75B7C is a WATCOM
 * initializer, which the port skips with c0, and time() (0x6BEFE) has one
 * caller, AIL_startup, which is SR_AIL_startup. */
static void dos_get_time(interrupt_frame *frame)
{
    struct tm *parts;
    unsigned int hundredths;
#if defined(_WIN32)
    struct timeb now;
    ftime(&now);
    parts = localtime(&now.time);
    hundredths = now.millitm / 10;
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    time_t seconds = now.tv_sec;
    parts = localtime(&seconds);
    hundredths = now.tv_usec / 10000;
#endif
    if (parts == NULL) {
        set_cx(frame, 0);
        set_dx(frame, (uint16_t)hundredths);
        return;
    }
    set_cx(frame, (uint16_t)((parts->tm_hour << 8) | parts->tm_min));
    set_dx(frame, (uint16_t)((parts->tm_sec << 8) | hundredths));
}

static void dos_read(interrupt_frame *frame)
{
    md_file *file = dos_file(frame_bx(frame));
    if (file == NULL) {
        dos_error(frame, 6);
        return;
    }
    long at = md_ftell(file);
    /* This is a 32-bit DOS4GW program: a transfer count lives in the full
     * ECX, not just CX, and can legitimately exceed 65535 in one call (the
     * TANK.RES resource loader reads its chunks in flat 0x10000-byte pages).
     * A count of 0 reads nothing, as in DOS. */
    uint32_t count = frame->ecx;
    if (trace_file_enabled()) {
        fprintf(stderr, "FILE READ h=%02x ofs=%08lx n=%04x dst=%08x\n",
                frame_bx(frame), at, count, frame->edx);
    }
    if (trace_enabled(TRACE_CALLS) && frame_cx(frame) > 0) {
        fprintf(stderr, "STACK at=%08lx n=%04x dst=%08x:\n", at, frame_cx(frame), frame->edx);
        trace_backtrace();
    }
    if (trace_enabled(TRACE_REGS) && frame_bx(frame) == 5 && at < 0x6000) {
        fprintf(stderr, "REGS at=%08lx n=%04x dst=%08x eax=%08x ebx=%08x ecx=%08x edx=%08x "
                        "esi=%08x edi=%08x ebp=%08x esp=%08x\n",
                at, frame_cx(frame), frame->edx, frame->eax, frame->ebx, frame->ecx, frame->edx,
                frame->esi, frame->edi, frame->ebp, frame->ignored_esp);
    }
    size_t length = md_fread(guest_pointer(frame->edx), count, file);
    if (md_ferror(file)) {
        md_clearerr(file);
        dos_error(frame, 5);
        return;
    }
    /* set_ax() only ever writes the low 16 bits: a full 0x10000-byte chunk
     * (or any transfer over 64K) would come back as AX=0, which every caller reads as "read nothing" even though
     * the data landed correctly -- exactly what broke loading
     * MAPS\LEVEL30.TB1 (a real 0x10000-byte resource chunk). */
    frame->eax = (uint32_t)length;
    set_carry(frame, 0);
}

static void dos_write(interrupt_frame *frame)
{
    uint16_t handle = frame_bx(frame);
    /* See dos_read: full ECX, not truncated CX, and the byte count is
     * reported back through the full EAX, not the 16-bit-only set_ax(). */
    uint32_t length = frame->ecx;
    if (handle == 1 || handle == 2) {
        if (trace_enabled(TRACE_LANG)) {
            const char *p = (const char *)guest_pointer(frame->edx);
            fprintf(stderr, "LANG@write h=%u n=%u lang=%u data=%.*s\n",
                    handle, length, md_debug_lang(),
                    (int)(length > 40 ? 40 : length), p);
        }
        fwrite(guest_pointer(frame->edx), 1, length, handle == 1 ? stdout : stderr);
        fflush(handle == 1 ? stdout : stderr);
        frame->eax = length;
        set_carry(frame, 0);
        return;
    }
    md_file *file = dos_file(handle);
    if (file == NULL) {
        dos_error(frame, 6);
        return;
    }
    /* A count of 0 cuts the file at the current position: WATCOM's open does
     * this for O_TRUNC (0x6AB79), and so does its write(fd, buf, 0). */
    if (length == 0) {
        if (trace_file_enabled()) fprintf(stderr, "FILE TRUNC h=%02x at=%08lx\n", handle, md_ftell(file));
        if (md_ftruncate(file) != 0) {
            md_clearerr(file);
            dos_error(frame, 5);
            return;
        }
        frame->eax = 0;
        set_carry(frame, 0);
        return;
    }
    size_t written = md_fwrite(guest_pointer(frame->edx), length, file);
    if (written != length) {
        dos_error(frame, 5);
        return;
    }
    frame->eax = length;
    set_carry(frame, 0);
}

static void dos_seek(interrupt_frame *frame)
{
    md_file *file = dos_file(frame_bx(frame));
    int origin = frame_al(frame) == 0 ? SEEK_SET : frame_al(frame) == 1 ? SEEK_CUR : SEEK_END;
    int32_t offset = (int32_t)((frame->ecx << 16) | frame_dx(frame));
    if (trace_file_enabled()) {
        fprintf(stderr, "FILE SEEK h=%02x al=%02x off=%08x at=%08lx\n",
                frame_bx(frame), frame_al(frame), (uint32_t)offset, file != NULL ? md_ftell(file) : -1L);
    }
    if (file == NULL || md_fseek(file, offset, origin) != 0) {
        dos_error(frame, 1);
        return;
    }
    long position = md_ftell(file);
    if (position < 0) {
        dos_error(frame, 1);
        return;
    }
    if (trace_enabled(TRACE_REGS) && (uint32_t)position == 0x42e131u) {
        fprintf(stderr, "SEEKREGS al=%02x off=%08x eax=%08x ebx=%08x ecx=%08x edx=%08x "
                        "esi=%08x edi=%08x ebp=%08x\n",
                frame_al(frame), (uint32_t)offset, frame->eax, frame->ebx, frame->ecx, frame->edx,
                frame->esi, frame->edi, frame->ebp);
        extern void X86_InterruptProcedure(uint8_t, void *);
        watch_next = 60;
    }
    frame->eax = (uint32_t)position;
    frame->edx = (uint32_t)position >> 16;
    set_carry(frame, 0);
}

static void dos_close(interrupt_frame *frame)
{
    uint16_t handle = frame_bx(frame);
    md_file *file = dos_file(handle);
    if (file == NULL) {
        dos_error(frame, 6);
        return;
    }
    dos_handles[handle] = NULL;
    if (md_fclose(file) != 0) {
        dos_error(frame, 6);
        return;
    }
    set_carry(frame, 0);
}

/* DOS 8.3 wildcard match against an uppercased name (Albion's
 * file_pattern_match, same algorithm). */
static int dos_pattern_match(const char *filename, const char *pattern)
{
    int i, asterisk;

    if (pattern[0] == '*' && pattern[1] == '.' && pattern[2] == '*' && pattern[3] == 0) return 1;

new_segment:
    asterisk = 0;
    if (*pattern == '*') {
        asterisk = 1;
        do {
            pattern++;
        } while (*pattern == '*');
    }

test_match:
    for (i = 0; (pattern[i] != 0) && (pattern[i] != '*'); i++) {
        if (filename[i] != pattern[i]) {
            if (filename[i] == 0) return 0;
            if ((pattern[i] == '?') && (filename[i] != '.')) continue;
            if (!asterisk) return 0;
            filename++;
            goto test_match;
        }
    }

    if (pattern[i] == '*') {
        filename += i;
        pattern += i;
        goto new_segment;
    }

    if (pattern[i] == 0) return filename[i] == 0;
    /* pattern ended mid-name with a pending asterisk segment already consumed */
    if (asterisk) {
        if (*filename == 0) return 1;
        filename++;
        goto test_match;
    }
    return 0;
}

/* Split "DIR\FILE.EXT" -> dir + pattern.  Empty dir means CWD. */
static int split_find_path(const char *path, char *dir_out, size_t dir_sz, char *pat_out, size_t pat_sz)
{
    const char *slash = NULL;
    const char *p;

    for (p = path; *p != 0; p++) {
        if (*p == '\\' || *p == '/') slash = p;
    }
    if (slash != NULL) {
        size_t length = (size_t)(slash - path);
        if (length >= dir_sz) return -1;
        memcpy(dir_out, path, length);
        dir_out[length] = 0;
        strncpy(pat_out, slash + 1, pat_sz - 1);
        pat_out[pat_sz - 1] = 0;
    } else {
        dir_out[0] = 0;
        strncpy(pat_out, path, pat_sz - 1);
        pat_out[pat_sz - 1] = 0;
    }
    if (pat_out[0] == 0) {
        if (pat_sz < 5) return -1;
        strcpy(pat_out, "*.*");
    }
    return 0;
}

static void fill_dta_from_result(const find_result *result)
{
    uint8_t *dta = (uint8_t *)guest_pointer(dta_addr);
    struct stat file_stat;
    uint16_t dos_time = 0, dos_date = 0;
    uint32_t size = 0;

    memset(dta, 0, 0x2b);
    if (result->cd_size >= 0) {
        size = (result->attr & 0x10) ? 0 : (uint32_t)result->cd_size;
    } else if (stat(result->host_path, &file_stat) == 0) {
        struct tm *parts = localtime(&file_stat.st_mtime);
        if (parts != NULL) {
            dos_time = (uint16_t)(((parts->tm_hour & 31) << 11) |
                                  ((parts->tm_min & 63) << 5) |
                                  ((parts->tm_sec / 2) & 31));
            dos_date = (uint16_t)((((parts->tm_year - 80) & 127) << 9) |
                                  (((parts->tm_mon + 1) & 15) << 5) |
                                  (parts->tm_mday & 31));
        }
        if (S_ISREG(file_stat.st_mode)) size = (uint32_t)file_stat.st_size;
    }
    dta[0x15] = result->attr;
    store_guest16(dta + 0x16, dos_time);
    store_guest16(dta + 0x18, dos_date);
    store_guest32(dta + 0x1a, size);
    memcpy(dta + 0x1e, result->dos_name, 12);
    dta[0x1e + 12] = 0;
}

/* Write the next collected match into the DTA.  Shared by FindFirst (after
 * collecting) and FindNext. */
static void find_advance(interrupt_frame *frame)
{
    if (find_next < find_count) {
        const find_result *result = &find_results[find_next++];
        fill_dta_from_result(result);
        if (trace_file_enabled()) {
            fprintf(stderr, "FILE FIND '%s' -> '%s' attr=%02x (%s)\n",
                    find_pattern, result->dos_name, result->attr, result->host_path);
        }
        set_carry(frame, 0);
        return;
    }
    if (trace_file_enabled()) {
        fprintf(stderr, "FILE FIND '%s' -> done\n", find_pattern);
    }
    dos_error(frame, 0x12); /* no more files */
}

static void clear_find_results(void)
{
    for (unsigned int index = 0; index < find_count; index++) free(find_results[index].host_path);
    find_count = 0;
    find_next = 0;
}

/* Adds a match unless one with the same DOS name is already listed; the
 * write root is collected first, so its copy wins, as it does for reads.
 * Only HIDDEN/SYSTEM/SUBDIR are filtered against CX, the rule Albion's
 * Game_dos_findfirst uses; archive on a regular file always passes. */
static void add_find_result(const char *dos_name, const char *host_path, uint8_t attr, uint16_t attr_mask,
                            int64_t cd_size)
{
    if (!dos_pattern_match(dos_name, find_pattern)) return;
    if ((attr & (uint8_t)~attr_mask & (0x02 | 0x04 | 0x10)) != 0) return;
    for (unsigned int index = 0; index < find_count; index++) {
        if (strcmp(find_results[index].dos_name, dos_name) == 0) return;
    }
    if (find_count >= MD_MAX_FIND_RESULTS) return;
    char *copy = strdup(host_path);
    if (copy == NULL) return;
    find_result *result = &find_results[find_count++];
    snprintf(result->dos_name, sizeof(result->dos_name), "%s", dos_name);
    result->host_path = copy;
    result->attr = attr;
    result->cd_size = cd_size;
}

/* A host name as DOS sees it: 8.3 and upper case, as virtualfs lists them;
 * anything longer is skipped there too. */
static int dos_name_of(const char *host_name, char *dos_name)
{
    size_t length = strlen(host_name);
    const char *dot = strchr(host_name, '.');
    if (length == 0 || length > 12 || host_name[0] == '.') return -1;
    if (dot == NULL ? length > 8 : (dot - host_name > 8 || strchr(dot + 1, '.') != NULL || strlen(dot + 1) > 3)) return -1;
    for (size_t index = 0; index <= length; index++) dos_name[index] = (char)toupper((unsigned char)host_name[index]);
    return 0;
}

/* The write root mirrors C:\ (MD-proc-vfs.c's write_root_path): "C:\DIR" or
 * "DIR" -> <write root>/DIR, upper case; nothing changes the DOS current
 * directory from the root. */
static void collect_write_root_matches(const char *dos_dir, uint16_t attr_mask)
{
    char host_dir[PATH_MAX];
    const char *source = dos_dir;
    size_t length;

    if (source[0] != 0 && source[1] == ':') source += 2;
    while (*source == '\\' || *source == '/') source++;
    length = (size_t)snprintf(host_dir, sizeof(host_dir), "%s", write_root);
    if (*source != 0 && length + 1 < sizeof(host_dir)) host_dir[length++] = '/';
    for (; *source != 0 && length + 1 < sizeof(host_dir); source++) {
        host_dir[length++] = (*source == '\\') ? '/' : (char)toupper((unsigned char)*source);
    }
    host_dir[length] = 0;

    DIR *dir = opendir(host_dir);
    if (dir == NULL) return;
    for (struct dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
        char dos_name[13];
        char host_path[PATH_MAX];
        struct stat file_stat;
        if (dos_name_of(entry->d_name, dos_name) != 0) continue;
        int written = snprintf(host_path, sizeof(host_path), "%s/%s", host_dir, entry->d_name);
        if (written < 0 || written >= (int)sizeof(host_path) || stat(host_path, &file_stat) != 0) continue;
        if (!S_ISREG(file_stat.st_mode) && !S_ISDIR(file_stat.st_mode)) continue;
        add_find_result(dos_name, host_path, S_ISDIR(file_stat.st_mode) ? 0x10 : 0x20, attr_mask, -1);
    }
    closedir(dir);
}

static void dos_set_dta(interrupt_frame *frame)
{
    dta_addr = frame->edx;
    if (trace_file_enabled()) {
        fprintf(stderr, "FILE SETDTA %08x\n", dta_addr);
    }
    set_carry(frame, 0);
}

typedef struct {
    uint16_t attr_mask;
    const char *dir;
} cd_find;

static void add_cd_match(const md_cd_entry *entry, void *arg)
{
    const cd_find *find = (const cd_find *)arg;
    char where[PATH_MAX];
    snprintf(where, sizeof(where), "CD:%s\\%s", find->dir, entry->dos_name);
    add_find_result(entry->dos_name, where, entry->directory ? 0x11 : 0x21, find->attr_mask, entry->size);
}

/* FindFirst lists the write root's matches first, then the data tree's: a
 * file the game wrote (a new TANK.SAV) is found even when the data directory
 * has none, and it shadows the data directory's copy, as it does for opens. */
static void dos_findfirst(interrupt_frame *frame)
{
    const char *path = (const char *)guest_pointer(frame->edx);
    char dir_path[PATH_MAX];
    char pattern[16];
    char rewritten[PATH_MAX];
    const char *dir_source;
    file_entry *dir_entry = NULL;
    file_entry *realdir = NULL;
    int vfs_err = 0;
    unsigned int index;
    uint16_t attr_mask = frame_cx(frame);

    if (split_find_path(path, dir_path, sizeof(dir_path), pattern, sizeof(pattern)) != 0) {
        dos_error(frame, 0x03);
        return;
    }
    for (index = 0; pattern[index] != 0; index++) pattern[index] = (char)toupper((unsigned char)pattern[index]);

    /* The CD drive: MD-cdrom.c's directory, read-only, nothing from the
     * write root. */
    const char *cd_dir = cd_relative_path(dir_path);
    if (cd_dir != NULL) {
        clear_find_results();
        strncpy(find_pattern, pattern, sizeof(find_pattern) - 1);
        find_pattern[sizeof(find_pattern) - 1] = 0;
        cd_find find = { attr_mask, cd_dir };
        if (MD_CdList(cd_dir, add_cd_match, &find) != 0) {
            if (trace_file_enabled()) fprintf(stderr, "FILE FINDFAIL '%s' (CD dir '%s')\n", path, cd_dir);
            dos_error(frame, 0x03);
            return;
        }
        if (trace_file_enabled()) fprintf(stderr, "FILE FIND1 '%s' CD dir='%s' cx=%04x: %u match(es)\n", path, cd_dir, attr_mask, find_count);
        find_advance(frame);
        return;
    }

    /* C:, the only other drive (resolve_dos_path). */
    dir_source = dir_path;
    if (dir_source[0] != 0 && dir_source[1] == ':') {
        if (toupper((unsigned char)dir_source[0]) != 'C') {
            dos_error(frame, 0x03);
            return;
        }
        if (snprintf(rewritten, sizeof(rewritten), "C:%s", dir_source + 2) >= (int)sizeof(rewritten)) {
            dos_error(frame, 0x03);
            return;
        }
        dir_source = rewritten;
    }

    if (dir_source[0] == 0) {
        dir_entry = vfs_get_current_dir();
    } else {
        vfs_err = vfs_get_real_name(dir_source, NULL, &realdir);
        if (vfs_err == 0 && realdir != NULL && (realdir->attributes & 1)) dir_entry = realdir;
    }

    clear_find_results();
    strncpy(find_pattern, pattern, sizeof(find_pattern) - 1);
    find_pattern[sizeof(find_pattern) - 1] = 0;
    /* a write root in the game directory is the data tree itself */
    if (!write_root_is_data_root) collect_write_root_matches(dir_source, attr_mask);
    if (dir_entry != NULL) {
        if (dir_entry->dir_visited == 0) vfs_visit_dir(dir_entry);
        for (file_entry *entry = dir_entry->first_child; entry != NULL; entry = entry->next) {
            add_find_result(entry->dos_name, entry->real_fullname, (entry->attributes & 1) ? 0x10 : 0x20, attr_mask, -1);
        }
    }

    if (trace_file_enabled()) {
        fprintf(stderr, "FILE FIND1 '%s' dir='%s' cx=%04x: %u match(es)%s\n", path,
                dir_entry != NULL ? dir_entry->dos_fullname : dir_path, attr_mask, find_count,
                dir_entry == NULL ? " (not in the data tree)" : "");
    }
    if (dir_entry == NULL && find_count == 0) {
        if (trace_file_enabled()) fprintf(stderr, "FILE FINDFAIL '%s' (dir '%s' err=%d)\n", path, dir_path, vfs_err);
        dos_error(frame, 0x03);
        return;
    }
    find_advance(frame);
}

static void dos_findnext(interrupt_frame *frame)
{
    if (find_pattern[0] == 0) {
        dos_error(frame, 0x12);
        return;
    }
    find_advance(frame);
}

static void handle_dos_interrupt(interrupt_frame *frame)
{
    switch (frame_ah(frame)) {
    /* AH=08h (getch) / AH=0Bh (kbhit) are not here: external_procedures.sci
     * routes both call sites through SR_checkch/SR_getch to Game_checkch/
     * Game_getch (MD-proc.c), which use key_available()/dos_blocking_getch().
     * A raw int 21h with AH=08/0B from any other site falls through to
     * "unimplemented". */
    case 0x09:
        dos_print_string(frame);
        break;
    case 0x19:
        set_ax(frame, 2);
        break;
    case 0x1a:
        dos_set_dta(frame);
        return;
    case 0x25:
        /* Standard AH=25h is AL=vector. With md_heap_flag in 2..8 the game's
         * setvect/getvect wrappers (loc_633DD / loc_633AB) emit the DOS/4GW
         * extended forms instead: AX=2504h CL=vector DX=handler (set) and
         * AX=2502h CL=vector (get, returns BX). Indexing those by AL (always
         * 4 or 2) never installs INT 9 — the menu ISR stays the null stub and
         * injected keys do nothing. */
        if ((frame->eax & 0xffffu) == 0x2502u) {
            uint8_t vector = (uint8_t)frame->ecx;
            if (trace_enabled(TRACE_INT))
                fprintf(stderr, "GETVECT-ext %02x -> %08x\n", vector, interrupt_vectors[vector]);
            frame->ebx = interrupt_vectors[vector];
        } else if ((frame->eax & 0xffffu) == 0x2504u) {
            uint8_t vector = (uint8_t)frame->ecx;
            if (trace_enabled(TRACE_INT)) {
                fprintf(stderr, "SETVECT-ext %02x = %08x:", vector, frame->edx);
                for (unsigned int i = 0; i < 16; i++)
                    fprintf(stderr, "%02x ", ((unsigned char *)frame->edx)[i]);
                fprintf(stderr, "\n");
            }
            interrupt_vectors[vector] = frame->edx;
        } else {
            if (trace_enabled(TRACE_INT)) {
                fprintf(stderr, "SETVECT %02x = %08x:", frame_al(frame), frame->edx);
                for (unsigned int i = 0; i < 16; i++)
                    fprintf(stderr, "%02x ", ((unsigned char *)frame->edx)[i]);
                fprintf(stderr, "\n");
            }
            interrupt_vectors[frame_al(frame)] = frame->edx;
        }
        break;
    case 0x2c:
        dos_get_time(frame);
        break;
    case 0x2f:
        /* Get DTA (AH=2Fh).  Was a stub returning 0; FindFirst consumers
         * that round-trip through Get DTA need the real address. */
        frame->ebx = dta_addr;
        break;
    case 0x30:
        /* Report the DOS4GW/PharLap extender signature in the upper 16 bits
         * of eax: c0's startup probes this (EBX='PHAR' before the call) to
         * decide whether it owns a real DPMI host. Without it, AL/AH still
         * come out right for a plain DOS-version query, but c0 takes the
         * legacy int21 AH=ED/4A resize dance instead of seeding loc_BF314
         * from PSP+0x5C -- which we never populate for that path -- and the
         * first heap growth ends up chasing a null pointer. */
        frame->eax = 0x44580700u;
        /* RBIL: BL is the ASCII major DOS version. c0's extender detector does
         * `sub bl, 0x30` and stores the result at loc_BF342, so report a
         * version loc_74773's DOS/4GW 2.x path accepts (2..8). */
        frame->ebx = (frame->ebx & 0xffffff00u) | '2';
        break;
    case 0x35:
        frame->ebx = interrupt_vectors[frame_al(frame)];
        break;
    case 0x3c:
        dos_create(frame);
        return;
    case 0x3d:
        dos_open(frame);
        return;
    case 0x3e:
        dos_close(frame);
        return;
    case 0x3f:
        dos_read(frame);
        return;
    case 0x40:
        dos_write(frame);
        return;
    case 0x42:
        dos_seek(frame);
        return;
    case 0x44:
        set_dx(frame, 0x0080);
        break;
    case 0x47:
        ((char *)guest_pointer(frame->esi))[0] = 0;
        break;
    case 0x4e:
        dos_findfirst(frame);
        return;
    case 0x4f:
        dos_findnext(frame);
        return;
    case 0x48: {
        uint32_t paragraphs = frame_bx(frame);
        if (paragraphs == 0xffff) {
            /* Common "how much is free" probe: callers ask for an
             * intentionally unsatisfiable amount and read the real size
             * back from bx, ignoring carry. */
            set_bx(frame, dosmem_largest() >> 4);
            break;
        }
        uint32_t block = dosmem_alloc(paragraphs * 16);
        if (block == 0) {
            set_bx(frame, dosmem_largest() >> 4);
            dos_error(frame, 8);
            return;
        }
        set_ax(frame, (uint16_t)(block >> 4));
        break;
    }
    case 0x49:
        /* Free memory block. The arena above never reclaims blocks -- a
         * short-lived game session leaking a few DOS allocations is
         * harmless, and it avoids trusting a guest-supplied segment as a
         * host pointer. */
        break;
    case 0x4a:
        /* Resize memory block.  Its callers are c0 (0x631C2, 0x631E5), which
         * the port skips for main_, and WATCOM's __brk (loc_74773), dead since
         * the heap is the host's: refuse, and say so. */
        fprintf(stderr, "Mass Destruction: INT 21h AH=4Ah (resize) is not expected\n");
        dos_error(frame, 8);
        return;
    default:
        fprintf(stderr, "Mass Destruction: unimplemented DOS function %02x\n", frame_ah(frame));
        dos_error(frame, 1);
        return;
    }
    set_carry(frame, 0);
}

/* MSCDEX (INT 2Fh AH=15h).  The install check returns CX = the first CD drive
 * letter (0 = A), not a version -- returning a version made the game build
 * "<'A'+0x2A2>:\tank.res".  The device requests used are IOCTL input
 * (command 3) and the Red Book audio ones, played by MD-music.c. */
static void handle_mscdex(interrupt_frame *frame)
{
    switch (frame_al(frame)) {
    case 0x00:
        frame->eax = (frame->eax & 0xffffff00u) | 0xff;
        set_bx(frame, 1);
        set_cx(frame, MD_CD_DRIVE_INDEX);
        break;
    case 0x01:
        set_ax(frame, 1);
        break;
    case 0x02:
        /* copyright string: nothing reads it */
        break;
    case 0x0b:
        set_ax(frame, (frame->ecx & 0xffffu) == MD_CD_DRIVE_INDEX ? 1 : 0);
        set_bx(frame, 0xadad);
        break;
    case 0x0d:
        break;
    case 0x10: {
        uint8_t *request = (uint8_t *)guest_pointer(md_rm_es + (frame->ebx & 0xffffu));
        uint8_t command = request[2];
        uint32_t transfer = lowmem_flat((((uint32_t)load_guest16(request + 0x10)) << 4) + load_guest16(request + 0x0e));
        uint8_t *control = (uint8_t *)guest_pointer(transfer);
        store_guest16(request + 3, 0x0100);
        if (command == 0x84) {
            /* PLAY AUDIO: +0Dh addressing mode, +0Eh first sector, +12h
             * sector count.  loc_7D72C sends HSG sectors from its TOC at 0xA4
             * (converted from our IOCTL 0Bh answers), a whole track at a
             * time; Red Book addressing is taken too. */
            uint32_t start = load_guest32(request + 0x0e);
            uint32_t sectors = load_guest32(request + 0x12);
            if (request[0x0d] == 1) start = md_msf_to_hsg(start);
            int track = MD_CD_LAST_TRACK;
            while (track > MD_CD_FIRST_TRACK && md_msf_to_hsg(md_cd_track_msf[track]) > start) track--;
            if (trace_file_enabled()) fprintf(stderr, "MSCDEX play %u+%u = track %d\n", start, sectors, track);
            if (track == MD_CD_FIRST_TRACK) {
                store_guest16(request + 3, 0x8100 | 0x0c);   /* data track: general failure */
                break;
            }
            MD_MusicPlay(track, start - md_msf_to_hsg(md_cd_track_msf[track]), sectors);
            break;
        }
        if (command == 0x85 || command == 0x88) {
            /* STOP AUDIO (loc_7D85A) / RESUME AUDIO */
            if (trace_file_enabled()) fprintf(stderr, "MSCDEX audio cmd=%02x\n", command);
            if (command == 0x85) MD_MusicStop();
            else MD_MusicResume();
            break;
        }
        if (command != 3) {
            fprintf(stderr, "Mass Destruction: unimplemented MSCDEX request command %02x\n", command);
            break;
        }
        switch (control[0]) {
        case 0x00:
            /* Status / capability probe. The game only needs the call to
             * succeed so the CD startup chain can continue. */
            break;
        case 0x0a:
            control[1] = MD_CD_FIRST_TRACK;
            control[2] = MD_CD_LAST_TRACK;
            store_guest32(control + 3, MD_CD_LEADOUT_MSF);
            break;
        case 0x0b: {
            uint8_t track = control[1];
            if (track < MD_CD_FIRST_TRACK || track > MD_CD_LAST_TRACK) {
                store_guest16(request + 3, 0x8100 | 0x0f);
                break;
            }
            store_guest32(control + 2, md_cd_track_msf[track]);
            control[6] = track == 1 ? 0x40 : 0x00;
            break;
        }
        case 0x06:
            store_guest32(control + 1, 0);
            break;
        case 0x08:
            store_guest32(control + 1, MD_CD_VOLUME_SIZE);
            break;
        default:
            fprintf(stderr, "Mass Destruction: unimplemented MSCDEX IOCTL subcommand %02x\n", control[0]);
            break;
        }
        break;
    }
    default:
        fprintf(stderr, "Mass Destruction: unimplemented MSCDEX function %02x\n", frame_al(frame));
        break;
    }
}

static void handle_int2f(interrupt_frame *frame)
{
    uint16_t ax = frame->eax & 0xffffu;
    if (frame_ah(frame) == 0x15) {
        handle_mscdex(frame);
        return;
    }
    switch (ax) {
    case 0x1600:
        /* Windows enhanced-mode check: AL=00h, no Windows, as under DOS and
         * DOSBox. loc_1FE48 takes any other AL for Windows and sets
         * "windows compatibility mode" (0xF3DF7), in which the frame tick
         * (loc_53752 / loc_53F1D) does not wait for vertical retrace: the
         * game then runs at the AIL timer's 80 Hz instead of mode 13h's 70 Hz,
         * the intro 15% fast. */
        set_ax(frame, 0x1600);
        break;
    case 0x4680:
        /* Windows/DOS ext compatibility probe. The game only accepts the
         * 0x80 response as the "not-present / safe" value for this call. */
        set_ax(frame, 0x0080);
        break;
    case 0x4a33:
        /* Another optional multiplex probe used by the startup path. Return the
         * neutral "not implemented / no extension" response expected by the
         * checks in the translated code. */
        set_ax(frame, 0x0000);
        break;
    default:
        /* Most multiplex calls are optional; a quiet, non-fatal no-response is
         * sufficient to let the game continue startup while the remaining DOS
         * compatibility layer is filled in. */
        frame->eax &= 0xffffff00u;
        break;
    }
    set_carry(frame, 0);
}

static void handle_dpmi_interrupt(interrupt_frame *frame)
{
    switch ((uint16_t)frame->eax) {
    case 0x0000:
        set_ax(frame, 0x0053);
        set_carry(frame, 0);
        return;
    case 0x0001:
    case 0x0006:
    case 0x0007:
    case 0x0009:
    case 0x0101:
    case 0x0201:
        set_ax(frame, 0);
        set_carry(frame, 0);
        return;
    case 0x0002:
        set_ax(frame, 0x0008);
        set_carry(frame, 0);
        return;
    case 0x0003:
        frame->ecx = 0;
        frame->edx = 0;
        set_carry(frame, 0);
        return;
    case 0x0200:
        frame->ecx = 0;
        frame->edx = interrupt_vectors[frame->ebx & 0xffu];
        set_carry(frame, 0);
        return;
    case 0x0100: {
        /* Allocate DOS memory block: BX = paragraphs -> AX = real-mode segment,
         * DX = selector.  The segment is a DOS one, the memory the pool's
         * (see MD_LOWMEM_BASE). */
        uint32_t bytes = (frame->ebx & 0xffffu) << 4;
        uint32_t block = (md_lowmem_next + 15u) & ~15u;
        if (bytes == 0 || block + bytes > MD_LOWMEM_BASE + MD_LOWMEM_SIZE) {
            set_ax(frame, 0x0008);
            set_bx(frame, (uint16_t)((MD_LOWMEM_BASE + MD_LOWMEM_SIZE - md_lowmem_next) >> 4));
            set_carry(frame, 1);
            return;
        }
        md_lowmem_next = block + bytes;
        md_dpmi_last_block = lowmem_flat(block);
        memset((void *)(uintptr_t)md_dpmi_last_block, 0, bytes);
        set_ax(frame, (uint16_t)(block >> 4));
        set_dx(frame, (uint16_t)(block >> 4));
        set_carry(frame, 0);
        return;
    }
    case 0x0102:
        set_ax(frame, 0x0008);
        set_carry(frame, 1);
        return;
    case 0x0300: {
        /* Simulate real-mode interrupt: ES:EDI = call structure.  Layout is the
         * DPMI one (EDI 00, ESI 04, EBP 08, rsvd 0C, EBX 10, EDX 14, ECX 18,
         * EAX 1C, flags 20, ES 22, DS 24).  Real-mode far pointers in the block
         * are seg:off pairs; flatten them so the nested handler gets usable
         * addresses (ES:BX is the MSCDEX request header). */
        uint8_t *structure = (uint8_t *)guest_pointer(frame->edi);
        uint16_t vector = frame->ebx & 0xffu;
        interrupt_frame nested;
        memset(&nested, 0, sizeof(nested));
        nested.edi = load_guest32(structure + 0x00);
        nested.esi = load_guest32(structure + 0x04);
        nested.ebp = load_guest32(structure + 0x08);
        nested.ebx = load_guest32(structure + 0x10);
        nested.edx = load_guest32(structure + 0x14);
        nested.ecx = load_guest32(structure + 0x18);
        nested.eax = load_guest32(structure + 0x1c);
        nested.eflags = load_guest16(structure + 0x20);
        md_rm_es = lowmem_flat((uint32_t)load_guest16(structure + 0x22) << 4);
        md_rm_ds = lowmem_flat((uint32_t)load_guest16(structure + 0x24) << 4);
        if (vector == 0x2f) handle_int2f(&nested);
        else if (vector == 0x21) handle_dos_interrupt(&nested);
        else {
            /* EXPERIMENT: nothing executes the real-mode driver behind this
             * vector, so copying the call structure back unchanged hands the
             * game its own input as the driver's answer.  For the Miles/AIL
             * driver (INT 66h) that garbage becomes a pointer and loc_6C8AD
             * faults on `cmp byte [ecx+ebx],0`.  Answer "nothing there"
             * instead, so the game takes its no-device branch. */
            if (vector == 0x66) {
                nested.eax = 0;
                nested.ebx = 0;
                nested.ecx = 0;
                nested.edx = 0;
                nested.esi = 0;
                nested.edi = 0;
                nested.eflags |= 1; /* CF: call failed */
            }
            if (trace_enabled(TRACE_FILE))
                fprintf(stderr, "Mass Destruction: DPMI 0300h vector %02x (no-op)\n", vector);
        }
        md_rm_es = 0;
        md_rm_ds = 0;
        store_guest32(structure + 0x00, nested.edi);
        store_guest32(structure + 0x04, nested.esi);
        store_guest32(structure + 0x08, nested.ebp);
        store_guest32(structure + 0x10, nested.ebx);
        store_guest32(structure + 0x14, nested.edx);
        store_guest32(structure + 0x18, nested.ecx);
        store_guest32(structure + 0x1c, nested.eax);
        store_guest16(structure + 0x20, (uint16_t)nested.eflags);
        set_carry(frame, 0);
        return;
    }
    case 0x0400:
        frame->ecx = 0;
        frame->edx = 0;
        set_carry(frame, 0);
        return;
    case 0x0500: {
        /* Free memory information: 48 bytes at ES:EDI.  The largest free
         * block in bytes, then page counts, -1 where there is nothing to
         * tell (no paging, no separate linear space); the rest is reserved,
         * all ones. */
        uint8_t *structure = (uint8_t *)guest_pointer(frame->edi);
        const uint32_t pages = MD_DPMI_FREE_MEMORY / 4096;
        memset(structure, 0xff, 0x30);
        store_guest32(structure + 0x00, MD_DPMI_FREE_MEMORY);
        store_guest32(structure + 0x04, pages);    /* max unlocked allocation */
        store_guest32(structure + 0x08, pages);    /* max locked allocation */
        store_guest32(structure + 0x10, pages);    /* unlocked pages */
        store_guest32(structure + 0x14, pages);    /* free pages */
        store_guest32(structure + 0x18, pages);    /* physical pages */
        set_carry(frame, 0);
        return;
    }
    case 0x0501:
    case 0x0600:
    case 0x0601:
    case 0x0800:
    case 0x0801:
        set_carry(frame, 0);
        return;
    default:
        fprintf(stderr, "Mass Destruction: unimplemented DPMI function %04x\n", (uint16_t)frame->eax);
        dos_error(frame, 0x8012);
        return;
    }
}

void X86_InterruptProcedure(uint8_t interrupt_number, void *registers)
{
    interrupt_frame *frame = (interrupt_frame *)registers;
    if (trace_enabled(TRACE_REGS) && watch_next) {
        fprintf(stderr, "WATCHNEXT INT %02x AX=%08x BX=%08x CX=%08x DX=%08x DI=%08x SI=%08x BP=%08x\n",
                interrupt_number, frame->eax, frame->ebx, frame->ecx, frame->edx,
                frame->edi, frame->esi, frame->ebp);
        if (--watch_next == 0) trace_backtrace();
    }
    if (trace_enabled(TRACE_INT)) {
        fprintf(stderr, "TRACE INT %02x AX=%04x BX=%04x CX=%04x DX=%04x DI=%08x SI=%08x\n",
                interrupt_number, (uint16_t)frame->eax, (uint16_t)frame->ebx,
                (uint16_t)frame->ecx, (uint16_t)frame->edx, frame->edi, frame->esi);
    }
    if (quit_requested || Thread_Exit) {
        set_carry(frame, 1);
        return;
    }

    switch (interrupt_number) {
    case 0x08:
        call_guest_interrupt_vector(0x08);
        set_carry(frame, 0);
        return;
    case 0x10:
        if (frame_ah(frame) == 0x00) {
            if (trace_enabled(TRACE_LANG)) {
                uintptr_t flag = (uintptr_t)&md_heap_flag;
                uint8_t *file = (uint8_t *)(flag + 0x158u); /* loc_BF49A stdout FILE */
                fprintf(stderr, "LANG@mode13 al=%02x lang=%u FILE:",
                        frame_al(frame), md_debug_lang());
                for (int i = 0; i < 48; i++) fprintf(stderr, " %02x", file[i]);
                fprintf(stderr, "\n");
                /* WATCOM FILE: try common buf/level fields, dump any "Loading" */
                for (int off = 0; off <= 0x40; off += 4) {
                    uint32_t p;
                    memcpy(&p, file + off, 4);
                    if (p < 0x10000u || p > 0x1000000u) continue;
                    uint8_t *b = (uint8_t *)(uintptr_t)p;
                    /* level/count often at +4/+8/+0x10 relative to FILE */
                    for (int cnt_off = 0; cnt_off <= 0x20; cnt_off += 4) {
                        uint32_t cnt;
                        memcpy(&cnt, file + cnt_off, 4);
                        if (cnt == 0 || cnt > 0x1000u) continue;
                        int found = 0;
                        for (uint32_t at = 0; at + 7 <= cnt && !found; at++) found = memcmp(b + at, "Loading", 7) == 0;
                        if (found) {
                            fprintf(stderr, "LANG@buf FILE+%d ptr@FILE+%d cnt=%u: %.*s\n",
                                    off, cnt_off, cnt, (int)(cnt > 80 ? 80 : cnt), b);
                        }
                    }
                }
            }
            MD_RequestPresent();
            set_carry(frame, 0);
            return;
        }
        if (frame_ah(frame) == 0x0f) {
            set_ax(frame, 0x5013);
            frame->ebx &= 0xffffff00u;
            set_carry(frame, 0);
            return;
        }
        break;
    case 0x16:
        if (frame_ah(frame) == 0x00 || frame_ah(frame) == 0x10) {
            uint8_t scan_code;
            if (dequeue_bios_key(&scan_code)) {
                if (trace_enabled(TRACE_KEYS)) fprintf(stderr, "KEY getch %02x\n", scan_code);
                set_ax(frame, (uint16_t)scan_code << 8);
                frame->eflags &= ~MD_ZF;
            } else {
                frame->eflags |= MD_ZF;
            }
            set_carry(frame, 0);
            return;
        }
        if (frame_ah(frame) == 0x01 || frame_ah(frame) == 0x11) {
            frame->eflags = (bios_key_read == bios_key_write) ? frame->eflags | MD_ZF : frame->eflags & ~MD_ZF;
            set_carry(frame, 0);
            return;
        }
        break;
    case 0x21:
        if (frame_ah(frame) == 0x4c) {
            Game_ExitMain_Asm(frame->eax & 0xff);
        }
        handle_dos_interrupt(frame);
        return;
    case 0x31:
        handle_dpmi_interrupt(frame);
        return;
    case 0x2f:
        handle_int2f(frame);
        set_carry(frame, 0);
        return;
    default:
        break;
    }

    fprintf(stderr, "Mass Destruction: unimplemented interrupt %02x (AX=%04x)\n",
            interrupt_number, (uint16_t)frame->eax);
    set_carry(frame, 1);
}

/* The game directory holds TANK.RES, or only TANK.INI: a partial install
 * leaves TANK.RES on the disc (RESFILE_PATH D:\TANK.RES). Before the chdir
 * into data_root, which keeps a relative MD_DATA_DIR valid; once found, kept. */
static int select_data_root(void)
{
    const char *configured_root = getenv("MD_DATA_DIR");
    const char *candidates[] = {
        configured_root,
        "."
    };
    if (data_root[0] != 0) return 0;
    for (unsigned int index = 0; index < sizeof(candidates) / sizeof(candidates[0]); index++) {
        if (candidates[index] == NULL) continue;
        if (dir_has_file(candidates[index], "TANK.RES") || dir_has_file(candidates[index], "TANK.INI")) {
            if (host_full_path(candidates[index], data_root) != 0) {
                data_root[0] = 0;
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

/* Before MD_RuntimeInit chdirs into data_root; main.c calls it first on
 * Windows, for the log. Without game data it is still mdfiles/, so the log
 * can say that the data was not found. */
extern "C" int MD_SelectWriteRoot(void)
{
    const char *configured_write_root = getenv("MD_WRITE_ROOT");
    char start_dir[PATH_MAX];
    int length;
    if (write_root[0] != 0) return 0;
    if (configured_write_root != NULL && *configured_write_root != 0) {
        length = snprintf(write_root, sizeof(write_root), "%s", configured_write_root);
    } else if (select_data_root() == 0) {
        length = snprintf(write_root, sizeof(write_root), "%s", data_root);
    } else if (getcwd(start_dir, sizeof(start_dir)) != NULL) {
        length = snprintf(write_root, sizeof(write_root), "%s/mdfiles", start_dir);
    } else {
        length = -1;
    }
    if (length < 0 || length >= (int)sizeof(write_root) || !host_path_is_absolute(write_root)) {
        write_root[0] = 0;
        return -1;
    }
    return 0;
}

int MD_RuntimeInit(void)
{
    extern file_entry Game_CDir;
    if (select_data_root() != 0) {
        fprintf(stderr, "Mass Destruction: game data not found; set MD_DATA_DIR to MASSDEST\n");
        return -1;
    }
    if (MD_SelectWriteRoot() != 0) {
        fprintf(stderr, "Mass Destruction: no usable write directory; set MD_WRITE_ROOT to an absolute path\n");
        return -1;
    }
    write_root_is_data_root = same_directory(write_root, data_root);
    if (chdir(data_root) != 0) {
        fprintf(stderr, "Mass Destruction: cannot chdir to data root: %s\n", data_root);
        return -1;
    }
    if (vfs_init(0) != 0) {
        fprintf(stderr, "Mass Destruction: virtualfs init failed\n");
        return -1;
    }
    vfs_visit_dir(&Game_CDir);
    /* The VGA window: anywhere below 4 GB, not at 0xA0000, which Windows
     * does not leave free.  Game_CleanState copies Game_FrameBuffer into
     * Game_ScreenWindow again when the game thread starts. */
    framebuffer = (uint8_t *)map_memory_32bit(MD_FRAMEBUFFER_MAP_SIZE, 0);
    if (framebuffer == NULL) return -1;
    memset(framebuffer, 0, MD_FRAMEBUFFER_MAP_SIZE);
    Game_FrameBuffer = framebuffer;
    Game_ScreenWindow = framebuffer;
    /* loc_93CD4, the game's draw target, starts out as 0xA0000 in its data. */
    uint8_t *draw_target = (uint8_t *)MD_Obj2Pointer(0x93CD4u);
    if (load_guest32(draw_target) == MD_A0000) store_guest32(draw_target, (uint32_t)(uintptr_t)framebuffer);
    perf_frequency = SDL_GetPerformanceFrequency();
    pit_restart();
    md_idt_base = (uint32_t)(uintptr_t)md_idt;
    init_guest_code_bounds();
    /* Real DOS always has *some* handler at every vector (the BIOS's own
     * default); seed all 256 with the null ISR so INT 21h AH=35h (GETVECT)
     * never hands the game a 0 it might later chain to. */
    for (unsigned int vector = 0; vector < MD_MAX_VECTORS; vector++) {
        interrupt_vectors[vector] = (uint32_t)(uintptr_t)&c_md_null_isr;
    }

    md_psp = (uint8_t *)map_memory_32bit(MD_PSP_SIZE, 0);
    md_dosmem_base = (uint8_t *)map_memory_32bit(MD_DOSMEM_SIZE, 0);
    /* The DPMI 0100h pool: its segments are DOS ones, its memory anywhere
     * below 4 GB (see MD_LOWMEM_BASE). */
    md_lowmem = (uint8_t *)map_memory_32bit(MD_LOWMEM_SIZE, 0);
    md_lowmem_bias = (uint32_t)(uintptr_t)md_lowmem - MD_LOWMEM_BASE;
    /* No BIOS data area at 0: the translated code reads none of it, and
     * address 0 cannot be mapped (vm.mmap_min_addr, Windows). */
    if (md_psp == NULL || md_dosmem_base == NULL ||
        md_lowmem == NULL) {
        fprintf(stderr, "Mass Destruction: could not reserve guest memory arenas\n");
        return -1;
    }
    md_lowmem_next = MD_LOWMEM_BASE;
    /* 2 = DOS/4GW 2.x, the value c0's extender probe would have stored. */
    md_heap_flag = 2;
    dosmem_init();

    /* Default DTA before any AH=1Ah: a host buffer inside the 32-bit text
     * mapping (-no-pie -Ttext-segment,0x10000000), so guest_pointer works. */
    dta_addr = (uint32_t)(uintptr_t)md_dta_default;
    find_count = 0;
    find_next = 0;
    find_pattern[0] = 0;
    parse_auto_keys();
    /* no audio device only silences the port */
    MD_AudioInit();
    MD_CdInit(data_root);
    MD_MusicInit();

    return 0;
}

void MD_RuntimeDeinit(void)
{
    MD_MusicDeinit();
    MD_CdDeinit();
    MD_AudioDeinit();
    for (int handle = 5; handle < MD_MAX_HANDLES; handle++) {
        if (dos_handles[handle] != NULL) md_fclose(dos_handles[handle]);
    }
    if (framebuffer != NULL) unmap_memory_32bit(framebuffer, MD_FRAMEBUFFER_MAP_SIZE);
    if (md_lowmem != NULL) unmap_memory_32bit(md_lowmem, MD_LOWMEM_SIZE);
    if (md_dosmem_base != NULL) unmap_memory_32bit(md_dosmem_base, MD_DOSMEM_SIZE);
    if (md_psp != NULL) unmap_memory_32bit(md_psp, MD_PSP_SIZE);
    framebuffer = NULL;
    md_dosmem_base = NULL;
    md_psp = NULL;
    md_lowmem = NULL;
}

}
