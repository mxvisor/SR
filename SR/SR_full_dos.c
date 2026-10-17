/**
 *
 *  Copyright (C) 2016-2025 Roman Pauer
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

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "SR_defs.h"
#include "SR_vars.h"

#undef DISPLAY_DISASSEMBLY

void SR_disassemble_add_address(unsigned int Entry, uint32_t address)
{
    output_data *output;
    bound_data *bound;
    uint_fast32_t SecNum, RelAdr;

    if (address >= section[Entry].start &&
        address <= section[Entry].start + section[Entry].size)
    {
        SecNum = Entry;
        RelAdr = address - section[Entry].start;
        section_entry_list_Insert(SecNum, RelAdr);
        bound = section_bound_list_Insert(SecNum, RelAdr);
        if (bound != NULL) bound->begin = 1;
    }
    else
    {
        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            section_entry_list_Insert(SecNum, RelAdr);
            bound = section_bound_list_Insert(SecNum, RelAdr);
            if (bound != NULL) bound->begin = 1;
        }
        else
        {
            SecNum = Entry;
            RelAdr = address - section[Entry].start;
        }
    }

    output = section_output_list_FindEntryEqual(SecNum, RelAdr);
    if (output != NULL)
    {
        output->has_label = 1;
    }
}

void SR_disassemble_align_fixup(const fixup_data *fixup)
{
    output_data *output;

    if (section[fixup->tsec].type == ST_CODE)
    {
        output = section_output_list_FindEntryEqual(fixup->tsec, fixup->tofs);

        if (output != NULL)
        {
            output->align = 4;
        }
    }
}

int SR_disassemble_convert_cjump(char *dst, const char *modifier, uint_fast32_t address, const extrn_data *extrn)
{
    char cbuf[32];
    char *ostr, *str1;
    intptr_t length1;

    ostr = ud_insn_asm(&ud_obj);

    str1 = strchr(ostr, ' ');
    if (str1 == NULL)
    {
        fprintf(stderr, "Error: error converting cjump: %i\n", 2);
        return 1;
    }

    length1 = str1 - ostr;

    // copy first part of the string
    strncpy(dst, ostr, length1);

    // copy second part of the string
    strcpy(&(dst[length1]), modifier);

    if (extrn != NULL)
    {
        // print label to string
        sprintf(&(dst[strlen(dst)]), " %s", extrn->proc);
    }
    else
    {
        // print label to string
        SR_get_label(cbuf, address);

        sprintf(&(dst[strlen(dst)]), " %s", cbuf);
    }

    return 0;
}

int SR_disassemble_convert_fixup(const char *ostr, char *dst, fixup_data *fixup, const extrn_data *extrn, int Entry, uint_fast32_t offset, int decoded_length)
{
    char cbuf[32];
    const char *str1, *str2;
    int *label_value;
    output_data *output;
    uint_fast32_t sec, ofs;
    intptr_t length1;

    if (fixup->type == FT_SEGMENT)
    {
        sprintf(cbuf, "0x%x", 0);
    }
    else if (fixup->type == FT_16BITOFS)
    {
        sprintf(cbuf, "0x%x", (unsigned int)((section[fixup->tsec].start + fixup->tofs) & 0xffff));
    }
    else
    {
        sprintf(cbuf, "0x%x", (unsigned int)(section[fixup->tsec].start + fixup->tofs));
    }

    // find substring (must be exactly one occurence)
    str1 = strstr(ostr, cbuf);
    if (str1 == NULL)
    {
        fprintf(stderr, "Error: error converting fixup: %i - %s - %s (%i: 0x%x)\n", 1, ostr, cbuf, Entry, (unsigned int)offset);

        return 1;
    }
    length1 = str1 - ostr;

    str2 = str1 + strlen(cbuf);

    if ( strstr(str2, cbuf) != NULL )
    {
        if ((decoded_length != 0) && ((fixup->type == FT_NORMAL) || (fixup->type == FT_SEGOFS32)))
        {
            // if two or more occurences in string, then do one's complement on searched data, and try searching for it

            uint8_t *tmpbuf;
            uint32_t *fixupofs;
            fixup_data tmpfixup;
            ud_t tmpud_obj;
            int ret, decoded_length2;

            tmpbuf = (uint8_t *) malloc(decoded_length + 4);
            if (tmpbuf != NULL)
            {
                memcpy(tmpbuf, &(section[Entry].adr[offset]), decoded_length + 4);
                fixupofs = (uint32_t *) &(tmpbuf[fixup->sofs - offset]);
                *fixupofs = (*fixupofs - fixup->tofs) + ~fixup->tofs;
                tmpfixup = *fixup;
                tmpfixup.tofs = ~tmpfixup.tofs;

                ud_init(&tmpud_obj);
                ud_set_mode(&tmpud_obj, 32);
                ud_set_syntax(&tmpud_obj, UD_SYN_INTEL);
                ud_set_input_buffer(&tmpud_obj, tmpbuf, decoded_length + 4);
                ud_set_pc(&tmpud_obj, section[Entry].start + offset);

                decoded_length2 = ud_disassemble(&tmpud_obj);

                if (decoded_length2 != 0)
                {
                    ret = SR_disassemble_convert_fixup(ud_insn_asm(&tmpud_obj), dst, &tmpfixup, extrn, 0, 0, 0);

                    free(tmpbuf);

                    if (ret == 0) return 0;
                }
                else
                {
                    free(tmpbuf);
                }
            }
        }
        fprintf(stderr, "Two string occurences in fixup: 0x%x, %i - 0x%x\n%s\n%s\n", (unsigned int)fixup->sofs, (unsigned int)fixup->tsec, (unsigned int)fixup->tofs, cbuf, ostr);
        return 2;
    }

    if (decoded_length == 0)
    {
        // if searching for one's complement of searched data, return it to original value
        fixup->tofs = ~fixup->tofs;
    }

    // copy first part of the string
    strncpy(dst, ostr, length1);

    // locate label
    label_value = section_label_list_FindEntryEqual(fixup->tsec, fixup->tofs);

    sec = fixup->tsec;
    if (fixup->type == FT_SEGMENT)
    {
        output = NULL;
    }
    else if (label_value != NULL)
    {
        if (SR_get_section_reladr(section[fixup->tsec].start + fixup->tofs + *label_value, &sec, &ofs))
        {
            output = section_output_list_FindEntryEqualOrLower(sec, ofs);
        }
        else output = NULL;
    }
    else
    {
        if ( fixup->tofs < 0 )
        {
            output = section_output_list_FindEntryFirst(fixup->tsec);
        }
        else
        {
            output = section_output_list_FindEntryEqualOrLower(fixup->tsec, fixup->tofs);
        }
    }

    // set label to target address
    if (output != NULL)
    {
        output->has_label = 1;
        ofs = output->ofs;
    }
    else
    {
        sec = fixup->tsec;
        ofs = fixup->tofs;
    }

    // print label to string
    if (extrn != NULL)
    {
        strcpy(cbuf, extrn->proc);
    }
    else if (fixup->type == FT_SEGMENT)
    {
        sprintf(cbuf, "%s", section[fixup->tsec].name);
    }
    else
    {
        SR_get_label(cbuf, section[sec].start + ofs);
    }

    if (((sec == fixup->tsec) && (ofs == fixup->tofs)) ||
        extrn != NULL)
    {
        sprintf(&(dst[length1]), "%s", cbuf);
    }
    else
    {
        sprintf(&(dst[length1]), "(%s + (%i))", cbuf, (int)((section[fixup->tsec].start + fixup->tofs) - (section[sec].start + ofs)));
    }

    // copy second part of the string
    strcat(&(dst[length1]), str2);

    return 0;
}

/* A procedure that never returns, found without a noret_procedures.sci entry:
 * every path from its start ends in "mov ah,4Ch / int 21h" (DOS terminate),
 * or in a call to such a procedure, and no path reaches a ret or leaves the
 * code (an indirect jump, a jump out of the section, undecodable bytes: then
 * the procedure is taken as returning).  The verdicts are remembered. */
