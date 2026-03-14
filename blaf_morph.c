/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_morph.c                                        ║
 * ║  Morphology: Lemmatizer + Autocorrect + Variant Registration ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "blaf_morph.h"
#include "blaf_mapper.h"
#include "blaf_sectors.h"

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

static int is_vowel(char c) {
    c = tolower((unsigned char)c);
    return c=='a'||c=='e'||c=='i'||c=='o'||c=='u';
}

/* ═══════════════════════════════════════════════════════════════
   IRREGULAR FORMS TABLE
   Maps inflected → base form.
   Covers the most common English irregulars across:
     - Verbs (tenses)
     - Nouns (plurals)
   ═══════════════════════════════════════════════════════════════ */

typedef struct { const char *inflected; const char *base; } IrregularForm;

static const IrregularForm irregulars[] = {

    /* ── Irregular verb tenses ── */
    /* be */
    {"am",          "be"},    {"is",          "be"},
    {"are",         "be"},    {"was",         "be"},
    {"were",        "be"},    {"been",        "be"},
    {"being",       "be"},
    /* have */
    {"has",         "have"},  {"had",         "have"},
    {"having",      "have"},
    /* do */
    {"does",        "do"},    {"did",         "do"},
    {"done",        "do"},    {"doing",       "do"},
    /* go */
    {"goes",        "go"},    {"went",        "go"},
    {"gone",        "go"},    {"going",       "go"},
    /* get */
    {"gets",        "get"},   {"got",         "get"},
    {"gotten",      "get"},   {"getting",     "get"},
    /* make */
    {"makes",       "make"},  {"made",        "make"},
    {"making",      "make"},
    /* take */
    {"takes",       "take"},  {"took",        "take"},
    {"taken",       "take"},  {"taking",      "take"},
    /* come */
    {"comes",       "come"},  {"came",        "come"},
    {"coming",      "come"},
    /* know */
    {"knows",       "know"},  {"knew",        "know"},
    {"known",       "know"},  {"knowing",     "know"},
    /* see */
    {"sees",        "see"},   {"saw",         "see"},
    {"seen",        "see"},   {"seeing",      "see"},
    /* say */
    {"says",        "say"},   {"said",        "say"},
    {"saying",      "say"},
    /* give */
    {"gives",       "give"},  {"gave",        "give"},
    {"given",       "give"},  {"giving",      "give"},
    /* find */
    {"finds",       "find"},  {"found",       "find"},
    {"finding",     "find"},
    /* think */
    {"thinks",      "think"}, {"thought",     "think"},
    {"thinking",    "think"},
    /* tell */
    {"tells",       "tell"},  {"told",        "tell"},
    {"telling",     "tell"},
    /* run */
    {"runs",        "run"},   {"ran",         "run"},
    {"running",     "run"},
    /* write */
    {"writes",      "write"}, {"wrote",       "write"},
    {"written",     "write"}, {"writing",     "write"},
    /* read */
    {"reads",       "read"},  {"reading",     "read"},
    /* bring */
    {"brings",      "bring"}, {"brought",     "bring"},
    {"bringing",    "bring"},
    /* keep */
    {"keeps",       "keep"},  {"kept",        "keep"},
    {"keeping",     "keep"},
    /* hold */
    {"holds",       "hold"},  {"held",        "hold"},
    {"holding",     "hold"},
    /* stand */
    {"stands",      "stand"}, {"stood",       "stand"},
    {"standing",    "stand"},
    /* send */
    {"sends",       "send"},  {"sent",        "send"},
    {"sending",     "send"},
    /* buy */
    {"buys",        "buy"},   {"bought",      "buy"},
    {"buying",      "buy"},
    /* build */
    {"builds",      "build"}, {"built",       "build"},
    {"building",    "build"},
    /* fall */
    {"falls",       "fall"},  {"fell",        "fall"},
    {"fallen",      "fall"},  {"falling",     "fall"},
    /* cut */
    {"cuts",        "cut"},   {"cutting",     "cut"},
    /* put */
    {"puts",        "put"},   {"putting",     "put"},
    /* set */
    {"sets",        "set"},   {"setting",     "set"},
    /* sit */
    {"sits",        "sit"},   {"sat",         "sit"},
    {"sitting",     "sit"},
    /* speak */
    {"speaks",      "speak"}, {"spoke",       "speak"},
    {"spoken",      "speak"}, {"speaking",    "speak"},
    /* begin */
    {"begins",      "begin"}, {"began",       "begin"},
    {"begun",       "begin"}, {"beginning",   "begin"},
    /* show */
    {"shows",       "show"},  {"showed",      "show"},
    {"shown",       "show"},  {"showing",     "show"},
    /* pay */
    {"pays",        "pay"},   {"paid",        "pay"},
    {"paying",      "pay"},
    /* lead */
    {"leads",       "lead"},  {"led",         "lead"},
    {"leading",     "lead"},
    /* feel */
    {"feels",       "feel"},  {"felt",        "feel"},
    {"feeling",     "feel"},
    /* leave */
    {"leaves",      "leave"}, {"left",        "leave"},
    {"leaving",     "leave"},
    /* lose */
    {"loses",       "lose"},  {"lost",        "lose"},
    {"losing",      "lose"},
    /* mean */
    {"means",       "mean"},  {"meant",       "mean"},
    {"meaning",     "mean"},
    /* become */
    {"becomes",     "become"},{"became",      "become"},
    {"becoming",    "become"},
    /* use */
    {"uses",        "use"},   {"used",        "use"},
    {"using",       "use"},

    /* ── Irregular noun plurals ── */
    {"men",         "man"},       {"women",       "woman"},
    {"children",    "child"},     {"people",      "person"},
    {"mice",        "mouse"},     {"geese",       "goose"},
    {"teeth",       "tooth"},     {"feet",        "foot"},
    {"oxen",        "ox"},        {"fungi",       "fungus"},
    {"cacti",       "cactus"},    {"alumni",      "alumnus"},
    {"nuclei",      "nucleus"},   {"stimuli",     "stimulus"},
    {"syllabi",     "syllabus"},  {"criteria",    "criterion"},
    {"phenomena",   "phenomenon"},{"data",        "datum"},
    {"media",       "medium"},    {"bacteria",    "bacterium"},
    {"analyses",    "analysis"},  {"crises",      "crisis"},
    {"theses",      "thesis"},    {"diagnoses",   "diagnosis"},
    {"hypotheses",  "hypothesis"},{"oases",       "oasis"},
    {"indices",     "index"},     {"matrices",    "matrix"},
    {"vertices",    "vertex"},    {"axes",        "axis"},
    {"leaves",      "leaf"},      {"lives",       "life"},
    {"knives",      "knife"},     {"wives",       "wife"},
    {"wolves",      "wolf"},      {"halves",      "half"},
    {"scarves",     "scarf"},     {"shelves",     "shelf"},
    {"loaves",      "loaf"},      {"selves",      "self"},
    {"thieves",     "thief"},     {"calves",      "calf"},
    /* identical singular/plural */
    {"sheep",       "sheep"},     {"deer",        "deer"},
    {"fish",        "fish"},      {"species",     "species"},
    {"aircraft",    "aircraft"},  {"series",      "series"},

    /* Sentinel */
    {NULL, NULL}
};

