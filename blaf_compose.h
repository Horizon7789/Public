/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_compose.h / blaf_compose.c                    ║
 * ║  Query Composer                                             ║
 * ║                                                              ║
 * ║  Combines subject + intent → precise web query string       ║
 * ║  and shapes the expected answer format.                     ║
 * ║                                                              ║
 * ║  "how far is London"  → "distance from London in km"        ║
 * ║  "how old is Obama"   → "Barack Obama age"                  ║
 * ║  "London vs Paris"    → "London vs Paris comparison"        ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_COMPOSE_H
#define BLAF_COMPOSE_H

#include "blaf_intent.h"
#include "blaf_grammar.h"

#define MAX_QUERY_LEN    256
#define MAX_ANSWER_HINT  128

/* ═══════════════════════════════════════════════════════════════
   COMPOSED QUERY
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char query[MAX_QUERY_LEN];      /* shaped search query              */
    char answer_hint[MAX_ANSWER_HINT]; /* what form the answer should take */
    char unit[32];                  /* expected unit: "km", "years", "$" */
    int  expect_numeric;            /* 1 if we expect a number back     */
    int  expect_comparison;         /* 1 if comparing two things        */
} ComposedQuery;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void compose_init(void);

/*
 * compose_query()
 * Takes a ResolvedIntent and shapes it into a precise web/LLM query.
 *
 * Example:
 *   // User typed: "how far is London from Paris"
 *   // intent.type    = INTENT_DISTANCE
 *   // intent.subject = "London"
 *   // intent.object  = "Paris"
 *   ComposedQuery cq = compose_query(&intent);
 *   // cq.query        = "distance from London to Paris in kilometres"
 *   // cq.answer_hint  = "Give the distance as a number with units."
 *   // cq.unit         = "km"
 *   // cq.expect_numeric = 1
 */
ComposedQuery compose_query(const ResolvedIntent *intent);

/*
 * compose_answer_prompt()
 * Builds a full LLM prompt from a ComposedQuery.
 * Includes the answer_hint so the LLM knows what format to use.
 *
 * Example output:
 *   "What is the distance from London to Paris in kilometres?
 *    Give the distance as a number with units. Be concise."
 */
void compose_answer_prompt(const ComposedQuery *cq, char *out, int outlen);

/*
 * compose_format_answer()
 * Post-processes the raw LLM/web response using the intent
 * to extract the relevant part of the answer.
 *
 * Example:
 *   raw = "The distance between London and Paris is approximately
 *           343 kilometres by air."
 *   compose_format_answer(&cq, raw, formatted, sizeof(formatted));
 *   // formatted = "343 km"  (if INST_SHORT_ANSWER)
 *   //             "The distance from London to Paris is 343 km."
 */
void compose_format_answer(const ComposedQuery *cq,
                           const char *raw_answer,
                           char *out, int outlen);

#endif /* BLAF_COMPOSE_H */


/* ════════════════════════════════════════════════════════════════
   IMPLEMENTATION
   ════════════════════════════════════════════════════════════════ */

#ifdef BLAF_COMPOSE_IMPL
#undef BLAF_COMPOSE_IMPL

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "blaf_instructions.h"

void compose_init(void) {
    printf("[COMPOSE] Query composer ready.\n");
}

/* ── Intent → query template table ── */
typedef struct {
    IntentType  type;
    const char *template_with_object;    /* when object is present   */
    const char *template_without_object; /* when only subject        */
    const char *answer_hint;
    const char *unit;
} ComposeTemplate;

static const ComposeTemplate compose_table[] = {
    {INTENT_DISTANCE,
        "distance from %s to %s in kilometres",
        "distance to %s in kilometres",
        "State the distance as a number with km or miles.",
        "km"},

    {INTENT_DURATION,
        "how long does %s take to %s",
        "duration of %s",
        "State the time as a number with units (minutes, hours, days).",
        "hours"},

    {INTENT_AGE,
        NULL,
        "age of %s",
        "State the age as a number in years.",
        "years"},

    {INTENT_COUNT,
        NULL,
        "total number of %s",
        "State the count as a number.",
        "count"},

    {INTENT_QUANTITY,
        NULL,
        "amount of %s",
        "State the quantity as a number with units.",
        ""},

    {INTENT_PRICE,
        NULL,
        "price of %s",
        "State the price as a number with currency.",
        "USD"},

    {INTENT_SIZE,
        NULL,
        "size and area of %s",
        "State the size as a number with units.",
        "km²"},

    {INTENT_SPEED,
        NULL,
        "speed of %s",
        "State the speed as a number with units.",
        "km/h"},

    {INTENT_TEMPERATURE,
        "temperature difference between %s and %s",
        "current temperature and climate in %s",
        "State the temperature in Celsius and Fahrenheit.",
        "°C"},

    {INTENT_DATE,
        NULL,
        "when was %s founded or created",
        "State the year or date.",
        "year"},

    {INTENT_FREQUENCY,
        NULL,
        "how often does %s occur",
        "State the frequency with a time unit.",
        "times/year"},

    {INTENT_LOCATION,
        NULL,
        "location and geography of %s",
        "State the country, region, and coordinates if relevant.",
        ""},

    {INTENT_DIRECTION,
        "how to get from %s to %s",
        "directions to %s",
        "Describe the route briefly.",
        ""},

    {INTENT_IDENTITY,
        NULL,
        "who is %s biographical information",
        "Give a concise biography.",
        ""},

    {INTENT_DEFINITION,
        NULL,
        "definition and explanation of %s",
        "Define in 1-2 sentences.",
        ""},

    {INTENT_DESCRIPTION,
        NULL,
        "overview and description of %s",
        "Give a factual 2-3 sentence overview.",
        ""},

    {INTENT_EXAMPLE,
        NULL,
        "examples of %s",
        "Give 2-3 concrete examples.",
        ""},

    {INTENT_CAUSE,
        NULL,
        "reason and cause for %s",
        "Explain the main cause in 1-2 sentences.",
        ""},

    {INTENT_EFFECT,
        NULL,
        "effects and consequences of %s",
        "State the main effects briefly.",
        ""},

    {INTENT_COMPARE,
        "comparison between %s and %s differences and similarities",
        "comparison overview of %s",
        "List 2-3 key differences.",
        ""},

    {INTENT_PROCESS,
        NULL,
        "how %s works mechanism explained",
        "Explain the mechanism in 2-3 sentences.",
        ""},

    {INTENT_METHOD,
        NULL,
        "how to %s step by step",
        "List the main steps briefly.",
        ""},

    {INTENT_CAPABILITY,
        NULL,
        "capabilities and features of %s",
        "State what it can and cannot do.",
        ""},

    {INTENT_GENERAL,
        NULL,
        "%s",
        "Answer factually and concisely.",
        ""},

    {INTENT_UNKNOWN,   NULL, NULL, NULL, NULL}
};