#define NORET_MAX_DEPTH 16
#define NORET_MAX_STEPS 4096
#define NORET_AH_UNKNOWN 0x100

typedef struct { uint32_t address; int verdict; } noret_verdict;
static noret_verdict *noret_verdicts;
static size_t noret_verdicts_count, noret_verdicts_size;

static int noret_verdict_find(uint32_t address)
{
    size_t i;
    for (i = 0; i < noret_verdicts_count; i++)
    {
        if (noret_verdicts[i].address == address) return noret_verdicts[i].verdict;
    }
    return -1;
}

static void noret_verdict_add(uint32_t address, int verdict)
{
    if (noret_verdicts_count == noret_verdicts_size)
    {
        size_t size = noret_verdicts_size ? noret_verdicts_size * 2 : 256;
        noret_verdict *grown = (noret_verdict *) realloc(noret_verdicts, size * sizeof(noret_verdict));
        if (grown == NULL) return;
        noret_verdicts = grown;
        noret_verdicts_size = size;
    }
    noret_verdicts[noret_verdicts_count].address = address;
    noret_verdicts[noret_verdicts_count].verdict = verdict;
    noret_verdicts_count++;
}

static int noret_is_conditional_jump(enum ud_mnemonic_code m)
{
    switch (m)
    {
    case UD_Ijcxz: case UD_Ijecxz: case UD_Ijrcxz:
    case UD_Ijo: case UD_Ijno: case UD_Ijb: case UD_Ijnb: case UD_Ijz: case UD_Ijnz:
    case UD_Ijbe: case UD_Ijnbe: case UD_Ijs: case UD_Ijns: case UD_Ijp: case UD_Ijnp:
    case UD_Ijl: case UD_Ijnl: case UD_Ijle: case UD_Ijnle:
    case UD_Ija: case UD_Ijae: case UD_Ijg: case UD_Ijge: case UD_Ijc: case UD_Ijnc:
    case UD_Iloop: case UD_Iloope: case UD_Iloopne:
        return 1;
    default:
        return 0;
    }
}

