/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_sectors.h — Dynamic Sector Registry            ║
 * ║                                                              ║
 * ║  Sectors are NO LONGER hardcoded constants.                 ║
 * ║  They live in a runtime registry that grows on demand.      ║
 * ║                                                              ║
 * ║  Usage:                                                      ║
 * ║    #include "blaf_sectors.h"                                 ║
 * ║    uint8_t s = sector_id("CRYPTO");   // get or create      ║
 * ║    const char *name = sector_name(s); // reverse lookup     ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_SECTORS_H
#define BLAF_SECTORS_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   RESERVED BUILT-IN SECTOR IDs  (0x00 - 0x0F)
   These are guaranteed stable and will never be reassigned.
   Custom sectors start at 0x10 and grow upward.
   ═══════════════════════════════════════════════════════════════ */

#define SECTOR_GENERAL    0x00
#define SECTOR_SECURITY   0x01
#define SECTOR_ICT        0x02
#define SECTOR_FINANCE    0x03
#define SECTOR_BIOLOGY    0x04
#define SECTOR_LANGUAGE   0x05
#define SECTOR_LEGAL      0x06
#define SECTOR_MEDICAL    0x07
#define SECTOR_EDUCATION  0x08
#define SECTOR_RESERVED_MAX 0x0F  /* IDs below this are protected */

#define MAX_SECTORS       255     /* sector is a uint8_t field     */

/* ═══════════════════════════════════════════════════════════════
   SECTOR REGISTRY ENTRY
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  id;
    char     name[32];
    uint8_t  locked;      /* 1 = built-in, cannot be deleted      */
} SectorEntry;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

/*
 * sectors_init()
 * Must be called once at startup — seeds the built-in sectors.
 * Safe to call multiple times (idempotent).
 */
void sectors_init(void);

/*
 * sector_id()
 * Get the numeric ID for a sector by name.
 * If the name doesn't exist, it is CREATED automatically.
 * Names are case-insensitive.
 *
 * Returns the sector ID (0x00 - 0xFE).
 * Returns 0xFF if the registry is full (255 sectors max).
 *
 * Example:
 *   uint8_t s = sector_id("CRYPTO");   // creates if new
 *   uint8_t s = sector_id("security"); // returns 0x01
 */
uint8_t sector_id(const char *name);

/*
 * sector_name()
 * Get the string name of a sector by its ID.
 * Returns "UNKNOWN" if not found.
 *
 * Example:
 *   printf("%s", sector_name(0x01)); // "SECURITY"
 */
const char* sector_name(uint8_t id);

/*
 * sector_exists()
 * Returns 1 if a sector with that name is registered, 0 if not.
 *
 * Example:
 *   if (!sector_exists("CRYPTO")) sector_id("CRYPTO");
 */
int sector_exists(const char *name);

/*
 * sector_count()
 * Returns how many sectors are currently registered.
 */
int sector_count(void);

/*
 * sectors_list()
 * Prints all registered sectors with their IDs.
 */
void sectors_list(void);

/*
 * sector_delete()
 * Removes a custom sector by name.
 * Cannot delete built-in sectors (id <= SECTOR_RESERVED_MAX).
 * Returns 0 on success, -1 if not found or protected.
 */
int sector_delete(const char *name);

#endif /* BLAF_SECTORS_H */
