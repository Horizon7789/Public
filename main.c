/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — main.c  v0.6                                        ║
 * ║  Entry Point, Live Input Loop, Query Orchestrator           ║
 * ║                                                              ║
 * ║  Build (offline):                                            ║
 * ║    gcc -O2 -o blaf main.c blaf_core.c blaf_mapper.c         ║
 * ║         blaf_advanced.c blaf_sectors.c blaf_grammar.c        ║
 * ║         blaf_output.c blaf_instructions.c blaf_llm.c         ║
 * ║         blaf_vectors.c blaf_intent.c blaf_compose.c          ║
 * ║         blaf_tts.c blaf_math.c blaf_pragmatics.c             ║
 * ║         blaf_extractor.c blaf_context.c blaf_learn.c -lm     ║
 * ║                                                              ║
 * ║  Build (with live web + LLM):                                ║
 * ║    add -DBLAF_USE_CURL -lcurl                                ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "blaf_core.h"
#include "blaf_sectors.h"
#include "blaf_mapper.h"
#include "blaf_advanced.h"
#include "blaf_grammar.h"
#include "blaf_syntax.h"
#include "blaf_output.h"
#include "blaf_instructions.h"
#include "blaf_llm.h"
#include "blaf_vectors.h"
#include "blaf_intent.h"
#include "blaf_compose.h"
#include "blaf_tts.h"
#include "blaf_pragmatics.h"
#include "blaf_math.h"
#include "blaf_extractor.h"
#include "blaf_context.h"
#include "blaf_learn.h"

/* From blaf_core.c */
extern void sectors_init (void);
extern void seed_concepts(void);
extern void reset_context(void);
extern void process      (const char *input);
extern int  concept_count;

/* ═══════════════════════════════════════════════════════════════
   SUBJECT RESULT CACHE
   Key = composed query string.  Value = raw fetched result.
   Different intents about the same subject cache separately.
   ═══════════════════════════════════════════════════════════════ */

#define CACHE_MAX  256
#define CACHE_KEY  256
#define CACHE_VAL  8192

typedef struct { char key[CACHE_KEY]; char val[CACHE_VAL]; int used; } CacheEntry;
static CacheEntry g_cache[CACHE_MAX];
static int        g_cache_count = 0;

static const char* cache_get(const char *key) {
    for (int i = 0; i < g_cache_count; i++)
        if (g_cache[i].used && strcasecmp(g_cache[i].key, key) == 0)
            return g_cache[i].val;
    return NULL;
}
static void cache_set(const char *key, const char *val) {
    for (int i = 0; i < g_cache_count; i++) {
        if (g_cache[i].used && strcasecmp(g_cache[i].key, key) == 0) {
            strncpy(g_cache[i].val, val, CACHE_VAL-1); return;
        }
    }
    if (g_cache_count < CACHE_MAX) {
        CacheEntry *e = &g_cache[g_cache_count++];
        strncpy(e->key, key, CACHE_KEY-1);
        strncpy(e->val, val, CACHE_VAL-1);
        e->used = 1;
        printf("[CACHE] Stored: \"%s\"\n", key);
    }
}

/* ═══════════════════════════════════════════════════════════════
   WIKIPEDIA FETCH
   ═══════════════════════════════════════════════════════════════ */

#ifdef BLAF_USE_CURL
#include <curl/curl.h>
typedef struct { char *data; size_t size; } WebBuf;
static size_t web_write_cb(void *ptr, size_t sz, size_t n, void *ud) {
    WebBuf *b = (WebBuf*)ud;
    b->data = realloc(b->data, b->size + sz*n + 1);
    memcpy(b->data + b->size, ptr, sz*n);
    b->size += sz*n; b->data[b->size] = '\0';
    return sz*n;
}
static char* fetch_wikipedia(const char *subject) {
    char url[512], encoded[256] = {0};
    int j = 0;
    for (int i = 0; subject[i] && j < 254; i++)
        encoded[j++] = (subject[i] == ' ') ? '_' : subject[i];
    snprintf(url, sizeof(url),
             "https://en.wikipedia.org/api/rest_v1/page/summary/%s", encoded);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    WebBuf buf = {malloc(1), 0}; buf.data[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  web_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "BLAF/0.6");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    char *extract = strstr(buf.data, "\"extract\":\"");
    char *result  = NULL;
    if (extract) {
        extract += strlen("\"extract\":\"");
        char *end = extract;
        while (*end && !(*end == '"' && *(end-1) != '\\')) end++;
        int len = (int)(end - extract);
        result = malloc(len + 1);
        strncpy(result, extract, len); result[len] = '\0';
        char *p = result;
        while ((p = strstr(p, "\\n"))) { *p=' '; *(p+1)=' '; }
    }
    free(buf.data);
    return result;
}
#else
static char* fetch_wikipedia(const char *subject) {
    printf("[WEB] STUB — compile with -DBLAF_USE_CURL -lcurl for live search.\n");
    char *stub = malloc(256);
    snprintf(stub, 256, "(%s: enable -DBLAF_USE_CURL for live results.)", subject);
    return stub;
}
#endif

