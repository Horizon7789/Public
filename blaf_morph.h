/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_morph.h                                        ║
 * ║  Morphology Engine                                          ║
 * ║                                                              ║
 * ║  Three systems:                                             ║
 * ║                                                              ║
 * ║  1. LEMMATIZER                                              ║
 * ║     Reduces inflected forms to base form.                   ║
 * ║     "asteroids" → "asteroid"                                ║
 * ║     "running"   → "run"                                     ║
 * ║     "walked"    → "walk"                                    ║
 * ║     "children"  → "child"                                   ║
 * ║     "was"       → "be"       (irregular)                    ║
 * ║                                                              ║
 * ║  2. VARIANT REGISTRATION                                    ║
 * ║     When a concept is learned, all its morphological        ║
 * ║     variants are registered pointing to the base form.      ║
 * ║     Learn "asteroid" → also register "asteroids"            ║
 * ║     Learn "run"      → also register "runs","running","ran" ║
 * ║                                                              ║
 * ║  3. AUTOCORRECT                                             ║
 * ║     Levenshtein edit distance against known concepts.       ║
 * ║     "asteorid" → "asteroid"  (distance 2)                   ║
 * ║     "blockchan" → "blockchain" (distance 2)                 ║
 * ║     Only triggers if no exact match exists.                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_MORPH_H
#define BLAF_MORPH_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   LEMMA RESULT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char base[64];          /* base/lemma form                  */
    char original[64];      /* what was passed in               */
    int  was_inflected;     /* 1 if base != original            */
    int  was_corrected;     /* 1 if spelling was fixed          */
    char correction_from[64]; /* original misspelling           */
} MorphResult;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void morph_init(void);

/*
 * morph_lemmatize()
 * Reduce a word to its base form using:
 *   1. Irregular form table (mice→mouse, ran→run, was→be)
 *   2. Suffix stripping rules (dogs→dog, running→run, walked→walk)
 *
 * Does NOT require concept table — purely rule-based.
 * Fast enough to call on every token.
 *
 * Example:
 *   char base[64];
 *   morph_lemmatize("asteroids", base, sizeof(base));
 *   // base = "asteroid"
 *
 *   morph_lemmatize("running", base, sizeof(base));
 *   // base = "run"
 *
 *   morph_lemmatize("children", base, sizeof(base));
 *   // base = "child"
 */
void morph_lemmatize(const char *word, char *out, int outlen);

/*
 * morph_process()
 * Full morphology pass on a single word:
 *   1. Lemmatize
 *   2. Autocorrect if no concept match and edit distance <= 2
 *
 * Returns MorphResult with base form and correction flags.
 * Use this in the query pipeline before concept lookup.
 */
MorphResult morph_process(const char *word);

/*
 * morph_normalize_query()
 * Apply morphology to an entire input string.
 * Lemmatizes and corrects each token, rebuilds sentence.
 *
 * Example:
 *   char out[512];
 *   morph_normalize_query("what are asteroids", out, sizeof(out));
 *   // out = "what are asteroid"  (plural → singular)
 *
 *   morph_normalize_query("what is blockchan", out, sizeof(out));
 *   // out = "what is blockchain"  (corrected)
 */
void morph_normalize_query(const char *input, char *out, int outlen);

/*
 * morph_register_variants()
 * Given a base word just learned, register all its likely
 * morphological variants into the concept table pointing
 * to the same sector/class as the base.
 *
 * Example: morph_register_variants("asteroid", sector, class)
 *   Registers: "asteroids" (plural)
 *
 * Example: morph_register_variants("run", sector, class)
 *   Registers: "runs", "running", "ran", "runner"
 *
 * Example: morph_register_variants("happy", sector, class)
 *   Registers: "happily", "happier", "happiest", "happiness"
 */
void morph_register_variants(const char *base_word,
                              uint8_t sector, uint8_t class_tag);

/*
 * morph_autocorrect()
 * Find the closest known word to the input using edit distance.
 * Only returns a match if distance <= max_distance and the
 * match confidence is above threshold.
 *
 * Returns 1 if correction found, 0 if not.
 * Writes corrected word to out.
 *
 * Example:
 *   char corrected[64];
 *   if (morph_autocorrect("asteorid", corrected, 64, 2))
 *       // corrected = "asteroid"
 */
int morph_autocorrect(const char *word, char *out, int outlen,
                      int max_distance);

/*
 * morph_edit_distance()
 * Levenshtein distance between two strings.
 * Exposed for debugging/testing.
 */
int morph_edit_distance(const char *a, const char *b);

/*
 * morph_plural()  morph_singular()
 * Convert between singular and plural forms.
 *
 * Example:
 *   char p[64];
 *   morph_plural("asteroid", p, sizeof(p));   // "asteroids"
 *   morph_singular("asteroids", p, sizeof(p)); // "asteroid"
 *   morph_singular("mice", p, sizeof(p));      // "mouse"
 */
void morph_plural  (const char *word, char *out, int outlen);
void morph_singular(const char *word, char *out, int outlen);

/*
 * morph_status()
 * Print morph engine stats.
 */
void morph_status(void);

#endif /* BLAF_MORPH_H */
