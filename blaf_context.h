/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_context.h                                      ║
 * ║  Context Mapping, Conversation History, Word Relations      ║
 * ║                                                              ║
 * ║  Three systems:                                             ║
 * ║                                                              ║
 * ║  1. CO-OCCURRENCE MAP                                       ║
 * ║     Words that appear together are related. If "egg" and    ║
 * ║     "yolk" co-occur in 5 answers, they share context.       ║
 * ║     Used to: disambiguate sectors, improve word choice,     ║
 * ║     suggest related queries.                                ║
 * ║                                                              ║
 * ║  2. CONVERSATION HISTORY                                    ║
 * ║     Last N turns stored. Enables:                           ║
 * ║     - "where is it?" → resolves "it" to last subject        ║
 * ║     - "tell me more" → continues last topic                 ║
 * ║     - "how far is that from here?" → "that" = last topic    ║
 * ║                                                              ║
 * ║  3. WORD CHOICE MAP                                         ║
 * ║     Synonym awareness + register variation.                 ║
 * ║     "big" ↔ "large" ↔ "enormous" (informal→formal scale)   ║
 * ║     Used to improve response phrasing by register.          ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_CONTEXT_H
#define BLAF_CONTEXT_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   CO-OCCURRENCE MAP
   ═══════════════════════════════════════════════════════════════ */

#define COOC_MAX_PAIRS  4096
#define COOC_WORD_LEN   48

typedef struct {
    char    word_a[COOC_WORD_LEN];
    char    word_b[COOC_WORD_LEN];
    uint8_t sector;    /* sector both words appeared in  */
    int     count;     /* times they co-occurred         */
} CoOccurrence;

/* ═══════════════════════════════════════════════════════════════
   CONVERSATION TURN
   ═══════════════════════════════════════════════════════════════ */

#define HISTORY_MAX_TURNS   20
#define HISTORY_MAX_LEN    256

typedef struct {
    char    input[HISTORY_MAX_LEN];    /* what user typed         */
    char    subject[96];               /* extracted subject        */
    char    response[HISTORY_MAX_LEN]; /* first line of response  */
    uint8_t sector;
    int     turn_id;
} ConversationTurn;

/* ═══════════════════════════════════════════════════════════════
   SYNONYM ENTRY
   ═══════════════════════════════════════════════════════════════ */

#define SYN_MAX_WORDS  8
#define SYN_WORD_LEN   32

typedef enum {
    REG_ANY      = 0,
    REG_INFORMAL = 1,
    REG_NEUTRAL  = 2,
    REG_FORMAL   = 3,
    REG_TECHNICAL= 4,
} Register;

typedef struct {
    char     words[SYN_MAX_WORDS][SYN_WORD_LEN]; /* synonym group     */
    Register registers[SYN_MAX_WORDS];            /* register of each  */
    int      count;
} SynonymGroup;

/* ═══════════════════════════════════════════════════════════════
   PRONOUN RESOLUTION RESULT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char    resolved[96];   /* what pronoun refers to   */
    int     found;          /* 1 if resolved, 0 if not  */
    int     turns_back;     /* how many turns ago it appeared */
} PronounResolution;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void context_init(void);

/* ── Co-occurrence ── */

/*
 * context_add_cooccurrence()
 * Record that two words appeared together in the same answer.
 * Call this for every pair of content words in an answer.
 *
 * Example: answer = "The egg yolk contains the embryo"
 *   context_add_cooccurrence("egg", "yolk", SECTOR_BIOLOGY);
 *   context_add_cooccurrence("egg", "embryo", SECTOR_BIOLOGY);
 *   context_add_cooccurrence("yolk", "embryo", SECTOR_BIOLOGY);
 */
void context_add_cooccurrence(const char *word_a, const char *word_b,
                               uint8_t sector);

/*
 * context_add_sentence()
 * Convenience — extract all pairs from a sentence automatically.
 * Skips stopwords. Registers all content-word pairs.
 */
void context_add_sentence(const char *sentence, uint8_t sector);

/*
 * context_get_related()
 * Get the top N words most often seen with the given word.
 * Returns number of results filled.
 *
 * Example:
 *   char related[5][48];
 *   int n = context_get_related("egg", SECTOR_GENERAL, related, 5);
 *   // related = {"yolk", "embryo", "shell", "bird", "hatch"}
 */
int context_get_related(const char *word, uint8_t sector,
                         char out[][COOC_WORD_LEN], int max_results);

/*
 * context_infer_sector()
 * Given a word, look at all its co-occurring partners and
 * return the most common sector among them.
 * Better than a single-word sector guess.
 *
 * Example: "python" co-occurs with "code","function","loop" → SECTOR_ICT
 *          not with "snake","reptile" → not SECTOR_BIOLOGY
 */
uint8_t context_infer_sector(const char *word);

/* ── Conversation history ── */

/*
 * context_add_turn()
 * Record a conversation turn.
 */
void context_add_turn(const char *input, const char *subject,
                       const char *response, uint8_t sector);

/*
 * context_last_subject()
 * Returns the subject from the most recent turn (or "" if none).
 */
const char* context_last_subject(void);

/*
 * context_last_response()
 * Returns the response from the most recent turn.
 */
const char* context_last_response(void);

/*
 * context_resolve_reference()
 * Resolve pronouns and references like "it", "that", "there",
 * "he", "she", "they", "this" to their actual referents
 * from conversation history.
 *
 * Example: "where is it?" after "who is Bill Gates"
 *   → PronounResolution { resolved="Bill Gates", found=1 }
 *
 * Example: "how far is that from Paris?"
 *   → PronounResolution { resolved="London", found=1 }
 *     (London was discussed 2 turns back)
 */
PronounResolution context_resolve_reference(const char *input);

/*
 * context_enrich_query()
 * If a query is missing a subject (bare pronoun or "tell me more"),
 * fill in the subject from history.
 *
 * Example: input = "tell me more" → "tell me more about London"
 * Example: input = "how old is he" → "how old is Bill Gates"
 *
 * Writes enriched query to out. Returns 1 if enrichment happened.
 */
int context_enrich_query(const char *input, char *out, int outlen);

/* ── Word choice ── */

/*
 * context_get_synonym()
 * Return a synonym of the given word at the requested register.
 * Falls back to original word if no synonym found.
 *
 * Example:
 *   context_get_synonym("big", REG_FORMAL) → "substantial"
 *   context_get_synonym("big", REG_INFORMAL) → "huge"
 *   context_get_synonym("big", REG_TECHNICAL) → "large-scale"
 */
const char* context_get_synonym(const char *word, Register reg);

/*
 * context_vary_word()
 * Returns a varied synonym on each call to avoid repetition.
 * Uses turn count to rotate through synonyms.
 */
const char* context_vary_word(const char *word, int turn);

/* ── Status ── */
void context_save(void);
void context_load(void);
void context_status(void);

#endif /* BLAF_CONTEXT_H */
