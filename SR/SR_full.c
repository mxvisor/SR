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
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "SR_defs.h"
#include "SR_vars.h"
#include "udis86_dep.h"

#undef DISPLAY_DISASSEMBLY

typedef struct _Entry_region_ {
    unsigned int Entry;
    uint_fast32_t begin_ofs;
    int isregion;
} Entry_region;

output_data *SR_disassemble_offset_init_output(unsigned int SecNum, uint_fast32_t offset, unsigned int length)
{
    output_data *output;
    unsigned int index;

    if (offset + length == section[SecNum].size)
    {
        output = section_output_list_FindEntryEqual(SecNum, offset + length);
        if (output != NULL)
        {
            if (NULL == section_alias_list_FindEntryEqual(SecNum, offset + length))
            {
                output->has_label = 0;
                output->align = 0;
            }
        }
    }

    for (index = length - 1; index != 0; index--)
    {
        section_output_list_Delete(SecNum, offset + index);
        section_bound_list_Delete(SecNum, offset + index);
    }

    output = section_output_list_FindEntryEqual(SecNum, offset);
    if (output != NULL)
    {
        if (output->str != NULL)
        {
            free(output->str);
            output->str = NULL;
        }
        output->len = length - 1;
        output->type = OT_INSTRUCTION;

        if (section[SecNum].type != ST_CODE)
        {
            output->type = OT_UNKNOWN;
            if (length & 3)
            {
                fprintf(stderr, "Warning: data replacement causes misalignement - %i - %i (0x%x)\n", SecNum, (unsigned int)offset, (unsigned int)(section[SecNum].start + offset));
            }
        }
    }

    return output;
}

/* ************************************************************************** */

#if (OUTPUT_TYPE != OUT_ORIG && OUTPUT_TYPE != OUT_DOS)
static void SR_disassemble_replace_instructions(replace_data *item, void *data)
{
    output_data *output;
    bound_data *bound;

#define DATA ((unsigned int)(uintptr_t) data)

    output = SR_disassemble_offset_init_output(DATA, item->ofs, item->length);

    if (output != NULL)
    {
        output->str = strdup(item->instr);

        bound = section_bound_list_Insert(DATA, item->ofs);
        if (bound != NULL) bound->begin = 1;
    }

    if (section[DATA].type == ST_CODE)
    {
        section_entry_list_Insert(DATA, item->ofs + item->length);
        bound = section_bound_list_Insert(DATA, item->ofs + item->length);
        if (bound != NULL) bound->begin = 1;
    }

#undef DATA
}
#endif

#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
static void SR_remove_nops(unsigned int Entry, uint_fast32_t begin_ofs, uint_fast32_t end_ofs)
{
    output_data *first;

    if (begin_ofs == 0)
    {
        first = section_output_list_FindEntryFirst(Entry);
        begin_ofs = (first != NULL)?first->ofs:end_ofs;
    }

    for (; begin_ofs < end_ofs; begin_ofs++)
    {
        section_output_list_Delete(Entry, begin_ofs);
    }
}

