#ifndef PROFILER_H_
#define PROFILER_H_

#include "genesis.h"

#define PROFILER_MDP_VERSION 1

#define PROFILER_SUBROUTINE_ENTER  (1 << 0)
#define PROFILER_SUBROUTINE_EXIT   (1 << 1)
#define PROFILER_INTERRUPT_ENTER   (1 << 2)
#define PROFILER_INTERRUPT_EXIT    (1 << 3)
#define PROFILER_MANUAL_BREAKPOINT (1 << 4)

#define PROFILER_PACKET_SUBROUTINE_ENTER  0
#define PROFILER_PACKET_SUBROUTINE_EXIT   1
#define PROFILER_PACKET_INTERRUPT_ENTER   2
#define PROFILER_PACKET_INTERRUPT_EXIT    3
#define PROFILER_PACKET_HINT              4
#define PROFILER_PACKET_VINT              5
#define PROFILER_PACKET_ADJUST_CYCLES     6
#define PROFILER_PACKET_MANUAL_BREAKPOINT 7

typedef struct profiler_breakpoint profiler_breakpoint;

struct profiler_breakpoint {
    uint8_t flags;
};

typedef struct __attribute__((__packed__)) profiler_packet {
    uint8_t packet_type;
    uint32_t cycle;
    uint32_t stack_pointer;
} profiler_packet;

typedef union mdp_header {
    struct __attribute__((__packed__)) {
        char magic[3];
        uint8_t version;
        uint32_t mclk;
        uint32_t divider;
    };
    char padding[256];
} mdp_header;

void profiler_start(m68k_context *context, char *file_name);
void profiler_stop(m68k_context *context);
void profiler_notify_hint(uint32_t cycles);
void profiler_notify_vint(uint32_t cycles);
void profiler_notify_adjust_cycles(uint32_t cycles);
void profiler_set_breakpoint_path(char *path);

#endif //PROFILER_H_
