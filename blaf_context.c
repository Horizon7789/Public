/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_context.c                                      ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "blaf_context.h"

/* ═══════════════════════════════════════════════════════════════
   STOPWORDS  (excluded from co-occurrence and enrichment)
   ═══════════════════════════════════════════════════════════════ */

static const char *STOPS[] = {
    "the","a","an","is","are","was","were","be","been","being",
    "have","has","had","do","does","did","will","would","shall",
    "should","may","might","must","can","could","of","in","on",
    "at","to","for","with","by","from","up","about","into",
    "and","or","but","if","then","than","that","this","these",
    "those","it","its","as","so","not","no","just","also",
    "which","their","there","about","would","could","more",
    NULL
};

static int is_stopword(const char *w) {
    for (int i = 0; STOPS[i]; i++)
        if (strcmp(w, STOPS[i]) == 0) return 1;
    return 0;
}

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

/* ═══════════════════════════════════════════════════════════════
   CO-OCCURRENCE TABLE
   ═══════════════════════════════════════════════════════════════ */

static CoOccurrence g_cooc[COOC_MAX_PAIRS];
static int          g_cooc_count = 0;

void context_add_cooccurrence(const char *wa, const char *wb, uint8_t sector) {
    if (!wa || !wb || strcmp(wa, wb) == 0) return;

    /* Canonical order: alphabetical so (egg,yolk) == (yolk,egg) */
    const char *a = (strcmp(wa, wb) < 0) ? wa : wb;
    const char *b = (strcmp(wa, wb) < 0) ? wb : wa;

    char la[COOC_WORD_LEN], lb[COOC_WORD_LEN];
    lower_copy(la, a, COOC_WORD_LEN);
    lower_copy(lb, b, COOC_WORD_LEN);

    /* Update existing */
    for (int i = 0; i < g_cooc_count; i++) {
        if (strcmp(g_cooc[i].word_a, la) == 0 &&
            strcmp(g_cooc[i].word_b, lb) == 0) {
            g_cooc[i].count++;
            return;
        }
    }
    /* Add new */
    if (g_cooc_count < COOC_MAX_PAIRS) {
        CoOccurrence *e = &g_cooc[g_cooc_count++];
        strncpy(e->word_a, la, COOC_WORD_LEN-1);
        strncpy(e->word_b, lb, COOC_WORD_LEN-1);
        e->sector = sector;
        e->count  = 1;
    }
}

void context_add_sentence(const char *sentence, uint8_t sector) {
    if (!sentence) return;

    /* Extract content words */
    char words[32][COOC_WORD_LEN];
    int  nwords = 0;
    char buf[1024]; strncpy(buf, sentence, sizeof(buf)-1);

    char *tok = strtok(buf, " \t\n.,;:!?\"'()[]{}");
    while (tok && nwords < 32) {
        if (strlen(tok) < 3) { tok = strtok(NULL," \t\n.,;:!?\"'()[]{}"); continue; }
        char l[COOC_WORD_LEN]; lower_copy(l, tok, COOC_WORD_LEN);
        if (!is_stopword(l)) {
            strncpy(words[nwords++], l, COOC_WORD_LEN-1);
        }
        tok = strtok(NULL, " \t\n.,;:!?\"'()[]{}");
    }

    /* Register all pairs (window of 5 words) */
    for (int i = 0; i < nwords; i++)
        for (int j = i+1; j < nwords && j < i+5; j++)
            context_add_cooccurrence(words[i], words[j], sector);
}