static void SR_disassemble_create_aligns_code(output_data *item, void *data)
{
#define DATA ((Entry_region *) data)

    switch (DATA->isregion)
    {
    case 0: // data
        if (item->type == OT_INSTRUCTION)
        {
            if (item->align == 0)
            {
                // first instruction after data gets aligned
                item->align = 4;
            }
            DATA->isregion = 1;
        }
        break;
    case 1: // instructions
        if (item->type != OT_INSTRUCTION)
        {
            if (item->type == OT_OFFSET && item->has_label && (item->align == 0 || item->align == 4))
            {
                // jump table after instructions
                DATA->isregion = 3;
            }
            else if (item->type == OT_UNKNOWN && item->len == 0 && item->str == NULL && item->has_label == 0 && item->align == 0 && (section[DATA->Entry].adr[item->ofs] == 0x90 || section[DATA->Entry].adr[item->ofs] == 0xCC))
            {
                DATA->isregion = 2;
                DATA->begin_ofs = item->ofs;
            }
            else
            {
                DATA->isregion = 0;
            }
        }
        break;
    case 2: // nops after instructions
        if (item->type == OT_INSTRUCTION)
        {
            if (item->has_label && item->align == 0 && (item->ofs & 0x03) == 0)
            {
                item->align = 4;

                // remove nops before alignment
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }
            else if (item->align == 0)
            {
                // first instruction after data gets aligned
                item->align = 4;
            }

            DATA->isregion = 1;
        }
        else if (item->type == OT_OFFSET)
        {
            if (item->has_label && (item->align == 0 || item->align == 4))
            {
                // jump table after instructions
                item->align = 4;

                // remove nops before alignment
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }

            DATA->isregion = 3;
        }
        else if (item->type == OT_NONE)
        {
            output_data *last;

            last = section_output_list_FindEntryLast(DATA->Entry);
            if (last != NULL && last->ofs == item->ofs)
            {
                // remove nops before end
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }

            DATA->isregion = 0;
        }
        else if (item->type != OT_UNKNOWN || item->len || item->str || item->has_label || item->align || (section[DATA->Entry].adr[item->ofs] != 0x90 && section[DATA->Entry].adr[item->ofs] != 0xCC))
        {
            DATA->isregion = 0;
        }
        break;
    case 3: // jump table
        if (item->type != OT_OFFSET)
        {
            if (item->type == OT_INSTRUCTION)
            {
                if (item->align == 0)
                {
                    // first instruction after data gets aligned
                    item->align = 4;
                }
                DATA->isregion = 1;
            }
            else if (item->type == OT_UNKNOWN && item->len == 0 && item->str == NULL && item->has_label == 0 && item->align == 0 && (section[DATA->Entry].adr[item->ofs] == 0x90 || section[DATA->Entry].adr[item->ofs] == 0xCC))
            {
                DATA->isregion = 2;
                DATA->begin_ofs = item->ofs;
            }
            else
            {
                DATA->isregion = 0;
            }
        }
        break;
    }

#undef DATA
}

static void SR_disassemble_create_aligns_data(output_data *item, void *data)
{
#define DATA ((Entry_region *) data)

    if (item->type == OT_INSTRUCTION)
    {
        DATA->isregion = 0;
    }
    else
    {
        if (DATA->isregion == 0)
        {
            if (item->align)
            {
                DATA->isregion = -1;
            }
            else
            {
                if (item->has_label)
                {
                    if (item->type != OT_NONE)
                    {
                        item->align = 4;
                        DATA->isregion = -1;
                    }
                }
                else
                {
                    if (item->type != OT_UNKNOWN)
                    {
                        DATA->isregion = -1;
                    }
                }
            }
        }
    }

#undef DATA
}
#endif

#if ((OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
static void SR_disassemble_create_regions(bound_data *item, void *data)
{
    output_data *output;


#define DATA ((Entry_region *) data)

    if (DATA->isregion && item->begin)
    {
        output = section_output_list_FindEntryLower(DATA->Entry, item->ofs);

        if (output != NULL)
        {
            section_region_list_Insert(DATA->Entry, DATA->begin_ofs, output->ofs);
        }
        DATA->isregion = 0;
    }

    if (item->begin)
    {
        if (item->end)
        {
            section_region_list_Insert(DATA->Entry, item->ofs, item->ofs);
        }
        else
        {
            DATA->isregion = 1;
            DATA->begin_ofs = item->ofs;
        }
    }
    else
    {
        if (DATA->isregion)
        {
            section_region_list_Insert(DATA->Entry, DATA->begin_ofs, item->ofs);

            DATA->isregion = 0;
        }
        else
        {
            section_region_list_Insert(DATA->Entry, item->ofs, item->ofs);
        }
    }


#undef DATA
}
#endif

/* ************************************************************************** */

#if (OUTPUT_TYPE == OUT_LLASM)
/* ************************************************************************** */

// Flags that a region reads before it writes them are written by the regions
// that lead to it (a jump to a conditional jump, a loop condition after a
// 'test'): the flags are asked from the predecessors of the region, until
// every region knows what it must leave behind.

typedef struct {
    uint_fast32_t target;       // index of the region that is entered
    uint_fast32_t source;       // index of the region that is left
} Flow_edge;

static int SR_flow_edge_compare(const void *a, const void *b)
{
    const Flow_edge *ea = (const Flow_edge *) a;
    const Flow_edge *eb = (const Flow_edge *) b;

    if (ea->target != eb->target) return (ea->target < eb->target) ? -1 : 1;
    if (ea->source != eb->source) return (ea->source < eb->source) ? -1 : 1;
    return 0;
}

