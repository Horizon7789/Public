/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_output.c                                       ║
 * ║  Sentence Formation & Response Generation Engine            ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "blaf_output.h"
#include "blaf_grammar.h"
#include "blaf_instructions.h"
#include "blaf_sectors.h"

/* ═══════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ═══════════════════════════════════════════════════════════════ */

static void capitalize_first(char *s) {
    if (s && s[0]) s[0] = toupper((unsigned char)s[0]);
}

static int word_count_of(const char *text) {
    int count = 0, in_word = 0;
    for (const char *p = text; *p; p++) {
        if (isspace((unsigned char)*p)) in_word = 0;
        else if (!in_word) { in_word = 1; count++; }
    }
    return count;
}

/*
 * Extract the first N sentences from a block of text.
 * Used to create short answers from longer web facts.
 */
static void extract_sentences(const char *src, char *dst, int max_sentences, int dstlen) {
    int sent = 0;
    int i = 0, j = 0;
    while (src[i] && j < dstlen - 1) {
        dst[j++] = src[i];
        if (src[i] == '.' || src[i] == '!' || src[i] == '?') {
            sent++;
            if (sent >= max_sentences) break;
        }
        i++;
    }
    dst[j] = '\0';
}

/*
 * Trim leading/trailing whitespace and newlines
 */
static void trim(char *s) {
    /* trim leading */
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s+start, strlen(s)-start+1);
    /* trim trailing */
    int end = strlen(s)-1;
    while (end >= 0 && isspace((unsigned char)s[end])) s[end--] = '\0';
}

/* ═══════════════════════════════════════════════════════════════
   INIT
   ═══════════════════════════════════════════════════════════════ */

void output_init(void) {
    printf("[OUTPUT] Sentence formation engine ready.\n");
}

/* ═══════════════════════════════════════════════════════════════
   CONTEXT BUILDER
   Generates a background paragraph for any subject/sector
   ═══════════════════════════════════════════════════════════════ */

void build_context(const char *subject, uint8_t sector,
                   char *out, int outlen) {
    const char *sector_str = sector_name(sector);
    snprintf(out, outlen,
        "%s is a concept in the %s domain. "
        "For more detailed background, a web lookup can be triggered "
        "using web_lookup(\"%s\").",
        subject, sector_str, subject);
}

/* ═══════════════════════════════════════════════════════════════
   CORE ANSWER BUILDER
   ═══════════════════════════════════════════════════════════════ */

