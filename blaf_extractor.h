/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_extractor.h                                    ║
 * ║  Answer Extraction & Subtopic Detection                     ║
 * ║                                                              ║
 * ║  Problem: Wikipedia returns 200 words. User asked one       ║
 * ║  thing. This finds and returns only the relevant part.      ║
 * ║                                                              ║
 * ║  Strategy:                                                   ║
 * ║    1. Split source into sentences                           ║
 * ║    2. Score each sentence against intent + subject          ║
 * ║    3. Return highest-scoring sentence(s)                    ║
 * ║    4. Map every new word found in the answer                ║
 * ║                                                              ║
 * ║  "how far is London from Paris"                             ║
 * ║    Source: full Wikipedia article on London                 ║
 * ║    Extracted: "London is 343 km from Paris by air."         ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_EXTRACTOR_H
#define BLAF_EXTRACTOR_H

#include "blaf_intent.h"
#include "blaf_grammar.h"

/* ═══════════════════════════════════════════════════════════════
   SENTENCE SCORE
   ═══════════════════════════════════════════════════════════════ */

#define MAX_SENTENCES     128
#define MAX_SENTENCE_LEN    512
#define MAX_EXTRACT_LEN   1024

typedef struct {
    char  text[MAX_SENTENCE_LEN];
    float score;
    int   position;     /* index in source — earlier = slightly preferred */
} ScoredSentence;

/* ═══════════════════════════════════════════════════════════════
   EXTRACTION RESULT
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char  answer[MAX_EXTRACT_LEN];   /* extracted answer text           */
    char  sentences[3][MAX_SENTENCE_LEN]; /* up to 3 supporting sentences */
    int   sentence_count;
    float confidence;                /* 0.0-1.0 how well we matched      */
    int   found_numeric;             /* 1 if a number was in the answer  */
    char  numeric_value[64];         /* e.g. "343" or "1.4 billion"      */
    char  numeric_unit[32];          /* e.g. "km", "years", "$"          */
} ExtractionResult;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void extractor_init(void);


int ingest_file_as_concept(const char *filepath, const char *concept_name);
int ingest_directory(const char *dirpath);


/*
 * extract_answer()
 * Given raw source text, intent, and subject — extract
 * the most relevant sentence(s) as the actual answer.
 *
 * Example:
 *   ExtractionResult r = extract_answer(wiki_text,
 *                                        &intent, "london");
 *   // r.answer = "London is 343 km from Paris by air."
 *   // r.found_numeric = 1, r.numeric_value = "343", r.unit = "km"
 */
ExtractionResult extract_answer(const char *source,
                                 const ResolvedIntent *intent,
                                 const char *subject);

/*
 * score_sentence()
 * Score a single sentence for relevance to the intent + subject.
 * Used internally but exposed for debugging.
 *
 * Scoring factors:
 *   +3.0  contains subject word(s)
 *   +2.0  contains intent keyword (distance/age/location etc.)
 *   +1.5  contains a number (for numeric intents)
 *   +1.0  contains unit word (km, years, $, etc.)
 *   -0.1  per position (earlier sentences slightly preferred)
 *   +0.5  sentence is short and direct (<20 words)
 */
float score_sentence(const char *sentence,
                     const ResolvedIntent *intent,
                     const char *subject);

/*
 * map_answer_words()
 * After extraction, tokenize the answer and register every
 * unknown word into the concept table.
 * This is how BLAF learns from its own sources — every
 * answer it gives teaches it new words.
 *
 * Returns number of new words mapped.
 */
int map_answer_words(const char *answer_text,
                     uint8_t sector_hint);

/*
 * extract_subtopics()
 * Given source text, identify distinct subtopics mentioned.
 * Returns comma-separated list of detected subtopic keywords.
 *
 * Example: Wikipedia article on "egg"
 *   → "embryo, yolk, shell, bird, reptile, incubation"
 */
void extract_subtopics(const char *source,
                       char *out, int outlen);

                       

#endif /* BLAF_EXTRACTOR_H */