/* ═══════════════════════════════════════════════════════════════
   QUERY ORCHESTRATOR
   ═══════════════════════════════════════════════════════════════ */

static void handle_query(const char *input) {

    /* ── Layer 0: Instructions ── */
    if (parse_instruction(input)) return;

    /* ── Layer 1: Math — before speech acts
     *   "two plus two" must not trigger a greeting check */
    if (math_detect(input)) {
        MathResult mr = math_evaluate(input);
        if (mr.success) {
            printf("\n╔══ MATH RESULT ═════════════════════════════════════\n");
            printf("║  %s = %s\n", input, mr.full_str);
            printf("╚════════════════════════════════════════════════════\n\n");
            return;
        }
        printf("[MATH] Could not evaluate: %s\n", mr.error);
    }

    /* ── Layer 2: Speech Act Detection ──
     *   "hi"           → ACT_GREETING  → social response, no search
     *   "what is hi"   → ACT_QUERY     → full info pipeline
     *   "hi, who is X" → ACT_MIXED     → greet + query              */
    DetectedAct da = detect_act(input);

    printf("\n[ACT] %s", act_name(da.act));
    if (da.act == ACT_MIXED)
        printf(" + %s", act_name(da.secondary_act));
    printf("\n");

    const PragmaticContext *ctx = get_pragmatic_context();

    /* Pure social — respond and done */
    if (da.is_social && !da.has_query) {
        char response[512];
        generate_social_response(&da, ctx, response, sizeof(response));
        printf("\n╔══ BLAF RESPONSE ═══════════════════════════════════\n");
        printf("║  %s\n", response);
        printf("╚════════════════════════════════════════════════════\n\n");
        update_pragmatic_context(&da, NULL);
        return;
    }

    /* Mixed — greet first, then handle the query part */
    if (da.act == ACT_MIXED && da.payload[0]) {
        char social_resp[256];
        generate_social_response(&da, ctx, social_resp, sizeof(social_resp));
        printf("║  [Social] %s\n", social_resp);
        input = da.payload;
    }

    /* ── Layer 3: Context Enrichment ──
     *   "where is it?"    → "where is London?"  (resolves pronoun)
     *   "tell me more"    → "tell me more about Bitcoin"
     *   "how far is it?"  → "how far is London?" */
    char enriched[512] = {0};
    if (context_enrich_query(input, enriched, sizeof(enriched))) {
        printf("[CONTEXT] Enriched: \"%s\"\n", enriched);
        input = enriched;
    }

    /* ── Layer 4: Grammar ── */
    ParsedQuery q = analyze_question(input);

    /* ── Layer 5: Intent ── */
    ResolvedIntent intent = resolve_intent(&q);

    /* ── Layer 6: Compose ── */
    ComposedQuery cq = compose_query(&intent);

    printf("[QUERY] Type:%-12s Intent:%-14s Subject:\"%s\"\n",
           question_type_name(q.type), intent_name(intent.type), q.subject);

    /* ── Layer 7: Core pipeline ── */
    process(input);

    /* ── Layer 8: Fetch facts ── */
    char *web_facts = NULL;
    int   web_enabled = !has_instruction(INST_WEB_OFF);

    if (intent.type != INTENT_UNKNOWN && q.subject[0]) {

        /* A. Local concept store first — no network needed */
        int local_idx = find_concept_index(q.subject, 0xFF);
        if (local_idx != -1 && concept_table[local_idx].summary != NULL) {
            printf("[LOCAL] HIT: Using saved knowledge for \"%s\"\n", q.subject);
            web_facts = strdup(concept_table[local_idx].summary);
        }

        /* B. RAM cache second */
        if (!web_facts) {
            const char *cached = cache_get(cq.query);
            if (cached) {
                printf("[CACHE] HIT: \"%s\"\n", cq.query);
                web_facts = strdup(cached);
            }
        }

        /* C. Web / LLM fetch */
        if (!web_facts && web_enabled) {
            printf("[WEB] Searching: \"%s\"\n", cq.query);
            web_facts = fetch_wikipedia(q.subject);

            if (!web_facts) {
                char prompt[512];
                compose_answer_prompt(&cq, prompt, sizeof(prompt));
                LLMResponse lr = llm_query(prompt);
                if (lr.success && lr.text[0])
                    web_facts = strdup(lr.text);
            }

            if (web_facts &&
                strstr(web_facts, "enable -DBLAF_USE_CURL") == NULL) {

                cache_set(cq.query, web_facts);

                /* Persist full summary to concept store */
                if (local_idx != -1) {
                    if (concept_table[local_idx].summary)
                        free(concept_table[local_idx].summary);
                    size_t slen = strlen(web_facts);
                    concept_table[local_idx].summary = malloc(slen + 1);
                    if (concept_table[local_idx].summary) {
                        strcpy(concept_table[local_idx].summary, web_facts);
                        concept_table[local_idx].summary_len = (uint32_t)slen;
                    }
                }

                /* ── LEARNING PIPELINE ──
                 * 1. blaf_learn   — proper POS tagging + SVO fact extraction
                 * 2. map_answer_words — register every new word into concept table
                 * 3. context_add_sentence — build co-occurrence map
                 * Replaces the old strtok loop + learn_from_sentence stubs */
                uint8_t sec = intent_sector_hint(intent.type);
                learn_paragraph(web_facts, sec);
                map_answer_words(web_facts, sec);
                context_add_sentence(web_facts, sec);

                knowledge_save(NULL);
                learn_save(NULL);
                context_save();
            }
        }
    }

    /* ── Layer 9: Smart extraction ──
     *   Extract the specific sentence(s) relevant to the intent.
     *   "how far is London" gets distance sentence, not full article. */
    char extracted[MAX_EXTRACT_LEN] = {0};
    if (web_facts) {
        ExtractionResult er = extract_answer(web_facts, &intent, q.subject);
        if (er.answer[0]) {
            strncpy(extracted, er.answer, MAX_EXTRACT_LEN-1);
            /* Learn from the precise extracted answer too */
            learn_paragraph(extracted, intent_sector_hint(intent.type));
        }
    }

    /* ── Layer 10: Format ── */
    char formatted[8192] = {0};
    const char *answer_src = extracted[0] ? extracted : web_facts;
    if (answer_src)
        compose_format_answer(&cq, answer_src, formatted, sizeof(formatted));

    /* ── Layer 11: Respond ── */
    BlafResponse resp = build_answer(&q, formatted[0] ? formatted : answer_src);
    resp.sector = intent_sector_hint(intent.type);
    print_response(&resp);

    /* tts_speak(resp.body, q.type); */  /* uncomment when diphones ready */

    /* ── Layer 12: Update session context ── */
    update_pragmatic_context(&da, q.subject);   /* once — was duplicated */
    context_add_turn(input, q.subject, resp.body, resp.sector);

    if (web_facts) free(web_facts);
}

