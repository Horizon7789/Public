#ifndef BLAF_CORE_H
#define BLAF_CORE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "blaf_grammar.h"

// --- CONSTANTS ---
#define MAX_WORD_LEN      64
#define MAX_CONCEPTS      4096
#define MAX_FACT_POOL     16384
#define MAX_SENTENCE_LEN  512
#define REASONING_SLOTS   16
#define MAX_INPUT_LEN     512
#define MAX_TOKENS        64
#define MAX_RESPONSE_LEN  1024

#define SECTOR_GENERAL    0x00
#define MAX_SCHEMA_DEPTH  16

// --- CORE STRUCTURES ---
typedef struct {
    uint8_t  type_tag   : 2;
    uint8_t  class_tag  : 4;
    uint8_t  integrity  : 3;
    uint8_t  sector     : 8;
    uint8_t  input_pin  : 8;
    uint8_t  output_pin : 8;
    uint32_t payload    : 31;
} ConceptBlock;

typedef enum {
    TRUST_INFERRED = 1, // Local heuristic (Pattern-based)
    TRUST_VERIFIED = 7  // Online/API (TextBlob/spaCy)
} TagTrust;

typedef struct {
    uint32_t subject_hash;
    uint8_t  predicate;
    uint32_t object_hash;
    uint8_t weight;
} Fact;

typedef struct {
    char     word[64];      // Or MAX_WORD_LEN
    ConceptBlock block;
    uint8_t  sector;
    uint8_t  trust;
    
    // Grammar Fields
    uint8_t  primary_class; 
    uint16_t noun_hits;     
    uint16_t verb_hits;     
    uint16_t adj_hits;      

    // Pointer Fields
    char     *summary;      // Keep this!
    uint32_t summary_len;
    uint32_t fact_start_index;
    uint8_t  fact_count;
    int8_t   sentiment;
    uint16_t saliency;
    uint32_t last_seen;
} ConceptEntry;


typedef struct {
    uint64_t register_bits;
    uint8_t  active_sector;
    uint8_t  superposition;
    uint8_t  depth;
} ContextRegister;

typedef struct {
    ConceptBlock parts[4];
    uint8_t      used;
    uint8_t      confidence;
} ReasoningSlot;

typedef struct {
    ReasoningSlot slots[REASONING_SLOTS];
    uint8_t       active_slots;
} ReasoningWindow;

typedef struct {
    uint8_t dominant_sector;
    uint8_t dominant_class;
    uint8_t has_noun;
    uint8_t has_verb;
    uint8_t avg_confidence;
} ReasoningSummary;

typedef struct {
    char    text[MAX_RESPONSE_LEN];
    uint8_t sector;
    uint8_t confidence;
    uint8_t intent_tag;
} ResponseFrame;


typedef struct {
    ConceptBlock *blocks[MAX_SENTENCE_LEN];
    char          words[MAX_SENTENCE_LEN][MAX_WORD_LEN];
    int           length;
    uint8_t       valid;
    int           break_at;
} SentenceBuffer;

typedef struct {
    char    tokens[MAX_TOKENS][MAX_WORD_LEN];
    int     count;
} TokenList;

// --- GLOBAL VARIABLES (EXTERN) ---
extern ConceptEntry    concept_table[MAX_CONCEPTS];
extern int             concept_count;
extern Fact            fact_pool[MAX_FACT_POOL];
extern int             fact_count;
extern ContextRegister ctx;
extern ReasoningWindow reasoning;
extern SentenceBuffer  sentence;

// --- PROTOTYPES (Declarations Only) ---
uint32_t hash_word(const char *word);
int      add_concept(const char *word, uint8_t type, uint8_t class, uint8_t integrity, uint8_t sector, uint8_t in_pin, uint8_t out_pin);
int      find_concept_index(const char *word, uint8_t sector);
void     tokenize(const char *input, TokenList *tl);
void extract_facts_from_text(const char *text, const char *primary_subject);
uint8_t map_verb_to_id(const char *verb);
void learn_from_sentence(const char *sentence);
const char* get_word_from_hash(uint32_t hash);

int  check_internet_connection(void);
int  tokenize_with_online_teacher(const char *sentence, Token *tokens);
int  tokenize_and_tag_local(const char *sentence, Token *tokens);
void sync_concept_pos_with_web(const char *word, int class_tag);
void update_word_pos_weight(const char *word, int class_tag);
void syntax_record_sentence(const char *sentence);
void learn_complex_fact_internal(Token *tokens, int count, const char *subject);

void store_fact_in_pool(uint32_t s, uint32_t v, uint32_t o);
void syntax_init(void);



#endif
