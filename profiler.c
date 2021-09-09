#include "profiler.h"
#include <stdio.h>
#include "debug.h"
#include "util.h"
#include "genesis.h"
#include "68kinst.h"
#include <stdlib.h>
#include <string.h>

static profiler_breakpoint * profiler_breakpoints = NULL;
static FILE * profiling_file = NULL;
static genesis_context *system_context = NULL;
static char *breakpoint_path = NULL;

void profiler_callback(m68k_context *context, uint32_t pc)
{
    profiler_breakpoint *pbp_info = &profiler_breakpoints[pc / 2];
    uint32_t sp = context->aregs[7];
    uint32_t cycle = context->current_cycle;
    if (pbp_info->flags & PROFILER_INTERRUPT_ENTER) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_INTERRUPT_ENTER,
            .cycle = cycle,
            .stack_pointer = sp,
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
        fwrite(&pc, sizeof (uint32_t), 1, profiling_file);
    }
    if (pbp_info->flags & PROFILER_SUBROUTINE_ENTER) {
        m68kinst inst;
        m68k_decode(&system_context->cart[pc / 2], &inst, pc);
        uint32_t target = m68k_branch_target(&inst, context->dregs, context->aregs);
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_SUBROUTINE_ENTER,
            .cycle = cycle,
            .stack_pointer = sp,
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
        fwrite(&target, sizeof (uint32_t), 1, profiling_file);
    }
    if (pbp_info->flags & PROFILER_MANUAL_BREAKPOINT) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_MANUAL_BREAKPOINT,
            .cycle = cycle,
            .stack_pointer = sp,
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
        fwrite(&pc, sizeof (uint32_t), 1, profiling_file);
    }
    if (pbp_info->flags & PROFILER_SUBROUTINE_EXIT) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_SUBROUTINE_EXIT,
            .cycle = cycle,
            .stack_pointer = sp,
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
    }
    if (pbp_info->flags & PROFILER_INTERRUPT_EXIT) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_INTERRUPT_EXIT,
            .cycle = cycle,
            .stack_pointer = sp,
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
    }
}

mdp_header profiler_get_header()
{
    mdp_header header;
    memset(&header, 0, sizeof (mdp_header));
    header.magic[0] = 'M';
    header.magic[1] = 'D';
    header.magic[2] = 'P';
    header.version = PROFILER_MDP_VERSION;
    header.mclk = system_context->normal_clock;
    header.divider = system_context->m68k->options->gen.clock_divider;
    return header;
}

void profiler_add_breakpoint(m68k_context *context, uint32_t addr, uint8_t flags)
{
    insert_breakpoint(context, addr, profiler_callback);
    profiler_breakpoints[addr / 2].flags |= flags;
}

void profiler_start(m68k_context *context, char *file_name)
{
    if (profiler_breakpoints != NULL) {
        profiler_stop(context);
    }
    system_context = context->system;
    uint32_t rom_size = system_context->header.info.rom_size;
    profiler_breakpoints = calloc(rom_size / 2, sizeof (profiler_breakpoint));
    profiling_file = fopen(file_name, "wb");
    if (profiling_file == NULL) {
        fatal_error("Can't create profiling file %s", file_name);
    }
    mdp_header header = profiler_get_header();
    fwrite(&header, sizeof (mdp_header), 1, profiling_file);
    for (uint32_t word_address = 0; word_address < rom_size / 2; word_address++) {
        uint32_t address = word_address * 2;
        m68kinst inst;
        uint16_t *curr = &system_context->cart[word_address];
        m68k_decode(curr, &inst, address);
        if (inst.op == M68K_BSR || inst.op == M68K_JSR) {
            profiler_add_breakpoint(context, address, PROFILER_SUBROUTINE_ENTER);
        } else if (inst.op == M68K_RTS) {
            profiler_add_breakpoint(context, address, PROFILER_SUBROUTINE_EXIT);
        }
        else if (inst.op == M68K_RTE) {
            profiler_add_breakpoint(context, address, PROFILER_INTERRUPT_EXIT);
        }
    }

    uint32_t irq2_addr = m68k_read_long(0x68, context); // EXT-INT
    uint32_t irq4_addr = m68k_read_long(0x70, context); // H-INT
    uint32_t irq6_addr = m68k_read_long(0x78, context); // V-INT
    if (irq2_addr < rom_size) {
        profiler_add_breakpoint(context, irq2_addr, PROFILER_INTERRUPT_ENTER);
    }
    if (irq4_addr < rom_size) {
        profiler_add_breakpoint(context, irq4_addr, PROFILER_INTERRUPT_ENTER);
    }
    if (irq6_addr < rom_size) {
        profiler_add_breakpoint(context, irq6_addr, PROFILER_INTERRUPT_ENTER);
    }

    if (breakpoint_path != NULL) {
        FILE *f = fopen(breakpoint_path, "rb");
        if (f != NULL) {
            printf("Reloading breakpoint file: %s\n", breakpoint_path);
            uint32_t added = 0;
            uint32_t addr;
            while (fread(&addr, sizeof (uint32_t), 1, f)) {
                profiler_add_breakpoint(context, addr, PROFILER_MANUAL_BREAKPOINT);
                added++;
            }
            fclose(f);
            printf("Added %u manual breakpoints\n",added);
        } else {
            fatal_error("Can't read breakpoint file %s", breakpoint_path);
        }
    }
    printf("Profiler started, writing to file %s\n", file_name);
}

void profiler_stop(m68k_context *context)
{
    if (profiler_breakpoints == NULL) {
        return;
    }
    uint32_t rom_size = system_context->header.info.rom_size;
    for (uint32_t word_address = 0; word_address < rom_size / 2; word_address++) {
        if (profiler_breakpoints[word_address].flags) {
            remove_breakpoint(context, word_address * 2);
        }
    }
    free(profiler_breakpoints);
    fclose(profiling_file);
    profiler_breakpoints = NULL;
    profiling_file = NULL;
    system_context = NULL;
    printf("Profiler stopped\n");
}

void profiler_notify_hint(uint32_t cycles)
{
    if (profiling_file != NULL) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_HINT,
            .cycle = cycles,
            .stack_pointer = system_context->m68k->aregs[7],
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
    }
}

void profiler_notify_vint(uint32_t cycles)
{
    if (profiling_file != NULL) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_VINT,
            .cycle = cycles,
            .stack_pointer = system_context->m68k->aregs[7],
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
    }
}

void profiler_notify_adjust_cycles(uint32_t cycles)
{
    if (profiling_file != NULL) {
        profiler_packet packet = (profiler_packet) {
            .packet_type = PROFILER_PACKET_ADJUST_CYCLES,
            .cycle = cycles,
            .stack_pointer = system_context->m68k->aregs[7],
        };
        fwrite(&packet, sizeof (profiler_packet), 1, profiling_file);
    }
}

void profiler_set_breakpoint_path(char *path) {
    free(breakpoint_path);
    if (path == NULL) {
        breakpoint_path = NULL;
        printf("Breakpoint file set to none\n");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    } else {
        fprintf(stderr, "Can't open breakpoint file \"%s\"\n", path);
        breakpoint_path = NULL;
        return;
    }
    uint32_t len = strlen(path);
    breakpoint_path = malloc(len + 1);
    memcpy(breakpoint_path, path, len + 1);
    printf("Breakpoint file set to %s\n", path);
}
