/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_intent.c                                       ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "blaf_intent.h"
#include "blaf_vectors.h"
#include "blaf_sectors.h"

/* ═══════════════════════════════════════════════════════════════
   PATTERN TABLE
   Each pattern is a trigger string → IntentType + modifier
   Patterns are checked against the full lowercased input.
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    const char *pattern;   /* substring to match in input */
    IntentType  type;
    const char *modifier;  /* extracted modifier label    */
} IntentPattern;

static const IntentPattern patterns[] = {

    /* ── DISTANCE ── */
    {"how far",         INTENT_DISTANCE,    "far"},
    {"distance from",   INTENT_DISTANCE,    "distance"},
    {"distance to",     INTENT_DISTANCE,    "distance"},
    {"distance between",INTENT_DISTANCE,    "distance"},
    {"how many km",     INTENT_DISTANCE,    "km"},
    {"how many miles",  INTENT_DISTANCE,    "miles"},
    {"kilometres from", INTENT_DISTANCE,    "km"},
    {"miles from",      INTENT_DISTANCE,    "miles"},

    /* ── DURATION ── */
    {"how long does",   INTENT_DURATION,    "duration"},
    {"how long will",   INTENT_DURATION,    "duration"},
    {"how long is",     INTENT_DURATION,    "duration"},
    {"how long",        INTENT_DURATION,    "duration"},
    {"how much time",   INTENT_DURATION,    "time"},
    {"takes how long",  INTENT_DURATION,    "duration"},

    /* ── AGE ── */
    {"how old",         INTENT_AGE,         "age"},
    {"what age",        INTENT_AGE,         "age"},
    {"age of",          INTENT_AGE,         "age"},
    {"years old",       INTENT_AGE,         "age"},

    /* ── COUNT ── */
    {"how many",        INTENT_COUNT,       "count"},
    {"number of",       INTENT_COUNT,       "count"},
    {"total number",    INTENT_COUNT,       "count"},
    {"how many people", INTENT_COUNT,       "population"},
    {"population of",   INTENT_COUNT,       "population"},

    /* ── QUANTITY / AMOUNT ── */
    {"how much does",   INTENT_PRICE,       "price"},
    {"how much is",     INTENT_PRICE,       "price"},
    {"price of",        INTENT_PRICE,       "price"},
    {"cost of",         INTENT_PRICE,       "price"},
    {"how much",        INTENT_QUANTITY,    "amount"},
    {"amount of",       INTENT_QUANTITY,    "amount"},

    /* ── SIZE ── */
    {"how big",         INTENT_SIZE,        "size"},
    {"how large",       INTENT_SIZE,        "size"},
    {"how small",       INTENT_SIZE,        "size"},
    {"how wide",        INTENT_SIZE,        "width"},
    {"how tall",        INTENT_SIZE,        "height"},
    {"how high",        INTENT_SIZE,        "height"},
    {"how deep",        INTENT_SIZE,        "depth"},
    {"size of",         INTENT_SIZE,        "size"},
    {"area of",         INTENT_SIZE,        "area"},

    /* ── SPEED ── */
    {"how fast",        INTENT_SPEED,       "speed"},
    {"how slow",        INTENT_SPEED,       "speed"},
    {"speed of",        INTENT_SPEED,       "speed"},
    {"velocity of",     INTENT_SPEED,       "speed"},

    /* ── TEMPERATURE ── */
    {"how hot",         INTENT_TEMPERATURE, "temperature"},
    {"how cold",        INTENT_TEMPERATURE, "temperature"},
    {"how warm",        INTENT_TEMPERATURE, "temperature"},
    {"temperature of",  INTENT_TEMPERATURE, "temperature"},
    {"temperature in",  INTENT_TEMPERATURE, "temperature"},
    {"weather in",      INTENT_TEMPERATURE, "weather"},
    {"weather of",      INTENT_TEMPERATURE, "weather"},
    {"climate of",      INTENT_TEMPERATURE, "climate"},

    /* ── DATE / TIME ── */
    {"when was",        INTENT_DATE,        "date"},
    {"when did",        INTENT_DATE,        "date"},
    {"when is",         INTENT_DATE,        "date"},
    {"what year",       INTENT_DATE,        "year"},
    {"what date",       INTENT_DATE,        "date"},
    {"founded in",      INTENT_DATE,        "founding"},
    {"born in",         INTENT_DATE,        "birth"},
    {"born on",         INTENT_DATE,        "birth"},

    /* ── FREQUENCY ── */
    {"how often",       INTENT_FREQUENCY,   "frequency"},
    {"how frequently",  INTENT_FREQUENCY,   "frequency"},
    {"how regular",     INTENT_FREQUENCY,   "frequency"},

    /* ── LOCATION ── */
    {"where is",        INTENT_LOCATION,    "location"},
    {"where are",       INTENT_LOCATION,    "location"},
    {"where was",       INTENT_LOCATION,    "location"},
    {"located in",      INTENT_LOCATION,    "location"},
    {"located at",      INTENT_LOCATION,    "location"},
    {"capital of",      INTENT_LOCATION,    "capital"},
    {"where does",      INTENT_LOCATION,    "location"},

    /* ── DIRECTION / NAVIGATION ── */
    {"how to get to",   INTENT_DIRECTION,   "navigation"},
    {"how do i get to", INTENT_DIRECTION,   "navigation"},
    {"directions to",   INTENT_DIRECTION,   "navigation"},
    {"route to",        INTENT_DIRECTION,   "route"},

    /* ── IDENTITY ── */
    {"who is",          INTENT_IDENTITY,    "person"},
    {"who was",         INTENT_IDENTITY,    "person"},
    {"who are",         INTENT_IDENTITY,    "person"},
    {"who invented",    INTENT_IDENTITY,    "inventor"},
    {"who created",     INTENT_IDENTITY,    "creator"},
    {"who founded",     INTENT_IDENTITY,    "founder"},
    {"who wrote",       INTENT_IDENTITY,    "author"},

    /* ── DEFINITION ── */
    {"what is",         INTENT_DEFINITION,  "definition"},
    {"what are",        INTENT_DEFINITION,  "definition"},
    {"what was",        INTENT_DEFINITION,  "definition"},
    {"define ",         INTENT_DEFINITION,  "definition"},
    {"meaning of",      INTENT_DEFINITION,  "meaning"},

    /* ── DESCRIPTION / EXPLANATION ── */
    {"tell me about",   INTENT_DESCRIPTION, "overview"},
    {"explain ",        INTENT_DESCRIPTION, "explanation"},
    {"describe ",       INTENT_DESCRIPTION, "description"},
    {"overview of",     INTENT_DESCRIPTION, "overview"},
    {"what do you know",INTENT_DESCRIPTION, "knowledge"},

    /* ── EXAMPLE ── */
    {"give me an example", INTENT_EXAMPLE,  "example"},
    {"example of",      INTENT_EXAMPLE,     "example"},
    {"for example",     INTENT_EXAMPLE,     "example"},

    /* ── CAUSE ── */
    {"why does",        INTENT_CAUSE,       "reason"},
    {"why do",          INTENT_CAUSE,       "reason"},
    {"why is",          INTENT_CAUSE,       "reason"},
    {"why was",         INTENT_CAUSE,       "reason"},
    {"reason for",      INTENT_CAUSE,       "reason"},
    {"cause of",        INTENT_CAUSE,       "cause"},

    /* ── EFFECT ── */
    {"what happens",    INTENT_EFFECT,      "effect"},
    {"what will happen",INTENT_EFFECT,      "effect"},
    {"effect of",       INTENT_EFFECT,      "effect"},
    {"result of",       INTENT_EFFECT,      "result"},

    /* ── COMPARISON ── */
    {"difference between",INTENT_COMPARE,   "difference"},
    {"compare ",        INTENT_COMPARE,     "comparison"},
    {" vs ",            INTENT_COMPARE,     "versus"},
    {" versus ",        INTENT_COMPARE,     "versus"},
    {"better than",     INTENT_COMPARE,     "comparison"},
    {"worse than",      INTENT_COMPARE,     "comparison"},

    /* ── CAPABILITY ── */
    {"can ",            INTENT_CAPABILITY,  "capability"},
    {"is it possible",  INTENT_CAPABILITY,  "possibility"},
    {"able to",         INTENT_CAPABILITY,  "ability"},

    /* ── PROCESS / HOW-TO ── */
    {"how does",        INTENT_PROCESS,     "mechanism"},
    {"how do",          INTENT_PROCESS,     "process"},
    {"how to",          INTENT_METHOD,      "steps"},
    {"steps to",        INTENT_METHOD,      "steps"},
    {"how can i",       INTENT_METHOD,      "method"},

    /* Sentinel */
    {NULL, INTENT_UNKNOWN, NULL}
};

