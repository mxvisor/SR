/*
 *  MMX for the llasm backend: every MMX instruction is a call of one of the helpers below.
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
 *  The registers mm0..mm7 are cpu->_mm[]. The operations are numbered by MMX_OPS; the
 *  same list gives the MMX_<NAME> constants of llasm_mmx.llinc (tools/gen_llasm_mmx.py
 *  writes them), which SR names in the code it emits. Memory operands come as host
 *  pointers (a "*" parameter of the llasm func). The state of the x87 stack is not
 *  touched: a program does not mix the two between emms instructions.
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "llasm_cpu.h"

/* the operations of x86_mmx_rr, _rm and _ri */
#define MMX_OPS(X) \
    X(PADDB) X(PADDW) X(PADDD) X(PADDQ) \
    X(PSUBB) X(PSUBW) X(PSUBD) X(PSUBQ) \
    X(PADDSB) X(PADDSW) X(PADDUSB) X(PADDUSW) \
    X(PSUBSB) X(PSUBSW) X(PSUBUSB) X(PSUBUSW) \
    X(PMULLW) X(PMULHW) X(PMULHUW) X(PMADDWD) X(PMULUDQ) \
    X(PCMPEQB) X(PCMPEQW) X(PCMPEQD) X(PCMPGTB) X(PCMPGTW) X(PCMPGTD) \
    X(PAND) X(PANDN) X(POR) X(PXOR) \
    X(PACKSSWB) X(PACKSSDW) X(PACKUSWB) \
    X(PUNPCKLBW) X(PUNPCKLWD) X(PUNPCKLDQ) X(PUNPCKHBW) X(PUNPCKHWD) X(PUNPCKHDQ) \
    X(PSLLW) X(PSLLD) X(PSLLQ) X(PSRLW) X(PSRLD) X(PSRLQ) X(PSRAW) X(PSRAD) \
    X(PAVGB) X(PAVGW) X(PMAXUB) X(PMAXSW) X(PMINUB) X(PMINSW) X(PSADBW) \
    X(MOVQ) \
    X(PFADD) X(PFSUB) X(PFMUL) X(PFMAX) X(PFMIN) \
    X(PFRCP) X(PFRSQRT) X(PF2ID) X(PI2FD) \
    X(PFACC) X(PFCMPGE) X(PFCMPGT) X(PFRCPIT1) X(PFRCPIT2)

enum {
#define X(name) MMX_##name,
    MMX_OPS(X)
#undef X
    MMX_OP_COUNT
};

#define LANE8(v, i)   ((uint8_t)((v) >> (8 * (i))))
#define LANE16(v, i)  ((uint16_t)((v) >> (16 * (i))))
#define LANE32(v, i)  ((uint32_t)((v) >> (32 * (i))))
#define S8(v, i)      ((int8_t)LANE8(v, i))
#define S16(v, i)     ((int16_t)LANE16(v, i))
#define S32(v, i)     ((int32_t)LANE32(v, i))

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* the lanes of the result are made by f(a lane of d, a lane of s) */
#define MAP8(f)  do { uint64_t r_ = 0; int i_; for (i_ = 0; i_ < 8; i_++) r_ |= (uint64_t)(uint8_t)(f) << (8 * i_); return r_; } while (0)
#define MAP16(f) do { uint64_t r_ = 0; int i_; for (i_ = 0; i_ < 4; i_++) r_ |= (uint64_t)(uint16_t)(f) << (16 * i_); return r_; } while (0)
#define MAP32(f) do { uint64_t r_ = 0; int i_; for (i_ = 0; i_ < 2; i_++) r_ |= (uint64_t)(uint32_t)(f) << (32 * i_); return r_; } while (0)

static uint64_t f32_pair(float lo, float hi)
{
    uint32_t a, b;

    memcpy(&a, &lo, 4);
    memcpy(&b, &hi, 4);
    return (uint64_t)a | ((uint64_t)b << 32);
}

static float f32_lane(uint64_t v, int i)
{
    uint32_t x = (uint32_t)(v >> (32 * i));
    float f;

    memcpy(&f, &x, 4);
    return f;
}

#define PF_BINARY(f) f32_pair((f)(f32_lane(d, 0), f32_lane(s, 0)), (f)(f32_lane(d, 1), f32_lane(s, 1)))
static float pf_add(float a, float b) { return a + b; }
static float pf_sub(float a, float b) { return a - b; }
static float pf_mul(float a, float b) { return a * b; }
static float pf_max(float a, float b) { return (a > b) ? a : b; }
static float pf_min(float a, float b) { return (a < b) ? a : b; }