static int SR_region_find(region_data **regions, uint_fast32_t count, uint_fast32_t begin_ofs, uint_fast32_t *index)
{
    uint_fast32_t low = 0, high = count;

    while (low < high)
    {
        uint_fast32_t mid = (low + high) / 2;

        if (regions[mid]->begin_ofs == begin_ofs)
        {
            *index = mid;
            return 1;
        }
        if (regions[mid]->begin_ofs < begin_ofs) low = mid + 1; else high = mid;
    }

    return 0;
}

static int SR_flow_edges_add(Flow_edge **edges, uint_fast32_t *count, uint_fast32_t *capacity, uint_fast32_t target, uint_fast32_t source)
{
    if (*count == *capacity)
    {
        Flow_edge *newedges;

        *capacity = (*capacity) ? (*capacity) * 2 : 1024;
        newedges = (Flow_edge *) realloc(*edges, (*capacity) * sizeof(Flow_edge));
        if (newedges == NULL) return 0;
        *edges = newedges;
    }

    (*edges)[*count].target = target;
    (*edges)[*count].source = source;
    (*count)++;

    return 1;
}

static int SR_llasm_propagate_flags(unsigned int Entry)
{
    region_data *region, **regions;
    output_data *output;
    Flow_edge *edges;
    uint_fast32_t count, capacity, edge_count, edge_capacity, i, k, index, target_ofs;
    uint_fast32_t *queue, queue_head, queue_tail;
    unsigned char *queued;
    int ret, falls;

    ret = 0;
    regions = NULL;
    edges = NULL;
    queue = NULL;
    queued = NULL;
    count = 0;
    capacity = 0;
    edge_count = 0;
    edge_capacity = 0;

    for (region = section_region_list_FindEntryFirst(Entry); region != NULL; region = section_region_list_FindEntryHigher(Entry, region->begin_ofs))
    {
        if (count == capacity)
        {
            region_data **newregions;

            capacity = capacity ? capacity * 2 : 1024;
            newregions = (region_data **) realloc(regions, capacity * sizeof(region_data *));
            if (newregions == NULL) { ret = 3; goto done; }
            regions = newregions;
        }
        regions[count++] = region;
    }

    if (count == 0) return 0;

    // edges between the regions
    for (i = 0; i < count; i++)
    {
        int decoded;

        output = section_output_list_FindEntryEqual(Entry, regions[i]->end_ofs);
        if (output == NULL || output->type != OT_INSTRUCTION) continue;

        ud_set_input_buffer(&ud_obj, &(section[Entry].adr[output->ofs]), output->len + 1);
        ud_set_pc(&ud_obj, section[Entry].start + output->ofs);
        decoded = ud_disassemble(&ud_obj);
        if (decoded != (int)(output->len + 1)) continue;

        falls = 1;
        target_ofs = (uint_fast32_t) -1;

        switch (ud_obj.mnemonic)
        {
            case UD_Ijmp:
                falls = 0;
                // fallthrough
            case UD_Ijo: case UD_Ijno: case UD_Ijb: case UD_Ijnb: case UD_Ijz: case UD_Ijnz:
            case UD_Ijbe: case UD_Ijnbe: case UD_Ijs: case UD_Ijns: case UD_Ijp: case UD_Ijnp:
            case UD_Ijl: case UD_Ijnl: case UD_Ijle: case UD_Ijnle: case UD_Ija: case UD_Ijae:
            case UD_Ijg: case UD_Ijge: case UD_Ijc: case UD_Ijnc:
            case UD_Ijcxz: case UD_Ijecxz: case UD_Ijrcxz:
            case UD_Iloop: case UD_Iloope: case UD_Iloopne: case UD_Iloopnz: case UD_Iloopz:
                if (ud_obj.operand[0].type == UD_OP_JIMM)
                {
                    uint_fast32_t address;

                    if (ud_obj.operand[0].size == 32) address = ud_obj.operand[0].lval.sdword + (uint_fast32_t) ud_obj.pc;
                    else if (ud_obj.operand[0].size == 8) address = ud_obj.operand[0].lval.sbyte + (uint_fast32_t) ud_obj.pc;
                    else break;

                    target_ofs = address - section[Entry].start;
                }
                break;
            case UD_Iret:
            case UD_Iretf:
            case UD_Iiretd:
            case UD_Iiretw:
            case UD_Iiretq:
                falls = 0;
                break;
            default:
                break;
        }

        if (target_ofs != (uint_fast32_t) -1 && SR_region_find(regions, count, target_ofs, &index))
        {
            if (!SR_flow_edges_add(&edges, &edge_count, &edge_capacity, index, i)) { ret = 3; goto done; }
        }

        if (falls && i + 1 < count && regions[i + 1]->begin_ofs == output->ofs + output->len + 1)
        {
            if (!SR_flow_edges_add(&edges, &edge_count, &edge_capacity, i + 1, i)) { ret = 3; goto done; }
        }
    }

    if (edge_count == 0) goto done;

    qsort(edges, edge_count, sizeof(Flow_edge), SR_flow_edge_compare);

    queue = (uint_fast32_t *) malloc(count * sizeof(uint_fast32_t) * 2);
    queued = (unsigned char *) calloc(count, 1);
    if (queue == NULL || queued == NULL) { ret = 3; goto done; }

    // analyse every region once, ask the predecessors for what is missing
    SR_llasm_dry_run = 1;
    queue_head = 0;
    queue_tail = 0;
    for (i = 0; i < count; i++)
    {
        queue[queue_tail++] = i;
        queued[i] = 1;
    }

    while (queue_head != queue_tail)
    {
        uint_fast32_t demand;

        i = queue[queue_head];
        queue_head = (queue_head + 1) % (count * 2);
        queued[i] = 0;

        // a replaced region is not translated
        if (section_replace_list_FindEntryEqual(Entry, regions[i]->begin_ofs) != NULL) continue;

        SR_llasm_region_leftover = 0;
        if (SR_disassemble_region_llasm(Entry, regions[i]) != 0) continue;

        // only the real flags are passed on
        demand = (SR_llasm_region_leftover & FL_WEAK) ? 0 : (SR_llasm_region_leftover & FL_ALL);
        if (demand == 0) continue;

        // edges of the region i as a target
        {
            Flow_edge key;
            uint_fast32_t low = 0, high = edge_count, first;

            key.target = i;
            key.source = 0;
            while (low < high)
            {
                uint_fast32_t mid = (low + high) / 2;
                if (SR_flow_edge_compare(&edges[mid], &key) < 0) low = mid + 1; else high = mid;
            }
            first = low;

            for (k = first; k < edge_count && edges[k].target == i; k++)
            {
                region_data *pred = regions[edges[k].source];

                regions[i]->flags_accept |= demand;

                if ((pred->flags_to_write | demand) != pred->flags_to_write)
                {
                    pred->flags_to_write |= demand;

                    if (!queued[edges[k].source])
                    {
                        queued[edges[k].source] = 1;
                        queue[queue_tail] = edges[k].source;
                        queue_tail = (queue_tail + 1) % (count * 2);
                    }
                }
            }
        }
    }
    SR_llasm_dry_run = 0;

done:
    SR_llasm_dry_run = 0;
    free(regions);
    free(edges);
    free(queue);
    free(queued);

    return ret;
}
#endif

