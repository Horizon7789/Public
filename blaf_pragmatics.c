/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_pragmatics.c                                   ║
 * ║  Speech Act Detection & Conversational Response Layer       ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "blaf_pragmatics.h"

/* ═══════════════════════════════════════════════════════════════
   PATTERN TABLE
   ═══════════════════════════════════════════════════════════════ */
typedef struct { const char *pattern; SpeechAct act; } ActPattern;

static const ActPattern act_patterns[] = {
    /* Greetings */
    {"good morning",     ACT_GREETING}, {"good afternoon",  ACT_GREETING},
    {"good evening",     ACT_GREETING}, {"good day",        ACT_GREETING},
    {"how are you",      ACT_GREETING}, {"what's up",       ACT_GREETING},
    {"whats up",         ACT_GREETING}, {"wassup",          ACT_GREETING},
    {"howdy",            ACT_GREETING}, {"greetings",       ACT_GREETING},
    {"hiya",             ACT_GREETING}, {"hey there",       ACT_GREETING},
    {"hello",            ACT_GREETING}, {"howdy",           ACT_GREETING},
    /* Farewells */
    {"goodbye",    ACT_FAREWELL}, {"good bye",   ACT_FAREWELL},
    {"see you",    ACT_FAREWELL}, {"take care",  ACT_FAREWELL},
    {"bye bye",    ACT_FAREWELL}, {"ciao",       ACT_FAREWELL},
    {"later",      ACT_FAREWELL}, {"peace out",  ACT_FAREWELL},
    /* Gratitude */
    {"thank you",    ACT_GRATITUDE}, {"thanks a lot",  ACT_GRATITUDE},
    {"many thanks",  ACT_GRATITUDE}, {"cheers",        ACT_GRATITUDE},
    {"appreciate it",ACT_GRATITUDE}, {"much appreciated",ACT_GRATITUDE},
    {"thx",          ACT_GRATITUDE},
    /* Apology */
    {"i'm sorry", ACT_APOLOGY}, {"im sorry",    ACT_APOLOGY},
    {"my bad",    ACT_APOLOGY}, {"excuse me",   ACT_APOLOGY},
    {"sorry",     ACT_APOLOGY}, {"my apologies",ACT_APOLOGY},
    /* Affirmation */
    {"yes,",       ACT_AFFIRMATION}, {"yeah",        ACT_AFFIRMATION},
    {"correct",    ACT_AFFIRMATION}, {"exactly",     ACT_AFFIRMATION},
    {"absolutely", ACT_AFFIRMATION}, {"of course",   ACT_AFFIRMATION},
    /* Negation */
    {"nope",          ACT_NEGATION}, {"not really",    ACT_NEGATION},
    {"that's wrong",  ACT_NEGATION}, {"thats wrong",   ACT_NEGATION},
    {"incorrect",     ACT_NEGATION}, {"wrong",         ACT_NEGATION},
    /* Acknowledgement */
    {"got it",     ACT_ACKNOWLEDGEMENT}, {"i see",      ACT_ACKNOWLEDGEMENT},
    {"understood", ACT_ACKNOWLEDGEMENT}, {"noted",      ACT_ACKNOWLEDGEMENT},
    {"makes sense",ACT_ACKNOWLEDGEMENT}, {"alright",    ACT_ACKNOWLEDGEMENT},
    /* Praise */
    {"well done", ACT_PRAISE}, {"good job",  ACT_PRAISE},
    {"nice one",  ACT_PRAISE}, {"awesome",   ACT_PRAISE},
    {"excellent", ACT_PRAISE}, {"brilliant", ACT_PRAISE},
    /* Complaint */
    {"that's not right", ACT_COMPLAINT}, {"bad answer",    ACT_COMPLAINT},
    {"wrong answer",     ACT_COMPLAINT}, {"you're wrong",  ACT_COMPLAINT},
    {"not correct",      ACT_COMPLAINT},
    /* Surprise */
    {"no way",    ACT_SURPRISE}, {"seriously", ACT_SURPRISE},
    {"really?",   ACT_SURPRISE}, {"wow",       ACT_SURPRISE},
    {"whoa",      ACT_SURPRISE}, {"omg",       ACT_SURPRISE},
    /* Curiosity */
    {"tell me more", ACT_CURIOSITY}, {"go on",       ACT_CURIOSITY},
    {"interesting",  ACT_CURIOSITY}, {"elaborate",   ACT_CURIOSITY},
    /* Repetition */
    {"say that again",   ACT_REPETITION}, {"repeat that", ACT_REPETITION},
    {"what did you say", ACT_REPETITION}, {"come again",  ACT_REPETITION},
    /* Clarification */
    {"what do you mean",     ACT_CLARIFICATION},
    {"explain that",         ACT_CLARIFICATION},
    {"be more specific",     ACT_CLARIFICATION},
    {"clarify",              ACT_CLARIFICATION},
    /* Frustration */
    {"ugh",           ACT_FRUSTRATION}, {"come on",       ACT_FRUSTRATION},
    {"this is wrong", ACT_FRUSTRATION}, {"not again",     ACT_FRUSTRATION},
    {NULL, ACT_UNKNOWN}
};