static int noret_analyse(uint32_t start, int depth)
{
    typedef struct { uint32_t address; int ah; } noret_state;
    noret_state stack[NORET_MAX_STEPS];
    noret_state visited[NORET_MAX_STEPS];
    int sp = 0, nvisited = 0, steps = 0, terminated = 0;
    ud_t ud;

    ud_init(&ud);
    ud_set_mode(&ud, 32);
    ud_set_syntax(&ud, UD_SYN_INTEL);

    stack[sp].address = start;
    stack[sp].ah = NORET_AH_UNKNOWN;
    sp++;

    while (sp > 0)
    {
        noret_state cur = stack[--sp];
        int i, seen = 0;

        for (i = 0; i < nvisited; i++)
        {
            if (visited[i].address == cur.address && visited[i].ah == cur.ah) { seen = 1; break; }
        }
        if (seen) continue;
        if (nvisited == NORET_MAX_STEPS) return 0;
        visited[nvisited++] = cur;

        for (;;)
        {
            uint_fast32_t sec, rel;
            enum ud_mnemonic_code m;
            unsigned int length;

            if (++steps > NORET_MAX_STEPS) return 0;
            if (!SR_get_section_reladr(cur.address, &sec, &rel) || section[sec].type != ST_CODE
                || rel >= section[sec].size)
            {
                return 0;
            }

            ud_set_input_buffer(&ud, &(section[sec].adr[rel]), section[sec].size - rel);
            ud_set_pc(&ud, cur.address);
            length = ud_disassemble(&ud);
            if (length == 0) return 0;
            m = ud.mnemonic;
            cur.address += length;

            if (m == UD_Iret || m == UD_Iretf || m == UD_Iiretw || m == UD_Iiretd || m == UD_Iiretq)
            {
                return 0;
            }
            else if (m == UD_Ijmp || noret_is_conditional_jump(m))
            {
                const struct ud_operand *op = &ud.operand[0];
                uint32_t target;

                if (op->type != UD_OP_JIMM) return 0;
                if (op->size == 8) target = (uint32_t) (cur.address + op->lval.sbyte);
                else if (op->size == 16) target = (uint32_t) (cur.address + op->lval.sword);
                else target = (uint32_t) (cur.address + op->lval.sdword);

                if (sp == NORET_MAX_STEPS) return 0;
                stack[sp].address = target;
                stack[sp].ah = cur.ah;
                sp++;
                if (m == UD_Ijmp) break;
            }
            else if (m == UD_Icall)
            {
                const struct ud_operand *op = &ud.operand[0];
                uint32_t target;

                if (op->type != UD_OP_JIMM || op->size != 32) return 0;
                target = (uint32_t) (cur.address + op->lval.sdword);
                if (depth >= NORET_MAX_DEPTH) return 0;
                if (SR_disassemble_find_noret(target))
                {
                    terminated = 1;
                    break;
                }
                cur.ah = NORET_AH_UNKNOWN;
            }
            else if (m == UD_Iint)
            {
                const struct ud_operand *op = &ud.operand[0];
                if (op->type == UD_OP_IMM && op->lval.ubyte == 0x21 && cur.ah == 0x4c)
                {
                    terminated = 1;
                    break;
                }
                cur.ah = NORET_AH_UNKNOWN;
            }
            else if (m == UD_Imov && ud.operand[0].type == UD_OP_REG && ud.operand[1].type == UD_OP_IMM)
            {
                enum ud_type reg = ud.operand[0].base;
                uint32_t value = ud.operand[1].lval.udword;
                if (reg == UD_R_AH) cur.ah = value & 0xff;
                else if (reg == UD_R_AX) cur.ah = (value >> 8) & 0xff;
                else if (reg == UD_R_EAX) cur.ah = (value >> 8) & 0xff;
            }
            else
            {
                const struct ud_operand *op = &ud.operand[0];
                if ((op->type == UD_OP_REG && (op->base == UD_R_AH || op->base == UD_R_AX || op->base == UD_R_EAX))
                    || m == UD_Icbw || m == UD_Icwde || m == UD_Ilahf || m == UD_Imul || m == UD_Iimul
                    || m == UD_Idiv || m == UD_Iidiv || m == UD_Iin || m == UD_Icpuid || m == UD_Ixchg
                    || m == UD_Ipopa || m == UD_Ipopad || m == UD_Ipop)
                {
                    cur.ah = NORET_AH_UNKNOWN;
                }
            }
        }
    }

    return terminated;
}