int context_get_related(const char *word, uint8_t sector,
                         char out[][COOC_WORD_LEN], int max) {
    char l[COOC_WORD_LEN]; lower_copy(l, word, COOC_WORD_LEN);

    /* Collect matches with scores */
    typedef struct { char w[COOC_WORD_LEN]; int c; } Match;
    Match matches[64]; int nm = 0;

    for (int i = 0; i < g_cooc_count; i++) {
        const char *partner = NULL;
        if (strcmp(g_cooc[i].word_a, l) == 0) partner = g_cooc[i].word_b;
        if (strcmp(g_cooc[i].word_b, l) == 0) partner = g_cooc[i].word_a;
        if (!partner) continue;
        if (sector != 0xFF && g_cooc[i].sector != sector &&
            g_cooc[i].sector != 0) continue;

        /* Add or update */
        int found = 0;
        for (int j = 0; j < nm; j++)
            if (strcmp(matches[j].w, partner) == 0) {
                matches[j].c += g_cooc[i].count; found=1; break;
            }
        if (!found && nm < 64) {
            strncpy(matches[nm].w, partner, COOC_WORD_LEN-1);
            matches[nm].c = g_cooc[i].count;
            nm++;
        }
    }

    /* Sort by count */
    for (int i = 0; i < nm-1; i++)
        for (int j = i+1; j < nm; j++)
            if (matches[j].c > matches[i].c) {
                Match tmp = matches[i]; matches[i] = matches[j]; matches[j] = tmp;
            }

    int ret = (nm < max) ? nm : max;
    for (int i = 0; i < ret; i++)
        strncpy(out[i], matches[i].w, COOC_WORD_LEN-1);
    return ret;
}

uint8_t context_infer_sector(const char *word) {
    char l[COOC_WORD_LEN]; lower_copy(l, word, COOC_WORD_LEN);

    int sector_votes[256] = {0};

    for (int i = 0; i < g_cooc_count; i++) {
        if (strcmp(g_cooc[i].word_a, l) == 0 ||
            strcmp(g_cooc[i].word_b, l) == 0)
            sector_votes[g_cooc[i].sector] += g_cooc[i].count;
    }

    int best_sector = 0, best_votes = 0;
    for (int s = 0; s < 256; s++)
        if (sector_votes[s] > best_votes) {
            best_votes  = sector_votes[s];
            best_sector = s;
        }

    return (uint8_t)best_sector;
}

/* ═══════════════════════════════════════════════════════════════
   CONVERSATION HISTORY
   ═══════════════════════════════════════════════════════════════ */

static ConversationTurn g_history[HISTORY_MAX_TURNS];
static int              g_history_count = 0;
static int              g_turn_id       = 0;

void context_add_turn(const char *input, const char *subject,
                       const char *response, uint8_t sector) {
    int slot = g_turn_id % HISTORY_MAX_TURNS;
    ConversationTurn *t = &g_history[slot];
    strncpy(t->input,    input    ? input    : "", HISTORY_MAX_LEN-1);
    strncpy(t->subject,  subject  ? subject  : "", 95);
    strncpy(t->response, response ? response : "", HISTORY_MAX_LEN-1);
    t->sector  = sector;
    t->turn_id = g_turn_id;
    g_turn_id++;
    if (g_history_count < HISTORY_MAX_TURNS) g_history_count++;
}

/* Get turn at offset from current (0 = most recent) */
static const ConversationTurn* get_turn(int offset) {
    if (g_history_count == 0 || offset >= g_history_count) return NULL;
    int slot = (g_turn_id - 1 - offset + HISTORY_MAX_TURNS*2) % HISTORY_MAX_TURNS;
    return &g_history[slot];
}

const char* context_last_subject(void) {
    const ConversationTurn *t = get_turn(0);
    return (t && t->subject[0]) ? t->subject : "";
}

const char* context_last_response(void) {
    const ConversationTurn *t = get_turn(0);
    return (t && t->response[0]) ? t->response : "";
}

/* ═══════════════════════════════════════════════════════════════
   PRONOUN / REFERENCE RESOLUTION
   ═══════════════════════════════════════════════════════════════ */

/* Pronoun → gender/type hint */
typedef struct { const char *pronoun; int is_person; int gender; } PronounHint;
/* gender: 0=any, 1=male, 2=female, 3=neuter, 4=plural */

static const PronounHint pronoun_hints[] = {
    {"he",    1, 1}, {"him",   1, 1}, {"his",  1, 1},
    {"she",   1, 2}, {"her",   1, 2}, {"hers", 1, 2},
    {"it",    0, 3}, {"its",   0, 3},
    {"they",  0, 4}, {"them",  0, 4}, {"their",0, 4},
    {"this",  0, 0}, {"that",  0, 0},
    {"there", 0, 0}, {"here",  0, 0},
    {"these", 0, 4}, {"those", 0, 4},
    {NULL, 0, 0}
};

static int is_pronoun_ref(const char *word) {
    char l[32]; lower_copy(l, word, 32);
    for (int i = 0; pronoun_hints[i].pronoun; i++)
        if (strcmp(pronoun_hints[i].pronoun, l) == 0) return 1;
    return 0;
}

