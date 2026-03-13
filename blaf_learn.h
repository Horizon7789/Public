/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_learn.h                                        ║
 * ║  Sentence Learning, POS Tagging, Fact Extraction            ║
 * ║                                                              ║
 * ║  Given a sentence, this module:                             ║
 * ║    1. Tokenizes cleanly (no substring artifacts)            ║
 * ║    2. Tags each token with its part of speech               ║
 * ║    3. Identifies the grammatical subject                    ║
 * ║    4. Extracts full SVO/copula/prepositional facts          ║
 * ║    5. Stores facts as (subject, predicate, object) triples  ║
 * ║    6. Maps every new word into the concept table            ║
 * ║                                                              ║
 * ║  Example:                                                    ║
 * ║    "The brain is the center of the nervous system"          ║
 * ║    → SUBJ: brain                                            ║
 * ║    → FACT: brain [is] center of the nervous system         ║
 * ║    → FACT: brain [located_in] nervous system               ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_LEARN_H
#define BLAF_LEARN_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   POS CLASSES
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    POS_UNKNOWN   = 0,
    POS_NOUN      = 1,
    POS_VERB      = 2,
    POS_AUX_VERB  = 3,   /* is, are, was, were, has, have, will... */
    POS_ADJ       = 4,
    POS_ADV       = 5,
    POS_ARTICLE   = 6,   /* a, an, the */
    POS_PREP      = 7,   /* in, on, of, by, for, with, through... */
    POS_CONJ      = 8,   /* and, or, but, because, although... */
    POS_PRONOUN   = 9,
    POS_NUMERAL   = 10,
    POS_PUNCT     = 11,
    POS_PROPER    = 12,  /* capitalized noun — name, place */
    POS_GERUND    = 14,  /* -ing verb used as noun */
    POS_PARTICLE  = 15,  /* to (infinitive marker), not */
} POSClass;

/* ═══════════════════════════════════════════════════════════════
   TOKEN
   ═══════════════════════════════════════════════════════════════ */

#define MAX_TOKEN_LEN  64
#define MAX_TOKENS_PER_SENT 80

typedef struct {
    char     word[MAX_TOKEN_LEN];
    char     lemma[MAX_TOKEN_LEN];  /* base form: "running" → "run"    */
    POSClass pos;
    uint8_t  trust;                 /* 0=inferred 1=rule 3=dict        */
    int      position;             /* index in sentence               */
    int      is_capitalized;       /* was first letter uppercase?     */
    int      is_sentence_start;   /* first token?                    */
} LearnToken;

/* ═══════════════════════════════════════════════════════════════
   FACT TRIPLE
   Stores subject → predicate → object relationships
   ═══════════════════════════════════════════════════════════════ */

#define MAX_FACT_LEN     256
#define MAX_FACTS_STORED 8192

typedef enum {
    FACT_IS,           /* brain IS center of nervous system    */
    FACT_HAS,          /* brain HAS neurons                    */
    FACT_DOES,         /* heart PUMPS blood                    */
    FACT_LOCATED_IN,   /* London LOCATED_IN England            */
    FACT_PART_OF,      /* yolk PART_OF egg                     */
    FACT_MADE_OF,      /* bone MADE_OF calcium                 */
    FACT_CAUSED_BY,    /* fever CAUSED_BY infection            */
    FACT_USED_FOR,     /* knife USED_FOR cutting               */
    FACT_CREATED_BY,   /* telephone CREATED_BY Bell            */
    FACT_PROPERTY,     /* gold PROPERTY yellow                 */
    FACT_RELATION,     /* brain RELATION nervous_system        */
    FACT_GENERAL,      /* catch-all                            */
} FactType;

typedef struct {
    char     subject[MAX_FACT_LEN];
    char     predicate[64];        /* verb/relation label              */
    char     object[MAX_FACT_LEN]; /* full noun phrase, not one word   */
    FactType type;
    uint8_t  sector;
    float    confidence;
} FactTriple;

/* ═══════════════════════════════════════════════════════════════
   LEARN RESULT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    FactTriple facts[16];
    int        fact_count;
    char       subject[MAX_FACT_LEN];
    int        new_words_mapped;
} LearnResult;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void learn_init(void);

/*
 * learn_sentence()
 * Master function. Takes a raw sentence, returns extracted facts.
 * Also registers all new words into the concept table.
 *
 * sector_hint = likely sector (from intent) — helps classification.
 *
 * Example:
 *   LearnResult r = learn_sentence(
 *       "The brain is the center of the nervous system.",
 *       SECTOR_BIOLOGY);
 *   // r.fact_count = 2
 *   // r.facts[0] = {subject:"brain", predicate:"is",
 *   //               object:"center of the nervous system"}
 *   // r.facts[1] = {subject:"brain", predicate:"located_in",
 *   //               object:"nervous system"}
 */
LearnResult learn_sentence(const char *sentence, uint8_t sector_hint);

/*
 * learn_paragraph()
 * Split paragraph into sentences and learn each one.
 * Returns total facts extracted.
 */
int learn_paragraph(const char *text, uint8_t sector_hint);

/*
 * learn_tag_tokens()
 * POS-tag an already-tokenized list.
 * Exposed for debugging.
 */
int learn_tag_tokens(LearnToken *tokens, int count);

/*
 * learn_find_subject()
 * Given a tagged token list, find the grammatical subject.
 * Returns index of subject token, -1 if not found.
 *
 * Strategy:
 *   1. First PROPER noun before the main verb
 *   2. First NOUN before the main verb
 *   3. First PRONOUN (then resolve from context)
 *   4. Noun after article ("the brain")
 */
int learn_find_subject(LearnToken *tokens, int count);

/*
 * learn_query_facts()
 * Look up stored facts for a subject.
 * Returns number of facts found, writes to out[].
 *
 * Example:
 *   FactTriple out[10];
 *   int n = learn_query_facts("brain", out, 10);
 */
int learn_query_facts(const char *subject,
                       FactTriple *out, int max_out);

/*
 * learn_print_facts()
 * Print all facts for a subject in human-readable form.
 */
void learn_print_facts(const char *subject);

/*
 * learn_save() / learn_load()
 * Persist fact triple store to disk.
 */
void learn_save(const char *path);
void learn_load(const char *path);

/*
 * pos_name()
 * Human-readable POS name.
 */
const char* pos_name(POSClass pos);

/*
 * fact_type_name()
 */
const char* fact_type_name(FactType t);

#endif /* BLAF_LEARN_H */
