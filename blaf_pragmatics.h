/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_pragmatics.h                                   ║
 * ║  Speech Act Detection & Conversational Response Layer       ║
 * ║                                                              ║
 * ║  Detects WHAT the user is doing with their words,           ║
 * ║  not just what the words mean.                              ║
 * ║                                                              ║
 * ║  "Hello"     → ACT_GREETING   (respond socially)            ║
 * ║  "Thanks"    → ACT_GRATITUDE  (acknowledge)                 ║
 * ║  "What is X" → ACT_QUERY      (look up and answer)          ║
 * ║  "Do this"   → ACT_COMMAND    (execute)                     ║
 * ║                                                              ║
 * ║  Responses are generated from act + session context,        ║
 * ║  NOT from hardcoded strings.                                ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_PRAGMATICS_H
#define BLAF_PRAGMATICS_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
   SPEECH ACT TYPES
   Based on Austin/Searle speech act theory, simplified for NLP.
   ═══════════════════════════════════════════════════════════════ */

typedef enum {

    /* ── Social / Phatic ── */
    ACT_GREETING,      /* hello, hi, hey, good morning, wassup       */
    ACT_FAREWELL,      /* bye, goodbye, see you, later, take care    */
    ACT_GRATITUDE,     /* thanks, thank you, cheers, appreciate it   */
    ACT_APOLOGY,       /* sorry, my bad, excuse me, pardon           */
    ACT_AFFIRMATION,   /* yes, yeah, ok, sure, correct, exactly      */
    ACT_NEGATION,      /* no, nope, nah, wrong, incorrect            */
    ACT_ACKNOWLEDGEMENT, /* ok, alright, got it, understood, noted   */
    ACT_PRAISE,        /* good, great, nice, awesome, well done      */
    ACT_COMPLAINT,     /* that's wrong, bad answer, you're off       */

    /* ── Expressive ── */
    ACT_SURPRISE,      /* wow, really, seriously, no way, whoa       */
    ACT_FRUSTRATION,   /* ugh, come on, seriously, this is wrong     */
    ACT_CURIOSITY,     /* interesting, tell me more, go on           */

    /* ── Directive ── */
    ACT_COMMAND,       /* run, stop, restart, calculate, find        */
    ACT_REQUEST,       /* can you, could you, please, would you      */
    ACT_PERMISSION,    /* can I, may I, is it ok if                  */

    /* ── Informational ── */
    ACT_QUERY,         /* who/what/where/when/why/how + subject      */
    ACT_STATEMENT,     /* declarative — user is asserting a fact     */
    ACT_INSTRUCTION,   /* short answer, be formal, don't do X        */

    /* ── Meta ── */
    ACT_REPETITION,    /* say that again, repeat, what did you say   */
    ACT_CLARIFICATION, /* what do you mean, explain that, clarify    */

    /* ── Mixed / Unknown ── */
    ACT_MIXED,         /* "Hello, who is Bill Gates" — greeting+query */
    ACT_UNKNOWN,       /* could not determine                        */

} SpeechAct;

/* ═══════════════════════════════════════════════════════════════
   DETECTED ACT
   ═══════════════════════════════════════════════════════════════ */

#define MAX_ACT_PAYLOAD 512

typedef struct {
    SpeechAct act;
    SpeechAct secondary_act;    /* for ACT_MIXED — the second act type */
    char      payload[MAX_ACT_PAYLOAD]; /* the query/command part if mixed */
    float     confidence;
    int       is_social;        /* 1 = no information lookup needed    */
    int       has_query;        /* 1 = also contains a query to answer */
} DetectedAct;

/* ═══════════════════════════════════════════════════════════════
   SESSION CONTEXT  (feeds response generation)
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int   turn_count;           /* how many turns in this session      */
    int   last_was_greeting;    /* did we just greet?                  */
    int   last_was_error;       /* did we just fail to answer?         */
    char  user_name[64];        /* if user introduced themselves       */
    char  last_topic[128];      /* last subject discussed              */
    SpeechAct last_act;
} PragmaticContext;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

void pragmatics_init(void);

/*
 * detect_act()
 * Classify the speech act of an input string.
 * Must be called BEFORE analyze_question() and intent pipeline.
 *
 * Example:
 *   DetectedAct a = detect_act("hello");
 *   // a.act       = ACT_GREETING
 *   // a.is_social = 1
 *   // a.has_query = 0
 *
 *   DetectedAct a = detect_act("hello, what is blockchain");
 *   // a.act           = ACT_MIXED
 *   // a.secondary_act = ACT_QUERY
 *   // a.payload       = "what is blockchain"
 *   // a.has_query     = 1
 */
DetectedAct detect_act(const char *input);

/*
 * generate_social_response()
 * Generates a contextual social reply without hardcoded strings.
 * Uses act type + session context + word vectors if available
 * to vary the response naturally.
 *
 * out     = output buffer
 * outlen  = buffer size
 *
 * Example:
 *   generate_social_response(&act, &ctx, out, sizeof(out));
 *   // "Hey there." or "Hello." or "Hi, good to hear from you."
 *   // — varies based on context, never the same string every time
 */
void generate_social_response(const DetectedAct *act,
                               const PragmaticContext *ctx,
                               char *out, int outlen);

/*
 * update_pragmatic_context()
 * Call after every turn to keep session context current.
 */
void update_pragmatic_context(const DetectedAct *act,
                               const char *topic);

/*
 * get_pragmatic_context()
 * Returns the current session context.
 */
const PragmaticContext* get_pragmatic_context(void);

/*
 * act_name()
 * Human-readable name for a SpeechAct.
 */
const char* act_name(SpeechAct act);

#endif /* BLAF_PRAGMATICS_H */