/* ═══════════════════════════════════════════════════════════════
   VECTOR ANCHOR TABLE
   Maps IntentType to representative words for vector fallback.
   When a phrase doesn't match any pattern, we find which
   intent's anchor words are closest to the phrase vector.
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    IntentType  type;
    const char *anchors[4]; /* representative words for this intent */
} VecAnchor;

static const VecAnchor vec_anchors[] = {
    {INTENT_DISTANCE,    {"distance", "far",    "km",       "miles"}},
    {INTENT_DURATION,    {"duration", "long",   "time",     "minutes"}},
    {INTENT_AGE,         {"age",      "old",    "years",    "born"}},
    {INTENT_COUNT,       {"count",    "many",   "number",   "total"}},
    {INTENT_QUANTITY,    {"amount",   "much",   "quantity", "volume"}},
    {INTENT_PRICE,       {"price",    "cost",   "money",    "expensive"}},
    {INTENT_SIZE,        {"size",     "big",    "large",    "area"}},
    {INTENT_SPEED,       {"speed",    "fast",   "velocity", "mph"}},
    {INTENT_TEMPERATURE, {"temperature","hot",  "cold",     "weather"}},
    {INTENT_DATE,        {"date",     "when",   "year",     "time"}},
    {INTENT_LOCATION,    {"location", "where",  "place",    "city"}},
    {INTENT_DIRECTION,   {"direction","route",  "navigate", "path"}},
    {INTENT_IDENTITY,    {"person",   "who",    "name",     "identity"}},
    {INTENT_DEFINITION,  {"definition","meaning","concept", "what"}},
    {INTENT_DESCRIPTION, {"describe", "explain","overview", "about"}},
    {INTENT_CAUSE,       {"reason",   "cause",  "why",      "because"}},
    {INTENT_EFFECT,      {"effect",   "result", "consequence","impact"}},
    {INTENT_COMPARE,     {"compare",  "versus", "difference","better"}},
    {INTENT_PROCESS,     {"process",  "works",  "mechanism","how"}},
    {INTENT_METHOD,      {"method",   "steps",  "procedure","how"}},
    {INTENT_UNKNOWN,     {NULL, NULL, NULL, NULL}}
};

