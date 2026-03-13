/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_learn.c                                        ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "blaf_learn.h"
#include "blaf_mapper.h"
#include "blaf_sectors.h"

/* ═══════════════════════════════════════════════════════════════
   HARDCODED POS TABLES
   These are finite closed-class words — they never change.
   ═══════════════════════════════════════════════════════════════ */

/* ── Articles (3) ── */
static const char *ARTICLES[] = {
    "a", "an", "the", NULL
};

/* ── Auxiliary Verbs (32) ── */
static const char *AUX_VERBS[] = {
    /* Primary */
    "is", "are", "was", "were", "am",
    "be", "been", "being",
    /* Have */
    "has", "have", "had",
    /* Do */
    "do", "does", "did",
    /* Modal */
    "will", "would", "shall", "should",
    "may", "might", "must",
    "can", "could",
    /* Semi-modal */
    "need", "dare", "ought",
    /* Going to */
    "going",
    /* Get passives */
    "get", "got", "gotten",
    /* Keep */
    "keep", "kept",
    NULL
};

/* ── Pronouns (28) ── */
static const char *PRONOUNS[] = {
    "i", "me", "my", "mine", "myself",
    "you", "your", "yours", "yourself",
    "he", "him", "his", "himself",
    "she", "her", "hers", "herself",
    "it", "its", "itself",
    "we", "us", "our", "ours", "ourselves",
    "they", "them", "their", "theirs", "themselves",
    "who", "whom", "whose",
    "this", "that", "these", "those",
    NULL
};

/* ── Prepositions (42) ── */
static const char *PREPOSITIONS[] = {
    "in", "on", "at", "by", "for", "with", "about", "against",
    "between", "into", "through", "during", "before", "after",
    "above", "below", "to", "from", "up", "down", "out", "off",
    "over", "under", "again", "further", "then", "once",
    "of", "than", "as", "until", "unless", "while", "since",
    "near", "among", "along", "around", "behind", "beside",
    "beyond", "despite", "except", "inside", "outside",
    "past", "per", "plus", "toward", "within", "without",
    NULL
};

/* ── Conjunctions (20) ── */
static const char *CONJUNCTIONS[] = {
    "and", "or", "but", "nor", "yet", "so", "for",
    "because", "although", "though", "even", "while",
    "whereas", "whether", "if", "unless", "until",
    "since", "when", "whenever", "wherever", "however",
    "therefore", "thus", "hence", "moreover", "furthermore",
    "nevertheless", "nonetheless", "meanwhile",
    NULL
};

/* ── Particles ── */
static const char *PARTICLES[] = {
    "to", "not", "no", "never", "always", "also",
    "just", "only", "even", "still", "already", "yet",
    NULL
};

/* ── Common adjectives (for heuristic tagging) ── */
static const char *COMMON_ADJ[] = {
    "large", "small", "big", "little", "great", "good", "bad",
    "high", "low", "new", "old", "young", "long", "short",
    "important", "different", "same", "other", "early", "late",
    "hard", "easy", "strong", "weak", "fast", "slow",
    "dark", "light", "hot", "cold", "warm", "cool",
    "deep", "wide", "narrow", "thick", "thin", "heavy", "light",
    "complex", "simple", "general", "specific", "special",
    "human", "social", "political", "natural", "physical",
    "medical", "scientific", "economic", "cultural", "central",
    "primary", "secondary", "main", "major", "minor", "key",
    "responsible", "capable", "able", "likely", "possible",
    "common", "rare", "similar", "various", "several",
    NULL
};

/* ── Copula verbs (trigger IS-A facts) ── */
static const char *COPULA[] = {
    "is", "are", "was", "were", "am",
    "becomes", "became", "remain", "remains", "remained",
    "seems", "seemed", "appear", "appears", "appeared",
    NULL
};