int SR_disassemble_find_noret(uint_fast32_t address)
{
    uint_fast32_t SecNum, RelAdr;
    static int depth;
    int verdict;

    if (SR_get_section_reladr(address, &SecNum, &RelAdr))
    {
        if (section_noret_list_CanFindEntryEqual(SecNum, RelAdr))
        {
            return 1;
        }

        if (section[SecNum].type != ST_CODE) return 0;

        verdict = noret_verdict_find((uint32_t) address);
        if (verdict < 0)
        {
            /* a call back into a procedure being analysed counts as returning */
            noret_verdict_add((uint32_t) address, 0);
            depth++;
            verdict = noret_analyse((uint32_t) address, depth);
            depth--;
            if (verdict)
            {
                size_t i;
                for (i = 0; i < noret_verdicts_count; i++)
                {
                    if (noret_verdicts[i].address == (uint32_t) address) noret_verdicts[i].verdict = 1;
                }
            }
        }
        return verdict;
    }

    return 0;
}

int SR_disassemble_offset_dos(unsigned int Entry, uint_fast32_t offset)
{
    output_data *output;
    fixup_data *fixup, *fixup2;
    extrn_data *extrn, *extrn2;
    int finished, decoded_length, ret;
    char cLabel[32];
    char cResult[128];
    char cResPart[128];

    ud_set_input_buffer(&ud_obj, &(section[Entry].adr[offset]), section[Entry].size - offset);
    ud_set_pc(&ud_obj, section[Entry].start + offset);

    finished = 0;

    while (!finished)
    {
        output = section_output_list_FindEntryEqual(Entry, offset);

        if (output == NULL)
        {
            fprintf(stderr, "Error: output not found - %i - 0x%x\n", Entry, (unsigned int)offset);

            return 1;
        }

        if (output->type > 0)
        {
            finished = 1;
            break;
        }

        decoded_length = ud_disassemble(&ud_obj);

        if (decoded_length == 0)
        {
            finished = 2;
            break;
        }

#ifdef DISPLAY_DISASSEMBLY
        printf("loc_%X: %s\n", (unsigned int)(section[Entry].start + offset), ud_insn_asm(&ud_obj));
#endif


        fixup2 = NULL;
        fixup = section_fixup_list_FindEntryEqualOrHigher(Entry, offset);

        if (fixup != NULL)
        {
            if (fixup->sofs >= offset + decoded_length) fixup = NULL;
            else
            {
                int fixuplen, fixuplen2;

                fixuplen = (fixup->type == FT_NORMAL || fixup->type == FT_SELFREL)?4:2;

                if (fixup->sofs > offset + decoded_length - fixuplen)
                {
                    fprintf(stderr, "Error: decoding fixup mismatch - %i - %i\n", Entry, (unsigned int)offset);

                    return 2;
                }

                if (fixup->sofs != offset + decoded_length - fixuplen)
                {
                    fixup2 = section_fixup_list_FindEntryHigher(Entry, fixup->sofs);

                    if (fixup2 != NULL)
                    {
                        if (fixup2->sofs >= offset + decoded_length) fixup2 = NULL;
                        else
                        {
                            fixuplen2 = (fixup2->type == FT_NORMAL || fixup2->type == FT_SELFREL)?4:2;

                            if (fixup2->sofs > offset + decoded_length - fixuplen2)
                            {
                                fprintf(stderr, "Error: decoding fixup mismatch - %i - %i\n", Entry, (unsigned int)offset);

                                return 2;
                            }
                        }
                    }

                }
            }
        }

        output = SR_disassemble_offset_init_output(Entry, offset, decoded_length);

        switch (ud_obj.mnemonic)
        {
            case UD_Icall:
                if (fixup == NULL &&
                    ud_obj.operand[0].type == UD_OP_JIMM &&
                    ud_obj.operand[0].size == 32
                   )
                {
                    uint_fast32_t address;

                    address = ud_obj.operand[0].lval.sdword + (uint_fast32_t) ud_obj.pc;

                    SR_disassemble_add_address(Entry, address);

                    if ( SR_disassemble_find_noret(address) )
                    {
                        finished = 1;
                    }

                    SR_get_label(cLabel, address);
                    sprintf(cResult, "call %s", cLabel);

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else if (fixup != NULL &&
                         ud_obj.operand[0].type == UD_OP_MEM)
                {
                    SR_disassemble_align_fixup(fixup);

                    extrn = section_extrn_list_FindEntryEqual(fixup->tsec, fixup->tofs);

                    ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResult, fixup, extrn, Entry, offset, decoded_length);

                    if (ret) return ret;

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else if (fixup == NULL &&
                         (ud_obj.operand[0].type == UD_OP_MEM || ud_obj.operand[0].type == UD_OP_REG))
                {
                    output->str = strdup(ud_insn_asm(&ud_obj));
                }
                else
                {
                    printf("fixup: %i\n", (int)(intptr_t)fixup);
                    printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                    printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);

                    return -10;
                }

                break;

            case UD_Iiretw:
            case UD_Iiretd:
            case UD_Iiretq:
            case UD_Iret:
            case UD_Iretf:

                output->str = strdup(ud_insn_asm(&ud_obj));

                finished = 1;

                break;

            case UD_Ijcxz:
            case UD_Ijecxz:
            case UD_Ijrcxz:
            case UD_Ijo:
            case UD_Ijno:
            case UD_Ijb:
            case UD_Ijnb:
            case UD_Ijz:
            case UD_Ijnz:
            case UD_Ijbe:
            case UD_Ijnbe:
            case UD_Ijs:
            case UD_Ijns:
            case UD_Ijp:
            case UD_Ijnp:
            case UD_Ijl:
            case UD_Ijnl:
            case UD_Ijle:
            case UD_Ijnle:
            case UD_Ija:
            case UD_Ijae:
            case UD_Ijg:
            case UD_Ijge:
            case UD_Ijc:
            case UD_Ijnc:
                if (fixup == NULL &&
                    ud_obj.operand[0].type == UD_OP_JIMM &&
                    (ud_obj.operand[0].size == 32 || ud_obj.operand[0].size == 8)
                   )
                {
                    uint_fast32_t address;
                    int jumptype;

                    if (ud_obj.operand[0].size == 32)
                    {
                        address = ud_obj.operand[0].lval.sdword + (uint_fast32_t) ud_obj.pc;
                        jumptype = 0;
                    }
                    else
                    {
                        address = ud_obj.operand[0].lval.sbyte + (uint_fast32_t) ud_obj.pc;
                        jumptype = ((ud_obj.mnemonic == UD_Ijcxz) || (ud_obj.mnemonic == UD_Ijecxz) || (ud_obj.mnemonic == UD_Ijrcxz))?2:1;
                    }

                    SR_disassemble_add_address(Entry, address);

                    ret = SR_disassemble_convert_cjump(cResult, ((jumptype == 0)?" near":((jumptype == 1)?" short":"")), address, NULL);

                    if (ret) return ret;

                    output->str = strdup(cResult);

#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else
                {
                    printf("fixup: %i\n", (int)(intptr_t)fixup);
                    printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                    printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);
                    return -20;
                }

                break;

            case UD_Ijmp:
                if (fixup == NULL &&
                    ud_obj.operand[0].type == UD_OP_JIMM &&
                    (ud_obj.operand[0].size == 32 || ud_obj.operand[0].size == 8)
                   )
                {
                    uint_fast32_t address;

                    if (ud_obj.operand[0].size == 32)
                    {
                        address = ud_obj.operand[0].lval.sdword + (uint_fast32_t) ud_obj.pc;
                    }
                    else
                    {
                        address = ud_obj.operand[0].lval.sbyte + (uint_fast32_t) ud_obj.pc;
                    }

                    SR_disassemble_add_address(Entry, address);

                    SR_get_label(cLabel, address);
                    sprintf(cResult, "jmp %s%s", ((ud_obj.operand[0].size == 32)?"":"short "), cLabel);

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else if (fixup != NULL &&
                         ud_obj.operand[0].type == UD_OP_MEM)
                {
                    SR_disassemble_align_fixup(fixup);

                    extrn = section_extrn_list_FindEntryEqual(fixup->tsec, fixup->tofs);

                    ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResult, fixup, extrn, Entry, offset, decoded_length);

                    if (ret) return ret;

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else if (fixup == NULL &&
                         (ud_obj.operand[0].type == UD_OP_MEM || ud_obj.operand[0].type == UD_OP_REG))
                {
                    output->str = strdup(ud_insn_asm(&ud_obj));
                }
                else if (fixup != NULL &&
                         fixup2 != NULL &&
                         fixup->type == FT_NORMAL &&
                         fixup2->type == FT_SEGMENT &&
                         ud_obj.operand[0].type == UD_OP_PTR)
                {
                    extrn = section_extrn_list_FindEntryEqual(fixup->tsec, fixup->tofs);

                    ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResult, fixup, extrn, Entry, offset, decoded_length);

                    if (ret) return ret;

                    {
                        int index;

                        cResult[4] = 'f';
                        cResult[5] = 'a';
                        cResult[6] = 'r';
                        cResult[7] = ' ';

                        index = 8;
                        while ((cResult[index] != ':') && (cResult[index] != 0))
                        {
                            cResult[index] = ' ';
                            index++;
                        }
                        if (cResult[index] == ':') cResult[index] = ' ';
                    }

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else if (fixup != NULL &&
                         fixup2 != NULL &&
                         fixup->type == FT_16BITOFS &&
                         fixup2->type == FT_SEGMENT &&
                         ud_obj.operand[0].type == UD_OP_PTR)
                {
                    extrn = section_extrn_list_FindEntryEqual(fixup->tsec, fixup->tofs);

                    ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResult, fixup, extrn, Entry, offset, decoded_length);

                    if (ret) return ret;

                    {
                        int index;

                        cResult[0] = 'j';
                        cResult[1] = 'm';
                        cResult[2] = 'p';
                        cResult[3] = ' ';
                        cResult[4] = 'f';
                        cResult[5] = 'a';
                        cResult[6] = 'r';
                        cResult[7] = ' ';

                        index = 13;
                        while ((cResult[index] != ':') && (cResult[index] != 0))
                        {
                            cResult[index] = ' ';
                            index++;
                        }
                        if (cResult[index] == ':') cResult[index] = ' ';
                    }

                    output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else
                {
                    printf("fixup: %i\n", (int)(intptr_t)fixup);
                    printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                    printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);

                    return -30;
                }

                finished = 1;

                break;

            case UD_Iloop:
            case UD_Iloope:
            case UD_Iloopn:
            case UD_Iloopne:
            case UD_Iloopnz:
            case UD_Iloopz:
                if (fixup == NULL &&
                    ud_obj.operand[0].type == UD_OP_JIMM &&
                    ud_obj.operand[0].size == 8
                   )
                {
                    uint_fast32_t address;

                    address = ud_obj.operand[0].lval.sbyte + (uint_fast32_t) ud_obj.pc;

                    SR_disassemble_add_address(Entry, address);

                    ret = SR_disassemble_convert_cjump(cResult, "", address, NULL);

                    if (ret) return ret;

                    output->str = strdup(cResult);

#ifdef DISPLAY_DISASSEMBLY
                    printf("\t%s\n", output->str);
#endif
                }
                else
                {
                    printf("fixup: %i\n", (int)(intptr_t)fixup);
                    printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                    printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);
                    return -40;
                }
                break;

            case UD_Iinvalid:
                printf("decoded length: %i (%i - 0x%x)\n", decoded_length, Entry, (unsigned int)offset);
                return 3;
                break;

            default:

                if (fixup != NULL)
                {
                    if (fixup->type == FT_SELFREL)
                    {
                        return -50;
                    }
                    else
                    {
                        int fixuplen8;

                        fixuplen8 = (fixup->type == FT_NORMAL || fixup->type == FT_SELFREL)?32:16;

                        if (((ud_obj.operand[0].type == UD_OP_IMM && ud_obj.operand[0].size == fixuplen8) || ud_obj.operand[0].type == UD_OP_MEM) ||
                            ((ud_obj.operand[1].type == UD_OP_IMM && ud_obj.operand[1].size == fixuplen8) || ud_obj.operand[1].type == UD_OP_MEM))
                        {
                            extrn = section_extrn_list_FindEntryEqual(fixup->tsec, fixup->tofs);
                            if (fixup2 != NULL)
                            {
                                extrn2 = section_extrn_list_FindEntryEqual(fixup2->tsec, fixup2->tofs);

                                ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResPart, fixup, extrn, Entry, offset, decoded_length);

                                if (ret) return ret;

                                ret = SR_disassemble_convert_fixup(cResPart, cResult, fixup2, extrn2, Entry, offset, decoded_length);

                                if (ret) return ret;
                            }
                            else
                            {
                                ret = SR_disassemble_convert_fixup(ud_insn_asm(&ud_obj), cResult, fixup, extrn, Entry, offset, decoded_length);

                                if (ret) return ret;
                            }


                            output->str = strdup(cResult);
#ifdef DISPLAY_DISASSEMBLY
                            printf("\t%s\n", output->str);
#endif
                        }
                        else
                        {
                            printf("fixup: %i - 0x%x - 0x%x\n", (unsigned int)fixup->tsec, (unsigned int)fixup->tofs, (unsigned int)(section[fixup->tsec].start + fixup->tofs));
                            printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                            printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);
                            return -51;
                        }

                    }
                }
                else
                {
                    if (ud_obj.mnemonic == UD_Ifld &&
                        ud_obj.operand[0].type == UD_OP_REG &&
                        ud_obj.operand[1].type == UD_OP_REG &&
                        ud_obj.operand[0].base == UD_R_ST0 &&
                        ud_obj.operand[1].base >= UD_R_ST0 &&
                        ud_obj.operand[1].base <= UD_R_ST7)
                    {
                        output->str = strdup("fld st0");
                        output->str[6] = '0' + ud_obj.operand[1].base - UD_R_ST0;
#ifdef DISPLAY_DISASSEMBLY
                        printf("\t%s\n", output->str);
#endif
                    }
                    else
                    if (ud_obj.mnemonic == UD_Ilsl &&
                        ud_obj.operand[0].type == UD_OP_REG &&
                        ud_obj.operand[1].type == UD_OP_REG &&
                        ud_obj.operand[0].base == UD_R_EAX &&
                        ud_obj.operand[1].base == UD_R_AX)
                    {
                        output->str = strdup("lsl eax, eax");
#ifdef DISPLAY_DISASSEMBLY
                        printf("\t%s\n", output->str);
#endif
                    }
                    else
                    if (ud_obj.mnemonic == UD_Ifldlpi)
                    {
                        output->str = strdup("fldpi");
#ifdef DISPLAY_DISASSEMBLY
                        printf("\t%s\n", output->str);
#endif
                    }
                    else
                    {
                        output->str = strdup(ud_insn_asm(&ud_obj));
/*						if (ud_obj.mnemonic == UD_Imov && offset == 0xc25)
                        {
                            printf("\t%s\n", output->str);
                            printf("op type: %i - %i - %i\n", ud_obj.operand[0].type, ud_obj.operand[1].type, ud_obj.operand[2].type);
                            printf("op size: %i - %i - %i\n", ud_obj.operand[0].size, ud_obj.operand[1].size, ud_obj.operand[2].size);
                        }*/

                    }
                }

                break;
        }


        offset += decoded_length;
    }

    return (finished == 2)?4:0;
}