#if (OUTPUT_TYPE == OUT_LLASM)
/* The handlers a program gives to the system.  SR follows code only from the
 * entry points and the calls it can see, so a routine whose address the
 * program hands to INT 21h AH=25h (an interrupt vector) is reached by no
 * one, and the fixup that holds its address is listed as invalid.  The
 * program is analysed after the first pass, and the routines found are what
 * fixup_interpret_as_code.sci would list by hand (--list_handlers=FILE; the
 * SDK's SR run feeds them to a second one):
 *  - INT 21h with AH=25h, EDX loaded with an address fixup in the same block;
 *  - the same INT 21h in a routine that takes EDX from its stack argument
 *    ("mov edx,[ebp+N]" after "push ebp; mov ebp,esp"): then every call of the
 *    routine passes the handler in that argument, either as a fixup itself
 *    or through a register loaded with one. */
typedef struct _Handler_insn_ {
    uint_fast32_t ofs;
    uint_fast32_t target;       // the call's target
    enum ud_mnemonic_code mnemonic;
    int reg;                    // the register family the instruction writes, -1 if none
    int pushreg;                // the register family a push takes, -1 if not a register push
    int ah;                     // AH an instruction sets from an immediate, -1 if not
    int imm_fixup;              // the instruction holds a fixup to code that is not code yet
    uint_fast32_t fixup_address;
    int stack_arg;              // "mov r,[ebp+N]": N, -1 if not
    int ebp_frame;              // "mov ebp,esp"
    int ah_write;               // the instruction writes AH (AX, EAX or AH itself)
    int low_add;                // "add ax,x" / "add al,x": the vector number is added to AL, AH is kept
    int intno;
    int branch;                 // leaves the straight line: jmp, call, ret (a conditional jump does not)
} Handler_insn;