/* ── Relation verbs → FactType mapping ── */
typedef struct { const char *verb; FactType type; const char *label; } VerbMapping;
static const VerbMapping VERB_MAP[] = {
    /* Location */
    {"located",    FACT_LOCATED_IN, "located_in"},
    {"situated",   FACT_LOCATED_IN, "situated_in"},
    {"found",      FACT_LOCATED_IN, "found_in"},
    {"resides",    FACT_LOCATED_IN, "resides_in"},
    {"lives",      FACT_LOCATED_IN, "lives_in"},
    {"exists",     FACT_LOCATED_IN, "exists_in"},
    /* Composition */
    {"contains",   FACT_HAS,        "contains"},
    {"consists",   FACT_HAS,        "consists_of"},
    {"composed",   FACT_MADE_OF,    "composed_of"},
    {"made",       FACT_MADE_OF,    "made_of"},
    {"built",      FACT_MADE_OF,    "built_from"},
    {"formed",     FACT_MADE_OF,    "formed_from"},
    /* Membership */
    {"belongs",    FACT_PART_OF,    "belongs_to"},
    {"part",       FACT_PART_OF,    "part_of"},
    {"member",     FACT_PART_OF,    "member_of"},
    {"includes",   FACT_HAS,        "includes"},
    {"comprises",  FACT_HAS,        "comprises"},
    /* Creation */
    {"invented",   FACT_CREATED_BY, "invented_by"},
    {"created",    FACT_CREATED_BY, "created_by"},
    {"founded",    FACT_CREATED_BY, "founded_by"},
    {"developed",  FACT_CREATED_BY, "developed_by"},
    {"discovered", FACT_CREATED_BY, "discovered_by"},
    {"designed",   FACT_CREATED_BY, "designed_by"},
    {"wrote",      FACT_CREATED_BY, "written_by"},
    /* Causation */
    {"causes",     FACT_CAUSED_BY,  "causes"},
    {"caused",     FACT_CAUSED_BY,  "caused_by"},
    {"leads",      FACT_CAUSED_BY,  "leads_to"},
    {"results",    FACT_CAUSED_BY,  "results_in"},
    {"produces",   FACT_DOES,       "produces"},
    {"generates",  FACT_DOES,       "generates"},
    /* Function */
    {"used",       FACT_USED_FOR,   "used_for"},
    {"serves",     FACT_USED_FOR,   "serves_as"},
    {"functions",  FACT_USED_FOR,   "functions_as"},
    {"acts",       FACT_USED_FOR,   "acts_as"},
    {"works",      FACT_USED_FOR,   "works_as"},
    /* Action */
    {"controls",   FACT_DOES,       "controls"},
    {"manages",    FACT_DOES,       "manages"},
    {"processes",  FACT_DOES,       "processes"},
    {"receives",   FACT_DOES,       "receives"},
    {"sends",      FACT_DOES,       "sends"},
    {"stores",     FACT_DOES,       "stores"},
    {"regulates",  FACT_DOES,       "regulates"},
    {"coordinates",FACT_DOES,       "coordinates"},
    {"connects",   FACT_DOES,       "connects"},
    {"supports",   FACT_DOES,       "supports"},
    {"pumps",      FACT_DOES,       "pumps"},
    {"carries",    FACT_DOES,       "carries"},
    {"protects",   FACT_DOES,       "protects"},
    {"provides",   FACT_DOES,       "provides"},
    {"allows",     FACT_DOES,       "allows"},
    {"enables",    FACT_DOES,       "enables"},
    {NULL, FACT_GENERAL, NULL}
};

/* ═══════════════════════════════════════════════════════════════
   FACT STORE
   ═══════════════════════════════════════════════════════════════ */

static FactTriple g_facts[MAX_FACTS_STORED];
static int        g_fact_count = 0;

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

static int in_table(const char **table, const char *word) {
    char l[MAX_TOKEN_LEN]; lower_copy(l, word, MAX_TOKEN_LEN);
    for (int i = 0; table[i]; i++)
        if (strcmp(table[i], l) == 0) return 1;
    return 0;
}

/* Suffix rules for POS heuristics */
static POSClass suffix_guess(const char *word) {
    int len = strlen(word);
    if (len < 3) return POS_UNKNOWN;

    /* Verb suffixes */
    if (len > 3 && strcmp(word+len-3, "ing") == 0) return POS_VERB; /* running */
    if (len > 2 && strcmp(word+len-2, "ed") == 0)  return POS_VERB; /* walked */
    if (len > 1 && strcmp(word+len-1, "s") == 0 &&
        len > 4) {
        /* ambiguous — could be verb (runs) or noun (dogs) */
        /* if preceded by noun context, lean noun; default verb */
        return POS_VERB;
    }

    /* Noun suffixes */
    if (len > 4 && strcmp(word+len-4, "tion") == 0)  return POS_NOUN;
    if (len > 4 && strcmp(word+len-4, "sion") == 0)  return POS_NOUN;
    if (len > 4 && strcmp(word+len-4, "ment") == 0)  return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ity") == 0)   return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ism") == 0)   return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ist") == 0)   return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ess") == 0)   return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ery") == 0)   return POS_NOUN;
    if (len > 4 && strcmp(word+len-4, "ness") == 0)  return POS_NOUN;
    if (len > 4 && strcmp(word+len-4, "ance") == 0)  return POS_NOUN;
    if (len > 4 && strcmp(word+len-4, "ence") == 0)  return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "age") == 0)   return POS_NOUN;
    if (len > 3 && strcmp(word+len-3, "ure") == 0)   return POS_NOUN;
    if (len > 2 && strcmp(word+len-2, "er") == 0)    return POS_NOUN; /* teacher */
    if (len > 2 && strcmp(word+len-2, "or") == 0)    return POS_NOUN; /* doctor */

    /* Adjective suffixes */
    if (len > 3 && strcmp(word+len-3, "ful") == 0)  return POS_ADJ;
    if (len > 4 && strcmp(word+len-4, "less") == 0) return POS_ADJ;
    if (len > 3 && strcmp(word+len-3, "ous") == 0)  return POS_ADJ;
    if (len > 3 && strcmp(word+len-3, "ive") == 0)  return POS_ADJ;
    if (len > 3 && strcmp(word+len-3, "ble") == 0)  return POS_ADJ;
    if (len > 2 && strcmp(word+len-2, "al") == 0)   return POS_ADJ;
    if (len > 2 && strcmp(word+len-2, "ic") == 0)   return POS_ADJ;
    if (len > 3 && strcmp(word+len-3, "ary") == 0)  return POS_ADJ;
    if (len > 4 && strcmp(word+len-4, "ical") == 0) return POS_ADJ;

    /* Adverb suffixes */
    if (len > 2 && strcmp(word+len-2, "ly") == 0)   return POS_ADV;

    return POS_UNKNOWN;
}