static uint64_t mmx_op(uint32_t op, uint64_t d, uint64_t s)
{
    uint64_t r;

    switch (op)
    {
    case MMX_PADDB:   MAP8(LANE8(d, i_) + LANE8(s, i_));
    case MMX_PADDW:   MAP16(LANE16(d, i_) + LANE16(s, i_));
    case MMX_PADDD:   MAP32(LANE32(d, i_) + LANE32(s, i_));
    case MMX_PADDQ:   return d + s;
    case MMX_PSUBB:   MAP8(LANE8(d, i_) - LANE8(s, i_));
    case MMX_PSUBW:   MAP16(LANE16(d, i_) - LANE16(s, i_));
    case MMX_PSUBD:   MAP32(LANE32(d, i_) - LANE32(s, i_));
    case MMX_PSUBQ:   return d - s;
    case MMX_PADDSB:  MAP8(clamp(S8(d, i_) + S8(s, i_), -128, 127));
    case MMX_PADDSW:  MAP16(clamp(S16(d, i_) + S16(s, i_), -32768, 32767));
    case MMX_PADDUSB: MAP8(clamp(LANE8(d, i_) + LANE8(s, i_), 0, 255));
    case MMX_PADDUSW: MAP16(clamp(LANE16(d, i_) + LANE16(s, i_), 0, 65535));
    case MMX_PSUBSB:  MAP8(clamp(S8(d, i_) - S8(s, i_), -128, 127));
    case MMX_PSUBSW:  MAP16(clamp(S16(d, i_) - S16(s, i_), -32768, 32767));
    case MMX_PSUBUSB: MAP8(clamp(LANE8(d, i_) - LANE8(s, i_), 0, 255));
    case MMX_PSUBUSW: MAP16(clamp(LANE16(d, i_) - LANE16(s, i_), 0, 65535));
    case MMX_PMULLW:  MAP16((uint32_t)(S16(d, i_) * S16(s, i_)));
    case MMX_PMULHW:  MAP16((uint32_t)(S16(d, i_) * S16(s, i_)) >> 16);
    case MMX_PMULHUW: MAP16((LANE16(d, i_) * (uint32_t)LANE16(s, i_)) >> 16);
    case MMX_PMADDWD: MAP32((uint32_t)(S16(d, 2 * i_) * S16(s, 2 * i_) + S16(d, 2 * i_ + 1) * S16(s, 2 * i_ + 1)));
    case MMX_PMULUDQ: return (uint64_t)LANE32(d, 0) * LANE32(s, 0);
    case MMX_PCMPEQB: MAP8(LANE8(d, i_) == LANE8(s, i_) ? 0xff : 0);
    case MMX_PCMPEQW: MAP16(LANE16(d, i_) == LANE16(s, i_) ? 0xffff : 0);
    case MMX_PCMPEQD: MAP32(LANE32(d, i_) == LANE32(s, i_) ? 0xffffffffu : 0);
    case MMX_PCMPGTB: MAP8(S8(d, i_) > S8(s, i_) ? 0xff : 0);
    case MMX_PCMPGTW: MAP16(S16(d, i_) > S16(s, i_) ? 0xffff : 0);
    case MMX_PCMPGTD: MAP32(S32(d, i_) > S32(s, i_) ? 0xffffffffu : 0);
    case MMX_PAND:    return d & s;
    case MMX_PANDN:   return ~d & s;
    case MMX_POR:     return d | s;
    case MMX_PXOR:    return d ^ s;
    case MMX_PACKSSWB:
        r = 0;
        { int i; for (i = 0; i < 4; i++) {
            r |= (uint64_t)(uint8_t)clamp(S16(d, i), -128, 127) << (8 * i);
            r |= (uint64_t)(uint8_t)clamp(S16(s, i), -128, 127) << (8 * (i + 4)); } }
        return r;
    case MMX_PACKSSDW:
        r = 0;
        { int i; for (i = 0; i < 2; i++) {
            r |= (uint64_t)(uint16_t)clamp(S32(d, i), -32768, 32767) << (16 * i);
            r |= (uint64_t)(uint16_t)clamp(S32(s, i), -32768, 32767) << (16 * (i + 2)); } }
        return r;
    case MMX_PACKUSWB:
        r = 0;
        { int i; for (i = 0; i < 4; i++) {
            r |= (uint64_t)(uint8_t)clamp(S16(d, i), 0, 255) << (8 * i);
            r |= (uint64_t)(uint8_t)clamp(S16(s, i), 0, 255) << (8 * (i + 4)); } }
        return r;
    case MMX_PUNPCKLBW: MAP8((i_ & 1) ? LANE8(s, i_ >> 1) : LANE8(d, i_ >> 1));
    case MMX_PUNPCKHBW: MAP8((i_ & 1) ? LANE8(s, 4 + (i_ >> 1)) : LANE8(d, 4 + (i_ >> 1)));
    case MMX_PUNPCKLWD: MAP16((i_ & 1) ? LANE16(s, i_ >> 1) : LANE16(d, i_ >> 1));
    case MMX_PUNPCKHWD: MAP16((i_ & 1) ? LANE16(s, 2 + (i_ >> 1)) : LANE16(d, 2 + (i_ >> 1)));
    case MMX_PUNPCKLDQ: return (uint64_t)LANE32(d, 0) | ((uint64_t)LANE32(s, 0) << 32);
    case MMX_PUNPCKHDQ: return (uint64_t)LANE32(d, 1) | ((uint64_t)LANE32(s, 1) << 32);
    /* the count is the whole source, a count over the lane width clears (or sign fills) */
    case MMX_PSLLW:   if (s > 15) return 0; MAP16(LANE16(d, i_) << s);
    case MMX_PSLLD:   if (s > 31) return 0; MAP32(LANE32(d, i_) << s);
    case MMX_PSLLQ:   return (s > 63) ? 0 : d << s;
    case MMX_PSRLW:   if (s > 15) return 0; MAP16(LANE16(d, i_) >> s);
    case MMX_PSRLD:   if (s > 31) return 0; MAP32(LANE32(d, i_) >> s);
    case MMX_PSRLQ:   return (s > 63) ? 0 : d >> s;
    case MMX_PSRAW:   { unsigned n = (s > 15) ? 15 : (unsigned)s; MAP16(S16(d, i_) >> n); }
    case MMX_PSRAD:   { unsigned n = (s > 31) ? 31 : (unsigned)s; MAP32(S32(d, i_) >> n); }
    case MMX_PAVGB:   MAP8((LANE8(d, i_) + LANE8(s, i_) + 1) >> 1);
    case MMX_PAVGW:   MAP16((LANE16(d, i_) + LANE16(s, i_) + 1) >> 1);
    case MMX_PMAXUB:  MAP8(LANE8(d, i_) > LANE8(s, i_) ? LANE8(d, i_) : LANE8(s, i_));
    case MMX_PMAXSW:  MAP16(S16(d, i_) > S16(s, i_) ? S16(d, i_) : S16(s, i_));
    case MMX_PMINUB:  MAP8(LANE8(d, i_) < LANE8(s, i_) ? LANE8(d, i_) : LANE8(s, i_));
    case MMX_PMINSW:  MAP16(S16(d, i_) < S16(s, i_) ? S16(d, i_) : S16(s, i_));
    case MMX_PSADBW:
        {
            uint32_t sum = 0;
            int i;

            for (i = 0; i < 8; i++)
            {
                int diff = (int)LANE8(d, i) - (int)LANE8(s, i);
                sum += (uint32_t)((diff < 0) ? -diff : diff);
            }
            return sum;
        }
    case MMX_MOVQ:    return s;
    case MMX_PFADD:   return PF_BINARY(pf_add);
    case MMX_PFSUB:   return PF_BINARY(pf_sub);
    case MMX_PFMUL:   return PF_BINARY(pf_mul);
    case MMX_PFMAX:   return PF_BINARY(pf_max);
    case MMX_PFMIN:   return PF_BINARY(pf_min);
    case MMX_PFRCP:   return f32_pair(1.0f / f32_lane(s, 0), 1.0f / f32_lane(s, 1));
    case MMX_PFRSQRT: return f32_pair(1.0f / sqrtf(f32_lane(s, 0)), 1.0f / sqrtf(f32_lane(s, 1)));
    case MMX_PF2ID:   return (uint64_t)(uint32_t)(int32_t)f32_lane(s, 0) | ((uint64_t)(uint32_t)(int32_t)f32_lane(s, 1) << 32);
    case MMX_PI2FD:   return f32_pair((float)S32(s, 0), (float)S32(s, 1));
    case MMX_PFACC:   return f32_pair(f32_lane(d, 0) + f32_lane(d, 1), f32_lane(s, 0) + f32_lane(s, 1));
    case MMX_PFCMPGE: return (uint64_t)((f32_lane(d, 0) >= f32_lane(s, 0)) ? 0xffffffffu : 0) |
                             ((uint64_t)((f32_lane(d, 1) >= f32_lane(s, 1)) ? 0xffffffffu : 0) << 32);
    case MMX_PFCMPGT: return (uint64_t)((f32_lane(d, 0) > f32_lane(s, 0)) ? 0xffffffffu : 0) |
                             ((uint64_t)((f32_lane(d, 1) > f32_lane(s, 1)) ? 0xffffffffu : 0) << 32);
    case MMX_PFRCPIT1:
    case MMX_PFRCPIT2: return f32_pair(f32_lane(d, 0) * (2.0f - f32_lane(s, 0) * f32_lane(d, 0)),
                                       f32_lane(d, 1) * (2.0f - f32_lane(s, 1) * f32_lane(d, 1)));
    }

    return d;
}

