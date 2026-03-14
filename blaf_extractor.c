/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_extractor.c                                    ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

#include "blaf_extractor.h"
#include "blaf_sectors.h"
#include "blaf_mapper.h"
#include "blaf_core.h"
#include "blaf_morph.h"

extern ConceptEntry concept_table[];
extern int find_concept_index(const char *word, uint8_t sector);
extern int add_concept(const char *word, uint8_t type, uint8_t class,
                        uint8_t trust, uint8_t sector,
                        uint8_t in_pin, uint8_t out_pin);
extern int knowledge_save(const char *path);

/* ═══════════════════════════════════════════════════════════════
   INTENT KEYWORD LISTS
   ═══════════════════════════════════════════════════════════════ */

typedef struct { IntentType type; const char *keywords[12]; } IntentKeywords;

static const IntentKeywords intent_kw[] = {
    {INTENT_DISTANCE,    {"km","kilometre","kilometer","mile","distance",
                          "far","away","apart","between","from","to",NULL}},
    {INTENT_DURATION,    {"minute","hour","day","week","month","year",
                          "takes","duration","long","time","period",NULL}},
    {INTENT_AGE,         {"born","age","old","year","founded","established",
                          "created","built","date","century",NULL}},
    {INTENT_COUNT,       {"million","billion","thousand","number","total",
                          "population","count","people","amount",NULL}},
    {INTENT_SIZE,        {"km²","square","area","large","small","big",
                          "wide","tall","high","size","hectare",NULL}},
    {INTENT_PRICE,       {"dollar","pound","euro","cost","price","worth",
                          "value","billion","million","revenue",NULL}},
    {INTENT_SPEED,       {"mph","kmh","km/h","fast","speed","velocity",
                          "per hour","mach","knot",NULL}},
    {INTENT_TEMPERATURE, {"celsius","fahrenheit","degree","temperature",
                          "hot","cold","warm","climate","weather",NULL}},
    {INTENT_LOCATION,    {"located","situated","capital","city","country",
                          "region","continent","north","south","east","west",NULL}},
    {INTENT_IDENTITY,    {"born","is","was","known","founder","ceo",
                          "president","inventor","author","wrote",NULL}},
    {INTENT_DEFINITION,  {"is","are","was","refers","defined","known",
                          "term","means","concept",NULL}},
    {INTENT_CAUSE,       {"because","due","caused","reason","result",
                          "therefore","hence","since","leads",NULL}},
    {INTENT_PROCESS,     {"works","operates","functions","process","method",
                          "mechanism","step","how","system",NULL}},
    {INTENT_COMPARE,     {"while","whereas","compared","unlike","similar",
                          "difference","contrast","both","versus",NULL}},
    {INTENT_UNKNOWN,     {NULL}}
};

/* ═══════════════════════════════════════════════════════════════
   UNIT WORDS
   ═══════════════════════════════════════════════════════════════ */

static const char *unit_words[] = {
    "km","mile","meter","metre","foot","feet","inch","yard",
    "kg","gram","pound","ton","tonne",
    "litre","liter","gallon","ml",
    "second","minute","hour","day","week","month","year","century",
    "celsius","fahrenheit","kelvin",
    "dollar","euro","pound","yen","usd","gbp",
    "mph","kmh","knot","mach",
    "watt","volt","amp","joule","calorie",
    "percent","%","degree","°",
    NULL
};

/* ═══════════════════════════════════════════════════════════════
   STOPWORDS — words that should NEVER be mapped as concepts
   ═══════════════════════════════════════════════════════════════ */

static const char *STOPWORDS[] = {
    /* articles, aux verbs, pronouns */
    "the","a","an","is","are","was","were","be","been","being",
    "have","has","had","do","does","did","will","would","shall",
    "should","may","might","must","can","could",
    /* prepositions */
    "of","in","on","at","to","for","with","by","from","up",
    "about","into","through","during","before","after","above",
    "below","between","within","without","toward","among",
    /* conjunctions — never content words */
    "and","or","but","if","then","than","that","this","these",
    "those","it","its","as","so","yet","both","either","not",
    "no","nor","just","also","although","though","whereas",
    "whether","unless","until","since","when","whenever","while",
    "because","however","therefore","thus","hence","furthermore",
    "moreover","nevertheless","nonetheless","meanwhile",
    /* pronouns */
    "who","whom","whose","what","where","how","why","they",
    "their","there","here","him","her","his","she","he","we",
    "our","us","them","you","your","my","me","i",
    /* filler */
    "very","more","most","much","many","some","any","all",
    "each","every","such","other","another","same","even",
    "only","said","which","been","well","now","new","one",
    NULL
};

