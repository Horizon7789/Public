/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_intent.h                                       ║
 * ║  Compound Pattern Intent Detector                           ║
 * ║                                                              ║
 * ║  Maps compound phrases to structured intent types:          ║
 * ║    "how far"   → INTENT_DISTANCE                            ║
 * ║    "how long"  → INTENT_DURATION or INTENT_LENGTH           ║
 * ║    "how much"  → INTENT_QUANTITY                            ║
 * ║    "how many"  → INTENT_COUNT                               ║
 * ║    "why does"  → INTENT_CAUSE                               ║
 * ║    "tell me"   → INTENT_EXPLAIN                             ║
 * ║    "compare"   → INTENT_COMPARE                             ║
 * ║                                                              ║
 * ║  Falls back to vector similarity when pattern not found.    ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_INTENT_H
#define BLAF_INTENT_H

#include <stdint.h>
#include "blaf_grammar.h"

/* ═══════════════════════════════════════════════════════════════
   INTENT TYPES
   Extends QuestionType with semantic resolution layer
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    /* Spatial */
    INTENT_LOCATION    = 100,  /* where is X                        */
    INTENT_DISTANCE    = 101,  /* how far is X from Y               */
    INTENT_DIRECTION   = 102,  /* which way to X / how to get to X  */

    /* Temporal */
    INTENT_DURATION    = 110,  /* how long does X take              */
    INTENT_DATE        = 111,  /* when was X / what date is X       */
    INTENT_AGE         = 112,  /* how old is X                      */
    INTENT_FREQUENCY   = 113,  /* how often does X                  */

    /* Quantitative */
    INTENT_QUANTITY    = 120,  /* how much X                        */
    INTENT_COUNT       = 121,  /* how many X                        */
    INTENT_SIZE        = 122,  /* how big / how large / how small   */
    INTENT_PRICE       = 123,  /* how much does X cost / price of X */
    INTENT_SPEED       = 124,  /* how fast is X                     */
    INTENT_TEMPERATURE = 125,  /* how hot / how cold / temperature  */

    /* Identity / Definition */
    INTENT_IDENTITY    = 130,  /* who is X                          */
    INTENT_DEFINITION  = 131,  /* what is X                         */
    INTENT_DESCRIPTION = 132,  /* describe X / tell me about X      */
    INTENT_EXAMPLE     = 133,  /* give me an example of X           */

    /* Relational */
    INTENT_CAUSE       = 140,  /* why does X / reason for X         */
    INTENT_EFFECT      = 141,  /* what happens when X               */
    INTENT_COMPARE     = 142,  /* X vs Y / difference between X Y   */
    INTENT_RELATION    = 143,  /* how does X relate to Y            */

    /* Capability / Process */
    INTENT_CAPABILITY  = 150,  /* can X do Y / is X able to         */
    INTENT_PROCESS     = 151,  /* how does X work / how to X        */
    INTENT_METHOD      = 152,  /* how do you X / steps to X         */

    /* Fallback */
    INTENT_GENERAL     = 199,  /* couldn't resolve further          */
    INTENT_UNKNOWN     = 200,  /* no match at all                   */
} IntentType;

/* ═══════════════════════════════════════════════════════════════
   RESOLVED INTENT
   Full result from intent detection
   ═══════════════════════════════════════════════════════════════ */

#define MAX_MODIFIER_LEN  64

typedef struct {
    IntentType  type;
    char        modifier[MAX_MODIFIER_LEN]; /* "far", "long", "many"... */
    char        subject[128];               /* primary entity           */
    char        object[128];                /* secondary entity (X vs Y)*/
    float       confidence;                 /* 0.0 - 1.0                */
    int         used_vectors;              /* 1 if vector fallback used */
} ResolvedIntent;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void intent_init(void);

/*
 * resolve_intent()
 * Takes a ParsedQuery and returns a ResolvedIntent with full
 * semantic decomposition.
 *
 * First tries the pattern table (O(1) lookup).
 * Falls back to vector similarity if pattern not found.
 *
 * Example:
 *   ParsedQuery q = analyze_question("how far is London from Paris");
 *   ResolvedIntent i = resolve_intent(&q);
 *   // i.type     = INTENT_DISTANCE
 *   // i.modifier = "far"
 *   // i.subject  = "London"
 *   // i.object   = "Paris"
 */
ResolvedIntent resolve_intent(const ParsedQuery *q);

/*
 * intent_name()
 * Human-readable name for an IntentType.
 */
const char* intent_name(IntentType t);

/*
 * intent_needs_numeric_answer()
 * Returns 1 if this intent expects a number in the answer.
 * Used by blaf_compose to shape the web query appropriately.
 */
int intent_needs_numeric_answer(IntentType t);

/*
 * intent_sector_hint()
 * Returns the most likely sector for this intent.
 * Feeds into BLAF's sector context.
 */
uint8_t intent_sector_hint(IntentType t);

#endif /* BLAF_INTENT_H */