static uint64_t load64(const uint8_t *p)
{
    uint64_t v;

    memcpy(&v, p, 8);
    return v;
}

/* op mm_dst, mm_src */
EXTERNC void CCALL x86_mmx_rr(CPU, uint32_t op, uint32_t dst, uint32_t src)
{
    cpu->_mm[dst & 7] = mmx_op(op, cpu->_mm[dst & 7], cpu->_mm[src & 7]);
}

/* op mm_dst, qword [mem] */
EXTERNC void CCALL x86_mmx_rm(CPU, uint32_t op, uint32_t dst, const uint8_t *src)
{
    cpu->_mm[dst & 7] = mmx_op(op, cpu->_mm[dst & 7], load64(src));
}

/* op mm_dst, imm8 (the shifts) */
EXTERNC void CCALL x86_mmx_ri(CPU, uint32_t op, uint32_t dst, uint32_t imm)
{
    cpu->_mm[dst & 7] = mmx_op(op, cpu->_mm[dst & 7], imm & 0xff);
}

/* movd mm, r32 */
EXTERNC void CCALL x86_mmx_movd_in(CPU, uint32_t dst, uint32_t value)
{
    cpu->_mm[dst & 7] = value;
}

/* movd mm, dword [mem] */
EXTERNC void CCALL x86_mmx_movd_load(CPU, uint32_t dst, const uint8_t *src)
{
    uint32_t value;

    memcpy(&value, src, 4);
    cpu->_mm[dst & 7] = value;
}