/* ═══════════════════════════════════════════════════════════════
   RESPONSE POOLS
   Grouped by act — response is chosen by:
     index = (turn_count + time_seed) % pool_size
   Never hardcoded to one string.
   ═══════════════════════════════════════════════════════════════ */

typedef struct { const char **lines; int count; } ResponsePool;

static const char *greeting_lines[] = {
    "Hello.", "Hi there.", "Hey.", "Greetings.",
    "Good to hear from you.", "Hi, what's on your mind?",
    "Hello — what would you like to know?"
};
static const char *farewell_lines[] = {
    "Goodbye.", "Take care.", "See you.", "Until next time.",
    "Bye.", "Farewell.", "Later."
};
static const char *gratitude_lines[] = {
    "You're welcome.", "No problem.", "Happy to help.",
    "Anytime.", "Glad I could assist.", "Of course."
};
static const char *apology_lines[] = {
    "No worries.", "It's fine.", "All good.",
    "Not a problem.", "Don't worry about it."
};
static const char *affirmation_lines[] = {
    "Got it.", "Understood.", "Noted.", "Alright.",
    "Good to know.", "OK, continuing."
};
static const char *negation_lines[] = {
    "Understood, let me try differently.",
    "OK, noted. What would you prefer?",
    "Acknowledged. How can I correct that?",
    "Fair enough. Let me know what you need."
};
static const char *acknowledgement_lines[] = {
    "Good.", "Great.", "Moving on.",
    "Understood.", "Noted."
};
static const char *praise_lines[] = {
    "Thank you.", "Glad that worked.",
    "Appreciated.", "Good to know I'm on track."
};
static const char *complaint_lines[] = {
    "I'll try to do better. What was wrong?",
    "Noted. Could you clarify what you expected?",
    "Apologies for that. Let me correct it.",
    "Thank you for the feedback. Try asking again."
};
static const char *surprise_lines[] = {
    "Indeed.", "Yes, it's true.", "Quite surprising.",
    "The world is full of interesting facts."
};
static const char *curiosity_lines[] = {
    "Ask me a specific question and I'll go deeper.",
    "What aspect would you like to explore?",
    "I can search for more detail — just ask."
};
static const char *repetition_lines[] = {
    "I'll restate that:", "Sure, here it is again:",
    "Repeating:", "One more time:"
};
static const char *clarification_lines[] = {
    "Could you be more specific?",
    "What exactly are you referring to?",
    "Which part would you like me to clarify?"
};
static const char *frustration_lines[] = {
    "I understand your frustration. Let me try again.",
    "Let's take another approach.",
    "Sorry about that. What specifically went wrong?"
};
static const char *unknown_lines[] = {
    "I'm not sure what you mean. Could you rephrase?",
    "Could you clarify?",
    "I didn't quite catch that."
};

#define POOL(arr) {arr, (int)(sizeof(arr)/sizeof(arr[0]))}

static const ResponsePool pools[] = {
    POOL(greeting_lines),      /* ACT_GREETING      */
    POOL(farewell_lines),      /* ACT_FAREWELL      */
    POOL(gratitude_lines),     /* ACT_GRATITUDE     */
    POOL(apology_lines),       /* ACT_APOLOGY       */
    POOL(affirmation_lines),   /* ACT_AFFIRMATION   */
    POOL(negation_lines),      /* ACT_NEGATION      */
    POOL(acknowledgement_lines),/* ACT_ACKNOWLEDGEMENT */
    POOL(praise_lines),        /* ACT_PRAISE        */
    POOL(complaint_lines),     /* ACT_COMPLAINT     */
    POOL(surprise_lines),      /* ACT_SURPRISE      */
    POOL(frustration_lines),   /* ACT_FRUSTRATION   */
    POOL(curiosity_lines),     /* ACT_CURIOSITY     */
    POOL(unknown_lines),       /* ACT_COMMAND       */
    POOL(unknown_lines),       /* ACT_REQUEST       */
    POOL(unknown_lines),       /* ACT_PERMISSION    */
    POOL(unknown_lines),       /* ACT_QUERY         */
    POOL(unknown_lines),       /* ACT_STATEMENT     */
    POOL(unknown_lines),       /* ACT_INSTRUCTION   */
    POOL(repetition_lines),    /* ACT_REPETITION    */
    POOL(clarification_lines), /* ACT_CLARIFICATION */
    POOL(unknown_lines),       /* ACT_MIXED         */
    POOL(unknown_lines),       /* ACT_UNKNOWN       */
};