static int is_stopword(const char *w) {
    for (int i = 0; STOPWORDS[i]; i++)
        if (strcmp(STOPWORDS[i], w) == 0) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

static int word_count(const char *text) {
    int c = 0, in_word = 0;
    while (*text) {
        if (isspace((unsigned char)*text)) in_word = 0;
        else if (!in_word) { in_word = 1; c++; }
        text++;
    }
    return c;
}

static int contains_number(const char *text) {
    while (*text) { if (isdigit((unsigned char)*text)) return 1; text++; }
    return 0;
}

static void extract_number(const char *text, char *num_buf, int nlen,
                            char *unit_buf, int ulen) {
    num_buf[0] = unit_buf[0] = '\0';
    const char *p = text;
    while (*p) {
        if (isdigit((unsigned char)*p) ||
            (*p == '.' && isdigit((unsigned char)*(p+1)))) {
            int j = 0;
            while ((*p=='.'||isdigit((unsigned char)*p)) && j < nlen-1)
                num_buf[j++] = *p++;
            num_buf[j] = '\0';
            while (*p == ' ' || *p == ',') p++;
            char candidate[32] = {0}; j = 0;
            while (*p && !isspace((unsigned char)*p) && j < 31)
                candidate[j++] = tolower((unsigned char)*p++);
            for (int u = 0; unit_words[u]; u++)
                if (strcmp(candidate, unit_words[u]) == 0) {
                    strncpy(unit_buf, candidate, ulen-1); break;
                }
            return;
        }
        p++;
    }
}

static int split_sentences(const char *text,
                            ScoredSentence *out, int max) {
    int count = 0;
    char buf[65536]; strncpy(buf, text, sizeof(buf)-1);
    char *p = buf, *start = p;

    while (*p && count < max) {
        if (*p == '.' || *p == '!' || *p == '?') {
            char next = *(p+1);
            if (next == ' ' || next == '\n' || next == '\0') {
                *p = '\0';
                while (*start && isspace((unsigned char)*start)) start++;
                int len = (int)strlen(start);
                if (len > 15 && len < MAX_SENTENCE_LEN) {
                    strncpy(out[count].text, start, MAX_SENTENCE_LEN-1);
                    out[count].position = count;
                    out[count].score    = 0.0f;
                    count++;
                }
                start = p + 1;
            }
        }
        p++;
    }
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start && strlen(start) > 15 && count < max) {
        strncpy(out[count].text, start, MAX_SENTENCE_LEN-1);
        out[count].position = count;
        out[count].score    = 0.0f;
        count++;
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
   SCORE SENTENCE
   ═══════════════════════════════════════════════════════════════ */

float score_sentence(const char *sentence,
                     const ResolvedIntent *intent,
                     const char *subject) {
    if (!sentence || !sentence[0]) return 0.0f;

    char l[MAX_SENTENCE_LEN]; lower_copy(l, sentence, MAX_SENTENCE_LEN);
    float score = 0.0f;

    /* Subject presence — lemmatize subject words for fuzzy match */
    if (subject && subject[0]) {
        char subj_copy[128]; lower_copy(subj_copy, subject, 128);
        char *tok = strtok(subj_copy, " ");
        while (tok) {
            if (strlen(tok) > 2) {
                /* Exact match */
                if (strstr(l, tok)) score += 2.5f;
                else {
                    /* Try lemma of subject word */
                    char lemma[64];
                    morph_lemmatize(tok, lemma, 64);
                    if (strcmp(lemma, tok) != 0 && strstr(l, lemma))
                        score += 2.0f;
                    /* Try plural */
                    char plural[64];
                    morph_plural(tok, plural, 64);
                    if (strstr(l, plural)) score += 1.8f;
                }
            }
            tok = strtok(NULL, " ");
        }
    }

    /* Intent keyword presence */
    if (intent) {
        for (int i = 0; intent_kw[i].type != INTENT_UNKNOWN; i++) {
            if (intent_kw[i].type != intent->type) continue;
            for (int j = 0; intent_kw[i].keywords[j]; j++)
                if (strstr(l, intent_kw[i].keywords[j])) score += 1.8f;
            break;
        }
        if (intent->modifier[0]) {
            char mod[64]; lower_copy(mod, intent->modifier, 64);
            if (strstr(l, mod)) score += 1.5f;
        }
        if (intent->object[0]) {
            char obj[128]; lower_copy(obj, intent->object, 128);
            if (strstr(l, obj)) score += 2.0f;
        }
    }

    /* Numeric bonus */
    if (contains_number(sentence)) {
        score += 1.5f;
        for (int u = 0; unit_words[u]; u++)
            if (strstr(l, unit_words[u])) { score += 1.0f; break; }
    }

    /* Definition sentence bonus — "X is a ..." or "X refers to ..."
     * These are almost always the most informative sentence */
    if (strstr(l, " is a ")   || strstr(l, " is an ")  ||
        strstr(l, " are a ")  || strstr(l, " are an ") ||
        strstr(l, " refers ")  || strstr(l, " defined") ||
        strstr(l, " known as") || strstr(l, " means "))
        score += 2.0f;

    /* GENERAL intent: first sentence is usually the definition */
    if (intent && intent->type == INTENT_GENERAL && !intent->modifier[0])
        score += 1.0f;

    /* Brevity bonus */
    int wc = word_count(sentence);
    if (wc < 20)  score += 0.8f;
    if (wc < 12)  score += 0.4f;
    if (wc > 50)  score -= 0.5f;
    if (wc > 100) score -= 1.0f;

    return score;
}

/* ═══════════════════════════════════════════════════════════════
   EXTRACT ANSWER
   ═══════════════════════════════════════════════════════════════ */

void extractor_init(void) {
    printf("[EXTRACTOR] Answer extraction engine ready.\n");
}

ExtractionResult extract_answer(const char *source,
                                 const ResolvedIntent *intent,
                                 const char *subject) {
    ExtractionResult result;
    memset(&result, 0, sizeof(result));
    if (!source || !source[0]) return result;

    ScoredSentence sentences[MAX_SENTENCES];
    int n = split_sentences(source, sentences, MAX_SENTENCES);
    if (n == 0) {
        strncpy(result.answer, source, MAX_EXTRACT_LEN-1);
        result.confidence = 0.3f;
        return result;
    }

    for (int i = 0; i < n; i++)
        sentences[i].score = score_sentence(sentences[i].text,
                                             intent, subject)
                             - (0.05f * sentences[i].position);

    /* Sort descending */
    for (int i = 1; i < n; i++) {
        ScoredSentence tmp = sentences[i];
        int j = i;
        while (j > 0 && sentences[j-1].score < tmp.score) {
            sentences[j] = sentences[j-1]; j--;
        }
        sentences[j] = tmp;
    }

    int take = 1;
    if (sentences[0].score > 4.0f && n > 1 &&
        sentences[1].score > 2.0f) take = 2;
    if (sentences[0].score > 6.0f && n > 2 &&
        sentences[2].score > 2.5f) take = 3;

    /* Re-sort taken by original position */
    for (int i = 0; i < take-1; i++)
        for (int j = i+1; j < take; j++)
            if (sentences[i].position > sentences[j].position) {
                ScoredSentence tmp = sentences[i];
                sentences[i] = sentences[j]; sentences[j] = tmp;
            }

    char combined[MAX_EXTRACT_LEN] = {0};
    for (int i = 0; i < take; i++) {
        if (i > 0) strncat(combined, " ", MAX_EXTRACT_LEN-strlen(combined)-1);
        strncat(combined, sentences[i].text, MAX_EXTRACT_LEN-strlen(combined)-1);
        strncpy(result.sentences[i], sentences[i].text, MAX_SENTENCE_LEN-1);
    }
    result.sentence_count = take;

    strncpy(result.answer, combined, MAX_EXTRACT_LEN-1);
    result.confidence = fminf(1.0f, sentences[0].score / 10.0f);

    extract_number(combined,
                   result.numeric_value, sizeof(result.numeric_value),
                   result.numeric_unit,  sizeof(result.numeric_unit));
    result.found_numeric = (result.numeric_value[0] != '\0');

    printf("[EXTRACTOR] %d sentences scored, took %d. Confidence: %.0f%%\n",
           n, take, result.confidence * 100);

    return result;
}

/* ═══════════════════════════════════════════════════════════════
   MAP ANSWER WORDS
   Register every content word — filtered by STOPWORDS.
   After mapping, register morphological variants.
   ═══════════════════════════════════════════════════════════════ */

int map_answer_words(const char *answer_text, uint8_t sector_hint) {
    if (!answer_text || !answer_text[0]) return 0;

    int mapped = 0;
    char buf[MAX_EXTRACT_LEN]; strncpy(buf, answer_text, sizeof(buf)-1);

    char *tok = strtok(buf, " \t\n.,;:!?\"'()[]{}—");
    while (tok) {
        if (strlen(tok) < 3) {
            tok = strtok(NULL, " \t\n.,;:!?\"'()[]{}—"); continue;
        }

        char l[64]; lower_copy(l, tok, 64);

        /* Skip stopwords */
        if (is_stopword(l)) {
            tok = strtok(NULL, " \t\n.,;:!?\"'()[]{}—"); continue;
        }

        /* Lemmatize before storing — store base form */
        char lemma[64]; morph_lemmatize(l, lemma, 64);
        const char *store = (lemma[0] && !is_stopword(lemma)) ? lemma : l;

        if (!is_stopword(store)) {
            /* map_word_web no-ops if already exists */
            map_word_web(store, 0, sector_hint, 0xFF, 0xFF);
            /* Register morphological variants */
            morph_register_variants(store, sector_hint, 0 /* NOUN */);
            mapped++;
        }
        tok = strtok(NULL, " \t\n.,;:!?\"'()[]{}—");
    }

    if (mapped > 0)
        printf("[EXTRACTOR] Mapped %d words from answer into concept table.\n",
               mapped);
    return mapped;
}

/* ═══════════════════════════════════════════════════════════════
   EXTRACT SUBTOPICS
   ═══════════════════════════════════════════════════════════════ */

void extract_subtopics(const char *source, char *out, int outlen) {
    out[0] = '\0';
    if (!source) return;

    typedef struct { char word[48]; int count; } WordFreq;
    WordFreq freq[256]; int nfreq = 0;

    char buf[65536]; strncpy(buf, source, sizeof(buf)-1);
    char *tok = strtok(buf, " \t\n.,;:!?\"'()[]{}—");

    while (tok) {
        int len = strlen(tok);
        if (len < 4 || len > 32) {
            tok=strtok(NULL," \t\n.,;:!?\"'()[]{}—"); continue;
        }
        char l[48]; lower_copy(l, tok, 48);
        if (is_stopword(l)) {
            tok=strtok(NULL," \t\n.,;:!?\"'()[]{}—"); continue;
        }

        /* Lemmatize so "asteroids" and "asteroid" count together */
        char lemma[48]; morph_lemmatize(l, lemma, 48);
        const char *key = (lemma[0] && !is_stopword(lemma)) ? lemma : l;

        int found = 0;
        for (int i = 0; i < nfreq; i++)
            if (strcmp(freq[i].word, key) == 0) { freq[i].count++; found=1; break; }
        if (!found && nfreq < 256) {
            strncpy(freq[nfreq].word, key, 47);
            freq[nfreq].count = 1;
            nfreq++;
        }
        tok = strtok(NULL, " \t\n.,;:!?\"'()[]{}—");
    }

    for (int i = 0; i < nfreq-1; i++)
        for (int j = i+1; j < nfreq; j++)
            if (freq[j].count > freq[i].count) {
                WordFreq tmp = freq[i]; freq[i] = freq[j]; freq[j] = tmp;
            }

    int added = 0;
    for (int i = 0; i < nfreq && added < 8; i++) {
        if (freq[i].count < 2) break;
        if (added > 0) strncat(out, ", ", outlen-strlen(out)-1);
        strncat(out, freq[i].word, outlen-strlen(out)-1);
        added++;
    }
}

/* ═══════════════════════════════════════════════════════════════
   INGEST FILE AS CONCEPT
   ═══════════════════════════════════════════════════════════════ */

int ingest_file_as_concept(const char *filepath, const char *concept_name) {
    struct stat st;
    if (stat(filepath, &st) != 0) {
        printf("[ERROR] Cannot access file: %s\n", filepath);
        return -1;
    }
    if (st.st_size == 0) return -1;

    int idx = find_concept_index(concept_name, 0x00);
    if (idx == -1) {
        add_concept(concept_name, 0, 0, 7, 0x00, 0xFF, 0xFF);
        idx = find_concept_index(concept_name, 0x00);
    }
    if (idx == -1) return -1;

    if (concept_table[idx].summary != NULL) {
        free(concept_table[idx].summary);
        concept_table[idx].summary = NULL;
    }

    concept_table[idx].summary = malloc(st.st_size + 1);
    if (!concept_table[idx].summary) {
        printf("[ERROR] Out of memory for %s\n", concept_name);
        return -1;
    }

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        free(concept_table[idx].summary);
        concept_table[idx].summary = NULL;
        return -1;
    }

    size_t bytesRead = fread(concept_table[idx].summary, 1, st.st_size, fp);
    concept_table[idx].summary[bytesRead] = '\0';
    concept_table[idx].summary_len        = (uint32_t)bytesRead;
    concept_table[idx].trust              = 7;
    concept_table[idx].last_seen          = (uint32_t)time(NULL);
    fclose(fp);

    /* Register morphological variants of the concept name */
    morph_register_variants(concept_name, 0x00, 0);

    printf("[INGEST] Mapped %s (%u bytes).\n",
           concept_name, concept_table[idx].summary_len);

    knowledge_save(NULL);
    return 0;
}
