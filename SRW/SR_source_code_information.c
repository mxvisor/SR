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
#include <string.h>
#include "SR_defs.h"
#include <stdlib.h>
#include "SR_vars.h"

static int SR_LoadSCI_fixup_interpret_as_code(FILE *file)
{
    char buf[8192];
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X", &address);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            section_code_list_Insert(SecNum, RelAdr);
        }
    }

    return 0;
}

/* code_sections.sci: names of initialised data sections that hold machine code */
static int SR_LoadSCI_code_sections(FILE *file)
{
    char buf[8192];
    size_t length;
    int items;
    unsigned int index;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        while (length != 0 && (buf[length - 1] == '\r' || buf[length - 1] == ' '))
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        for (index = 0; index < num_sections; index++)
        {
            if (strcmp(section[index].name, buf) == 0)
            {
                section[index].type = ST_CODE;
            }
        }
    }

    return 0;
}

static int SR_LoadSCI_displaced_labels(FILE *file)
{
    char buf[8192];
    int *label_value;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, displacement;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X,%i", &address, &displacement);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            label_value = section_label_list_Insert(SecNum, RelAdr);

            if (label_value == NULL) return 1;

            *label_value = displacement;
        }
    }

    return 0;
}

static int SR_LoadSCI_noret_procedures(FILE *file)
{
    char buf[8192];
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X", &address);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            section_noret_list_Insert(SecNum, RelAdr);
        }
    }

    return 0;
}

static int SR_LoadSCI_code16_areas(FILE *file)
{
    char buf[8192];
    int *code16_area_value;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, arealength;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X,%i", &address, &arealength);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            code16_area_value = section_code16_list_Insert(SecNum, RelAdr);

            if (code16_area_value == NULL) return 1;

            *code16_area_value = arealength;
        }
    }

    return 0;
}

static int SR_LoadSCI_fixup_do_not_interpret_as_code(FILE *file)
{
    char buf[8192];
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X", &address);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            section_nocode_list_Insert(SecNum, RelAdr);
        }
    }

    return 0;
}

#if (OUTPUT_TYPE != OUT_WINDOWS)
static int SR_LoadSCI_external_procedures(FILE *file)
{
    char buf[8192];
    char *str1, *str2;
    extrn_data *extrn;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        str1 = strchr(buf, ',');

        if (str1 == NULL) continue;
        *str1 = 0;
        str1++;

        str2 = strchr(str1, ',');
        if (str2 != NULL)
        {
            *str2 = 0;
            str2++;

            while (*str2 == ' ') str2++;
            if (*str2 == 0) str2 = NULL;
        }

        sscanf(buf, "loc_%X", &address);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            extrn = section_extrn_list_Insert(SecNum, RelAdr, str1, str2);
            if (extrn == NULL) return 1;
        }
    }

    return 0;
}

static int SR_LoadSCI_global_aliases(FILE *file)
{
    char buf[8192];
    char *str1;
    alias_data *alias;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        str1 = strchr(buf, ',');

        if (str1 == NULL) continue;
        *str1 = 0;
        str1++;

        sscanf(buf, "loc_%X", &address);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            alias = section_alias_list_Insert(SecNum, RelAdr, str1);
            if (alias == NULL) return 1;
        }
    }

    return 0;
}

static int SR_LoadSCI_ignored_areas(FILE *file)
{
    char buf[8192];
    int *ignored_area_value;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, arealength;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X,%i", &address, &arealength);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            ignored_area_value = section_ignored_list_Insert(SecNum, RelAdr);

            if (ignored_area_value == NULL) return 1;

            *ignored_area_value = arealength;
        }
    }

    return 0;
}

