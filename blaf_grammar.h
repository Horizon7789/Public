/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_grammar.h                                      ║
 * ║  Quantifiable POS fast-lookup + Question Detection          ║
 * ║                                                              ║
 * ║  Pronouns, articles, and aux verbs are stored in their own  ║
 * ║  dedicated hash tables for O(1) lookup — bypassing the main ║
 * ║  concept table entirely.                                     ║
 * ║                                                              ║
 * ║  Question detection resolves:                               ║
 * ║    WHO IS / WHAT IS / WHERE IS / WHEN IS / WHY / HOW        ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_GRAMMAR_H
#define BLAF_GRAMMAR_H

#include <stdint.h>
#include <string.h> // Fixes the strncmp error

// Define these FIRST so the mapping function knows what they are
#define CLASS_GENERAL   0
#define CLASS_NOUN      1
#define CLASS_VERB      2
#define CLASS_ADJ       3
#define CLASS_ARTICLE   4
#define CLASS_PRONOUN   5
#define CLASS_AUX_VERB  6
#define CLASS_PREP      7
#define CLASS_CONJ      8
#define CLASS_PUNCTUATION 9
#define CLASS_ADV         10  
#define CLASS_NUMERAL     11

//Define the Token struct here
typedef struct {
    char word[64];
    int class;
    int trust;
} Token;

// Now the mapping function will work
static inline int map_penn_to_blaf(const char *penn_tag) {
    if (!strncmp(penn_tag, "NN", 2)) return CLASS_NOUN;
    if (!strncmp(penn_tag, "VB", 2)) return CLASS_VERB;
    if (!strncmp(penn_tag, "JJ", 2)) return CLASS_ADJ;
    return CLASS_GENERAL;
}


/* ═══════════════════════════════════════════════════════════════
   QUESTION TYPES
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    Q_NONE      = 0,   /* Not a question                          */
    Q_WHO_IS    = 1,   /* "who is X"   → person/entity lookup     */
    Q_WHAT_IS   = 2,   /* "what is X"  → definition/concept       */
    Q_WHERE_IS  = 3,   /* "where is X" → location lookup          */
    Q_WHEN_IS   = 4,   /* "when is X"  → time/event lookup        */
    Q_WHY       = 5,   /* "why ..."    → cause/reason             */
    Q_HOW       = 6,   /* "how ..."    → process/method           */
    Q_IS        = 7,   /* "is X Y"     → boolean/comparison       */
    Q_DO_DOES   = 8,   /* "does X do Y" → action query            */
    Q_CAN       = 9,   /* "can X do Y" → capability query         */
    Q_STATEMENT = 10,  /* Declarative statement                   */
    Q_COMMAND   = 11,  /* Imperative command ("run X", "find Y")  */
} QuestionType;

/* ═══════════════════════════════════════════════════════════════
   PARSED QUERY
   Result of question analysis — tells the engine what to do
   ═══════════════════════════════════════════════════════════════ */

#define MAX_SUBJECT_LEN  128
#define MAX_PREDICATE_LEN 256

typedef struct {
    QuestionType type;
    char         subject[MAX_SUBJECT_LEN];    /* "Bill Gates", "London"  */
    char         predicate[MAX_PREDICATE_LEN];/* "is a programmer"       */
    char         raw_input[512];              /* original user input     */
    uint8_t      needs_web;                   /* 1 = requires web lookup */
    uint8_t      is_question;                 /* 1 = ends in ? or starts with Q word */
} ParsedQuery;

/* ═══════════════════════════════════════════════════════════════
   POS FAST-LOOKUP TABLES
   These bypass the main concept table for common function words.
   Loaded once at init, looked up in O(1) via small hash.
   ═══════════════════════════════════════════════════════════════ */

/* Entry in a POS fast table */
typedef struct {
    char    word[24];
    uint8_t class_tag;
    uint8_t input_pin;
    uint8_t output_pin;
    uint8_t subtype;
    uint8_t primary_class;   // e.g., CLASS_NOUN
    uint8_t secondary_class; // e.g., CLASS_VERB
    uint8_t current_context; // The class it is acting as right now
} POSEntry;


/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

/*
 * grammar_init()
 * Loads all POS fast-lookup tables.
 * Must be called once at startup.
 */
void grammar_init(void);

/*
 * is_article()   — "the", "a", "an"
 * is_pronoun()   — "i", "you", "he", "she", "it", "they", "we"
 * is_aux_verb()  — "is", "are", "was", "were", "will", "can", "do", "does"
 * is_prep()      — "in", "on", "at", "by", "for", "with", "from", "to"
 * is_conj()      — "and", "or", "but", "because", "if", "so"
 * is_question_word() — "who", "what", "where", "when", "why", "how"
 *
 * All return 1 if matched, 0 if not. O(1) lookup.
 */
int is_article      (const char *word);
int is_pronoun      (const char *word);
int is_aux_verb     (const char *word);
int is_prep         (const char *word);
int is_conj         (const char *word);
int is_question_word(const char *word);

/*
 * get_pos_entry()
 * Returns the full POSEntry for a function word, or NULL if not found.
 * Checks all POS tables in priority order.
 */
const POSEntry* get_pos_entry(const char *word);

/*
 * analyze_question()
 * Takes a raw input string and returns a fully parsed ParsedQuery.
 * Detects question type, extracts subject and predicate.
 * Sets needs_web=1 if subject is an unknown proper noun.
 *
 * Example:
 *   ParsedQuery q = analyze_question("who is Bill Gates");
 *   // q.type    = Q_WHO_IS
 *   // q.subject = "Bill Gates"
 *   // q.needs_web = 1
 */
ParsedQuery analyze_question(const char *input);

/*
 * question_type_name()
 * Returns a human-readable name for a QuestionType.
 */
const char* question_type_name(QuestionType t);

/*
 * grammar_dump()
 * Print all POS tables — useful for debugging.
 */
void grammar_dump(void);

#endif /* BLAF_GRAMMAR_H */
