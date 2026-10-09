#include "guest_memory.h"

#include <hilog/log.h>
#include <sys/mman.h>
#include <string.h>

#define TAG "isaac.mem"
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, 0, TAG, __VA_ARGS__))

uint8_t *isaac_arena_base = 0;

int guest_mem_init(void) {
    if (isaac_arena_base != 0) {
        return 0;
    }
    void *p = mmap(0, ISAAC_GUEST_MEM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        LOGE("guest arena mmap(4GiB, NORESERVE) FAILED");
        return -1;
    }
    isaac_arena_base = (uint8_t *)p;
    return 0;
}

void guest_mem_shutdown(void) {
    if (isaac_arena_base != 0) {
        munmap(isaac_arena_base, ISAAC_GUEST_MEM_SIZE);
        isaac_arena_base = 0;
    }
}

/* Strings live at the very top of the low 4 GiB but below 0xF0000000 so a
 * guest size_t-style (addr + len) can never wrap past the arena edge. */
#define ISAAC_STR_TOP 0xF0000000u
static uint32_t s_str_bump = ISAAC_STR_TOP;

uint32_t guest_mem_push_string(const char *s, size_t len) {
    if (isaac_arena_base == 0 || s == 0) {
        return 0;
    }
    size_t total = len + 1;
    if (s_str_bump < total || (uint64_t)s_str_bump - total < ISAAC_GUEST_MEM_SIZE / 2) {
        return 0;
    }
    s_str_bump -= (uint32_t)total;
    memcpy(isaac_arena_base + s_str_bump, s, len);
    isaac_arena_base[s_str_bump + len] = 0;
    return s_str_bump;
}

uint32_t guest_mem_top(void) {
    return s_str_bump;
}
