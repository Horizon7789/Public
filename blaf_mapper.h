/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_mapper.h — External Concept Definition API     ║
 * ║                                                              ║
 * ║  Usage:  #include "blaf_mapper.h"                           ║
 * ║  Then call any define_* or map_word() function              ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_MAPPER_H
#define BLAF_MAPPER_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   TYPE TAG CONSTANTS
   ═══════════════════════════════════════════════════════════════ */
#define TYPE_LITERAL      0b00
#define TYPE_OPERATOR     0b01
#define TYPE_IDENTIFIER   0b10
#define TYPE_META         0b11

/* ═══════════════════════════════════════════════════════════════
   CLASS TAG CONSTANTS (Part of Speech)
   ═══════════════════════════════════════════════════════════════ */

   /*
#define CLASS_NOUN        0
#define CLASS_VERB        1
#define CLASS_ADJ         2
#define CLASS_PREP        3
#define CLASS_ADV         4
#define CLASS_PRONOUN     5
#define CLASS_CONJ        6
#define CLASS_INTERJ      7
#define CLASS_ARTICLE     8
#define CLASS_AUX_VERB    9
#define CLASS_PUNCTUATION 10
#define CLASS_NUMERAL     11
*/

/* ═══════════════════════════════════════════════════════════════
   INTEGRITY / TRUST LEVELS
   ═══════════════════════════════════════════════════════════════ */
#define TRUST_SANDBOX     0   /* Raw, unverified                  */
#define TRUST_SINGLE      1   /* Confirmed by 1 source            */
#define TRUST_VERIFIED_SOURCES    3   /* Confirmed by 2+ sources          */
#define TRUST_IMMUTABLE   7   /* Locked core concept              */

/* ═══════════════════════════════════════════════════════════════
   SECTOR CONSTANTS
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
/* Add new sectors here as needed — increment the hex value */

/* ═══════════════════════════════════════════════════════════════
   PIN MASK CONSTANTS
   Each bit = one class that can connect at this position.
   ═══════════════════════════════════════════════════════════════ */
#define PIN_NONE          0b00000000
#define PIN_NOUN          0b00000001
#define PIN_VERB          0b00000010
#define PIN_ADJ           0b00000100
#define PIN_PREP          0b00001000
#define PIN_ADV           0b00010000
#define PIN_PRONOUN       0b00100000
#define PIN_CONJ          0b01000000
#define PIN_ARTICLE       0b10000000
#define PIN_ANY           0xFF

/* ═══════════════════════════════════════════════════════════════
   CONCEPT DEFINITION STRUCT
   Fill this in and pass to map_word() or map_word_batch()
   ═══════════════════════════════════════════════════════════════ */
typedef struct {
    const char *word;        /* The word/concept string            */
    uint8_t     type;        /* TYPE_* constant                    */
    uint8_t     class;       /* CLASS_* constant                   */
    uint8_t     trust;       /* TRUST_* constant                   */
    uint8_t     sector;      /* SECTOR_* constant                  */
    uint8_t     input_pin;   /* PIN_* mask — what can come before  */
    uint8_t     output_pin;  /* PIN_* mask — what must follow      */
} ConceptDef;

/* ═══════════════════════════════════════════════════════════════
   FUNCTION DECLARATIONS
   ═══════════════════════════════════════════════════════════════ */

/*
 * map_word()
 * Add a single concept to the system.
 * Returns 0 on success, -1 if table is full.
 *
 * Example:
 *   map_word(&(ConceptDef){
 *       "encryption", TYPE_IDENTIFIER, CLASS_NOUN,
 *       TRUST_VERIFIED, SECTOR_SECURITY,
 *       PIN_ARTICLE|PIN_ADJ, PIN_ANY
 *   });
 */
int map_word(const ConceptDef *def);

/*
 * map_word_batch()
 * Add an array of ConceptDef entries in one call.
 * count = number of entries in the array.
 * Returns number of successfully added concepts.
 *
 * Example:
 *   ConceptDef words[] = {
 *       {"dog",  TYPE_IDENTIFIER, CLASS_NOUN, TRUST_IMMUTABLE, SECTOR_BIOLOGY,  PIN_ANY, PIN_ANY},
 *       {"bark", TYPE_IDENTIFIER, CLASS_VERB, TRUST_IMMUTABLE, SECTOR_BIOLOGY,  PIN_ANY, PIN_ANY},
 *   };
 *   map_word_batch(words, 2);
 */
int map_word_batch(const ConceptDef *defs, int count);

/*
 * map_word_web()
 * Register a word fetched from the web.
 * Starts at TRUST_SANDBOX — call promote_word() after verification.
 *
 * Example:
 *   map_word_web("reentrancy", CLASS_NOUN, SECTOR_SECURITY, PIN_ANY, PIN_ANY);
 */
int map_word_web(const char *word, uint8_t class,
                 uint8_t sector, uint8_t input_pin, uint8_t output_pin);

/*
 * promote_word()
 * Bump a word's trust level up by one step.
 * Call this after each additional source confirms the word.
 * Returns 0 on success, -1 if word not found.
 *
 * Example:
 *   promote_word("reentrancy", SECTOR_SECURITY);
 */
int promote_word(const char *word, uint8_t sector);

/*
 * define_sector()
 * Register a new sector at runtime.
 * Returns the new sector ID, or -1 if sector table is full.
 *
 * Example:
 *   uint8_t SECTOR_CRYPTO = define_sector("crypto");
 */
uint8_t define_sector(const char *name);

/*
 * sector_name()
 * Get the string name of a sector by its ID.
 * Returns "UNKNOWN" if not found.
 */
const char* sector_name(uint8_t sector_id);

/*
 * lookup_def()
 * Get the ConceptDef of a mapped word (for inspection or editing).
 * Returns NULL if not found.
 *
 * Example:
 *   ConceptDef *d = lookup_def("virus", SECTOR_BIOLOGY);
 *   if (d) d->trust = TRUST_IMMUTABLE;
 */
ConceptDef* lookup_def(const char *word, uint8_t sector);

/*
 * print_concept()
 * Pretty-print a word's full bit definition.
 *
 * Example:
 *   print_concept("virus", SECTOR_BIOLOGY);
 */
void print_concept(const char *word, uint8_t sector);

/*
 * print_all_concepts()
 * Dump the entire concept table — useful for debugging.
 */
void print_all_concepts(void);

/*
 * export_concepts()
 * Write all concepts to a .blaf file for persistence.
 * Returns 0 on success, -1 on file error.
 *
 * Example:
 *   export_concepts("my_concepts.blaf");
 */
int export_concepts(const char *filename);

/*
 * import_concepts()
 * Load concepts from a .blaf file into the table.
 * Returns number of concepts loaded, -1 on file error.
 *
 * Example:
 *   import_concepts("my_concepts.blaf");
 */
int import_concepts(const char *filename);

/* Persistence — save/load learned concept table */
int  knowledge_save(const char *path);   /* NULL = default path */
int  knowledge_load(const char *path);   /* NULL = default path */
void knowledge_autosave(void);

#endif /* BLAF_MAPPER_H */