PronounResolution context_resolve_reference(const char *input) {
    PronounResolution r = {{0}, 0, 0};
    if (!input) return r;

    char l[HISTORY_MAX_LEN]; lower_copy(l, input, HISTORY_MAX_LEN);

    /* Scan input for pronoun/reference words */
    char buf[HISTORY_MAX_LEN]; strncpy(buf, l, sizeof(buf)-1);
    char *tok = strtok(buf, " \t\n.,?!");
    int found_pronoun = 0;
    while (tok) {
        if (is_pronoun_ref(tok)) { found_pronoun = 1; break; }
        tok = strtok(NULL, " \t\n.,?!");
    }

    if (!found_pronoun) return r;

    /* Resolve to most recent subject in history */
    for (int offset = 0; offset < g_history_count; offset++) {
        const ConversationTurn *t = get_turn(offset);
        if (!t || !t->subject[0]) continue;

        strncpy(r.resolved, t->subject, 95);
        r.found      = 1;
        r.turns_back = offset;
        printf("[CONTEXT] Resolved reference → \"%s\" (from %d turn%s ago)\n",
               r.resolved, offset, offset == 1 ? "" : "s");
        return r;
    }
    return r;
}

int context_enrich_query(const char *input, char *out, int outlen) {
    if (!input || !out) return 0;
    strncpy(out, input, outlen-1);

    char l[HISTORY_MAX_LEN]; lower_copy(l, input, HISTORY_MAX_LEN);

    /* "tell me more" / "go on" / "continue" / "elaborate" */
    const char *more_phrases[] = {
        "tell me more","more about that","go on","continue",
        "elaborate","keep going","and then","what else",NULL
    };
    for (int i = 0; more_phrases[i]; i++) {
        if (strstr(l, more_phrases[i])) {
            const char *last = context_last_subject();
            if (last && last[0]) {
                snprintf(out, outlen, "tell me more about %s", last);
                printf("[CONTEXT] Enriched: \"%s\" → \"%s\"\n", input, out);
                return 1;
            }
        }
    }

    /* Pronoun resolution */
    PronounResolution pr = context_resolve_reference(input);
    if (pr.found) {
        /* Replace pronoun with resolved subject */
        const char *pronouns[] = {
            " it "," its "," he "," him "," his ",
            " she "," her "," they "," them "," their ",
            " this "," that "," there "," these "," those ",NULL
        };
        char enriched[HISTORY_MAX_LEN];
        strncpy(enriched, input, sizeof(enriched)-1);

        for (int i = 0; pronouns[i]; i++) {
            char *pos = strcasestr(enriched, pronouns[i]);
            if (pos) {
                char replacement[128];
                snprintf(replacement, sizeof(replacement), " %s ", pr.resolved);
                /* Simple replace: copy before + replacement + after */
                char tmp[HISTORY_MAX_LEN];
                int before_len = (int)(pos - enriched);
                strncpy(tmp, enriched, before_len);
                tmp[before_len] = '\0';
                strncat(tmp, replacement, sizeof(tmp)-strlen(tmp)-1);
                strncat(tmp, pos + strlen(pronouns[i]),
                        sizeof(tmp)-strlen(tmp)-1);
                strncpy(enriched, tmp, sizeof(enriched)-1);
                break;
            }
        }

        strncpy(out, enriched, outlen-1);
        printf("[CONTEXT] Pronoun resolved: \"%s\" → \"%s\"\n", input, out);
        return 1;
    }

    /* If query has no subject at all, inject last subject */
    /* Detect no-subject queries: single word questions */
    char buf2[HISTORY_MAX_LEN]; strncpy(buf2, l, sizeof(buf2)-1);
    int token_count = 0;
    char *tok = strtok(buf2, " \t\n");
    while (tok) { token_count++; tok = strtok(NULL, " \t\n"); }

    const char *no_subj_starts[] = {
        "how far","how long","how old","how much","how many",
        "where is","what is","who is","when was","why does",NULL
    };
    for (int i = 0; no_subj_starts[i]; i++) {
        if (strncmp(l, no_subj_starts[i], strlen(no_subj_starts[i])) == 0) {
            /* Check if there's actually a subject after the opener */
            int opener_len = strlen(no_subj_starts[i]);
            const char *rest = l + opener_len;
            while (*rest == ' ') rest++;
            if (*rest == '\0' || strlen(rest) < 2) {
                /* No subject — inject last subject */
                const char *last = context_last_subject();
                if (last && last[0]) {
                    snprintf(out, outlen, "%s %s", input, last);
                    printf("[CONTEXT] Injected subject: \"%s\" → \"%s\"\n",
                           input, out);
                    return 1;
                }
            }
        }
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   WORD CHOICE / SYNONYM MAP
   ═══════════════════════════════════════════════════════════════ */

/* Hand-built synonym groups with register labels
 * Format: { {word, synonym, synonym, ...}, {REG_X, REG_X,...}, count }
 */
static const SynonymGroup synonym_table[] = {
    {{"big","large","huge","enormous","substantial","sizable","grand","vast"},
     {REG_INFORMAL,REG_NEUTRAL,REG_INFORMAL,REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_TECHNICAL}, 8},

    {{"small","little","tiny","minute","compact","modest","slight","minor"},
     {REG_NEUTRAL,REG_INFORMAL,REG_INFORMAL,REG_TECHNICAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL}, 8},

    {{"fast","quick","rapid","swift","speedy","brisk","hasty","expedient"},
     {REG_NEUTRAL,REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_INFORMAL,REG_NEUTRAL,REG_INFORMAL,REG_TECHNICAL}, 8},

    {{"slow","gradual","leisurely","sluggish","unhurried","deliberate"},
     {REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_INFORMAL,REG_FORMAL,REG_FORMAL}, 6},

    {{"good","great","excellent","fine","superior","admirable","notable"},
     {REG_NEUTRAL,REG_INFORMAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_FORMAL}, 7},

    {{"bad","poor","inferior","substandard","inadequate","deficient"},
     {REG_NEUTRAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_TECHNICAL}, 6},

    {{"old","ancient","aged","historic","antique","archaic","veteran"},
     {REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_TECHNICAL,REG_NEUTRAL}, 7},

    {{"new","modern","recent","contemporary","current","novel","fresh"},
     {REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_INFORMAL}, 7},

    {{"important","significant","crucial","vital","key","notable","major"},
     {REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL}, 7},

    {{"make","create","build","construct","produce","develop","generate"},
     {REG_INFORMAL,REG_NEUTRAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_TECHNICAL}, 7},

    {{"use","utilise","employ","apply","leverage","deploy","operate"},
     {REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_TECHNICAL,REG_TECHNICAL}, 7},

    {{"show","demonstrate","display","exhibit","reveal","illustrate"},
     {REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL}, 6},

    {{"say","state","assert","declare","indicate","note","mention"},
     {REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_NEUTRAL}, 7},

    {{"get","obtain","acquire","receive","gain","secure","attain"},
     {REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL}, 7},

    {{"many","numerous","several","multiple","various","diverse","abundant"},
     {REG_NEUTRAL,REG_FORMAL,REG_NEUTRAL,REG_NEUTRAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL}, 7},

    {{"start","begin","commence","initiate","launch","establish","found"},
     {REG_INFORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL}, 7},

    {{"end","finish","conclude","complete","terminate","finalise","close"},
     {REG_INFORMAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL}, 7},

    {{"need","require","demand","necessitate","call for"},
     {REG_INFORMAL,REG_FORMAL,REG_FORMAL,REG_TECHNICAL,REG_FORMAL}, 5},

    {{"help","assist","support","aid","facilitate","enable"},
     {REG_INFORMAL,REG_FORMAL,REG_NEUTRAL,REG_NEUTRAL,REG_FORMAL,REG_FORMAL}, 6},

    {{"find","discover","identify","locate","detect","uncover"},
     {REG_NEUTRAL,REG_FORMAL,REG_FORMAL,REG_NEUTRAL,REG_TECHNICAL,REG_FORMAL}, 6},

    /* Sentinel */
    {{{0}},{0},0}
};

const char* context_get_synonym(const char *word, Register reg) {
    if (!word) return word;
    char l[SYN_WORD_LEN]; lower_copy(l, word, SYN_WORD_LEN);

    for (int g = 0; synonym_table[g].count > 0; g++) {
        /* Find the word in this group */
        int found_idx = -1;
        for (int i = 0; i < synonym_table[g].count; i++)
            if (strcmp(synonym_table[g].words[i], l) == 0) {
                found_idx = i; break;
            }
        if (found_idx < 0) continue;

        /* Find best match for requested register */
        /* First try exact register match, then neutral, then any */
        for (int i = 0; i < synonym_table[g].count; i++) {
            if (i == found_idx) continue;
            if (synonym_table[g].registers[i] == reg)
                return synonym_table[g].words[i];
        }
        /* Fallback: neutral */
        for (int i = 0; i < synonym_table[g].count; i++) {
            if (i == found_idx) continue;
            if (synonym_table[g].registers[i] == REG_NEUTRAL)
                return synonym_table[g].words[i];
        }
        /* Fallback: any other */
        for (int i = 0; i < synonym_table[g].count; i++) {
            if (i != found_idx) return synonym_table[g].words[i];
        }
    }
    return word; /* no synonym found — return original */
}

const char* context_vary_word(const char *word, int turn) {
    if (!word) return word;
    char l[SYN_WORD_LEN]; lower_copy(l, word, SYN_WORD_LEN);

    for (int g = 0; synonym_table[g].count > 0; g++) {
        for (int i = 0; i < synonym_table[g].count; i++) {
            if (strcmp(synonym_table[g].words[i], l) == 0) {
                /* Rotate through group members by turn */
                int idx = turn % synonym_table[g].count;
                return synonym_table[g].words[idx];
            }
        }
    }
    return word;
}

/* ═══════════════════════════════════════════════════════════════
   INIT / STATUS
   ═══════════════════════════════════════════════════════════════ */

void context_init(void) {
    int n = 0;
    while (synonym_table[n].count > 0) n++;
    printf("[CONTEXT] Co-occurrence map: %d slots. "
           "History: %d turns. Synonyms: %d groups.\n",
           COOC_MAX_PAIRS, HISTORY_MAX_TURNS, n);
}


/* ═══════════════════════════════════════════════════════════════
   CO-OCCURRENCE PERSISTENCE
   Simple CSV: word_a,word_b,sector,count
   ═══════════════════════════════════════════════════════════════ */

#define COOC_PATH "blaf_cooc.csv"

void context_save(void) {
    FILE *f = fopen(COOC_PATH, "w");
    if (!f) return;
    for (int i = 0; i < g_cooc_count; i++)
        fprintf(f, "%s,%s,%d,%d\n",
                g_cooc[i].word_a, g_cooc[i].word_b,
                g_cooc[i].sector, g_cooc[i].count);
    fclose(f);
    printf("[CONTEXT] Saved %d co-occurrence pairs.\n", g_cooc_count);
}

void context_load(void) {
    FILE *f = fopen(COOC_PATH, "r");
    if (!f) return;
    char line[256];
    int loaded = 0;
    while (fgets(line, sizeof(line), f) && g_cooc_count < COOC_MAX_PAIRS) {
        char wa[COOC_WORD_LEN], wb[COOC_WORD_LEN];
        int sector, count;
        if (sscanf(line, "%47[^,],%47[^,],%d,%d", wa, wb, &sector, &count) == 4) {
            /* Check if already present */
            int found = 0;
            for (int i = 0; i < g_cooc_count; i++)
                if (strcmp(g_cooc[i].word_a, wa) == 0 &&
                    strcmp(g_cooc[i].word_b, wb) == 0) {
                    g_cooc[i].count += count; found = 1; break;
                }
            if (!found) {
                CoOccurrence *e = &g_cooc[g_cooc_count++];
                strncpy(e->word_a, wa, COOC_WORD_LEN-1);
                strncpy(e->word_b, wb, COOC_WORD_LEN-1);
                e->sector = (uint8_t)sector;
                e->count  = count;
                loaded++;
            }
        }
    }
    fclose(f);
    printf("[CONTEXT] Loaded %d co-occurrence pairs.\n", loaded);
}
void context_status(void) {
    printf("\n[CONTEXT STATUS]\n");
    printf("  Co-occurrences : %d pairs recorded\n", g_cooc_count);
    printf("  History turns  : %d\n", g_history_count);
    printf("  Last subject   : \"%s\"\n", context_last_subject());
    const ConversationTurn *t = get_turn(0);
    if (t) printf("  Last turn      : \"%s\"\n", t->input);
    printf("\n");
}