typedef struct _Handler_collect_ {
    unsigned int Entry;
    Handler_insn *insn;
    size_t count, capacity;
} Handler_collect;

static int handler_reg_family(enum ud_type reg)
{
    if (reg >= UD_R_AL && reg <= UD_R_BH) return (int)(reg - UD_R_AL) & 3;
    if (reg >= UD_R_AX && reg <= UD_R_DI) return (int)(reg - UD_R_AX);
    if (reg >= UD_R_EAX && reg <= UD_R_EDI) return (int)(reg - UD_R_EAX);
    return -1;
}

static void handler_collect_insn(output_data *output, void *data)
{
    Handler_collect *collect = (Handler_collect *) data;
    Handler_insn *insn;
    ud_t u;
    fixup_data *fixup;
    const struct ud_operand *op0, *op1;
    const unsigned int Entry = collect->Entry;

    if (output->type != 2) return;
    if (collect->count == collect->capacity)
    {
        collect->capacity = collect->capacity ? collect->capacity * 2 : 4096;
        collect->insn = (Handler_insn *) realloc(collect->insn, collect->capacity * sizeof(Handler_insn));
        if (collect->insn == NULL) { collect->count = collect->capacity = 0; return; }
    }
    insn = &collect->insn[collect->count++];
    memset(insn, 0, sizeof(*insn));
    insn->reg = insn->pushreg = insn->ah = insn->stack_arg = insn->intno = -1;

    ud_init(&u);
    ud_set_mode(&u, 32);
    ud_set_input_buffer(&u, &(section[Entry].adr[output->ofs]), section[Entry].size - output->ofs);
    ud_set_pc(&u, section[Entry].start + output->ofs);
    if (ud_disassemble(&u) == 0) { collect->count--; return; }

    insn->ofs = output->ofs;
    insn->mnemonic = u.mnemonic;
    op0 = &u.operand[0];
    op1 = &u.operand[1];

    switch (u.mnemonic)
    {
        case UD_Ijmp: case UD_Iret: case UD_Iretf: case UD_Iiretd: case UD_Iiretw: case UD_Icall:
            insn->branch = 1;
            break;
        default:
            break;
    }
    if (u.mnemonic == UD_Icall && op0->type == UD_OP_JIMM && op0->size == 32)
    {
        insn->target = op0->lval.sdword + (uint_fast32_t) u.pc;
    }
    if (u.mnemonic == UD_Iint) insn->intno = op0->lval.ubyte;
    if (u.mnemonic == UD_Ipush && op0->type == UD_OP_REG) insn->pushreg = handler_reg_family(op0->base);

    if (u.mnemonic != UD_Ipush && u.mnemonic != UD_Icmp && u.mnemonic != UD_Itest && op0->type == UD_OP_REG)
    {
        insn->reg = handler_reg_family(op0->base);
    }
    if (u.mnemonic == UD_Ipop && op0->type == UD_OP_REG) insn->reg = handler_reg_family(op0->base);

    if (insn->reg == 0 && op0->base != UD_R_AL) insn->ah_write = 1;

    if (u.mnemonic == UD_Iadd && op0->type == UD_OP_REG && (op0->base == UD_R_AX || op0->base == UD_R_AL)
        && op1->type != UD_OP_IMM)
    {
        insn->low_add = 1;
    }

    if (u.mnemonic == UD_Imov && op0->type == UD_OP_REG)
    {
        if (op0->base == UD_R_AH && op1->type == UD_OP_IMM) insn->ah = op1->lval.ubyte;
        else if (op0->base == UD_R_AX && op1->type == UD_OP_IMM) insn->ah = op1->lval.uword >> 8;
        else if (op0->base == UD_R_EAX && op1->type == UD_OP_IMM) insn->ah = (op1->lval.udword >> 8) & 0xff;
        if (u.mnemonic == UD_Imov && op1->type == UD_OP_MEM && op1->base == UD_R_EBP && op1->index == UD_NONE
            && op1->offset == 8 && op1->lval.sbyte >= 8)
        {
            insn->stack_arg = op1->lval.sbyte;
        }
        if (op0->base == UD_R_EBP && op1->type == UD_OP_REG && op1->base == UD_R_ESP) insn->ebp_frame = 1;
    }

    // a fixup in the instruction that points to code nobody has disassembled
    fixup = section_fixup_list_FindEntryEqualOrHigher(Entry, output->ofs);
    if (fixup != NULL && fixup->sofs <= output->ofs + output->len
        && fixup->type == FT_NORMAL && fixup->tsec < num_sections && section[fixup->tsec].type == ST_CODE
        && ((u.mnemonic == UD_Imov && op1->type == UD_OP_IMM) || (u.mnemonic == UD_Ipush && op0->type == UD_OP_IMM)
            || (u.mnemonic == UD_Ilea && op1->type == UD_OP_MEM && op1->base == UD_NONE && op1->index == UD_NONE)))
    {
        output_data *target = section_output_list_FindEntryEqual(fixup->tsec, fixup->tofs);
        if (target == NULL || target->type != 2)
        {
            insn->imm_fixup = 1;
            insn->fixup_address = section[fixup->tsec].start + fixup->tofs;
        }
    }
}