/* ═══════════════════════════════════════════════════════════════
   BANNER
   ═══════════════════════════════════════════════════════════════ */

static void print_banner(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     BLAF — Bit-Level AI Flow  v0.6                  ║\n");
    printf("║     Pragmatics · Math · Semantic Intent · TTS        ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Queries:                                            ║\n");
    printf("║    who is Bill Gates                                 ║\n");
    printf("║    how far is London from Paris                      ║\n");
    printf("║    what is blockchain                                ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Math (BODMAS):                                      ║\n");
    printf("║    (3 + 4) * 2                                       ║\n");
    printf("║    two plus three times four                         ║\n");
    printf("║    square root of 144                                ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Social (no search triggered):                       ║\n");
    printf("║    hi  /  hello  /  thanks  /  bye                   ║\n");
    printf("║    hi what is blockchain  (mixed — greet + query)    ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Context-aware follow-ups:                           ║\n");
    printf("║    where is it?  /  tell me more  /  how old is he  ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Instructions:  short answer · formal · give context ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Commands:                                           ║\n");
    printf("║    sectors · grammar · concepts · tts status         ║\n");
    printf("║    vec status · ctx status · llm status              ║\n");
    printf("║    facts [subject]   — show stored facts             ║\n");
    printf("║    learn [sentence]  — manually teach a fact         ║\n");
    printf("║    related [word]    — show co-occurring words       ║\n");
    printf("║    search [text]     — search local summaries        ║\n");
    printf("║    speak [text]      — TTS output                    ║\n");
    printf("║    load vectors [path]                               ║\n");
    printf("║    ingest [path] [name]                              ║\n");
    printf("║    brain [subject]   — show raw fact hashes          ║\n");
    printf("║    deduplicate · clear facts · clear summaries       ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
}

/* ═══════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════ */

int main(void) {

    sectors_init();
    grammar_init();
    output_init();
    instructions_init();
    intent_init();
    compose_init();
    math_init();
    pragmatics_init();
    extractor_init();
    context_init();
    syntax_init();

    /* Load all persistent knowledge before seeding
     * so seed_concepts() skips already-known words */
    knowledge_load(NULL);
    context_load();
    learn_init();        /* learn_init() calls learn_load() internally */
    seed_concepts();

    printf("Concepts loaded: %d\n", concept_count);
    sectors_list();

    /* ── TTS ─────────────────────────────────────────────────
     * tts_init("./diphones", 1.0f, 120.0f);
     * tts_load_cmu_dict("./cmudict.dict");
     */

    /* ── Vectors ─────────────────────────────────────────────
     * vec_init("./glove.6B.50d.txt", 50000);
     */

    /* ── LLM ─────────────────────────────────────────────────
     * llm_init(LLM_GOOGLE,    "API_KEY",    "gemini-1.5-pro");
     * llm_init(LLM_OPENAI,    "sk-...",     "gpt-4o");
     * llm_init(LLM_ANTHROPIC, "sk-ant-...", "claude-sonnet-4-6");
     * llm_init(LLM_LOCAL, "", "llama3");
     * llm_set_endpoint("http://localhost:11434/v1/chat/completions");
     */

    print_banner();

    char input[512];
    while (1) {
        printf("BLAF> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;

        int len = strlen(input);
        while (len > 0 && (input[len-1]=='\n'||input[len-1]=='\r')) input[--len]='\0';
        if (len == 0) continue;

        /* ── 1. Exit ── */
        if (!strcmp(input,"quit")||!strcmp(input,"exit")||!strcmp(input,"q")) {
            printf("Goodbye.\n"); break;
        }

        /* ── 2. System ── */
        if (!strcmp(input,"help"))       { print_banner();    continue; }
        if (!strcmp(input,"sectors"))    { sectors_list();    continue; }
        if (!strcmp(input,"grammar"))    { grammar_dump();    continue; }

        /* ── 3. Status ── */
        if (!strcmp(input,"tts status")) { tts_status();      continue; }
        if (!strcmp(input,"vec status")) { vec_status();      continue; }
        if (!strcmp(input,"llm status")) { llm_status();      continue; }
        if (!strcmp(input,"ctx status")) { context_status();  continue; }

        /* ── 4. Configuration ── */
        if (!strncmp(input,"load vectors ",13)) {
            vec_init(input+13, 50000); intent_init(); continue;
        }
        if (!strncmp(input,"add sector ",11)) {
            uint8_t id = sector_id(input+11);
            printf("Sector \"%s\" = 0x%02X\n", input+11, id); continue;
        }

        /* ── 5. Knowledge ingestion ── */
        if (!strncmp(input, "ingest ", 7)) {
            char path[256], cname[64];
            if (sscanf(input + 7, "%s %s", path, cname) == 2) {
                ingest_file_as_concept(path, cname);
            } else {
                printf("[ERROR] Usage: ingest [path] [concept_name]\n");
            }
            continue;
        }

        /* ── 6. Concepts listing ── */
        if (!strcmp(input, "concepts") || !strcmp(input, "list concepts")) {
            size_t total_mem = 0;
            printf("\n%-20s | %-10s | %-10s\n", "CONCEPT", "SIZE", "TRUST");
            printf("-------------------------------------------\n");
            for (int i = 0; i < concept_count; i++) {
                printf("%-20s | %-10u | %-10u\n",
                    concept_table[i].word,
                    concept_table[i].summary_len,
                    concept_table[i].trust);
                total_mem += concept_table[i].summary_len;
            }
            printf("-------------------------------------------\n");
            printf("Total Heap Usage: %zu bytes (~%.2f KB)\n\n",
                   total_mem, total_mem/1024.0);
            continue;
        }

        /* ── 7. Search local summaries ── */
        if (!strncmp(input, "search ", 7)) {
            const char *query = input + 7;
            int found_count = 0;
            printf("\n--- SEARCH RESULTS FOR: '%s' ---\n", query);
            for (int i = 0; i < concept_count; i++) {
                if (concept_table[i].summary &&
                    strstr(concept_table[i].summary, query)) {
                    printf("[%02d] %-15s | Match found.\n",
                           ++found_count, concept_table[i].word);
                }
            }
            if (found_count == 0) printf("No local matches found.\n");
            continue;
        }

        /* ── 8. Facts — our new structured fact store ── */
        if (!strncmp(input, "facts ", 6)) {
            learn_print_facts(input+6); continue;
        }

        /* ── 9. Teach manually ── */
        if (!strncmp(input, "learn ", 6)) {
            LearnResult lr = learn_sentence(input+6, 0x00);
            printf("[LEARN] Subject:\"%s\" | Facts:%d | Words:%d\n",
                   lr.subject, lr.fact_count, lr.new_words_mapped);
            learn_save(NULL);
            continue;
        }

        /* ── 10. Co-occurrence — what words appear with this word ── */
        if (!strncmp(input, "related ", 8)) {
            char rel[5][COOC_WORD_LEN];
            int nr = context_get_related(input+8, 0xFF, rel, 5);
            printf("Related to \"%s\": ", input+8);
            for (int i = 0; i < nr; i++) printf("%s ", rel[i]);
            printf(nr ? "\n" : "(none yet)\n");
            continue;
        }

        /* ── 11. Brain — raw hash-based fact view ── */
        if (!strncmp(input, "brain ", 6)) {
            char *target = input + 6;
            uint32_t target_hash = hash_word(target);
            int found = 0;
            printf("\n--- BRAIN CONNECTIONS FOR: %s ---\n", target);
            for (int i = 0; i < fact_count; i++) {
                if (fact_pool[i].subject_hash == target_hash) {
                    const char *obj = get_word_from_hash(fact_pool[i].object_hash);
                    printf("  %s -> [%d] -> %s\n",
                           target, fact_pool[i].predicate,
                           obj ? obj : "???");
                    found = 1;
                }
            }
            if (!found) printf("  No atomic facts found. Try: facts %s\n", target);
            continue;
        }

        /* ── 12. Maintenance ── */
        if (!strcmp(input, "deduplicate")) {
            knowledge_save(NULL); continue;
        }
        if (!strcmp(input, "clear facts")) {
            fact_count = 0;
            printf("[CORE] Fact pool cleared.\n"); continue;
        }
        if (!strcmp(input, "clear summaries")) {
            for (int i = 0; i < concept_count; i++) {
                if (concept_table[i].summary) {
                    free(concept_table[i].summary);
                    concept_table[i].summary     = NULL;
                    concept_table[i].summary_len = 0;
                }
            }
            knowledge_save(NULL); continue;
        }
        if (!strncmp(input, "remove ", 7))                          continue;
        if (!strncmp(input,"purge_sector ",13)||!strncmp(input,"wipe ",5)) {
            uint8_t sec = (uint8_t)strtol(
                input + (input[0]=='p' ? 13 : 5), NULL, 16);
            (void)sec; /* keep your sector loop logic here */
            continue;
        }
        if (!strcmp(input, "purge_untrusted"))                      continue;

        /* ── 13. TTS ── */
        if (!strncmp(input,"speak ",6)) {
            tts_speak(input+6, Q_STATEMENT); continue;
        }

        /* ── Default: full query pipeline ── */
        reset_context();
        handle_query(input);
    }

    /* Final save on clean exit */
    knowledge_save(NULL);
    context_save();
    learn_save(NULL);
    vec_free();
    return 0;
}
