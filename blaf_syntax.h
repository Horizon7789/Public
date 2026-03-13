#ifndef BLAF_SYNTAX_H
#define BLAF_SYNTAX_H

#include <stdint.h>
#include "blaf_grammar.h"

#define MAX_SCHEMA_LENGTH 12
#define MAX_GRAMMAR_LIBRARY 1000

typedef struct {
    uint8_t pos_sequence[MAX_SCHEMA_LENGTH]; // Sequence of CLASS_NOUN, CLASS_VERB, etc.
    uint8_t length;
    uint32_t frequency;                      // How often BLAF sees this pattern
} SentenceSchema;

typedef struct {
    uint32_t noun_hits;
    uint32_t verb_hits;
    uint32_t adj_hits;
} POSWeight;

extern SentenceSchema grammar_library[MAX_GRAMMAR_LIBRARY];
extern int schema_count;

// API
void syntax_record_sentence(const char *input);
void syntax_dump_library(void);
char* syntax_generate_from_fact(uint32_t s_hash, uint32_t v_hash, uint32_t o_hash);

#endif