typedef struct _Handler_wrapper_ {
    uint_fast32_t address;
    int argument;
} Handler_wrapper;

static void handler_report(FILE *out, uint_fast32_t address, uint_fast32_t *seen, size_t *seen_count)
{
    char label[32];
    size_t i;

    for (i = 0; i < *seen_count; i++) if (seen[i] == address) return;
    seen[(*seen_count)++] = address;
    SR_get_label(label, address);
    fprintf(out, "%s\n", label);
}

static int handler_find_writer(const Handler_insn *insn, long from, int reg)
{
    long j;

    for (j = from; j >= 0 && j > from - 32; j--)
    {
        if (reg == 0)
        {
            // AH: the writers of AL alone and the vector number added to AL leave it
            if (insn[j].ah_write && !insn[j].low_add) return (int) j;
        }
        else if (insn[j].reg == reg) return (int) j;
        if (insn[j].branch) break;
    }
    return -1;
}

static void handler_scan_section(uint_fast32_t start, const Handler_insn *insn, size_t count, FILE *out, uint_fast32_t *seen, size_t *seen_count,
                                 Handler_wrapper *wrappers, size_t *wrapper_count)
{
    size_t i;

    // INT 21h AH=25h: EDX is the handler
    for (i = 0; i < count; i++)
    {
        int w;

        if (insn[i].intno != 0x21) continue;
        w = handler_find_writer(insn, (long) i - 1, 0);
        if (w < 0 || insn[w].ah != 0x25) continue;
        w = handler_find_writer(insn, (long) i - 1, 2);
        if (w < 0) continue;
        if (insn[w].imm_fixup)
        {
            handler_report(out, insn[w].fixup_address, seen, seen_count);
        }
        else if (insn[w].stack_arg >= 8 && *wrapper_count < 64)
        {
            long j;

            for (j = w; j > 0 && j > (long) w - 64; j--)
            {
                if (insn[j].ebp_frame && insn[j - 1].mnemonic == UD_Ipush)
                {
                    wrappers[*wrapper_count].address = start + insn[j - 1].ofs;
                    wrappers[*wrapper_count].argument = (insn[w].stack_arg - 8) / 4;
                    (*wrapper_count)++;
                    break;
                }
                if (insn[j].mnemonic == UD_Iret) break;
            }
        }
    }
}

static void handler_scan_calls(const Handler_insn *insn, size_t count, const Handler_wrapper *wrappers, size_t wrapper_count,
                               FILE *out, uint_fast32_t *seen, size_t *seen_count)
{
    size_t i, wi;

    for (i = 0; i < count; i++)
    {
        if (insn[i].mnemonic != UD_Icall || insn[i].target == 0) continue;
        for (wi = 0; wi < wrapper_count; wi++)
        {
            long j;
            int pushes = 0;

            if (wrappers[wi].address != insn[i].target) continue;
            for (j = (long) i - 1; j >= 0 && j > (long) i - 48; j--)
            {
                if (insn[j].branch) break;
                if (insn[j].mnemonic != UD_Ipush) continue;
                if (pushes++ != wrappers[wi].argument) continue;
                if (insn[j].imm_fixup)
                {
                    handler_report(out, insn[j].fixup_address, seen, seen_count);
                }
                else if (insn[j].pushreg >= 0)
                {
                    int w = handler_find_writer(insn, j - 1, insn[j].pushreg);
                    if (w >= 0 && insn[w].imm_fixup) handler_report(out, insn[w].fixup_address, seen, seen_count);
                }
                break;
            }
        }
    }
}