/* movd r32, mm: the low dword is the result */
EXTERNC uint32_t CCALL x86_mmx_movd_out(CPU, uint32_t src)
{
    return (uint32_t)cpu->_mm[src & 7];
}

/* movd dword [mem], mm */
EXTERNC void CCALL x86_mmx_movd_store(CPU, uint32_t src, uint8_t *dst)
{
    uint32_t value = (uint32_t)cpu->_mm[src & 7];

    memcpy(dst, &value, 4);
}

/* movq qword [mem], mm (also movntq) */
EXTERNC void CCALL x86_mmx_movq_store(CPU, uint32_t src, uint8_t *dst)
{
    memcpy(dst, &cpu->_mm[src & 7], 8);
}

/* pshufw mm_dst, mm_src or qword [mem], imm8: the helper takes the source as a value */
static uint64_t pshufw(uint64_t s, uint32_t imm)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 4; i++)
    {
        r |= (uint64_t)LANE16(s, (imm >> (2 * i)) & 3) << (16 * i);
    }
    return r;
}

EXTERNC void CCALL x86_mmx_pshufw_rr(CPU, uint32_t dst, uint32_t src, uint32_t imm)
{
    cpu->_mm[dst & 7] = pshufw(cpu->_mm[src & 7], imm);
}

EXTERNC void CCALL x86_mmx_pshufw_rm(CPU, uint32_t dst, const uint8_t *src, uint32_t imm)
{
    cpu->_mm[dst & 7] = pshufw(load64(src), imm);
}

/* pextrw r32, mm, imm8 */
EXTERNC uint32_t CCALL x86_mmx_pextrw(CPU, uint32_t src, uint32_t imm)
{
    return LANE16(cpu->_mm[src & 7], imm & 3);
}

/* pinsrw mm, r32 (or the word loaded from memory), imm8 */
EXTERNC void CCALL x86_mmx_pinsrw(CPU, uint32_t dst, uint32_t value, uint32_t imm)
{
    unsigned shift = 16 * (imm & 3);

    cpu->_mm[dst & 7] = (cpu->_mm[dst & 7] & ~((uint64_t)0xffff << shift)) | ((uint64_t)(value & 0xffff) << shift);
}

/* pmovmskb r32, mm */
EXTERNC uint32_t CCALL x86_mmx_pmovmskb(CPU, uint32_t src)
{
    uint32_t mask = 0;
    int i;

    for (i = 0; i < 8; i++)
    {
        mask |= (uint32_t)(LANE8(cpu->_mm[src & 7], i) >> 7) << i;
    }
    return mask;
}

/* maskmovq mm_data, mm_mask: the bytes with the high bit set go to [edi] */
EXTERNC void CCALL x86_mmx_maskmovq(CPU, uint32_t data, uint32_t mask, uint8_t *dst)
{
    int i;

    for (i = 0; i < 8; i++)
    {
        if (LANE8(cpu->_mm[mask & 7], i) & 0x80)
        {
            dst[i] = LANE8(cpu->_mm[data & 7], i);
        }
    }
}
