/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_grammar.c                                      ║
 * ║  Quantifiable POS fast-lookup + Question Detection          ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "blaf_grammar.h"
#include "blaf_mapper.h"

/* ═══════════════════════════════════════════════════════════════
   POS FAST-LOOKUP TABLES
   Stored as flat arrays — small enough that linear scan is O(1)
   in practice (max ~40 entries per table).
   ═══════════════════════════════════════════════════════════════ */

/* subtype codes for pronouns */
#define PRN_SUBJECT    0
#define PRN_OBJECT     1
#define PRN_POSSESSIVE 2
#define PRN_REFLEXIVE  3

/* subtype codes for aux verbs */
#define AUX_BE      0   /* is, are, was, were, been, being */
#define AUX_HAVE    1   /* have, has, had */
#define AUX_DO      2   /* do, does, did */
#define AUX_MODAL   3   /* will, would, can, could, shall, should, may, might, must */

static POSEntry articles[] = {
    {"the",  CLASS_ARTICLE, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"a",    CLASS_ARTICLE, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"an",   CLASS_ARTICLE, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"",0,0,0,0}
};

static POSEntry pronouns[] = {
    /* Subject pronouns */
    {"i",        CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"you",      CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"he",       CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"she",      CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"it",       CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"we",       CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"they",     CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"who",      CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    {"what",     CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_SUBJECT},
    /* Object pronouns */
    {"me",       CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_OBJECT},
    {"him",      CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_OBJECT},
    {"her",      CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_OBJECT},
    {"us",       CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_OBJECT},
    {"them",     CLASS_PRONOUN, PIN_ANY, PIN_ANY, PRN_OBJECT},
    /* Possessive */
    {"my",       CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    {"your",     CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    {"his",      CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    {"its",      CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    {"our",      CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    {"their",    CLASS_PRONOUN, PIN_ANY, PIN_NOUN, PRN_POSSESSIVE},
    /* Demonstrative */
    {"this",     CLASS_PRONOUN, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"that",     CLASS_PRONOUN, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"these",    CLASS_PRONOUN, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"those",    CLASS_PRONOUN, PIN_ANY, PIN_NOUN|PIN_ADJ, 0},
    {"",0,0,0,0}
};

static POSEntry aux_verbs[] = {
    /* BE */
    {"is",      CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"are",     CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"was",     CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"were",    CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"been",    CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"being",   CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    {"am",      CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_BE},
    /* HAVE */
    {"have",    CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_HAVE},
    {"has",     CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_HAVE},
    {"had",     CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_HAVE},
    /* DO */
    {"do",      CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_DO},
    {"does",    CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_DO},
    {"did",     CLASS_AUX_VERB, PIN_ANY, PIN_ANY, AUX_DO},
    /* MODAL */
    {"will",    CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"would",   CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"can",     CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"could",   CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"shall",   CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"should",  CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"may",     CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"might",   CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"must",    CLASS_AUX_VERB, PIN_ANY, PIN_VERB, AUX_MODAL},
    {"",0,0,0,0}
};

static POSEntry prepositions[] = {
    {"in",      CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"on",      CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"at",      CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"by",      CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"for",     CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_VERB|PIN_ARTICLE, 0},
    {"with",    CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"from",    CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"to",      CLASS_PREP, PIN_ANY, PIN_VERB|PIN_NOUN, 0},
    {"of",      CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"about",   CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"into",    CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"through", CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"between", CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"under",   CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"over",    CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"after",   CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"before",  CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"during",  CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"without", CLASS_PREP, PIN_ANY, PIN_NOUN|PIN_ARTICLE, 0},
    {"",0,0,0,0}
};

static POSEntry conjunctions[] = {
    {"and",     CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"or",      CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"but",     CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"because", CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"if",      CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"so",      CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"yet",     CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"nor",     CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"although",CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"though",  CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"while",   CLASS_CONJ, PIN_ANY, PIN_ANY, 0},
    {"",0,0,0,0}
};

/* Question words — stored separately for fast detection */
typedef struct { char word[16]; QuestionType qtype; } QWord;
static QWord question_words[] = {
    {"who",   Q_WHO_IS},
    {"what",  Q_WHAT_IS},
    {"where", Q_WHERE_IS},
    {"when",  Q_WHEN_IS},
    {"why",   Q_WHY},
    {"how",   Q_HOW},
    {"",      Q_NONE}
};

static int g_grammar_init = 0;

/* ═══════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n-1 && src[i]; i++) dst[i] = tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static int table_lookup(POSEntry *table, const char *word) {
    for (int i = 0; table[i].word[0]; i++)
        if (strcmp(table[i].word, word) == 0) return i;
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
   INIT
   ═══════════════════════════════════════════════════════════════ */

void grammar_init(void) {
    if (g_grammar_init) return;
    g_grammar_init = 1;

    int art=0, prn=0, aux=0, prp=0, cnj=0;
    for (int i = 0; articles[i].word[0];     i++) art++;
    for (int i = 0; pronouns[i].word[0];     i++) prn++;
    for (int i = 0; aux_verbs[i].word[0];    i++) aux++;
    for (int i = 0; prepositions[i].word[0]; i++) prp++;
    for (int i = 0; conjunctions[i].word[0]; i++) cnj++;

    printf("[GRAMMAR] POS tables loaded — articles:%d pronouns:%d "
           "aux_verbs:%d preps:%d conj:%d\n",
           art, prn, aux, prp, cnj);
}

/* ═══════════════════════════════════════════════════════════════
   POS LOOKUP
   ═══════════════════════════════════════════════════════════════ */

int is_article(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    return table_lookup(articles, l) >= 0;
}
int is_pronoun(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    return table_lookup(pronouns, l) >= 0;
}
int is_aux_verb(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    return table_lookup(aux_verbs, l) >= 0;
}
int is_prep(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    return table_lookup(prepositions, l) >= 0;
}
int is_conj(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    return table_lookup(conjunctions, l) >= 0;
}
int is_question_word(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    for (int i = 0; question_words[i].word[0]; i++)
        if (strcmp(question_words[i].word, l) == 0) return 1;
    return 0;
}


/*
int tokenize_and_tag_local(const char *sentence, Token *tokens) {
    char buf[512];
    strncpy(buf, sentence, 511);
    
    int count = 0;
    char *tok = strtok(buf, " \t\n");
    
    while (tok && count < 64) {
        strncpy(tokens[count].word, tok, 63);
        
        // Use your existing lookup tables
        const POSEntry *entry = get_pos_entry(tok);
        if (entry) {
            tokens[count].class = entry->class_tag;
            tokens[count].trust = 1; // TRUST_INFERRED
        } else {
            // Heuristic: If it's not in our function word tables, 
            // guess it's a Noun for now (Common in technical text)
            tokens[count].class = CLASS_NOUN;
            tokens[count].trust = 1;
        }
        
        count++;
        tok = strtok(NULL, " \t\n");
    }
    return count;
}

*/


const POSEntry* get_pos_entry(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    int idx;
    if ((idx = table_lookup(articles,     l)) >= 0) return &articles[idx];
    if ((idx = table_lookup(pronouns,     l)) >= 0) return &pronouns[idx];
    if ((idx = table_lookup(aux_verbs,    l)) >= 0) return &aux_verbs[idx];
    if ((idx = table_lookup(prepositions, l)) >= 0) return &prepositions[idx];
    if ((idx = table_lookup(conjunctions, l)) >= 0) return &conjunctions[idx];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   QUESTION DETECTION ALGORITHM
   ═══════════════════════════════════════════════════════════════ */

/*
 * Tokenize into a small local array for analysis
 */
#define MAX_Q_TOKENS 32
typedef struct { char t[MAX_Q_TOKENS][64]; int n; } QTokens;

static QTokens qtokenize(const char *input) {
    QTokens qt = {{{0}}, 0};
    char buf[512];
    strncpy(buf, input, 511);
    /* strip trailing ? ! . */
    int len = strlen(buf);
    while (len > 0 && (buf[len-1]=='?'||buf[len-1]=='!'||buf[len-1]=='.')) {
        buf[--len] = '\0';
    }
    char *tok = strtok(buf, " \t\n");
    while (tok && qt.n < MAX_Q_TOKENS) {
        lower_copy(qt.t[qt.n++], tok, 64);
        tok = strtok(NULL, " \t\n");
    }
    return qt;
}

/*
 * Extract subject: everything after the question word + aux verb
 * e.g. "who is Bill Gates" → subject = "Bill Gates"
 *      "what is blockchain" → subject = "blockchain"
 *      "where is London" → subject = "London"
 */
static void extract_subject(QTokens *qt, int start, char *out, int outlen) {
    out[0] = '\0';
    int remaining = outlen - 1;
    for (int i = start; i < qt->n && remaining > 0; i++) {
        if (i > start) strncat(out, " ", remaining--);
        strncat(out, qt->t[i], remaining);
        remaining -= strlen(qt->t[i]);
    }
}

/*
 * Detect if a string looks like a proper noun (capitalized in original)
 */
static int looks_like_proper_noun(const char *original_input, const char *subject) {
    /* If the subject appears capitalized in the original input, it's likely a proper noun */
    char upper_first[128];
    strncpy(upper_first, subject, 127);
    upper_first[0] = toupper((unsigned char)upper_first[0]);
    return strstr(original_input, upper_first) != NULL;
}

ParsedQuery analyze_question(const char *input) {
    ParsedQuery q = {Q_NONE, {0}, {0}, {0}, 0, 0};
    strncpy(q.raw_input, input, 511);

    QTokens qt = qtokenize(input);
    if (qt.n == 0) return q;

    /* Check for trailing ? */
    if (input[strlen(input)-1] == '?') q.is_question = 1;

    /* ── Detect question type from first token ── */
    char *w0 = qt.t[0];

    /* WHO / WHO IS / WHO ARE — bare "who" = open person query */
    if (strcmp(w0, "who") == 0) {
        q.is_question = 1;
        q.type = Q_WHO_IS;
        if (qt.n == 1) {
            /* bare "who" — treat entire input as the subject to search */
            strncpy(q.subject, "who (person/entity lookup)", MAX_SUBJECT_LEN-1);
            q.needs_web = 0; /* prompt user for a name instead */
            snprintf(q.predicate, MAX_PREDICATE_LEN,
                     "Who are you asking about? Try: who is [name]");
        } else {
            int skip = 1;
            if (qt.n > 1 && is_aux_verb(qt.t[1])) skip = 2;
            extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
            q.needs_web = 1;
            snprintf(q.predicate, MAX_PREDICATE_LEN,
                     "biographical and professional information about %s", q.subject);
        }
        return q;
    }

    /* WHAT IS/ARE */
    if (strcmp(w0, "what") == 0) {
        q.is_question = 1;
        q.type = Q_WHAT_IS;
        if (qt.n == 1) {
            /* bare "what" — no subject given yet */
            q.needs_web = 0;
            snprintf(q.predicate, MAX_PREDICATE_LEN, "You typed just \"what\". Try: what is [topic]");
            return q;
        }
                int skip = 1;
        if (qt.n > 1 && is_aux_verb(qt.t[1])) skip = 2;
        /* skip articles after aux */
        if (skip < qt.n && is_article(qt.t[skip])) skip++;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = 1;
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "definition and explanation of %s", q.subject);
        return q;
    }

    /* WHERE IS/ARE */
    if (strcmp(w0, "where") == 0) {
        q.is_question = 1;
        q.type = Q_WHERE_IS;
        if (qt.n == 1) {
            /* bare "where" — no subject given yet */
            q.needs_web = 0;
            snprintf(q.predicate, MAX_PREDICATE_LEN, "You typed just \"where\". Try: where is [topic]");
            return q;
        }
                int skip = 1;
        if (qt.n > 1 && is_aux_verb(qt.t[1])) skip = 2;
        if (skip < qt.n && is_article(qt.t[skip])) skip++;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = 1;
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "location and geography of %s", q.subject);
        return q;
    }

    /* WHEN IS/ARE/WAS */
    if (strcmp(w0, "when") == 0) {
        q.is_question = 1;
        q.type = Q_WHEN_IS;
        if (qt.n == 1) {
            /* bare "when" — no subject given yet */
            q.needs_web = 0;
            snprintf(q.predicate, MAX_PREDICATE_LEN, "You typed just \"when\". Try: when is [topic]");
            return q;
        }
                int skip = 1;
        if (qt.n > 1 && is_aux_verb(qt.t[1])) skip = 2;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = 1;
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "date, time or schedule of %s", q.subject);
        return q;
    }

    /* WHY */
    if (strcmp(w0, "why") == 0) {
        q.is_question = 1;
        q.type = Q_WHY;
        if (qt.n == 1) {
            /* bare "why" — no subject given yet */
            q.needs_web = 0;
            snprintf(q.predicate, MAX_PREDICATE_LEN, "You typed just \"why\". Try: why is [topic]");
            return q;
        }
                extract_subject(&qt, 1, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = 1;
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "reason and cause for %s", q.subject);
        return q;
    }

    /* HOW */
    if (strcmp(w0, "how") == 0) {
        q.is_question = 1;
        q.type = Q_HOW;
        if (qt.n == 1) {
            /* bare "how" — no subject given yet */
            q.needs_web = 0;
            snprintf(q.predicate, MAX_PREDICATE_LEN, "You typed just \"how\". Try: how is [topic]");
            return q;
        }
                int skip = 1;
        if (qt.n > 1 && is_aux_verb(qt.t[1])) skip = 2;
        if (skip < qt.n && is_article(qt.t[skip])) skip++;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = (q.subject[0] != '\0');
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "process and method for %s", q.subject);
        return q;
    }

    /* IS/ARE/WAS (inverted question: "is X Y?") */
    if (is_aux_verb(w0)) {
        q.is_question = 1;
        q.type = Q_IS;
        /* subject is next noun phrase */
        int skip = 1;
        if (skip < qt.n && is_article(qt.t[skip])) skip++;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        /* check if proper noun → needs web */
        q.needs_web = looks_like_proper_noun(input, q.subject);
        snprintf(q.predicate, MAX_PREDICATE_LEN,
                 "status and properties of %s", q.subject);
        return q;
    }

    /* DO/DOES */
    if (strcmp(w0, "do")   == 0 ||
        strcmp(w0, "does") == 0 ||
        strcmp(w0, "did")  == 0) {
        q.is_question = 1;
        q.type = Q_DO_DOES;
        int skip = 1;
        if (skip < qt.n && is_article(qt.t[skip])) skip++;
        extract_subject(&qt, skip, q.subject, MAX_SUBJECT_LEN);
        q.needs_web = looks_like_proper_noun(input, q.subject);
        return q;
    }

    /* CAN */
    if (strcmp(w0, "can") == 0) {
        q.is_question = 1;
        q.type = Q_CAN;
        extract_subject(&qt, 1, q.subject, MAX_SUBJECT_LEN);
        return q;
    }

    /* COMMAND — starts with a bare verb (imperative) */
    /* If first word is not a function word and looks like a verb instruction */
    if (!is_article(w0) && !is_pronoun(w0) && !is_aux_verb(w0)) {
        /* Heuristic: if second token is an article or noun, likely command */
        if (qt.n > 1 && (is_article(qt.t[1]) || !is_aux_verb(qt.t[1]))) {
            q.type = Q_COMMAND;
            extract_subject(&qt, 0, q.subject, MAX_SUBJECT_LEN);
            return q;
        }
    }

    /* Default: statement */
    q.type = Q_STATEMENT;
    extract_subject(&qt, 0, q.subject, MAX_SUBJECT_LEN);
    return q;
}

const char* question_type_name(QuestionType t) {
    switch (t) {
        case Q_NONE:      return "NONE";
        case Q_WHO_IS:    return "WHO_IS";
        case Q_WHAT_IS:   return "WHAT_IS";
        case Q_WHERE_IS:  return "WHERE_IS";
        case Q_WHEN_IS:   return "WHEN_IS";
        case Q_WHY:       return "WHY";
        case Q_HOW:       return "HOW";
        case Q_IS:        return "IS/ARE";
        case Q_DO_DOES:   return "DO/DOES";
        case Q_CAN:       return "CAN";
        case Q_STATEMENT: return "STATEMENT";
        case Q_COMMAND:   return "COMMAND";
        default:          return "UNKNOWN";
    }
}

void grammar_dump(void) {
    printf("\n[GRAMMAR DUMP]\n");
    printf("  Articles    : ");
    for (int i = 0; articles[i].word[0]; i++) printf("%s ", articles[i].word);
    printf("\n  Pronouns    : ");
    for (int i = 0; pronouns[i].word[0]; i++) printf("%s ", pronouns[i].word);
    printf("\n  Aux Verbs   : ");
    for (int i = 0; aux_verbs[i].word[0]; i++) printf("%s ", aux_verbs[i].word);
    printf("\n  Prepositions: ");
    for (int i = 0; prepositions[i].word[0]; i++) printf("%s ", prepositions[i].word);
    printf("\n  Conjunctions: ");
    for (int i = 0; conjunctions[i].word[0]; i++) printf("%s ", conjunctions[i].word);
    printf("\n  Q-Words     : ");
    for (int i = 0; question_words[i].word[0]; i++) printf("%s ", question_words[i].word);
    printf("\n\n");
}
