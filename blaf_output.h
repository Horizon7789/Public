/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_output.h                                       ║
 * ║  Sentence Formation & Response Generation Engine            ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_OUTPUT_H
#define BLAF_OUTPUT_H

#include <stdint.h>
#include "blaf_grammar.h"
#include "blaf_instructions.h"

#define MAX_RESPONSE_WORDS 2048
#define MAX_RESPONSE_CHARS 8192

/* ═══════════════════════════════════════════════════════════════
   RESPONSE STRUCT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char    body[MAX_RESPONSE_CHARS];   /* The full response text       */
    char    context[MAX_RESPONSE_CHARS];/* Optional context paragraph   */
    uint8_t has_context;
    uint8_t confidence;                 /* 0-255                        */
    uint8_t sector;
    int     word_count;
} BlafResponse;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void output_init(void);

/*
 * build_answer()
 * Main output function. Given a ParsedQuery + web facts string,
 * forms a complete natural language answer respecting all
 * active instructions (short/long, formal/casual, etc.)
 *
 * web_facts = raw text fetched from web (NULL if no web lookup)
 * Returns a BlafResponse with body + optional context.
 */
BlafResponse build_answer(const ParsedQuery *q, const char *web_facts);

/*
 * build_context()
 * Generates a context paragraph for any subject.
 * Called automatically if INST_CONTEXT_ON is set.
 */
void build_context(const char *subject, uint8_t sector,
                   char *out, int outlen);

/*
 * print_response()
 * Formats and prints a BlafResponse to stdout respecting
 * active instruction flags.
 */
void print_response(const BlafResponse *r);

/*
 * truncate_to_limit()
 * Trims a response to the max_response_words instruction if set.
 */
void truncate_to_limit(char *text, int max_words);

#endif /* BLAF_OUTPUT_H */