/* ═══════════════════════════════════════════════════════════════
   IRREGULAR LOOKUP
   ═══════════════════════════════════════════════════════════════ */

static const char* lookup_irregular(const char *word) {
    char l[64]; lower_copy(l, word, 64);
    for (int i = 0; irregulars[i].inflected; i++)
        if (strcmp(irregulars[i].inflected, l) == 0)
            return irregulars[i].base;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   SUFFIX STRIPPING  (regular inflections)
   Rules applied in order — first match wins.
   ═══════════════════════════════════════════════════════════════ */

void morph_lemmatize(const char *word, char *out, int outlen) {
    char l[64]; lower_copy(l, word, 64);
    int  len = (int)strlen(l);

    /* 1. Irregular table */
    const char *irr = lookup_irregular(l);
    if (irr) { strncpy(out, irr, outlen-1); return; }

    /* 2. Suffix rules — order matters (longer first) */

    /* -nesses → -ness omitted — keep -ness as is */
    /* -ational → -ate  (educational→educate, organizational→organize) */
    if (len > 8 && strcmp(l+len-6,"ations")==0)
      { strncpy(out,l,len-6); out[len-6]='\0'; strncat(out,"ate",outlen-strlen(out)-1); return; }
    if (len > 7 && strcmp(l+len-5,"ation")==0)
      { strncpy(out,l,len-5); out[len-5]='\0'; strncat(out,"ate",outlen-strlen(out)-1); return; }

    /* -ings → base (running→run, taking→take) */
    if (len > 5 && strcmp(l+len-4,"ings")==0) {
        strncpy(out, l, len-4); out[len-4]='\0';
        /* double consonant: running→runn→run */
        int ol = strlen(out);
        if (ol>1 && out[ol-1]==out[ol-2] && !is_vowel(out[ol-1]))
            out[ol-1]='\0';
        /* silent e: taking→tak→take */
        else if (ol>1 && !is_vowel(out[ol-1]) && is_vowel(out[ol-2]))
            strncat(out,"e",outlen-strlen(out)-1);
        return;
    }

    /* -ing → base */
    if (len > 4 && strcmp(l+len-3,"ing")==0) {
        strncpy(out, l, len-3); out[len-3]='\0';
        int ol = strlen(out);
        if (ol < 2) { strncpy(out,l,outlen-1); return; } /* too short */
        if (out[ol-1]==out[ol-2] && !is_vowel(out[ol-1]))
            out[ol-1]='\0';
        else if (!is_vowel(out[ol-1]) && ol>2 && is_vowel(out[ol-2]))
            strncat(out,"e",outlen-strlen(out)-1);
        return;
    }

    /* -ied → -y (tried→try, studied→study) */
    if (len > 4 && strcmp(l+len-3,"ied")==0)
      { strncpy(out,l,len-3); out[len-3]='\0'; strncat(out,"y",outlen-strlen(out)-1); return; }

    /* -ies → -y (studies→study, flies→fly) */
    if (len > 4 && strcmp(l+len-3,"ies")==0)
      { strncpy(out,l,len-3); out[len-3]='\0'; strncat(out,"y",outlen-strlen(out)-1); return; }

    /* -ed → base (walked→walk, loved→love, stopped→stop) */
    if (len > 4 && strcmp(l+len-2,"ed")==0) {
        strncpy(out, l, len-2); out[len-2]='\0';
        int ol = strlen(out);
        if (ol>1 && out[ol-1]==out[ol-2] && !is_vowel(out[ol-1]))
            out[ol-1]='\0'; /* stopped→stop */
        return;
    }

    /* -er → base noun (teacher→teach — only if verb makes sense) */
    /* Skipped — too ambiguous (butter, water are not verbs) */

    /* Plural: -sses → -ss (classes→class) */
    if (len > 4 && strcmp(l+len-4,"sses")==0)
      { strncpy(out,l,len-2); out[len-2]='\0'; return; }

    /* Plural: -xes,-zes,-ches,-shes → strip -es */
    if (len > 4 && strcmp(l+len-2,"es")==0) {
        char stem[64]; strncpy(stem,l,len-2); stem[len-2]='\0';
        int sl = strlen(stem);
        if (sl > 0) {
            char last = stem[sl-1];
            if (last=='x'||last=='z'||
                (sl>1&&stem[sl-1]=='h'&&(stem[sl-2]=='c'||stem[sl-2]=='s')))
              { strncpy(out,stem,outlen-1); return; }
        }
        /* -ves → -f or -fe (leaves→leaf, knives→knife) */
        if (sl>1&&stem[sl-1]=='v'&&stem[sl-2]!='a') {
            strncpy(out,stem,sl-1); out[sl-1]='\0';
            strncat(out,"f",outlen-strlen(out)-1); return;
        }
    }

    /* Plural: -s → base (dogs→dog, cats→cat) */
    /* Only strip if word is long enough and doesn't end in ss/us/is */
    if (len > 3 && l[len-1]=='s' && l[len-2]!='s' &&
        l[len-2]!='u' && l[len-2]!='i') {
        strncpy(out, l, len-1); out[len-1]='\0'; return;
    }

    /* No rule matched — return as-is */
    strncpy(out, l, outlen-1);
}

/* ═══════════════════════════════════════════════════════════════
   SINGULAR → PLURAL
   ═══════════════════════════════════════════════════════════════ */

void morph_plural(const char *word, char *out, int outlen) {
    char l[64]; lower_copy(l, word, 64);
    int  len = (int)strlen(l);
    strncpy(out, l, outlen-1);

    if (len == 0) return;

    /* Irregular reverse lookup */
    for (int i = 0; irregulars[i].inflected; i++) {
        if (strcmp(irregulars[i].base, l) == 0 &&
            strcmp(irregulars[i].inflected, l) != 0) {
            strncpy(out, irregulars[i].inflected, outlen-1);
            return;
        }
    }

    char last  = l[len-1];
    char prev  = len > 1 ? l[len-2] : '\0';

    /* ends in consonant + y → -ies */
    if (last=='y' && prev && !is_vowel(prev))
      { strncpy(out,l,len-1); out[len-1]='\0'; strncat(out,"ies",outlen-strlen(out)-1); return; }
    /* ends in -s, -x, -z, -ch, -sh → -es */
    if (last=='s'||last=='x'||last=='z'||
        (last=='h'&&(prev=='c'||prev=='s')))
      { strncat(out,"es",outlen-strlen(out)-1); return; }
    /* ends in -fe → -ves */
    if (len>2&&last=='e'&&prev=='f')
      { strncpy(out,l,len-2); out[len-2]='\0'; strncat(out,"ves",outlen-strlen(out)-1); return; }
    /* ends in -f → -ves */
    if (last=='f'&&len>2)
      { strncpy(out,l,len-1); out[len-1]='\0'; strncat(out,"ves",outlen-strlen(out)-1); return; }
    /* default → -s */
    strncat(out,"s",outlen-strlen(out)-1);
}

void morph_singular(const char *word, char *out, int outlen) {
    morph_lemmatize(word, out, outlen);
}

/* ═══════════════════════════════════════════════════════════════
   LEVENSHTEIN EDIT DISTANCE
   ═══════════════════════════════════════════════════════════════ */

int morph_edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a);
    int lb = (int)strlen(b);

    /* Use two rows to save memory */
    if (la > 127 || lb > 127) return 99;

    int prev[128], curr[128];
    for (int j = 0; j <= lb; j++) prev[j] = j;

    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del  = prev[j]   + 1;
            int ins  = curr[j-1] + 1;
            int sub  = prev[j-1] + cost;
            curr[j]  = del < ins ? (del < sub ? del : sub)
                                 : (ins < sub ? ins : sub);
        }
        memcpy(prev, curr, (lb+1)*sizeof(int));
    }
    return prev[lb];
}