/* Lemmatize a word — strip common inflections */
static void lemmatize(const char *word, char *lemma, int len) {
    lower_copy(lemma, word, len);
    int wlen = strlen(lemma);

    /* -ing → base (running → run, taking → take) */
    if (wlen > 5 && strcmp(lemma+wlen-3, "ing") == 0) {
        lemma[wlen-3] = '\0';
        /* if ends in consonant+consonant, drop one: running→run */
        int l = strlen(lemma);
        if (l > 1 && lemma[l-1] == lemma[l-2] &&
            !strchr("aeiou", lemma[l-1]))
            lemma[l-1] = '\0';
        return;
    }
    /* -ed → base */
    if (wlen > 4 && strcmp(lemma+wlen-2, "ed") == 0) {
        lemma[wlen-2] = '\0'; return;
    }
    /* -es → base */
    if (wlen > 4 && strcmp(lemma+wlen-2, "es") == 0) {
        lemma[wlen-2] = '\0'; return;
    }
    /* -s → base (basic, may over-strip) */
    if (wlen > 4 && lemma[wlen-1] == 's' &&
        lemma[wlen-2] != 's' && lemma[wlen-2] != 'u') {
        lemma[wlen-1] = '\0'; return;
    }
}

/* ═══════════════════════════════════════════════════════════════
   TOKENIZER
   Properly splits on whitespace and punctuation.
   Handles contractions. Does NOT produce substrings.
   ═══════════════════════════════════════════════════════════════ */

