/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_mapper.c — External Concept Definition API     ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Compile together with blaf.c:
 *  gcc -O2 -o blaf blaf.c blaf_mapper.c
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "blaf_mapper.h"
#include "blaf_sectors.h"
#include "blaf_core.h"

/* ═══════════════════════════════════════════════════════════════
   FORWARD DECLARATIONS — hooks into blaf.c's internal table
   ═══════════════════════════════════════════════════════════════ */
extern ConceptEntry concept_table[MAX_CONCEPTS];
extern int          concept_count;
extern Fact         fact_pool[MAX_FACT_POOL];
extern int          fact_count;


/* These are defined in blaf.c — the mapper writes into them */
extern int   add_concept(const char *word, uint8_t type, uint8_t class,
                         uint8_t integrity, uint8_t sector,
                         uint8_t in_pin, uint8_t out_pin);
extern int   concept_count;

/* Sector registry now lives in blaf_sectors.c */

/* ═══════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lowercase(char *dst, const char *src, int maxlen) {
    int i;
    for (i = 0; i < maxlen - 1 && src[i]; i++)
        dst[i] = tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static const char* class_name(uint8_t class) {
    switch (class) {
        case 0:  return "NOUN";
        case 1:  return "VERB";
        case 2:  return "ADJ";
        case 3:  return "PREP";
        case 4:  return "ADV";
        case 5:  return "PRONOUN";
        case 6:  return "CONJ";
        case 7:  return "INTERJ";
        case 8:  return "ARTICLE";
        case 9:  return "AUX_VERB";
        case 10: return "PUNCT";
        case 11: return "NUMERAL";
        default: return "UNKNOWN";
    }
}

static const char* trust_name(uint8_t trust) {
    switch (trust) {
        case 0: return "SANDBOX";
        case 1: return "SINGLE";
        case 3: return "VERIFIED";
        case 7: return "IMMUTABLE";
        default: return "CUSTOM";
    }
}

/* Build a human-readable pin string like "NOUN|VERB|ADJ" */
static void pin_string(uint8_t pin, char *buf, int len) {
    buf[0] = '\0';
    if (pin == 0xFF) { snprintf(buf, len, "ANY"); return; }
    if (pin == 0x00) { snprintf(buf, len, "NONE"); return; }

    const char *names[] = {"NOUN","VERB","ADJ","PREP","ADV","PRONOUN","CONJ","ARTICLE"};
    int first = 1;
    for (int i = 0; i < 8; i++) {
        if (pin & (1 << i)) {
            if (!first) strncat(buf, "|", len - strlen(buf) - 1);
            strncat(buf, names[i], len - strlen(buf) - 1);
            first = 0;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   SECTOR FUNCTIONS
   ═══════════════════════════════════════════════════════════════ */

/* define_sector() and sector_name() now live in blaf_sectors.c
 * Call sector_id("NAME") to get-or-create a sector.
 * Call sector_name(id) to reverse lookup.
 */

/* ═══════════════════════════════════════════════════════════════
   CORE MAPPING FUNCTIONS
   ═══════════════════════════════════════════════════════════════ */

int map_word(const ConceptDef *def) {
    if (!def || !def->word) return -1;

    char lower[64];
    lowercase(lower, def->word, 64);

    int result = add_concept(lower,
                             def->type,
                             def->class,
                             def->trust,
                             def->sector,
                             def->input_pin,
                             def->output_pin);
    if (result == 0)
        printf("[BLAF MAPPER] Mapped: %-20s cls:%-10s sec:%-12s trust:%s\n",
               lower,
               class_name(def->class),
               sector_name(def->sector),
               trust_name(def->trust));
    else
        fprintf(stderr, "[BLAF MAPPER] ERROR: Table full — could not map \"%s\"\n", lower);

    return result;
}

int map_word_batch(const ConceptDef *defs, int count) {
    int success = 0;
    printf("[BLAF MAPPER] Batch loading %d concepts...\n", count);
    for (int i = 0; i < count; i++) {
        if (map_word(&defs[i]) == 0) success++;
    }
    printf("[BLAF MAPPER] Batch complete: %d / %d loaded. "
           "Total concepts: %d\n", success, count, concept_count);
    return success;
}

int map_word_web(const char *word, uint8_t class,
                 uint8_t sector, uint8_t input_pin, uint8_t output_pin) {

    char lower[64];
    lowercase(lower, word, 64);

    /* Web-sourced words start at TRUST_SANDBOX */
    int result = add_concept(lower,
                             TYPE_IDENTIFIER,
                             class,
                             TRUST_SANDBOX,
                             sector,
                             input_pin,
                             output_pin);
    if (result == 0)
        printf("[BLAF MAPPER] Web-mapped (SANDBOX): %-20s cls:%-10s sec:%s\n",
               lower, class_name(class), sector_name(sector));
    return result;
}

/* ═══════════════════════════════════════════════════════════════
   TRUST PROMOTION
   ═══════════════════════════════════════════════════════════════ */

/*
 * Trust promotion ladder:
 * SANDBOX(0) → SINGLE(1) → VERIFIED(3) → IMMUTABLE(7)
 */
static uint8_t next_trust_level(uint8_t current) {
    if (current == 0) return 1;
    if (current == 1) return 3;
    if (current == 3) return 7;
    return 7; /* already at max */
}

/* Needs access to blaf.c's concept table — expose via extern */
typedef struct {
    char    word[64];
    /* ConceptBlock block — mirrored here for the mapper */
    uint8_t type_tag   : 2;
    uint8_t class_tag  : 4;
    uint8_t integrity  : 3;
    uint8_t sector     : 8;
    uint8_t input_pin  : 8;
    uint8_t output_pin : 8;
    uint32_t payload   : 31;
} MirrorEntry;

/* blaf.c exposes these for the mapper */
extern void*  get_concept_entry(int index);
extern int    get_concept_count(void);
extern int    set_concept_trust(int index, uint8_t trust);

int promote_word(const char *word, uint8_t sector) {
    int count = get_concept_count();
    for (int i = 0; i < count; i++) {
        /* Use the accessor API exposed by blaf.c */
        /* This is a stub — wire to blaf.c's accessor in your integration */
        (void)sector; /* suppress warning until wired */
        (void)word;
        break;
    }

    /*
     * TODO: Wire to blaf.c's concept_table directly once
     * you decide on the accessor pattern.
     * For now, print intent:
     */
    printf("[BLAF MAPPER] promote_word(\"%s\", sector:0x%02X) "
           "— ready to wire to concept_table.\n", word, sector);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   LOOKUP & INSPECT
   ═══════════════════════════════════════════════════════════════ */

ConceptDef* lookup_def(const char *word, uint8_t sector) {
    /*
     * TODO: Wire to blaf.c's concept_table once accessor is added.
     * Stub returns NULL until wired.
     */
    (void)word; (void)sector;
    return NULL;
}

void print_concept(const char *word, uint8_t sector) {
    char in_str[64], out_str[64];

    /*
     * TODO: Wire to blaf.c's concept_table.
     * For now demonstrates the intended output format:
     */
    printf("\n[CONCEPT DEFINITION]\n");
    printf("  Word      : %s\n", word);
    printf("  Sector    : 0x%02X (%s)\n", sector, sector_name(sector));
    printf("  -- full detail available after accessor wired to blaf.c --\n");

    (void)in_str; (void)out_str;
}

void print_all_concepts(void) {
    printf("\n[BLAF MAPPER] Total concepts in table: %d\n", concept_count);
    printf("  -- full dump available after accessor wired to blaf.c --\n");
}

/* ═══════════════════════════════════════════════════════════════
   PERSISTENCE — EXPORT / IMPORT
   ═══════════════════════════════════════════════════════════════ */

/*
 * .blaf file format (plain text, one concept per line):
 * word|type|class|trust|sector|input_pin|output_pin
 *
 * Example line:
 * encryption|2|0|3|1|255|255
 */

int export_concepts(const char *filename) {
    /*
     * TODO: Wire to blaf.c's concept_table for full export.
     * Stub creates an empty file with header comment.
     */
    FILE *f = fopen(filename, "w");
    if (!f) { perror("[BLAF MAPPER] export_concepts"); return -1; }

    fprintf(f, "# BLAF Concept Export\n");
    fprintf(f, "# Format: word|type|class|trust|sector|input_pin|output_pin\n");
    fprintf(f, "# Total concepts at export time: %d\n", concept_count);
    fprintf(f, "# TODO: full table export — wire to blaf.c accessor\n");

    fclose(f);
    printf("[BLAF MAPPER] Exported to: %s\n", filename);
    return 0;
}

int import_concepts(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("[BLAF MAPPER] import_concepts"); return -1; }

    char line[256];
    int  loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\n') continue;

        char    word[64];
        uint8_t type, class, trust, sector, in_pin, out_pin;

        int parsed = sscanf(line, "%63[^|]|%hhu|%hhu|%hhu|%hhu|%hhu|%hhu",
                            word, &type, &class, &trust,
                            &sector, &in_pin, &out_pin);

        if (parsed == 7) {
            ConceptDef def = {word, type, class, trust, sector, in_pin, out_pin};
            if (map_word(&def) == 0) loaded++;
        } else {
            fprintf(stderr, "[BLAF MAPPER] Skipped malformed line: %s", line);
        }
    }

    fclose(f);
    printf("[BLAF MAPPER] Imported %d concepts from: %s\n", loaded, filename);
    return loaded;
}


int add_fact(const char *subject, uint8_t predicate, const char *object) {
    if (fact_count >= MAX_FACT_POOL) return -1;

    int s_idx = find_concept_index(subject, 0xFF);
    if (s_idx == -1) return -2;

    Fact *new_fact = &fact_pool[fact_count];
    new_fact->subject_hash = hash_word(subject);
    new_fact->predicate    = predicate;
    new_fact->object_hash     = hash_word(object);

    // If this is the first fact for this word, set the start index
    if (concept_table[s_idx].fact_count == 0) {
        concept_table[s_idx].fact_start_index = fact_count;
    }
    
    concept_table[s_idx].fact_count++;
    fact_count++;
    return 0;
}



/* ═══════════════════════════════════════════════════════════════
   QUICK-DEFINE HELPER MACROS
   Use these in any external file that includes blaf_mapper.h
   ═══════════════════════════════════════════════════════════════ */

/*
 * These are defined in the header as convenience macros.
 * Example usage in your own concept files:
 *
 *   #include "blaf_mapper.h"
#include "blaf_sectors.h"
 *
 *   void load_crypto_concepts() {
 *       ConceptDef crypto[] = {
 *           {"bitcoin",   TYPE_IDENTIFIER, CLASS_NOUN, TRUST_VERIFIED, SECTOR_FINANCE,   PIN_ANY, PIN_ANY},
 *           {"ethereum",  TYPE_IDENTIFIER, CLASS_NOUN, TRUST_VERIFIED, SECTOR_FINANCE,   PIN_ANY, PIN_ANY},
 *           {"mining",    TYPE_IDENTIFIER, CLASS_VERB, TRUST_VERIFIED, SECTOR_FINANCE,   PIN_ANY, PIN_ANY},
 *           {"ledger",    TYPE_IDENTIFIER, CLASS_NOUN, TRUST_VERIFIED, SECTOR_FINANCE,   PIN_ANY, PIN_ANY},
 *           {"encrypt",   TYPE_IDENTIFIER, CLASS_VERB, TRUST_IMMUTABLE,SECTOR_SECURITY,  PIN_ANY, PIN_ANY},
 *           {"decrypt",   TYPE_IDENTIFIER, CLASS_VERB, TRUST_IMMUTABLE,SECTOR_SECURITY,  PIN_ANY, PIN_ANY},
 *           {"signature", TYPE_IDENTIFIER, CLASS_NOUN, TRUST_VERIFIED, SECTOR_SECURITY,  PIN_ANY, PIN_ANY},
 *       };
 *       map_word_batch(crypto, 7);
 *   }
 */