static int SR_list_handlers(const char *name)
{
    FILE *out;
    unsigned int index;
    Handler_collect *collects;
    Handler_wrapper wrappers[64];
    size_t wrapper_count = 0, seen_count = 0;
    uint_fast32_t seen[1024];

    out = fopen(name, "wt");
    if (out == NULL)
    {
        fprintf(stderr, "Error: cannot create %s\n", name);
        return 1;
    }
    collects = (Handler_collect *) calloc(num_sections, sizeof(Handler_collect));
    if (collects == NULL) { fclose(out); return 1; }

    for (index = 0; index < num_sections; index++)
    {
        if (section[index].type != ST_CODE) continue;
        collects[index].Entry = index;
        section_output_list_ForEach(index, &handler_collect_insn, &collects[index]);
    }
    // the wrappers are found in every section before the calls of any are looked at
    for (index = 0; index < num_sections; index++)
    {
        if (section[index].type != ST_CODE) continue;
        handler_scan_section(section[index].start, collects[index].insn, collects[index].count, out, seen, &seen_count, wrappers, &wrapper_count);
    }
    for (index = 0; index < num_sections; index++)
    {
        if (section[index].type != ST_CODE) continue;
        handler_scan_calls(collects[index].insn, collects[index].count, wrappers, wrapper_count, out, seen, &seen_count);
        free(collects[index].insn);
    }
    free(collects);
    fclose(out);
    return 0;
}
#endif


#if (OUTPUT_TYPE != OUT_ORIG)
/* An address of fixup_interpret_as_code.sci that an instruction reads or
 * writes as memory ("mov ax,[ebx+table]"; lea, which only takes the address,
 * aside) is a table, not code: SR translates it as a procedure, as the list
 * says, and the program's copy of the table then holds other bytes.  Only a
 * warning: SR's output stays as the list asks. */
static void SR_check_code_fixup_as_memory(output_data *output, void *data)
{
    const unsigned int Entry = (unsigned int)(uintptr_t) data;
    fixup_data *fixup;
    ud_t u;
    unsigned int op;
    uint_fast32_t SecNum, RelAdr;
    char label[32], where[32];

    if (output->type != 2) return;
    // only an instruction with a fixup: a displacement that is an address
    fixup = section_fixup_list_FindEntryEqualOrHigher(Entry, output->ofs);
    if (fixup == NULL || fixup->sofs >= output->ofs + output->len) return;

    ud_init(&u);
    ud_set_mode(&u, 32);
    ud_set_input_buffer(&u, &(section[Entry].adr[output->ofs]), section[Entry].size - output->ofs);
    ud_set_pc(&u, section[Entry].start + output->ofs);
    if (ud_disassemble(&u) == 0 || u.mnemonic == UD_Ilea) return;

    for (op = 0; op < 3; op++)
    {
        if (u.operand[op].type != UD_OP_MEM || u.operand[op].offset != 32) continue;
        if (!SR_get_section_reladr(u.operand[op].lval.udword, &SecNum, &RelAdr)) continue;
        if (section[SecNum].type != ST_CODE || !section_code_list_CanFindEntryEqual(SecNum, RelAdr)) continue;

        SR_get_label(label, u.operand[op].lval.udword);
        SR_get_label(where, section[Entry].start + output->ofs);
        fprintf(stderr, "Warning: %s is in fixup_interpret_as_code.sci, but %s reads or writes it as memory: a table, not code?\n", label, where);
    }
}
#endif