static int SR_LoadSCI_instruction_replacements(FILE *file)
{
    char buf[8192];
    char *str1, *str2, *str3;
    replace_data *replace;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, instr_empty;
    unsigned int address, instr_len;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        str1 = strchr(buf, ',');

        if (str1 == NULL) continue;
        *str1 = 0;
        str1++;

        str2 = strchr(str1, ',');

        if (str2 == NULL) continue;
        *str2 = 0;
        str2++;

        instr_empty = 0;

        str3 = strchr(str2, '|');
        if (str3 == NULL)
        {
            str3 = str2;
            while (*str3 == ' ') str3++;
            if ((*str3 == ';') || (*str3 == '@'))
            {
                instr_empty = 1;
            }
        }
        else
        {
            while (str3 != NULL)
            {
                *str3 = '\n';
                str3++;

                str3 = strchr(str3, '|');
            }
        }

        sscanf(buf, "loc_%X", &address);
        sscanf(str1, "%i", &instr_len);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            replace = section_replace_list_Insert(SecNum, RelAdr, str2, instr_len, instr_empty);
            if (replace == NULL) return 1;
        }
    }

    return 0;
}

#if (OUTPUT_TYPE == OUT_LLASM)
static int SR_LoadSCI_unaligned_ebp_areas(FILE *file)
{
    char buf[8192];
    int *ua_ebp_area_value;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, arealength;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X,%i", &address, &arealength);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            ua_ebp_area_value = section_ua_ebp_list_Insert(SecNum, RelAdr);

            if (ua_ebp_area_value == NULL) return 1;

            *ua_ebp_area_value = arealength;
        }
    }

    return 0;
}

static int SR_LoadSCI_unaligned_esp_areas(FILE *file)
{
    char buf[8192];
    int *ua_esp_area_value;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items, arealength;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X,%i", &address, &arealength);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            ua_esp_area_value = section_ua_esp_list_Insert(SecNum, RelAdr);

            if (ua_esp_area_value == NULL) return 1;

            *ua_esp_area_value = arealength;
        }
    }

    return 0;
}
#endif

static int SR_LoadSCI_instruction_flags(FILE *file)
{
    char buf[8192];
    char *str1, *str2, *str3;
    bound_data *iflags;
    uint_fast32_t SecNum, RelAdr;
    size_t length;
    int items;
    unsigned int address, to_set, to_clear;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        str1 = strchr(buf, ',');

        if (str1 == NULL) continue;
        *str1 = 0;
        str1++;

        str2 = strchr(str1, ',');

        if (str2 == NULL) continue;
        *str2 = 0;
        str2++;

        str3 = strchr(str2, ';');

        if (str3 != NULL)
        {
            *str3 = 0;
        }

        sscanf(buf, "loc_%X", &address);
        sscanf(str1, "%i", &to_set);
        sscanf(str2, "%i", &to_clear);

        if (SR_get_section_reladr(address, &SecNum, &RelAdr))
        {
            iflags = section_iflags_list_Insert(SecNum, RelAdr, to_set, to_clear);
            if (iflags == NULL) return 1;
        }
    }

    return 0;
}
#endif

/* code_entry_points.sci: addresses of procedures that are called through a
 * pointer (a callback, a list of effects), so that no call leads to them and
 * SR would not know that they hold code */
static int SR_LoadSCI_code_entry_points(FILE *file)
{
    char buf[8192];
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        sscanf(buf, "loc_%X", &address);

        {
            uint_fast32_t SecNum, RelAdr;

            if (SR_get_section_reladr(address, &SecNum, &RelAdr))
            {
                SR_disassemble_add_address((unsigned int)SecNum,
                                           (uint32_t)(section[SecNum].start + RelAdr));
            }
        }
    }

    return 0;
}

#if (OUTPUT_TYPE == OUT_LLASM)
// addresses of the "jmp reg" instructions whose register holds an address of the guest's code
// (not an encoded host offset): they go through x86_resolve_guest_code
// "base loc_X": a fixup of a table of code addresses (or of the base of computed jump targets)
// that is used as a number of the original program, not as a label
static uint32_t *guest_address_jumps = NULL, *guest_address_bases = NULL;
static size_t guest_address_jumps_count = 0, guest_address_jumps_alloc = 0;
static size_t guest_address_bases_count = 0, guest_address_bases_alloc = 0;
// "target loc_X": a code address reached by a computed jump: it is an entry point with a label
static uint32_t *guest_address_targets = NULL;
static size_t guest_address_targets_count = 0, guest_address_targets_alloc = 0;

