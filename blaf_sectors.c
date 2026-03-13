/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_sectors.c — Dynamic Sector Registry            ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "blaf_sectors.h"

/* ═══════════════════════════════════════════════════════════════
   REGISTRY STATE
   ═══════════════════════════════════════════════════════════════ */

static SectorEntry registry[MAX_SECTORS];
static int         reg_count    = 0;
static int         initialized  = 0;

/* Next available ID for custom sectors — starts above reserved range */
static uint8_t next_custom_id = SECTOR_RESERVED_MAX + 1;

/* ═══════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void upper(char *dst, const char *src, int maxlen) {
    int i;
    for (i = 0; i < maxlen - 1 && src[i]; i++)
        dst[i] = toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* ═══════════════════════════════════════════════════════════════
   INIT — seed built-in sectors
   ═══════════════════════════════════════════════════════════════ */

void sectors_init(void) {
    if (initialized) return;

    /* Built-in sectors — locked, IDs fixed forever */
    struct { uint8_t id; const char *name; } builtins[] = {
        {SECTOR_GENERAL,   "GENERAL"},
        {SECTOR_SECURITY,  "SECURITY"},
        {SECTOR_ICT,       "ICT"},
        {SECTOR_FINANCE,   "FINANCE"},
        {SECTOR_BIOLOGY,   "BIOLOGY"},
        {SECTOR_LANGUAGE,  "LANGUAGE"},
        {SECTOR_LEGAL,     "LEGAL"},
        {SECTOR_MEDICAL,   "MEDICAL"},
        {SECTOR_EDUCATION, "EDUCATION"},
    };

    int n = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < n; i++) {
        SectorEntry *e = &registry[reg_count++];
        e->id     = builtins[i].id;
        e->locked = 1;
        strncpy(e->name, builtins[i].name, 31);
    }

    initialized = 1;
    printf("[SECTORS] Registry initialized — %d built-in sectors. "
           "Custom sectors start at 0x%02X.\n", reg_count, next_custom_id);
}

/* ═══════════════════════════════════════════════════════════════
   CORE API
   ═══════════════════════════════════════════════════════════════ */

uint8_t sector_id(const char *name) {
    if (!initialized) sectors_init();

    char upper_name[32];
    upper(upper_name, name, 32);

    /* Search existing registry */
    for (int i = 0; i < reg_count; i++) {
        if (strcmp(registry[i].name, upper_name) == 0)
            return registry[i].id;
    }

    /* Not found — create it */
    if (reg_count >= MAX_SECTORS || next_custom_id == 0xFF) {
        fprintf(stderr, "[SECTORS] ERROR: Registry full. Cannot add \"%s\".\n",
                upper_name);
        return 0xFF;
    }

    SectorEntry *e = &registry[reg_count++];
    e->id     = next_custom_id++;
    e->locked = 0;
    strncpy(e->name, upper_name, 31);

    printf("[SECTORS] New sector registered: 0x%02X → %s "
           "(%d total sectors)\n", e->id, e->name, reg_count);

    return e->id;
}

const char* sector_name(uint8_t id) {
    if (!initialized) sectors_init();
    for (int i = 0; i < reg_count; i++)
        if (registry[i].id == id)
            return registry[i].name;
    return "UNKNOWN";
}

int sector_exists(const char *name) {
    if (!initialized) sectors_init();
    char upper_name[32];
    upper(upper_name, name, 32);
    for (int i = 0; i < reg_count; i++)
        if (strcmp(registry[i].name, upper_name) == 0)
            return 1;
    return 0;
}

int sector_count(void) {
    if (!initialized) sectors_init();
    return reg_count;
}

void sectors_list(void) {
    if (!initialized) sectors_init();
    printf("\n[SECTORS] Registry (%d total):\n", reg_count);
    for (int i = 0; i < reg_count; i++) {
        printf("  0x%02X  %-20s  %s\n",
               registry[i].id,
               registry[i].name,
               registry[i].locked ? "[built-in]" : "[custom]");
    }
    printf("\n");
}

int sector_delete(const char *name) {
    if (!initialized) sectors_init();

    char upper_name[32];
    upper(upper_name, name, 32);

    for (int i = 0; i < reg_count; i++) {
        if (strcmp(registry[i].name, upper_name) == 0) {
            if (registry[i].locked) {
                fprintf(stderr, "[SECTORS] Cannot delete built-in sector: %s\n",
                        upper_name);
                return -1;
            }
            /* Shift remaining entries down */
            for (int j = i; j < reg_count - 1; j++)
                registry[j] = registry[j + 1];
            reg_count--;
            printf("[SECTORS] Deleted sector: %s\n", upper_name);
            return 0;
        }
    }

    fprintf(stderr, "[SECTORS] Sector not found: %s\n", upper_name);
    return -1;
}