/* ═══════════════════════════════════════════════════════════════
   AUTOCORRECT
   Scans concept table for closest match.
   Only corrects if:
     - No exact match exists
     - Best candidate has edit distance <= max_dist
     - Word length difference <= 3 (avoids absurd matches)
   ═══════════════════════════════════════════════════════════════ */

/* Hook into blaf_core's concept table */
extern int         get_concept_count(void);
extern const char* get_concept_word (int i);
extern int         find_concept_index(const char *word, uint8_t sector);

int morph_autocorrect(const char *word, char *out, int outlen,
                      int max_dist) {
    if (!word || !word[0]) return 0;

    char l[64]; lower_copy(l, word, 64);
    int  wlen = (int)strlen(l);

    /* If word already known — no correction needed */
    if (find_concept_index(l, 0xFF) >= 0) {
        strncpy(out, l, outlen-1);
        return 0;
    }

    int   best_dist = max_dist + 1;
    char  best_word[64] = {0};
    int   n = get_concept_count();

    for (int i = 0; i < n; i++) {
        const char *cw = get_concept_word(i);
        if (!cw || !cw[0]) continue;

        /* Skip words with very different lengths — fast pre-filter */
        int cwlen = (int)strlen(cw);
        if (abs(cwlen - wlen) > 3) continue;
        /* Skip very short words — too many false matches */
        if (cwlen < 4) continue;

        int d = morph_edit_distance(l, cw);
        if (d < best_dist) {
            best_dist = d;
            strncpy(best_word, cw, 63);
        }
        /* Early exit if we find a perfect edit-1 match */
        if (best_dist == 1) break;
    }

    if (best_dist <= max_dist && best_word[0]) {
        strncpy(out, best_word, outlen-1);
        printf("[MORPH] Autocorrect: \"%s\" → \"%s\" (dist:%d)\n",
               word, out, best_dist);
        return 1;
    }

    strncpy(out, l, outlen-1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   FULL MORPH PROCESS
   For a single word: lemmatize → check → autocorrect
   ═══════════════════════════════════════════════════════════════ */

MorphResult morph_process(const char *word) {
    MorphResult r;
    memset(&r, 0, sizeof(r));
    if (!word || !word[0]) return r;

    strncpy(r.original, word, 63);
    char l[64]; lower_copy(l, word, 64);

    /* Step 1: Lemmatize */
    char lemma[64];
    morph_lemmatize(l, lemma, 64);
    r.was_inflected = (strcmp(lemma, l) != 0);

    /* Step 2: Check if lemma is known */
    if (find_concept_index(lemma, 0xFF) >= 0) {
        strncpy(r.base, lemma, 63);
        return r;
    }

    /* Step 3: Check if original is known (some inflected forms are
     * independently registered) */
    if (find_concept_index(l, 0xFF) >= 0) {
        strncpy(r.base, l, 63);
        r.was_inflected = 0;
        return r;
    }

    /* Step 4: Autocorrect on the lemma */
    char corrected[64];
    if (morph_autocorrect(lemma, corrected, 64, 2)) {
        strncpy(r.base, corrected, 63);
        strncpy(r.correction_from, lemma, 63);
        r.was_corrected = 1;
        return r;
    }

    /* Step 5: Use lemma as best guess */
    strncpy(r.base, lemma, 63);
    return r;
}

/* ═══════════════════════════════════════════════════════════════
   NORMALIZE QUERY
   Apply morph_process to each token, rebuild sentence.
   Preserves question words and grammar words as-is.
   ═══════════════════════════════════════════════════════════════ */

/* Words that should never be lemmatized or corrected */
static const char *morph_skip[] = {
    "is","are","was","were","be","been","being",
    "have","has","had","do","does","did","will","would",
    "shall","should","may","might","must","can","could",
    "the","a","an","of","in","on","at","to","for","with",
    "by","from","and","or","but","not","no","nor",
    "who","what","where","when","why","how",
    "i","you","he","she","it","we","they","me","him","her",
    "this","that","these","those","its","my","your","his",
    NULL
};

static int should_skip_morph(const char *word) {
    char l[64]; lower_copy(l, word, 64);
    for (int i = 0; morph_skip[i]; i++)
        if (strcmp(morph_skip[i], l) == 0) return 1;
    return 0;
}

void morph_normalize_query(const char *input, char *out, int outlen) {
    out[0] = '\0';
    if (!input) return;

    char buf[512]; strncpy(buf, input, 511);
    char *tok  = strtok(buf, " \t");
    int   first = 1;

    while (tok) {
        if (!first) strncat(out, " ", outlen - strlen(out) - 1);
        first = 0;

        if (should_skip_morph(tok)) {
            strncat(out, tok, outlen - strlen(out) - 1);
        } else {
            MorphResult mr = morph_process(tok);
            strncat(out, mr.base[0] ? mr.base : tok,
                    outlen - strlen(out) - 1);
        }
        tok = strtok(NULL, " \t");
    }
}

/* ═══════════════════════════════════════════════════════════════
   VARIANT REGISTRATION
   When a word is learned, register likely inflections so that
   searching any form finds the base concept.
   ═══════════════════════════════════════════════════════════════ */

extern int add_concept(const char *word, uint8_t type, uint8_t class,
                        uint8_t integrity, uint8_t sector,
                        uint8_t in_pin, uint8_t out_pin);

void morph_register_variants(const char *base_word,
                              uint8_t sector, uint8_t class_tag) {
    if (!base_word || !base_word[0]) return;
    char l[64]; lower_copy(l, base_word, 64);
    int len = (int)strlen(l);
    if (len < 2) return;

    int registered = 0;

    /* Helper: register a variant if not already known */
    #define REG(w) do { \
        if (find_concept_index((w), 0xFF) < 0) { \
            add_concept((w), 0, class_tag, 1, sector, 0xFF, 0xFF); \
            registered++; \
        } \
    } while(0)

    char var[64];

    /* ── NOUN variants ── */
    if (class_tag == 0 /* CLASS_NOUN */ || class_tag == 12 /* PROPER */) {
        /* Plural */
        morph_plural(l, var, sizeof(var));
        if (strcmp(var, l) != 0) REG(var);

        /* -ity form: "secure" → "security" (only if ends in 'e') */
        if (len > 3 && l[len-1] == 'e') {
            snprintf(var, sizeof(var), "%.*sity", len-1, l);
            REG(var);
        }
        /* -ness: happy → happiness (y→i+ness) */
        if (len > 3 && l[len-1] == 'y') {
            snprintf(var, sizeof(var), "%.*siness", len-1, l);
            REG(var);
        }
    }

    /* ── VERB variants ── */
    if (class_tag == 1 /* CLASS_VERB */) {
        char base[64]; strncpy(base, l, 63);
        char last = base[len-1];
        char prev = len > 1 ? base[len-2] : '\0';

        /* -s (3rd person singular) */
        if (last == 'e')
            snprintf(var, sizeof(var), "%ss", base);           /* love→loves */
        else if (last=='y' && prev && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*sies", len-1, base);/* fly→flies */
        else
            snprintf(var, sizeof(var), "%ss", base);           /* run→runs */
        REG(var);

        /* -ing */
        if (last == 'e' && len > 2 && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*sing", len-1, base); /* take→taking */
        else if (!is_vowel(last) && prev && is_vowel(prev) &&
                 (len<3||!is_vowel(base[len-3])))
            snprintf(var, sizeof(var), "%s%cing", base, last);  /* run→running */
        else
            snprintf(var, sizeof(var), "%sing", base);          /* walk→walking */
        REG(var);

        /* -ed */
        if (last == 'e')
            snprintf(var, sizeof(var), "%sd", base);            /* love→loved */
        else if (last=='y' && prev && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*sied", len-1, base); /* try→tried */
        else if (!is_vowel(last) && prev && is_vowel(prev) &&
                 (len<3||!is_vowel(base[len-3])))
            snprintf(var, sizeof(var), "%s%ced", base, last);   /* stop→stopped */
        else
            snprintf(var, sizeof(var), "%sed", base);           /* walk→walked */
        REG(var);

        /* -er (agent noun) */
        if (last == 'e')
            snprintf(var, sizeof(var), "%sr", base);            /* write→writer */
        else
            snprintf(var, sizeof(var), "%ser", base);           /* teach→teacher */
        REG(var);
    }

    /* ── ADJECTIVE variants ── */
    if (class_tag == 2 /* CLASS_ADJ */) {
        char last = l[len-1];
        char prev = len > 1 ? l[len-2] : '\0';

        /* -ly adverb */
        if (last == 'e')
            snprintf(var, sizeof(var), "%sly", l);               /* safe→safely */
        else if (last=='y' && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*sily", len-1, l);     /* happy→happily */
        else
            snprintf(var, sizeof(var), "%sly", l);               /* quick→quickly */
        REG(var);

        /* -er comparative */
        if (last == 'e')
            snprintf(var, sizeof(var), "%sr", l);
        else if (last=='y' && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*sier", len-1, l);
        else
            snprintf(var, sizeof(var), "%ser", l);
        REG(var);

        /* -est superlative */
        if (last == 'e')
            snprintf(var, sizeof(var), "%sst", l);
        else if (last=='y' && !is_vowel(prev))
            snprintf(var, sizeof(var), "%.*siest", len-1, l);
        else
            snprintf(var, sizeof(var), "%sest", l);
        REG(var);
    }

    #undef REG

    if (registered > 0)
        printf("[MORPH] Registered %d variants of \"%s\"\n",
               registered, base_word);
}

/* ═══════════════════════════════════════════════════════════════
   INIT / STATUS
   ═══════════════════════════════════════════════════════════════ */

static int g_morph_init = 0;

void morph_init(void) {
    if (g_morph_init) return;
    g_morph_init = 1;
    int nirr = 0;
    while (irregulars[nirr].inflected) nirr++;
    int nskip = 0;
    while (morph_skip[nskip]) nskip++;
    printf("[MORPH] Morphology engine ready — "
           "irregulars:%d skip_list:%d\n", nirr, nskip);
}

void morph_status(void) {
    printf("\n[MORPH STATUS]\n");
    printf("  Irregular forms : %d\n",
           (int)(sizeof(irregulars)/sizeof(irregulars[0])) - 1);
    printf("  Skip list       : grammar + question words\n");
    printf("  Autocorrect     : max edit distance 2\n");
    printf("  Variant gen     : nouns(plural) verbs(s/ing/ed/er) adj(ly/er/est)\n\n");
}