int SR_is_guest_address_jump(uint32_t address)
{
    size_t index;

    for (index = 0; index < guest_address_jumps_count; index++)
    {
        if (guest_address_jumps[index] == address) return 1;
    }

    return 0;
}

int SR_is_guest_address_target(uint32_t address)
{
    size_t index;

    for (index = 0; index < guest_address_targets_count; index++)
    {
        if (guest_address_targets[index] == address) return 1;
    }

    return 0;
}

int SR_is_guest_address_base(uint32_t address)
{
    size_t index;

    for (index = 0; index < guest_address_bases_count; index++)
    {
        if (guest_address_bases[index] == address) return 1;
    }

    return 0;
}

static int SR_LoadSCI_guest_address_jumps(FILE *file)
{
    char buf[8192];
    size_t length;
    int items;
    unsigned int address;

    while (!feof(file))
    {
        // read enters
        items = fscanf(file, "%8191[\n]", buf);
        // read line
        buf[0] = 0;
        items = fscanf(file, "%8191[^\n]", buf);
        if (items <= 0) continue;
        length = strlen(buf);
        if (length != 0 && buf[length - 1] == '\r')
        {
            length--;
            buf[length] = 0;
        }

        if (length == 0) continue;
        if (buf[0] == ';' || buf[0] == '#') continue;

        if (strncmp(buf, "target ", 7) == 0)
        {
            uint_fast32_t SecNum, RelAdr;

            if (sscanf(buf + 7, "loc_%X", &address) != 1) continue;

            if (guest_address_targets_count == guest_address_targets_alloc)
            {
                uint32_t *list;

                guest_address_targets_alloc = guest_address_targets_alloc ? guest_address_targets_alloc * 2 : 64;
                list = (uint32_t *)realloc(guest_address_targets, guest_address_targets_alloc * sizeof(uint32_t));
                if (list == NULL) return 1;
                guest_address_targets = list;
            }
            guest_address_targets[guest_address_targets_count++] = address;

            if (SR_get_section_reladr(address, &SecNum, &RelAdr))
            {
                SR_disassemble_add_address((unsigned int)SecNum, (uint32_t)(section[SecNum].start + RelAdr));
            }
            continue;
        }

        if (strncmp(buf, "base ", 5) == 0)
        {
            if (sscanf(buf + 5, "loc_%X", &address) != 1) continue;

            if (guest_address_bases_count == guest_address_bases_alloc)
            {
                uint32_t *list;

                guest_address_bases_alloc = guest_address_bases_alloc ? guest_address_bases_alloc * 2 : 16;
                list = (uint32_t *)realloc(guest_address_bases, guest_address_bases_alloc * sizeof(uint32_t));
                if (list == NULL) return 1;
                guest_address_bases = list;
            }
            guest_address_bases[guest_address_bases_count++] = address;
            continue;
        }

        if (sscanf(buf, "loc_%X", &address) != 1) continue;

        if (guest_address_jumps_count == guest_address_jumps_alloc)
        {
            uint32_t *list;

            guest_address_jumps_alloc = guest_address_jumps_alloc ? guest_address_jumps_alloc * 2 : 64;
            list = (uint32_t *)realloc(guest_address_jumps, guest_address_jumps_alloc * sizeof(uint32_t));
            if (list == NULL) return 1;
            guest_address_jumps = list;
        }
        guest_address_jumps[guest_address_jumps_count++] = address;
    }

    return 0;
}
#endif