int SR_full_disassembly(void)
{
    unsigned int index;
    int finished, ret;
#if (OUTPUT_TYPE != OUT_ORIG)
    uint_fast32_t offset;
#endif
#if (OUTPUT_TYPE != OUT_ORIG && OUTPUT_TYPE != OUT_DOS)
    replace_data *replace;
#endif
#if ((OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    output_data *output;
    region_data *region;
#endif
#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    Entry_region ER;
#endif
    Word_t entry_index;


#if (OUTPUT_TYPE == OUT_ORIG)
    ret = 0;
#endif


#if (OUTPUT_TYPE != OUT_ORIG && OUTPUT_TYPE != OUT_DOS)
    replace = section_replace_list_FindEntryEqualOrLower(ESPObjectNum, ESP - 1);
    if ((replace == NULL) || (replace->ofs + replace->length < ESP))
    {
        if (section_alias_list_FindEntryEqual(ESPObjectNum, ESP) == NULL)
        {
            section_alias_list_Insert(ESPObjectNum, ESP, "stack_start");
        }
    }

    for (index = 0; index < num_sections; index++)
    {
        section_replace_list_ForEach(index, &SR_disassemble_replace_instructions, (void *)(uintptr_t) index);
    }
#endif

#if ((OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM))
    ret = init_udis86_dep();

    if (ret) return ret;
#endif

    finished = 0;

    while (!finished)
    {
        finished = 1;

        for (index = 0; index < num_sections; index++)
        {
            // process only code segments
            if (section[index].type != ST_CODE) continue;

            while (section_entry_list_FindFirstIndex(index, &entry_index))
            {
                finished = 0;

#if (OUTPUT_TYPE != OUT_ORIG)
                offset = (uint_fast32_t)entry_index;
#endif
                section_entry_list_Delete(index, entry_index);

#if (OUTPUT_TYPE == OUT_DOS)
                ret = SR_disassemble_offset_dos(index, offset);
#endif
#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
                ret = SR_disassemble_offset_win32(index, offset);
#endif

                if (ret) return ret;
            }
        }
    }

#if (OUTPUT_TYPE == OUT_LLASM)
    if (list_handlers)
    {
        ret = SR_list_handlers(handlers_name);
        if (ret) return ret;
    }
#endif

#if (OUTPUT_TYPE != OUT_ORIG)
    for (index = 0; index < num_sections; index++)
    {
        if (section[index].type != ST_CODE) continue;
        section_output_list_ForEach(index, &SR_check_code_fixup_as_memory, (void *)(uintptr_t) index);
    }
#endif

#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    // align code and data in code segments
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

        ER.Entry = index;
        ER.begin_ofs = 0;
        ER.isregion = 2;

        section_output_list_ForEach(index, &SR_disassemble_create_aligns_code, (void *) &ER);

        ER.Entry = index;
        ER.isregion = -1;

        section_output_list_ForEach(index, &SR_disassemble_create_aligns_data, (void *) &ER);
    }
#endif

#if ((OUTPUT_TYPE == OUT_ARM_LINUX) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    // create regions from bounds
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

        ER.Entry = index;
        ER.isregion = 0;

        section_bound_list_ForEach(index, &SR_disassemble_create_regions, (void *) &ER);
        if (ER.isregion)
        {
            output = section_output_list_FindEntryLast(index);

            if (output != NULL)
            {
                section_region_list_Insert(index, ER.begin_ofs, output->ofs);
            }
        }
    }


#if (OUTPUT_TYPE == OUT_LLASM)
    // flags that regions read before they write them
    for (index = 0; index < num_sections; index++)
    {
        if (section[index].type != ST_CODE) continue;

        ret = SR_llasm_propagate_flags(index);
        if (ret) return ret;
    }
#endif

    // process regions
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

        region = section_region_list_FindEntryFirst(index);
        //region = section_region_list_FindEntryLast(index);

        while (region != NULL)
        {
            replace = section_replace_list_FindEntryEqual(index, region->begin_ofs);

            if (replace == NULL)
            {
#if (OUTPUT_TYPE == OUT_ARM_LINUX)
                ret = SR_disassemble_region_arm(index, region);
#elif (OUTPUT_TYPE == OUT_LLASM)
                ret = SR_disassemble_region_llasm(index, region);
#elif (OUTPUT_TYPE == OUT_X64)
                ret = SR_disassemble_region_x64(index, region);
#endif

                if (ret) return ret;
            }

            region = section_region_list_FindEntryHigher(index, region->begin_ofs);
            /*if (region->begin_ofs != 0)
            {
                region = section_region_list_FindEntryLower(index, region->begin_ofs);
            }
            else
            {
                region = NULL;
            }*/
        }
    }
#endif

    return 0;
}