static int tokenize_sentence(const char *sentence,
                              LearnToken *tokens, int max) {
    int count = 0;
    const char *p = sentence;

    while (*p && count < max) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Skip standalone punctuation (not inside words) */
        if (ispunct((unsigned char)*p) && *p != '\'') {
            /* Store as PUNCT token */
            if (*p == '.' || *p == ',' || *p == ';' || *p == ':' ||
                *p == '!' || *p == '?' || *p == '(' || *p == ')') {
                tokens[count].word[0] = *p;
                tokens[count].word[1] = '\0';
                tokens[count].pos     = POS_PUNCT;
                tokens[count].position = count;
                tokens[count].is_sentence_start = (count == 0);
                count++;
                p++;
                continue;
            }
            p++; continue;
        }

        /* Read a word (letters, digits, hyphens, apostrophes) */
        char buf[MAX_TOKEN_LEN];
        int  j = 0;
        int  was_upper = isupper((unsigned char)*p);

        while (*p && j < MAX_TOKEN_LEN-1 &&
               (isalnum((unsigned char)*p) ||
                *p == '\'' || *p == '-')) {
            buf[j++] = *p++;
        }
        buf[j] = '\0';

        if (j == 0) { p++; continue; }

        /* Strip trailing apostrophe/hyphen */
        while (j > 0 && (buf[j-1] == '\'' || buf[j-1] == '-'))
            buf[--j] = '\0';
        if (j == 0) continue;

        /* Handle contractions: "isn't" → "is" + "not"
         *                       "they're" → "they" + "are"  */
        char *apos = strchr(buf, '\'');
        if (apos) {
            /* Split at apostrophe */
            *apos = '\0';
            char part1[MAX_TOKEN_LEN], part2[MAX_TOKEN_LEN];
            strncpy(part1, buf, MAX_TOKEN_LEN-1);
            strncpy(part2, apos+1, MAX_TOKEN_LEN-1);

            /* Expand common contractions */
            const char *exp = NULL;
            if      (strcmp(part2,"t")==0||strcmp(part2,"nt")==0) exp="not";
            else if (strcmp(part2,"re")==0)  exp="are";
            else if (strcmp(part2,"ve")==0)  exp="have";
            else if (strcmp(part2,"ll")==0)  exp="will";
            else if (strcmp(part2,"d")==0)   exp="would";
            else if (strcmp(part2,"s")==0)   exp="is";
            else if (strcmp(part2,"m")==0)   exp="am";

            /* Store part1 */
            lower_copy(tokens[count].word, part1, MAX_TOKEN_LEN);
            lemmatize(part1, tokens[count].lemma, MAX_TOKEN_LEN);
            tokens[count].is_capitalized    = was_upper;
            tokens[count].is_sentence_start = (count == 0);
            tokens[count].position          = count;
            count++;

            /* Store expanded part2 */
            if (exp && count < max) {
                strncpy(tokens[count].word,  exp, MAX_TOKEN_LEN-1);
                strncpy(tokens[count].lemma, exp, MAX_TOKEN_LEN-1);
                tokens[count].is_capitalized    = 0;
                tokens[count].is_sentence_start = 0;
                tokens[count].position          = count;
                count++;
            }
            continue;
        }

        lower_copy(tokens[count].word, buf, MAX_TOKEN_LEN);
        lemmatize(buf, tokens[count].lemma, MAX_TOKEN_LEN);
        tokens[count].is_capitalized    = was_upper && (count > 0);
        tokens[count].is_sentence_start = (count == 0);
        tokens[count].position          = count;
        count++;
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
   POS TAGGER
   Multi-pass: closed class → suffix → context
   ═══════════════════════════════════════════════════════════════ */

int learn_tag_tokens(LearnToken *tokens, int count) {
    /* Pass 1: closed-class lookup (deterministic) */
    for (int i = 0; i < count; i++) {
        const char *w = tokens[i].word;
        tokens[i].pos   = POS_UNKNOWN;
        tokens[i].trust = 0;

        if (in_table(ARTICLES,     w)) { tokens[i].pos=POS_ARTICLE;  tokens[i].trust=3; continue; }
        if (in_table(AUX_VERBS,    w)) { tokens[i].pos=POS_AUX_VERB; tokens[i].trust=3; continue; }
        if (in_table(PRONOUNS,     w)) { tokens[i].pos=POS_PRONOUN;  tokens[i].trust=3; continue; }
        if (in_table(PREPOSITIONS, w)) { tokens[i].pos=POS_PREP;     tokens[i].trust=3; continue; }
        if (in_table(CONJUNCTIONS, w)) { tokens[i].pos=POS_CONJ;     tokens[i].trust=3; continue; }
        if (in_table(PARTICLES,    w)) { tokens[i].pos=POS_PARTICLE; tokens[i].trust=3; continue; }
        if (in_table(COMMON_ADJ,   w)) { tokens[i].pos=POS_ADJ;      tokens[i].trust=2; continue; }

        /* Punctuation */
        if (tokens[i].pos == POS_PUNCT) continue;

        /* Proper noun — capitalized and not sentence start */
        if (tokens[i].is_capitalized && !tokens[i].is_sentence_start) {
            tokens[i].pos   = POS_PROPER;
            tokens[i].trust = 1;
            continue;
        }

        /* Pure number */
        int is_num = 1;
        for (int j = 0; tokens[i].word[j]; j++)
            if (!isdigit((unsigned char)tokens[i].word[j]) &&
                tokens[i].word[j] != '.' && tokens[i].word[j] != ',')
                { is_num = 0; break; }
        if (is_num) { tokens[i].pos=POS_NUMERAL; tokens[i].trust=3; continue; }
    }

    /* Pass 2: suffix heuristics for unknowns */
    for (int i = 0; i < count; i++) {
        if (tokens[i].pos == POS_UNKNOWN) {
            tokens[i].pos   = suffix_guess(tokens[i].word);
            tokens[i].trust = 1;
        }
    }

    /* Pass 3: context correction
     * - ARTICLE → next token is NOUN or ADJ+NOUN
     * - PREP → next token should be NOUN phrase (ARTICLE+NOUN)
     * - Unknown after AUX → probably VERB or ADJ
     * - Unknown at end → probably NOUN
     */
    for (int i = 0; i < count; i++) {
        if (tokens[i].pos != POS_UNKNOWN) continue;

        /* After article → NOUN */
        if (i > 0 && tokens[i-1].pos == POS_ARTICLE) {
            tokens[i].pos   = POS_NOUN;
            tokens[i].trust = 1;
            continue;
        }
        /* After auxiliary verb → VERB or ADJ */
        if (i > 0 && tokens[i-1].pos == POS_AUX_VERB) {
            tokens[i].pos   = POS_VERB;
            tokens[i].trust = 1;
            continue;
        }
        /* After adjective → NOUN */
        if (i > 0 && tokens[i-1].pos == POS_ADJ) {
            tokens[i].pos   = POS_NOUN;
            tokens[i].trust = 1;
            continue;
        }
        /* Last content word → NOUN */
        if (i == count-1 || tokens[i+1].pos == POS_PUNCT) {
            tokens[i].pos   = POS_NOUN;
            tokens[i].trust = 1;
        }
    }

    /* Pass 4: remaining unknowns → NOUN (safe default) */
    for (int i = 0; i < count; i++)
        if (tokens[i].pos == POS_UNKNOWN) {
            tokens[i].pos   = POS_NOUN;
            tokens[i].trust = 0;
        }

    return count;
}

/* ═══════════════════════════════════════════════════════════════
   SUBJECT FINDER
   ═══════════════════════════════════════════════════════════════ */

int learn_find_subject(LearnToken *tokens, int count) {
    /* Find the main verb position first */
    int main_verb = -1;
    for (int i = 0; i < count; i++) {
        if (tokens[i].pos == POS_AUX_VERB ||
            tokens[i].pos == POS_VERB) {
            main_verb = i; break;
        }
    }

    int search_end = (main_verb > 0) ? main_verb : count;

    /* Priority 1: PROPER noun before main verb */
    for (int i = 0; i < search_end; i++)
        if (tokens[i].pos == POS_PROPER) return i;

    /* Priority 2: NOUN after article before main verb */
    for (int i = 1; i < search_end; i++)
        if (tokens[i].pos == POS_NOUN &&
            tokens[i-1].pos == POS_ARTICLE) return i;

    /* Priority 3: Any NOUN before main verb */
    for (int i = 0; i < search_end; i++)
        if (tokens[i].pos == POS_NOUN) return i;

    /* Priority 4: PRONOUN */
    for (int i = 0; i < search_end; i++)
        if (tokens[i].pos == POS_PRONOUN) return i;

    /* Fallback: first non-article, non-punct token */
    for (int i = 0; i < count; i++)
        if (tokens[i].pos != POS_ARTICLE &&
            tokens[i].pos != POS_PUNCT) return i;

    return -1;
}

/* ═══════════════════════════════════════════════════════════════
   NOUN PHRASE BUILDER
   Collects article + adj* + noun (+ noun)* as one phrase.
   Skips articles in the stored phrase but includes adjectives.
   ═══════════════════════════════════════════════════════════════ */

static int build_noun_phrase(LearnToken *tokens, int start, int count,
                              char *out, int outlen) {
    out[0] = '\0';
    int i    = start;
    int added = 0;

    /* Skip leading articles */
    while (i < count && tokens[i].pos == POS_ARTICLE) i++;

    /* Collect adj* noun+ */
    while (i < count) {
        POSClass p = tokens[i].pos;
        if (p == POS_NOUN || p == POS_PROPER || p == POS_ADJ ||
            p == POS_NUMERAL || p == POS_GERUND) {
            if (added > 0) strncat(out, " ", outlen-strlen(out)-1);
            strncat(out, tokens[i].word, outlen-strlen(out)-1);
            added++;
            i++;
        } else {
            break;
        }
    }
    return i; /* next position after phrase */
}

/* ═══════════════════════════════════════════════════════════════
   STORE FACT — deduplicate
   ═══════════════════════════════════════════════════════════════ */

static void store_fact(const char *subject, const char *predicate,
                        const char *object, FactType type,
                        uint8_t sector, float confidence) {
    if (!subject[0] || !object[0]) return;
    if (strcmp(subject, object) == 0) return;

    /* Deduplicate */
    for (int i = 0; i < g_fact_count; i++)
        if (strcasecmp(g_facts[i].subject,   subject)   == 0 &&
            strcasecmp(g_facts[i].predicate, predicate) == 0 &&
            strcasecmp(g_facts[i].object,    object)    == 0)
            return;

    if (g_fact_count >= MAX_FACTS_STORED) return;

    FactTriple *f = &g_facts[g_fact_count++];
    strncpy(f->subject,   subject,   MAX_FACT_LEN-1);
    strncpy(f->predicate, predicate, 63);
    strncpy(f->object,    object,    MAX_FACT_LEN-1);
    f->type       = type;
    f->sector     = sector;
    f->confidence = confidence;

    printf("[FACT] %-20s [%-16s] %s\n", subject, predicate, object);
}

/* ═══════════════════════════════════════════════════════════════
   FACT EXTRACTION
   Identifies patterns and calls store_fact with full phrases.
   ═══════════════════════════════════════════════════════════════ */

static void extract_facts(LearnToken *tokens, int count,
                            int subj_idx, const char *subject,
                            uint8_t sector, LearnResult *result) {

    for (int i = subj_idx + 1; i < count; i++) {
        if (tokens[i].pos == POS_PUNCT) break;

        /* ── PATTERN 1: COPULA (subject IS/ARE/WAS object) ── */
        if (tokens[i].pos == POS_AUX_VERB &&
            in_table(COPULA, tokens[i].word)) {

            /* Check if negated: "is not" */
            int neg = (i+1 < count &&
                       strcmp(tokens[i+1].word, "not") == 0);
            int obj_start = neg ? i+2 : i+1;

            char object[MAX_FACT_LEN] = {0};
            int  next = build_noun_phrase(tokens, obj_start, count,
                                           object, sizeof(object));

            /* Also collect prepositional tail:
             * "brain is center OF THE NERVOUS SYSTEM"
             * → append " of the nervous system" */
            while (next < count &&
                   tokens[next].pos == POS_PREP &&
                   next+1 < count) {
                strncat(object, " ", sizeof(object)-strlen(object)-1);
                strncat(object, tokens[next].word,
                        sizeof(object)-strlen(object)-1);
                char np[MAX_FACT_LEN] = {0};
                next = build_noun_phrase(tokens, next+1, count,
                                          np, sizeof(np));
                if (np[0]) {
                    strncat(object, " ", sizeof(object)-strlen(object)-1);
                    strncat(object, np, sizeof(object)-strlen(object)-1);
                }
            }

            if (object[0] && !neg) {
                store_fact(subject, tokens[i].word,
                            object, FACT_IS, sector, 0.9f);
                if (result->fact_count < 16) {
                    FactTriple *f = &result->facts[result->fact_count++];
                    strncpy(f->subject,   subject,         MAX_FACT_LEN-1);
                    strncpy(f->predicate, tokens[i].word,  63);
                    strncpy(f->object,    object,          MAX_FACT_LEN-1);
                    f->type = FACT_IS; f->sector = sector;
                }
            }
            break;
        }

        /* ── PATTERN 2: KNOWN RELATION VERB ── */
        if (tokens[i].pos == POS_VERB || tokens[i].pos == POS_AUX_VERB) {
            const char *pred_label = NULL;
            FactType    pred_type  = FACT_GENERAL;

            for (int v = 0; VERB_MAP[v].verb; v++) {
                if (strcmp(tokens[i].lemma, VERB_MAP[v].verb) == 0 ||
                    strcmp(tokens[i].word,  VERB_MAP[v].verb) == 0) {
                    pred_label = VERB_MAP[v].label;
                    pred_type  = VERB_MAP[v].type;
                    break;
                }
            }

            /* Skip filler aux verbs like "does", "did" */
            if (!pred_label &&
                (strcmp(tokens[i].word,"does")==0 ||
                 strcmp(tokens[i].word,"did") ==0 ||
                 strcmp(tokens[i].word,"do")  ==0))
                continue;

            /* Find the object — skip prepositions to get the NP */
            int obj_start = i+1;
            while (obj_start < count &&
                   (tokens[obj_start].pos == POS_ARTICLE ||
                    tokens[obj_start].pos == POS_PREP)) {
                if (tokens[obj_start].pos == POS_PREP) {
                    /* Prep gives us relation hint */
                    if (strcmp(tokens[obj_start].word,"of")==0 &&
                        !pred_label) pred_label = "part_of";
                    if (strcmp(tokens[obj_start].word,"by")==0 &&
                        !pred_label) pred_label = "created_by";
                    if ((strcmp(tokens[obj_start].word,"in")==0 ||
                         strcmp(tokens[obj_start].word,"at")==0) &&
                        !pred_label) pred_label = "located_in";
                }
                obj_start++;
            }

            char object[MAX_FACT_LEN] = {0};
            build_noun_phrase(tokens, obj_start, count,
                               object, sizeof(object));

            if (object[0]) {
                const char *label = pred_label ? pred_label : tokens[i].lemma;
                store_fact(subject, label, object,
                            pred_type, sector,
                            pred_label ? 0.85f : 0.6f);
                if (result->fact_count < 16) {
                    FactTriple *f = &result->facts[result->fact_count++];
                    strncpy(f->subject,   subject, MAX_FACT_LEN-1);
                    strncpy(f->predicate, label,   63);
                    strncpy(f->object,    object,  MAX_FACT_LEN-1);
                    f->type   = pred_type;
                    f->sector = sector;
                }
            }
            break;
        }

        /* ── PATTERN 3: PREPOSITIONAL (subject IN/OF/BY noun) ── */
        if (tokens[i].pos == POS_PREP) {
            const char *prep = tokens[i].word;
            char object[MAX_FACT_LEN] = {0};
            build_noun_phrase(tokens, i+1, count, object, sizeof(object));

            if (object[0]) {
                char label[64];
                if      (strcmp(prep,"in")==0||strcmp(prep,"at")==0)
                    snprintf(label,64,"located_in");
                else if (strcmp(prep,"of")==0)
                    snprintf(label,64,"part_of");
                else if (strcmp(prep,"by")==0)
                    snprintf(label,64,"created_by");
                else if (strcmp(prep,"for")==0)
                    snprintf(label,64,"used_for");
                else
                    snprintf(label,64,"related_to_%s", prep);

                store_fact(subject, label, object,
                            FACT_RELATION, sector, 0.5f);
                if (result->fact_count < 16) {
                    FactTriple *f = &result->facts[result->fact_count++];
                    strncpy(f->subject,   subject, MAX_FACT_LEN-1);
                    strncpy(f->predicate, label,   63);
                    strncpy(f->object,    object,  MAX_FACT_LEN-1);
                    f->type = FACT_RELATION; f->sector = sector;
                }
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   WORD REGISTRATION — map every content word
   ═══════════════════════════════════════════════════════════════ */

static int register_tokens(LearnToken *tokens, int count, uint8_t sector) {
    int mapped = 0;
    for (int i = 0; i < count; i++) {
        if (tokens[i].pos == POS_ARTICLE  ||
            tokens[i].pos == POS_PUNCT    ||
            tokens[i].pos == POS_PRONOUN  ||
            tokens[i].pos == POS_PARTICLE ||
            tokens[i].pos == POS_CONJ) continue;

        if (strlen(tokens[i].word) < 2) continue;

        /* Convert POS to CLASS for concept table */
        uint8_t cls = 0;
        switch (tokens[i].pos) {
            case POS_VERB:    cls=1; break;
            case POS_AUX_VERB:cls=9; break;
            case POS_ADJ:     cls=2; break;
            case POS_ADV:     cls=4; break;
            case POS_PREP:    cls=3; break;
            case POS_NUMERAL: cls=11; break;
            default:          cls=0; break; /* NOUN */
        }

        map_word_web(tokens[i].word, cls, sector, 0xFF, 0xFF);
        mapped++;
    }
    return mapped;
}

/* ═══════════════════════════════════════════════════════════════
   LEARN SENTENCE  — master function
   ═══════════════════════════════════════════════════════════════ */

LearnResult learn_sentence(const char *sentence, uint8_t sector_hint) {
    LearnResult result;
    memset(&result, 0, sizeof(result));

    if (!sentence || !sentence[0]) return result;

    /* 1. Tokenize */
    LearnToken tokens[MAX_TOKENS_PER_SENT];
    memset(tokens, 0, sizeof(tokens));
    int count = tokenize_sentence(sentence, tokens, MAX_TOKENS_PER_SENT);
    if (count == 0) return result;

    /* 2. POS tag */
    learn_tag_tokens(tokens, count);

    /* 3. Find subject */
    int subj_idx = learn_find_subject(tokens, count);
    if (subj_idx < 0) {
        /* No subject found — register words anyway */
        result.new_words_mapped = register_tokens(tokens, count, sector_hint);
        return result;
    }

    /* Build full subject phrase (not just one word) */
    char subject[MAX_FACT_LEN] = {0};
    /* Include preceding adjectives */
    int phrase_start = subj_idx;
    while (phrase_start > 0 &&
           (tokens[phrase_start-1].pos == POS_ADJ ||
            tokens[phrase_start-1].pos == POS_ARTICLE))
        phrase_start--;
    /* Skip articles in subject phrase */
    int i = phrase_start;
    while (i < count && tokens[i].pos == POS_ARTICLE) i++;
    /* Collect adj + noun */
    int first = 1;
    while (i <= subj_idx + 1 && i < count) {
        if (tokens[i].pos == POS_PUNCT) break;
        if (tokens[i].pos != POS_ARTICLE) {
            if (!first) strncat(subject, " ", sizeof(subject)-strlen(subject)-1);
            strncat(subject, tokens[i].word, sizeof(subject)-strlen(subject)-1);
            first = 0;
        }
        i++;
    }
    if (!subject[0])
        strncpy(subject, tokens[subj_idx].word, MAX_FACT_LEN-1);

    strncpy(result.subject, subject, MAX_FACT_LEN-1);

    /* 4. Extract facts */
    extract_facts(tokens, count, subj_idx, subject, sector_hint, &result);

    /* 5. Register all content words */
    result.new_words_mapped = register_tokens(tokens, count, sector_hint);

    return result;
}

/* ═══════════════════════════════════════════════════════════════
   LEARN PARAGRAPH
   ═══════════════════════════════════════════════════════════════ */

int learn_paragraph(const char *text, uint8_t sector_hint) {
    if (!text) return 0;
    int total = 0;
    char buf[65536]; strncpy(buf, text, sizeof(buf)-1);

    char *start = buf;
    char *p     = buf;

    while (*p) {
        if (*p == '.' || *p == '!' || *p == '?') {
            char next = *(p+1);
            if (next == ' ' || next == '\n' || next == '\0') {
                *p = '\0';
                while (*start == ' ' || *start == '\n') start++;
                if (strlen(start) > 10) {
                    LearnResult r = learn_sentence(start, sector_hint);
                    total += r.fact_count;
                }
                start = p + 1;
            }
        }
        p++;
    }
    /* trailing */
    while (*start == ' ' || *start == '\n') start++;
    if (strlen(start) > 10) {
        LearnResult r = learn_sentence(start, sector_hint);
        total += r.fact_count;
    }
    return total;
}

/* ═══════════════════════════════════════════════════════════════
   QUERY / PRINT FACTS
   ═══════════════════════════════════════════════════════════════ */

int learn_query_facts(const char *subject, FactTriple *out, int max) {
    int n = 0;
    for (int i = 0; i < g_fact_count && n < max; i++)
        if (strcasecmp(g_facts[i].subject, subject) == 0)
            out[n++] = g_facts[i];
    return n;
}

void learn_print_facts(const char *subject) {
    int found = 0;
    printf("\n--- FACTS ABOUT: %s ---\n", subject);
    for (int i = 0; i < g_fact_count; i++) {
        if (strcasecmp(g_facts[i].subject, subject) == 0) {
            printf("  [%s] %-18s → %s\n",
                   fact_type_name(g_facts[i].type),
                   g_facts[i].predicate,
                   g_facts[i].object);
            found++;
        }
    }
    if (!found) printf("  No facts stored yet.\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════
   PERSISTENCE
   ═══════════════════════════════════════════════════════════════ */

#define FACTS_PATH "blaf_facts.bin"

void learn_save(const char *path) {
    const char *fp = path ? path : FACTS_PATH;
    FILE *f = fopen(fp, "wb");
    if (!f) return;
    uint32_t count = (uint32_t)g_fact_count;
    fwrite(&count, 4, 1, f);
    fwrite(g_facts, sizeof(FactTriple), g_fact_count, f);
    fclose(f);
    printf("[LEARN] Saved %d facts to %s\n", g_fact_count, fp);
}

void learn_load(const char *path) {
    const char *fp = path ? path : FACTS_PATH;
    FILE *f = fopen(fp, "rb");
    if (!f) return;
    uint32_t count;
    fread(&count, 4, 1, f);
    if (count > MAX_FACTS_STORED) count = MAX_FACTS_STORED;
    int loaded = (int)fread(g_facts, sizeof(FactTriple), count, f);
    fclose(f);
    g_fact_count = loaded;
    printf("[LEARN] Loaded %d facts from %s\n", loaded, fp);
}

/* ═══════════════════════════════════════════════════════════════
   INIT / NAMES
   ═══════════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════════
   PUBLIC TOKENIZE + TAG
   Called by blaf_core.c's tokenize_and_tag_local() so that the
   core can update noun_hits/verb_hits/adj_hits per word.
   ═══════════════════════════════════════════════════════════════ */
int learn_tokenize_and_tag(const char *text, LearnToken *out, int max) {
    if (!text || !out || max <= 0) return 0;
    memset(out, 0, sizeof(LearnToken) * max);
    int count = tokenize_sentence(text, out, max);
    learn_tag_tokens(out, count);
    return count;
}

void learn_init(void) {
    /* Count aux verbs for status */
    int naux=0, nprep=0, nconj=0;
    while(AUX_VERBS[naux])  naux++;
    while(PREPOSITIONS[nprep]) nprep++;
    while(CONJUNCTIONS[nconj]) nconj++;
    int nverb=0;
    while(VERB_MAP[nverb].verb) nverb++;

    printf("[LEARN] POS engine ready — "
           "aux_verbs:%d preps:%d conj:%d relation_verbs:%d\n",
           naux, nprep, nconj, nverb);
    learn_load(NULL);
}

const char* pos_name(POSClass p) {
    switch(p) {
        case POS_NOUN:     return "NOUN";
        case POS_VERB:     return "VERB";
        case POS_AUX_VERB: return "AUX";
        case POS_ADJ:      return "ADJ";
        case POS_ADV:      return "ADV";
        case POS_ARTICLE:  return "ART";
        case POS_PREP:     return "PREP";
        case POS_CONJ:     return "CONJ";
        case POS_PRONOUN:  return "PRON";
        case POS_NUMERAL:  return "NUM";
        case POS_PUNCT:    return "PUNCT";
        case POS_PROPER:   return "PROPER";
        case POS_GERUND:   return "GERUND";
        case POS_PARTICLE: return "PART";
        default:           return "?";
    }
}

const char* fact_type_name(FactType t) {
    switch(t) {
        case FACT_IS:         return "IS";
        case FACT_HAS:        return "HAS";
        case FACT_DOES:       return "DOES";
        case FACT_LOCATED_IN: return "LOCATED_IN";
        case FACT_PART_OF:    return "PART_OF";
        case FACT_MADE_OF:    return "MADE_OF";
        case FACT_CAUSED_BY:  return "CAUSED_BY";
        case FACT_USED_FOR:   return "USED_FOR";
        case FACT_CREATED_BY: return "CREATED_BY";
        case FACT_PROPERTY:   return "PROPERTY";
        case FACT_RELATION:   return "RELATION";
        default:              return "GENERAL";
    }
}