BlafResponse build_answer(const ParsedQuery *q, const char *web_facts) {
    BlafResponse r = {0};
    r.confidence = 128;
    r.has_context = 0;

    const InstructionState *inst = get_instructions();
    int short_mode  = has_instruction(INST_SHORT_ANSWER);
    int formal_mode = has_instruction(INST_FORMAL_TONE);
    int context_on  = has_instruction(INST_CONTEXT_ON);
    int no_explain  = has_instruction(INST_NO_EXPLAIN);
    int bullets     = has_instruction(INST_BULLETS_ONLY);

    char body[MAX_RESPONSE_CHARS] = {0};

    /* ── CASE 1: We have web facts — primary path ── */
    if (web_facts && strlen(web_facts) > 10) {

        char facts[MAX_RESPONSE_CHARS];
        strncpy(facts, web_facts, MAX_RESPONSE_CHARS - 1);
        trim(facts);

        switch (q->type) {

            case Q_WHO_IS: {
                if (short_mode) {
                    char first_sent[512] = {0};
                    extract_sentences(facts, first_sent, 1, sizeof(first_sent));
                    trim(first_sent);
                    snprintf(body, sizeof(body), "%s", first_sent);
                } else if (bullets) {
                    snprintf(body, sizeof(body),
                        "Here is what I found about %s:\n\n%s",
                        q->subject, facts);
                } else {
                    snprintf(body, sizeof(body), "%s", facts);
                }
                r.confidence = 220;
                break;
            }

            case Q_WHAT_IS: {
                if (short_mode) {
                    char first_sent[512] = {0};
                    extract_sentences(facts, first_sent, 2, sizeof(first_sent));
                    trim(first_sent);
                    /* Only prepend "X is" if facts don't already start with subject */
                    char subj_lower[128] = {0};
                    for(int _i=0;q->subject[_i]&&_i<127;_i++)
                        subj_lower[_i]=tolower((unsigned char)q->subject[_i]);
                    char facts_lower[64] = {0};
                    for(int _i=0;first_sent[_i]&&_i<63;_i++)
                        facts_lower[_i]=tolower((unsigned char)first_sent[_i]);
                    if (strstr(facts_lower, subj_lower))
                        snprintf(body, sizeof(body), "%s", first_sent);
                    else
                        snprintf(body, sizeof(body), "%s is %s",
                                 q->subject, first_sent);
                } else {
                    snprintf(body, sizeof(body), "%s", facts);
                }
                r.confidence = 210;
                break;
            }

            case Q_WHERE_IS: {
                snprintf(body, sizeof(body), "%s", facts);
                r.confidence = 200;
                break;
            }

            case Q_WHEN_IS: {
                snprintf(body, sizeof(body), "%s", facts);
                r.confidence = 195;
                break;
            }

            case Q_WHY:
            case Q_HOW: {
                if (short_mode) {
                    char excerpt[1024] = {0};
                    extract_sentences(facts, excerpt, 3, sizeof(excerpt));
                    snprintf(body, sizeof(body), "%s", excerpt);
                } else {
                    snprintf(body, sizeof(body), "%s", facts);
                }
                r.confidence = 190;
                break;
            }

            case Q_IS: {
                /* Boolean-style answer */
                char first[256] = {0};
                extract_sentences(facts, first, 1, sizeof(first));
                snprintf(body, sizeof(body),
                    "Based on available information: %s", first);
                r.confidence = 180;
                break;
            }

            default: {
                snprintf(body, sizeof(body), "%s", facts);
                r.confidence = 160;
                break;
            }
        }

    /* ── CASE 2: No web facts — use grammar-based formation ── */
    } else {

        switch (q->type) {

            case Q_WHO_IS:
                snprintf(body, sizeof(body),
                    "I don't have information about \"%s\" in my current mappings. "
                    "Would you like me to search online for details about this person or entity?",
                    q->subject);
                break;

            case Q_WHAT_IS:
                snprintf(body, sizeof(body),
                    "\"%s\" is not yet mapped in my concept table. "
                    "I can look this up online to fetch a definition.",
                    q->subject);
                break;

            case Q_WHERE_IS:
                snprintf(body, sizeof(body),
                    "I don't have location data for \"%s\". "
                    "An online lookup would give you the most accurate result.",
                    q->subject);
                break;

            case Q_WHY:
                snprintf(body, sizeof(body),
                    "To answer why \"%s\", I would need to search for context. "
                    "Shall I look that up?",
                    q->subject);
                break;

            case Q_HOW:
                snprintf(body, sizeof(body),
                    "To explain how \"%s\" works, I can either use my mapped concepts "
                    "or fetch a detailed explanation online.",
                    q->subject);
                break;

            case Q_STATEMENT:
                snprintf(body, sizeof(body),
                    "I understood your statement about \"%s\". "
                    "The relevant concepts have been processed through the reasoning window.",
                    q->subject);
                break;

            case Q_COMMAND:
                snprintf(body, sizeof(body),
                    "Processing command: \"%s\".",
                    q->subject);
                break;

            default:
                snprintf(body, sizeof(body),
                    "I processed your input but couldn't form a complete response. "
                    "Try rephrasing or asking a specific question.");
                break;
        }
    }

    /* ── Apply formal tone ── */
    if (formal_mode) {
        /* Capitalize properly — body already starts capitalized */
        /* Replace casual contractions */
        char *pos;
        while ((pos = strstr(body, "don't")) != NULL)
            memcpy(pos, "do not", 6);
        while ((pos = strstr(body, "can't")) != NULL)
            memcpy(pos, "cannot", 6);
        while ((pos = strstr(body, "won't")) != NULL)
            memcpy(pos, "will not", 8);
        while ((pos = strstr(body, "I'm")) != NULL)
            memcpy(pos, "I am", 4);
    }

    /* ── Add context if requested ── */
    if (context_on && q->subject[0] && !short_mode) {
        r.has_context = 1;
        build_context(q->subject, r.sector, r.context, MAX_RESPONSE_CHARS);
    }

    /* ── Truncate if word limit is set ── */
    if (inst->max_response_words > 0)
        truncate_to_limit(body, inst->max_response_words);

    capitalize_first(body);
    strncpy(r.body, body, MAX_RESPONSE_CHARS - 1);
    r.word_count = word_count_of(r.body);
    return r;
}

/* ═══════════════════════════════════════════════════════════════
   PRINT RESPONSE
   Respects instruction flags for formatting
   ═══════════════════════════════════════════════════════════════ */

void print_response(const BlafResponse *r) {
    printf("\n╔══ BLAF RESPONSE ═══════════════════════════════════\n");

    if (r->body[0])
        printf("║\n%s\n", r->body);

    if (r->has_context && r->context[0]) {
        printf("\n── Context ──────────────────────────────────────────\n");
        printf("%s\n", r->context);
    }

    if (!has_instruction(INST_NO_EXPLAIN) && !has_instruction(INST_SHORT_ANSWER))
        printf("\n[confidence:%d | sector:0x%02X | words:%d]\n",
               r->confidence, r->sector, r->word_count);

    printf("╚════════════════════════════════════════════════════\n\n");
}

/* ═══════════════════════════════════════════════════════════════
   WORD LIMITER
   ═══════════════════════════════════════════════════════════════ */

void truncate_to_limit(char *text, int max_words) {
    if (max_words <= 0) return;
    int count = 0, in_word = 0;
    for (char *p = text; *p; p++) {
        if (isspace((unsigned char)*p)) in_word = 0;
        else if (!in_word) {
            in_word = 1;
            count++;
            if (count >= max_words) {
                *(p+1) = '\0';
                strcat(text, "...");
                break;
            }
        }
    }
}