/* ─── helpers ─── */
static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

ComposedQuery compose_query(const ResolvedIntent *intent) {
    ComposedQuery cq = {{0},{0},{0},0,0};
    if (!intent) return cq;

    /* Find template */
    const ComposeTemplate *tmpl = NULL;
    for (int i = 0; compose_table[i].type != INTENT_UNKNOWN; i++) {
        if (compose_table[i].type == intent->type) {
            tmpl = &compose_table[i];
            break;
        }
    }
    if (!tmpl) {
        /* Fallback: just use subject as query */
        strncpy(cq.query, intent->subject, MAX_QUERY_LEN-1);
        strncpy(cq.answer_hint, "Answer factually.", MAX_ANSWER_HINT-1);
        return cq;
    }

    /* Fill query template */
    if (intent->object[0] && tmpl->template_with_object) {
        snprintf(cq.query, MAX_QUERY_LEN,
                 tmpl->template_with_object, intent->subject, intent->object);
        cq.expect_comparison = 1;
    } else if (tmpl->template_without_object) {
        snprintf(cq.query, MAX_QUERY_LEN,
                 tmpl->template_without_object, intent->subject);
    } else {
        strncpy(cq.query, intent->subject, MAX_QUERY_LEN-1);
    }

    strncpy(cq.answer_hint, tmpl->answer_hint, MAX_ANSWER_HINT-1);
    strncpy(cq.unit, tmpl->unit, 31);
    cq.expect_numeric = intent_needs_numeric_answer(intent->type);

    printf("[COMPOSE] Query: \"%s\"\n", cq.query);
    return cq;
}

void compose_answer_prompt(const ComposedQuery *cq, char *out, int outlen) {
    if (!cq || !out) return;

    int short_mode = has_instruction(INST_SHORT_ANSWER);

    if (short_mode) {
        snprintf(out, outlen,
            "%s Answer in one sentence%s.",
            cq->query,
            cq->expect_numeric ? " with the number and unit" : "");
    } else {
        snprintf(out, outlen,
            "%s\n%s%s",
            cq->query,
            cq->answer_hint,
            cq->unit[0] ? " Use metric units where applicable." : "");
    }
}

void compose_format_answer(const ComposedQuery *cq,
                           const char *raw_answer,
                           char *out, int outlen) {
    if (!raw_answer || !out) return;

    int short_mode = has_instruction(INST_SHORT_ANSWER);

    if (!short_mode || !cq->expect_numeric) {
        strncpy(out, raw_answer, outlen-1);
        return;
    }

    /* For numeric answers in short mode: try to extract the number + unit */
    /* Scan for first number in the response */
    const char *p = raw_answer;
    char number[32] = {0};
    int  found = 0;

    while (*p) {
        if (isdigit((unsigned char)*p) ||
            (*p == '.' && isdigit((unsigned char)*(p+1)))) {
            int j = 0;
            while ((*p == '.' || isdigit((unsigned char)*p)) && j < 30)
                number[j++] = *p++;
            number[j] = '\0';
            found = 1;
            break;
        }
        p++;
    }

    if (found && cq->unit[0]) {
        /* Skip whitespace to get unit from raw if present */
        while (*p == ' ') p++;
        char raw_unit[32] = {0};
        int j = 0;
        while (*p && !isspace((unsigned char)*p) && j < 30)
            raw_unit[j++] = *p++;

        if (raw_unit[0])
            snprintf(out, outlen, "%s %s", number, raw_unit);
        else
            snprintf(out, outlen, "%s %s", number, cq->unit);
    } else if (found) {
        strncpy(out, number, outlen-1);
    } else {
        /* No number found — return trimmed first sentence */
        const char *end = strpbrk(raw_answer, ".!?");
        int len = end ? (int)(end - raw_answer + 1) : (int)strlen(raw_answer);
        if (len >= outlen) len = outlen - 1;
        strncpy(out, raw_answer, len);
        out[len] = '\0';
    }
}

#endif /* BLAF_COMPOSE_IMPL */