/* ═══════════════════════════════════════════════════════════════
   SESSION CONTEXT
   ═══════════════════════════════════════════════════════════════ */
static PragmaticContext g_ctx = {0, 0, 0, {0}, {0}, ACT_UNKNOWN};
static int g_init = 0;

void pragmatics_init(void) {
    if (g_init) return;
    g_init = 1;
    memset(&g_ctx, 0, sizeof(g_ctx));
    srand((unsigned)time(NULL));
    int n = 0;
    while (act_patterns[n].pattern) n++;
    printf("[PRAGMATICS] Speech act engine ready — %d patterns.\n", n);
}

/* ═══════════════════════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════════════════════ */
static void lower_copy(char *d, const char *s, int n) {
    int i; for(i=0;i<n-1&&s[i];i++) d[i]=tolower((unsigned char)s[i]); d[i]='\0';
}

/*
 * Check if input is PURELY a single word social token:
 *   "hi", "hello", "bye", "thanks" — no additional content
 * vs "hi what is X" which is MIXED
 */
static int is_pure_social(const char *lower_input) {
    /* Count tokens */
    int tokens = 0;
    const char *p = lower_input;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p) { tokens++; while (*p && !isspace((unsigned char)*p)) p++; }
    }
    return tokens <= 2; /* 1-2 words = pure social */
}

/*
 * Find query portion after a social opener.
 * "hello what is blockchain" → "what is blockchain"
 */
static int extract_query_portion(const char *input, char *out, int outlen) {
    /* Question starters that indicate a query follows */
    const char *q_starters[] = {
        "what is","what are","who is","who are","where is","where are",
        "when is","when was","why is","why does","how is","how does",
        "how far","how long","how much","how many","how old","how do",
        "what was","tell me","explain","define","describe",
        NULL
    };
    char l[512]; lower_copy(l, input, 512);
    for (int i = 0; q_starters[i]; i++) {
        char *pos = strstr(l, q_starters[i]);
        if (pos) {
            int offset = (int)(pos - l);
            strncpy(out, input + offset, outlen - 1);
            out[outlen-1] = '\0';
            return 1;
        }
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   DETECT ACT
   ═══════════════════════════════════════════════════════════════ */

DetectedAct detect_act(const char *input) {
    DetectedAct da = {ACT_UNKNOWN, ACT_UNKNOWN, {0}, 0.0f, 0, 0};
    if (!input || !input[0]) return da;

    char l[512]; lower_copy(l, input, 512);

    /* Explicit "what is X" — always a QUERY, never a greeting
     * even if X is a social word like "hi" or "hello" */
    if (strncmp(l, "what is ",  8) == 0 ||
        strncmp(l, "what are ", 9) == 0 ||
        strncmp(l, "define ",   7) == 0) {
        da.act        = ACT_QUERY;
        da.is_social  = 0;
        da.has_query  = 1;
        da.confidence = 1.0f;
        strncpy(da.payload, input, MAX_ACT_PAYLOAD-1);
        return da;
    }

    /* Pattern scan */
    SpeechAct matched = ACT_UNKNOWN;
    for (int i = 0; act_patterns[i].pattern; i++) {
        if (strstr(l, act_patterns[i].pattern)) {
            matched = act_patterns[i].act;
            break;
        }
    }

    /* Single letters that are greetings */
    if (matched == ACT_UNKNOWN) {
        if (strcmp(l, "hi")  == 0 || strcmp(l, "hi!") == 0 ||
            strcmp(l, "hey") == 0 || strcmp(l, "hey!") == 0)
            matched = ACT_GREETING;
        if (strcmp(l, "bye") == 0 || strcmp(l, "bye!") == 0)
            matched = ACT_FAREWELL;
        if (strcmp(l, "ok")  == 0 || strcmp(l, "ok.") == 0)
            matched = ACT_ACKNOWLEDGEMENT;
        if (strcmp(l, "thanks") == 0 || strcmp(l, "thx") == 0)
            matched = ACT_GRATITUDE;
    }

    if (matched == ACT_UNKNOWN) {
        da.act = ACT_UNKNOWN;
        return da;
    }

    /* Check if there's also a query embedded after the social part */
    char query_part[MAX_ACT_PAYLOAD] = {0};
    int has_q = extract_query_portion(input, query_part, MAX_ACT_PAYLOAD);

    if (has_q && !is_pure_social(l)) {
        /* Mixed: greeting + query */
        da.act           = ACT_MIXED;
        da.secondary_act = ACT_QUERY;
        da.has_query     = 1;
        da.is_social     = 0;
        strncpy(da.payload, query_part, MAX_ACT_PAYLOAD-1);
        da.confidence    = 0.9f;
    } else {
        /* Pure social act */
        da.act        = matched;
        da.is_social  = 1;
        da.has_query  = 0;
        da.confidence = 0.95f;
    }

    return da;
}

/* ═══════════════════════════════════════════════════════════════
   GENERATE SOCIAL RESPONSE
   Uses turn_count + rand to vary responses naturally.
   ═══════════════════════════════════════════════════════════════ */

void generate_social_response(const DetectedAct *act,
                               const PragmaticContext *ctx,
                               char *out, int outlen) {
    if (!act || !out) return;

    int pool_idx = (int)act->act;
    if (pool_idx < 0 || pool_idx >= (int)(sizeof(pools)/sizeof(pools[0]))) {
        strncpy(out, "I'm here.", outlen-1);
        return;
    }

    const ResponsePool *pool = &pools[pool_idx];
    if (pool->count == 0) {
        strncpy(out, "OK.", outlen-1);
        return;
    }

    /* Vary by turn count + random so same act gets different responses */
    int idx = (ctx->turn_count + rand()) % pool->count;
    strncpy(out, pool->lines[idx], outlen-1);

    /* Personalise greeting if we know the user's name */
    if (act->act == ACT_GREETING && ctx->user_name[0]) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s, %s", out, ctx->user_name);
        strncpy(out, tmp, outlen-1);
    }

    /* If last topic is known and user seems curious, hint at it */
    if (act->act == ACT_CURIOSITY && ctx->last_topic[0]) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s Ask me about \"%s\" for more.",
                 out, ctx->last_topic);
        strncpy(out, tmp, outlen-1);
    }
}

