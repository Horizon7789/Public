/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_llm.h / blaf_llm.c                            ║
 * ║  LLM Feed & Mapping Endpoints                               ║
 * ║                                                              ║
 * ║  Endpoints for ingesting knowledge from LLMs (GPT, Claude,  ║
 * ║  Gemini etc.) and mapping their outputs into BLAF's         ║
 * ║  bit-aligned concept table.                                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_LLM_H
#define BLAF_LLM_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   LLM PROVIDER IDs
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    LLM_NONE       = 0,
    LLM_OPENAI     = 1,   /* GPT-4, GPT-4o etc.   */
    LLM_ANTHROPIC  = 2,   /* Claude                */
    LLM_GOOGLE     = 3,   /* Gemini                */
    LLM_MISTRAL    = 4,   /* Mistral               */
    LLM_LOCAL      = 5,   /* Local model via API   */
} LLMProvider;

/* ═══════════════════════════════════════════════════════════════
   LLM CONFIG
   Set once — used for all LLM calls
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    LLMProvider provider;
    char        api_key[256];
    char        model[64];        /* e.g. "gpt-4o", "claude-3-5-sonnet" */
    char        endpoint[256];    /* custom endpoint URL if LLM_LOCAL   */
    int         max_tokens;
    float       temperature;      /* 0.0 = deterministic                */
} LLMConfig;

/* ═══════════════════════════════════════════════════════════════
   LLM RESPONSE
   ═══════════════════════════════════════════════════════════════ */

#define MAX_LLM_RESPONSE 8192

typedef struct {
    char    text[MAX_LLM_RESPONSE];
    int     tokens_used;
    int     success;         /* 1 = OK, 0 = error */
    char    error[256];
} LLMResponse;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

/*
 * llm_init()
 * Set the LLM config. Call once before any LLM operations.
 *
 * Example:
 *   llm_init(LLM_OPENAI, "sk-...", "gpt-4o");
 */
void llm_init(LLMProvider provider, const char *api_key, const char *model);

/*
 * llm_set_endpoint()
 * Override endpoint URL (for local models or proxies).
 */
void llm_set_endpoint(const char *url);

/*
 * llm_query()
 * Send a raw prompt to the configured LLM.
 * Returns LLMResponse with text + success flag.
 *
 * Example:
 *   LLMResponse r = llm_query("What is blockchain in one sentence?");
 */
LLMResponse llm_query(const char *prompt);

/*
 * llm_map_concept()
 * Ask the LLM to classify a word and auto-map it into BLAF.
 * The LLM returns JSON with: class, sector, input_pin, output_pin.
 * BLAF parses the JSON and calls map_word() automatically.
 *
 * Returns 0 on success, -1 on failure.
 *
 * Example:
 *   llm_map_concept("reentrancy");
 *   // LLM returns: {"class":"NOUN","sector":"SECURITY","trust":"VERIFIED"}
 *   // BLAF maps it automatically
 */
int llm_map_concept(const char *word);

/*
 * llm_map_batch()
 * Send a list of unmapped words to the LLM for bulk classification.
 * Returns number of words successfully mapped.
 *
 * Example:
 *   const char *words[] = {"blockchain","merkle","nonce","consensus"};
 *   llm_map_batch(words, 4);
 */
int llm_map_batch(const char **words, int count);

/*
 * llm_answer_question()
 * Route a full question to the LLM when BLAF can't answer locally.
 * Parses the response and also maps any new concepts found in it.
 *
 * Returns LLMResponse with the answer.
 *
 * Example:
 *   LLMResponse r = llm_answer_question("Who is Bill Gates?");
 */
LLMResponse llm_answer_question(const char *question);

/*
 * llm_extract_facts()
 * Given a Wikipedia/web text blob, ask the LLM to extract
 * key-value facts and map them as concept relationships.
 *
 * Returns number of facts extracted and mapped.
 *
 * Example:
 *   llm_extract_facts("Bill Gates", wikipedia_text);
 */
int llm_extract_facts(const char *subject, const char *text);

/*
 * llm_status()
 * Print current LLM config and connection status.
 */
void llm_status(void);

#endif /* BLAF_LLM_H */