int SR_LoadSCI(void)
{
    const static char fixup_interpret_as_code[] = "fixup_interpret_as_code.sci";
    const static char code_sections[] = "code_sections.sci";
    const static char code_entry_points[] = "code_entry_points.sci";
    const static char displaced_labels[] = "displaced_labels.sci";
    const static char noret_procedures[] = "noret_procedures.sci";
    const static char code16_areas[] = "code16_areas.sci";
    const static char fixup_do_not_interpret_as_code[] = "fixup_do_not_interpret_as_code.sci";
#if (OUTPUT_TYPE != OUT_WINDOWS)
    const static char external_procedures[] = "external_procedures.sci";
    const static char global_aliases[] = "global_aliases.sci";
    const static char ignored_areas[] = "ignored_areas.sci";
    const static char instruction_replacements[] = "instruction_replacements.sci";
    const static char instruction_replacements_FPU[] = "instruction_replacements_FPU.sci";
#if (OUTPUT_TYPE == OUT_LLASM)
    const static char unaligned_ebp_areas[] = "unaligned_ebp_areas.sci";
    const static char unaligned_esp_areas[] = "unaligned_esp_areas.sci";
#endif
    const static char instruction_flags[] = "instruction_flags.sci";
#if (OUTPUT_TYPE == OUT_LLASM)
    const static char guest_address_jumps_sci[] = "guest_address_jumps.sci";
#endif
#endif
    FILE *f;
    int ret;


    f = fopen(code_sections, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", code_sections);

        ret = SR_LoadSCI_code_sections(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(fixup_interpret_as_code, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", fixup_interpret_as_code);

        ret = SR_LoadSCI_fixup_interpret_as_code(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(code_entry_points, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", code_entry_points);

        ret = SR_LoadSCI_code_entry_points(f);

        fclose(f);

        if (ret) return ret;
    }


#if (OUTPUT_TYPE == OUT_LLASM)
    f = fopen(guest_address_jumps_sci, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", guest_address_jumps_sci);

        ret = SR_LoadSCI_guest_address_jumps(f);

        fclose(f);

        if (ret) return ret;
    }
#endif

    f = fopen(displaced_labels, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", displaced_labels);

        ret = SR_LoadSCI_displaced_labels(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(noret_procedures, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", noret_procedures);

        ret = SR_LoadSCI_noret_procedures(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(code16_areas, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", code16_areas);

        ret = SR_LoadSCI_code16_areas(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(fixup_do_not_interpret_as_code, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", fixup_do_not_interpret_as_code);

        ret = SR_LoadSCI_fixup_do_not_interpret_as_code(f);

        fclose(f);

        if (ret) return ret;
    }


#if (OUTPUT_TYPE != OUT_WINDOWS)
    f = fopen(external_procedures, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", external_procedures);

        ret = SR_LoadSCI_external_procedures(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(global_aliases, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", global_aliases);

        ret = SR_LoadSCI_global_aliases(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(ignored_areas, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", ignored_areas);

        ret = SR_LoadSCI_ignored_areas(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(instruction_replacements, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", instruction_replacements);

        ret = SR_LoadSCI_instruction_replacements(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(instruction_replacements_FPU, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", instruction_replacements_FPU);

        ret = SR_LoadSCI_instruction_replacements(f);

        fclose(f);

        if (ret) return ret;
    }


#if (OUTPUT_TYPE == OUT_LLASM)

    f = fopen(unaligned_ebp_areas, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", unaligned_ebp_areas);

        ret = SR_LoadSCI_unaligned_ebp_areas(f);

        fclose(f);

        if (ret) return ret;
    }


    f = fopen(unaligned_esp_areas, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", unaligned_esp_areas);

        ret = SR_LoadSCI_unaligned_esp_areas(f);

        fclose(f);

        if (ret) return ret;
    }


#endif

    f = fopen(instruction_flags, "rt");
    if (f != NULL)
    {
        fprintf(stderr, "\tLoading %s...\n", instruction_flags);

        ret = SR_LoadSCI_instruction_flags(f);

        fclose(f);

        if (ret) return ret;
    }
#endif

    return 0;
}