void update_pragmatic_context(const DetectedAct *act, const char *topic) {
    g_ctx.turn_count++;
    g_ctx.last_act = act->act;
    g_ctx.last_was_greeting = (act->act == ACT_GREETING);
    if (topic && topic[0])
        strncpy(g_ctx.last_topic, topic, 127);

    /* Detect self-introduction: "my name is X" or "I am X" */
    if (act->payload[0]) {
        const char *name_markers[] = {"my name is ", "i am ", "i'm ", "call me ", NULL};
        char l[MAX_ACT_PAYLOAD]; lower_copy(l, act->payload, MAX_ACT_PAYLOAD);
        for (int i = 0; name_markers[i]; i++) {
            char *pos = strstr(l, name_markers[i]);
            if (pos) {
                pos += strlen(name_markers[i]);
                char name[64] = {0}; int j = 0;
                while (*pos && !isspace((unsigned char)*pos) && j < 63)
                    name[j++] = *pos++;
                if (name[0]) {
                    name[0] = toupper((unsigned char)name[0]);
                    strncpy(g_ctx.user_name, name, 63);
                    printf("[PRAGMATICS] User name recorded: %s\n", name);
                }
                break;
            }
        }
    }
}

const PragmaticContext* get_pragmatic_context(void) { return &g_ctx; }

const char* act_name(SpeechAct act) {
    switch(act) {
        case ACT_GREETING:       return "GREETING";
        case ACT_FAREWELL:       return "FAREWELL";
        case ACT_GRATITUDE:      return "GRATITUDE";
        case ACT_APOLOGY:        return "APOLOGY";
        case ACT_AFFIRMATION:    return "AFFIRMATION";
        case ACT_NEGATION:       return "NEGATION";
        case ACT_ACKNOWLEDGEMENT:return "ACKNOWLEDGEMENT";
        case ACT_PRAISE:         return "PRAISE";
        case ACT_COMPLAINT:      return "COMPLAINT";
        case ACT_SURPRISE:       return "SURPRISE";
        case ACT_FRUSTRATION:    return "FRUSTRATION";
        case ACT_CURIOSITY:      return "CURIOSITY";
        case ACT_COMMAND:        return "COMMAND";
        case ACT_REQUEST:        return "REQUEST";
        case ACT_PERMISSION:     return "PERMISSION";
        case ACT_QUERY:          return "QUERY";
        case ACT_STATEMENT:      return "STATEMENT";
        case ACT_INSTRUCTION:    return "INSTRUCTION";
        case ACT_REPETITION:     return "REPETITION";
        case ACT_CLARIFICATION:  return "CLARIFICATION";
        case ACT_MIXED:          return "MIXED";
        default:                 return "UNKNOWN";
    }
}