static int g_intent_init = 0;

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

/*
 * Extract secondary object from comparison patterns.
 * "distance between London and Paris" → object = "Paris"
 */
static void extract_object(const char *input, const char *subject,
                            char *out, int outlen) {
    out[0] = '\0';
    const char *markers[] = {" and ", " from ", " to ", " versus ", " vs ", NULL};
    for (int i = 0; markers[i]; i++) {
        char *pos = strstr(input, markers[i]);
        if (pos) {
            pos += strlen(markers[i]);
            strncpy(out, pos, outlen-1);
            /* Trim at next marker or end */
            for (int j = 0; out[j]; j++) {
                if (out[j] == '?' || out[j] == '!') { out[j]='\0'; break; }
            }
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   VECTOR FALLBACK
   When no pattern matches, compute phrase vector and find
   nearest intent anchor.
   ═══════════════════════════════════════════════════════════════ */

static IntentType vector_intent_fallback(const char *input, float *out_conf) {
    if (!vec_loaded()) {
        *out_conf = 0.0f;
        return INTENT_GENERAL;
    }

    /* Tokenize input into words */
    char buf[512]; lower_copy(buf, input, 512);
    const float *vecs[16];
    int   nv = 0;
    char *tok = strtok(buf, " \t\n?,.");
    while (tok && nv < 16) {
        const float *v = vec_get(tok);
        if (v) vecs[nv++] = v;
        tok = strtok(NULL, " \t\n?,.");
    }

    if (nv == 0) { *out_conf = 0.0f; return INTENT_GENERAL; }

    /* Compute mean vector of the input */
    float phrase_vec[VEC_DIMS];
    vec_mean((const float**)vecs, nv, phrase_vec);

    /* Compare against each anchor group */
    IntentType best_type = INTENT_GENERAL;
    float      best_sim  = 0.0f;

    for (int a = 0; vec_anchors[a].type != INTENT_UNKNOWN; a++) {
        const float *anchor_vecs[4];
        int nanchors = 0;
        for (int j = 0; j < 4 && vec_anchors[a].anchors[j]; j++) {
            const float *av = vec_get(vec_anchors[a].anchors[j]);
            if (av) anchor_vecs[nanchors++] = av;
        }
        if (nanchors == 0) continue;

        float anchor_mean[VEC_DIMS];
        vec_mean((const float**)anchor_vecs, nanchors, anchor_mean);

        float sim = vec_similarity_raw(phrase_vec, anchor_mean, VEC_DIMS);
        if (sim > best_sim) {
            best_sim  = sim;
            best_type = vec_anchors[a].type;
        }
    }

    *out_conf = best_sim;
    return best_type;
}

/* ═══════════════════════════════════════════════════════════════
   INIT
   ═══════════════════════════════════════════════════════════════ */

void intent_init(void) {
    if (g_intent_init) return;
    g_intent_init = 1;
    int n = 0;
    while (patterns[n].pattern) n++;
    printf("[INTENT] Pattern table loaded — %d patterns. "
           "Vector fallback: %s\n",
           n, vec_loaded() ? "enabled" : "disabled (load GloVe to enable)");
}

/* ═══════════════════════════════════════════════════════════════
   RESOLVE INTENT
   ═══════════════════════════════════════════════════════════════ */

ResolvedIntent resolve_intent(const ParsedQuery *q) {
    ResolvedIntent r = {INTENT_UNKNOWN, {0}, {0}, {0}, 0.0f, 0};

    if (!q || !q->raw_input[0]) return r;

    strncpy(r.subject, q->subject, 127);

    char lower[512];
    lower_copy(lower, q->raw_input, 512);

    /* ── Step 1: Pattern table scan ── */
    for (int i = 0; patterns[i].pattern; i++) {
        if (strstr(lower, patterns[i].pattern)) {
            r.type       = patterns[i].type;
            r.confidence = 0.95f;
            strncpy(r.modifier, patterns[i].modifier, MAX_MODIFIER_LEN-1);

            /* Extract comparison object if needed */
            if (r.type == INTENT_COMPARE || r.type == INTENT_DISTANCE)
                extract_object(lower, r.subject, r.object, 128);

            printf("[INTENT] Pattern match: \"%s\" → %s (%.0f%%)\n",
                   patterns[i].pattern, intent_name(r.type),
                   r.confidence * 100);
            return r;
        }
    }

    /* ── Step 2: Vector fallback ── */
    float conf = 0.0f;
    IntentType vtype = vector_intent_fallback(q->raw_input, &conf);
    if (conf > 0.5f) {
        r.type         = vtype;
        r.confidence   = conf;
        r.used_vectors = 1;
        printf("[INTENT] Vector fallback: %s (%.0f%% confidence)\n",
               intent_name(vtype), conf * 100);
        return r;
    }

    /* ── Step 3: Map from QuestionType as last resort ── */
    switch (q->type) {
        case Q_WHO_IS:   r.type = INTENT_IDENTITY;   break;
        case Q_WHAT_IS:  r.type = INTENT_DEFINITION; break;
        case Q_WHERE_IS: r.type = INTENT_LOCATION;   break;
        case Q_WHY:      r.type = INTENT_CAUSE;       break;
        case Q_HOW:      r.type = INTENT_PROCESS;     break;
        default:         r.type = INTENT_GENERAL;     break;
    }
    r.confidence = 0.5f;
    printf("[INTENT] Fallback from question type: %s\n", intent_name(r.type));
    return r;
}

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */

const char* intent_name(IntentType t) {
    switch (t) {
        case INTENT_LOCATION:    return "LOCATION";
        case INTENT_DISTANCE:    return "DISTANCE";
        case INTENT_DIRECTION:   return "DIRECTION";
        case INTENT_DURATION:    return "DURATION";
        case INTENT_DATE:        return "DATE";
        case INTENT_AGE:         return "AGE";
        case INTENT_FREQUENCY:   return "FREQUENCY";
        case INTENT_QUANTITY:    return "QUANTITY";
        case INTENT_COUNT:       return "COUNT";
        case INTENT_SIZE:        return "SIZE";
        case INTENT_PRICE:       return "PRICE";
        case INTENT_SPEED:       return "SPEED";
        case INTENT_TEMPERATURE: return "TEMPERATURE";
        case INTENT_IDENTITY:    return "IDENTITY";
        case INTENT_DEFINITION:  return "DEFINITION";
        case INTENT_DESCRIPTION: return "DESCRIPTION";
        case INTENT_EXAMPLE:     return "EXAMPLE";
        case INTENT_CAUSE:       return "CAUSE";
        case INTENT_EFFECT:      return "EFFECT";
        case INTENT_COMPARE:     return "COMPARE";
        case INTENT_RELATION:    return "RELATION";
        case INTENT_CAPABILITY:  return "CAPABILITY";
        case INTENT_PROCESS:     return "PROCESS";
        case INTENT_METHOD:      return "METHOD";
        case INTENT_GENERAL:     return "GENERAL";
        default:                 return "UNKNOWN";
    }
}

int intent_needs_numeric_answer(IntentType t) {
    return (t == INTENT_DISTANCE  || t == INTENT_DURATION ||
            t == INTENT_AGE       || t == INTENT_COUNT    ||
            t == INTENT_QUANTITY  || t == INTENT_SIZE     ||
            t == INTENT_PRICE     || t == INTENT_SPEED    ||
            t == INTENT_TEMPERATURE || t == INTENT_FREQUENCY);
}

uint8_t intent_sector_hint(IntentType t) {
    switch (t) {
        case INTENT_LOCATION:
        case INTENT_DISTANCE:
        case INTENT_DIRECTION:   return 0x00; /* GENERAL / GEOGRAPHY */
        case INTENT_PRICE:
        case INTENT_QUANTITY:    return 0x03; /* FINANCE */
        case INTENT_TEMPERATURE: return 0x00; /* GENERAL */
        case INTENT_IDENTITY:    return 0x00;
        case INTENT_CAUSE:
        case INTENT_PROCESS:     return 0x02; /* ICT as default tech */
        default:                 return 0x00;
    }
}
