/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_llm.c                                          ║
 * ║  LLM Feed & Mapping Endpoints                               ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Enable real LLM calls: compile with -DBLAF_USE_CURL -lcurl
 *  and call llm_init() with your API key before use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "blaf_llm.h"
#include "blaf_grammar.h"
#include "blaf_mapper.h"
#include "blaf_sectors.h"

#ifdef BLAF_USE_CURL
#include <curl/curl.h>
typedef struct { char *data; size_t size; } CurlBuf;
static size_t curl_cb(void *ptr, size_t sz, size_t n, void *ud) {
    CurlBuf *b = (CurlBuf*)ud;
    b->data = realloc(b->data, b->size + sz*n + 1);
    memcpy(b->data + b->size, ptr, sz*n);
    b->size += sz*n;
    b->data[b->size] = '\0';
    return sz*n;
}
#endif

/* ═══════════════════════════════════════════════════════════════
   STATE
   ═══════════════════════════════════════════════════════════════ */

static LLMConfig g_cfg = {LLM_NONE, {0}, {0}, {0}, 512, 0.0f};
static int       g_init = 0;

/* ═══════════════════════════════════════════════════════════════
   PROVIDER ENDPOINTS
   ═══════════════════════════════════════════════════════════════ */

/* Gemini needs model + action + key baked into the URL */
static char g_gemini_url[512];

static const char* provider_url(LLMProvider p) {
    switch (p) {
        case LLM_OPENAI:    return "https://api.openai.com/v1/chat/completions";
        case LLM_ANTHROPIC: return "https://api.anthropic.com/v1/messages";
        case LLM_GOOGLE:
            /* Build: .../models/{model}:generateContent?key={api_key} */
            snprintf(g_gemini_url, sizeof(g_gemini_url),
                "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
                g_cfg.model, g_cfg.api_key);
            return g_gemini_url;
        case LLM_MISTRAL:   return "https://api.mistral.ai/v1/chat/completions";
        case LLM_LOCAL:     return g_cfg.endpoint[0] ? g_cfg.endpoint
                                  : "http://localhost:11434/v1/chat/completions";
        default:            return "";
    }
}

static const char* provider_name(LLMProvider p) {
    switch (p) {
        case LLM_OPENAI:    return "OpenAI";
        case LLM_ANTHROPIC: return "Anthropic";
        case LLM_GOOGLE:    return "Google";
        case LLM_MISTRAL:   return "Mistral";
        case LLM_LOCAL:     return "Local";
        default:            return "None";
    }
}

/* ═══════════════════════════════════════════════════════════════
   INIT
   ═══════════════════════════════════════════════════════════════ */

void llm_init(LLMProvider provider, const char *api_key, const char *model) {
    g_cfg.provider    = provider;
    g_cfg.max_tokens  = 512;
    g_cfg.temperature = 0.0f;
    if (api_key) strncpy(g_cfg.api_key, api_key, 255);
    if (model)   strncpy(g_cfg.model,   model,    63);

    /* Default models per provider */
    if (!model || !model[0]) {
        switch (provider) {
            case LLM_OPENAI:    strncpy(g_cfg.model, "gpt-4o",                   63); break;
            case LLM_ANTHROPIC: strncpy(g_cfg.model, "claude-sonnet-4-6",        63); break;
            case LLM_GOOGLE:    strncpy(g_cfg.model, "gemini-1.5-pro",           63); break;
            case LLM_MISTRAL:   strncpy(g_cfg.model, "mistral-large-latest",     63); break;
            default: break;
        }
    }
    g_init = 1;
    printf("[LLM] Configured: %s / %s\n", provider_name(provider), g_cfg.model);
}

void llm_set_endpoint(const char *url) {
    strncpy(g_cfg.endpoint, url, 255);
    printf("[LLM] Custom endpoint: %s\n", url);
}

void llm_status(void) {
    printf("\n[LLM STATUS]\n");
    printf("  Provider : %s\n", provider_name(g_cfg.provider));
    printf("  Model    : %s\n", g_cfg.model[0] ? g_cfg.model : "(none)");
    printf("  Endpoint : %s\n", provider_url(g_cfg.provider));
    printf("  API Key  : %s\n", g_cfg.api_key[0] ? "SET" : "NOT SET");
    printf("  Max Tokens: %d\n", g_cfg.max_tokens);
    printf("  Curl     : %s\n",
#ifdef BLAF_USE_CURL
    "enabled"
#else
    "STUB (compile with -DBLAF_USE_CURL -lcurl)"
#endif
    );
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════
   BUILD JSON PAYLOADS
   ═══════════════════════════════════════════════════════════════ */

static void build_openai_payload(const char *prompt, char *buf, int len) {
    snprintf(buf, len,
        "{"
        "\"model\":\"%s\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":%d,"
        "\"temperature\":%.1f"
        "}",
        g_cfg.model, prompt, g_cfg.max_tokens, g_cfg.temperature);
}

static void build_anthropic_payload(const char *prompt, char *buf, int len) {
    snprintf(buf, len,
        "{"
        "\"model\":\"%s\","
        "\"max_tokens\":%d,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]"
        "}",
        g_cfg.model, g_cfg.max_tokens, prompt);
}

/* Gemini payload — completely different structure from OpenAI */
static void build_gemini_payload(const char *prompt, char *buf, int len) {
    snprintf(buf, len,
        "{"
        "\"contents\":[{\"parts\":[{\"text\":\"%s\"}]}],"
        "\"generationConfig\": {"
        "  \"maxOutputTokens\":%d,"
        "  \"temperature\":%.1f"
        "}"
        "}",
        prompt, g_cfg.max_tokens, g_cfg.temperature);
}

/* ═══════════════════════════════════════════════════════════════
   PARSE RESPONSE TEXT
   Extracts the "content" text from various LLM JSON responses
   ═══════════════════════════════════════════════════════════════ */

static void parse_llm_response(const char *json, char *out, int outlen) {
    out[0] = '\0';

    /* OpenAI / Mistral: "content":"..." */
    const char *tag = "\"content\":\"";
    char *pos = strstr(json, tag);
    if (pos) {
        pos += strlen(tag);
        char *end = pos;
        /* handle escaped quotes */
        while (*end && !(*end == '"' && *(end-1) != '\\')) end++;
        int n = (int)(end - pos);
        if (n >= outlen) n = outlen - 1;
        strncpy(out, pos, n);
        out[n] = '\0';
        /* unescape \\n */
        char *p = out;
        while ((p = strstr(p, "\\n")) != NULL) { *p = ' '; *(p+1) = ' '; }
        return;
    }

    /* Anthropic: "text":"..." */
    tag = "\"text\":\"";
    pos = strstr(json, tag);
    if (pos) {
        pos += strlen(tag);
        char *end = strchr(pos, '"');
        if (end) {
            int n = (int)(end - pos);
            if (n >= outlen) n = outlen - 1;
            strncpy(out, pos, n);
            out[n] = '\0';
        }
        return;
    }

    /* Gemini: candidates[0].content.parts[0].text */
    tag = "\"text\":\"";   /* also matches Gemini */
    pos = strstr(json, "candidates");
    if (pos) {
        pos = strstr(pos, "\"text\":\"");
        if (pos) {
            pos += strlen("\"text\":\"");
            char *end = pos;
            while (*end && !(*end == '"' && *(end-1) != '\\')) end++;
            int n = (int)(end - pos);
            if (n >= outlen) n = outlen - 1;
            strncpy(out, pos, n);
            out[n] = '\0';
            /* unescape \n */
            char *p = out;
            while ((p = strstr(p, "\\n"))) { *p=' '; *(p+1)=' '; }
            return;
        }
    }

    /* Fallback: just copy the raw response */
    strncpy(out, json, outlen - 1);
}

/* ═══════════════════════════════════════════════════════════════
   CORE LLM CALL
   ═══════════════════════════════════════════════════════════════ */

LLMResponse llm_query(const char *prompt) {
    LLMResponse resp = {{0}, 0, 0, {0}};

    if (!g_init || g_cfg.provider == LLM_NONE) {
        snprintf(resp.error, 255,
                 "LLM not configured. Call llm_init() first.");
        return resp;
    }

#ifdef BLAF_USE_CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(resp.error, 255, "curl_easy_init failed");
        return resp;
    }

    char payload[MAX_LLM_RESPONSE * 2];
    char safe_prompt[MAX_LLM_RESPONSE];

    /* Escape quotes in prompt */
    int j = 0;
    for (int i = 0; prompt[i] && j < (int)sizeof(safe_prompt)-2; i++) {
        if (prompt[i] == '"') safe_prompt[j++] = '\\';
        safe_prompt[j++] = prompt[i];
    }
    safe_prompt[j] = '\0';

    switch (g_cfg.provider) {
        case LLM_OPENAI:
        case LLM_MISTRAL:
        case LLM_LOCAL:
            build_openai_payload(safe_prompt, payload, sizeof(payload));
            break;
        case LLM_ANTHROPIC:
            build_anthropic_payload(safe_prompt, payload, sizeof(payload));
            break;
        case LLM_GOOGLE:
            build_gemini_payload(safe_prompt, payload, sizeof(payload));
            break;
        default:
            build_openai_payload(safe_prompt, payload, sizeof(payload));
    }

    CurlBuf buf = {malloc(1), 0};
    buf.data[0] = '\0';

    struct curl_slist *headers = NULL;
    char auth_header[320];

    if (g_cfg.provider == LLM_ANTHROPIC) {
        snprintf(auth_header, sizeof(auth_header),
                 "x-api-key: %s", g_cfg.api_key);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else if (g_cfg.provider == LLM_GOOGLE) {
        /* Gemini: API key is in the URL — no Authorization header needed */
        (void)auth_header;
    } else {
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Bearer %s", g_cfg.api_key);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            provider_url(g_cfg.provider));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        parse_llm_response(buf.data, resp.text, MAX_LLM_RESPONSE);
        resp.success = 1;
    } else {
        snprintf(resp.error, 255, "curl error: %s", curl_easy_strerror(res));
    }
    free(buf.data);

#else
    /* STUB MODE */
    printf("[LLM STUB] Would send to %s: \"%s\"\n",
           provider_name(g_cfg.provider), prompt);
    snprintf(resp.text, MAX_LLM_RESPONSE,
        "[LLM STUB] Response to: %s\n"
        "Compile with -DBLAF_USE_CURL -lcurl and set API key to enable real calls.",
        prompt);
    resp.success = 1;
#endif

    return resp;
}

/* ═══════════════════════════════════════════════════════════════
   CONCEPT MAPPING VIA LLM
   ═══════════════════════════════════════════════════════════════ */

int llm_map_concept(const char *word) {
    char prompt[512];
    snprintf(prompt, sizeof(prompt),
        "Classify the word \"%s\" for a bit-level AI system. "
        "Reply with ONLY this JSON, no other text:\n"
        "{\"class\":\"NOUN|VERB|ADJ|ADV|PREP\","
        "\"sector\":\"GENERAL|SECURITY|ICT|FINANCE|BIOLOGY|LEGAL|MEDICAL|EDUCATION\","
        "\"trust\":\"VERIFIED\","
        "\"definition\":\"one sentence definition\"}", word);

    LLMResponse r = llm_query(prompt);
    if (!r.success || !r.text[0]) {
        printf("[LLM MAP] Failed to classify \"%s\"\n", word);
        return -1;
    }

    printf("[LLM MAP] Response for \"%s\": %s\n", word, r.text);

    /* Parse JSON fields */
    uint8_t class  = CLASS_NOUN;
    uint8_t sector = SECTOR_GENERAL;
    uint8_t trust  = TRUST_VERIFIED_SOURCES;

    char *cp = strstr(r.text, "\"class\":\"");
    if (cp) {
        cp += strlen("\"class\":\"");
        if      (strncmp(cp, "VERB", 4) == 0) class = CLASS_VERB;
        else if (strncmp(cp, "ADJ",  3) == 0) class = CLASS_ADJ;
        else if (strncmp(cp, "ADV",  3) == 0) class = CLASS_ADV;
        else if (strncmp(cp, "PREP", 4) == 0) class = CLASS_PREP;
        else                                   class = CLASS_NOUN;
    }

    char *sp = strstr(r.text, "\"sector\":\"");
    if (sp) {
        sp += strlen("\"sector\":\"");
        if      (strncmp(sp, "SECURITY",  8) == 0) sector = SECTOR_SECURITY;
        else if (strncmp(sp, "ICT",       3) == 0) sector = SECTOR_ICT;
        else if (strncmp(sp, "FINANCE",   7) == 0) sector = SECTOR_FINANCE;
        else if (strncmp(sp, "BIOLOGY",   7) == 0) sector = SECTOR_BIOLOGY;
        else if (strncmp(sp, "LEGAL",     5) == 0) sector = SECTOR_LEGAL;
        else if (strncmp(sp, "MEDICAL",   7) == 0) sector = SECTOR_MEDICAL;
        else if (strncmp(sp, "EDUCATION", 9) == 0) sector = SECTOR_EDUCATION;
        else {
            /* custom sector — parse name and register it */
            char sname[32] = {0};
            int i = 0;
            while (*sp && *sp != '"' && i < 31) sname[i++] = *sp++;
            if (sname[0]) sector = sector_id(sname);
        }
    }

    ConceptDef def = {word, TYPE_IDENTIFIER, class, trust,
                      sector, PIN_ANY, PIN_ANY};
    return map_word(&def);
}

int llm_map_batch(const char **words, int count) {
    int mapped = 0;
    printf("[LLM MAP BATCH] Classifying %d words...\n", count);
    for (int i = 0; i < count; i++) {
        if (llm_map_concept(words[i]) == 0) mapped++;
    }
    printf("[LLM MAP BATCH] %d / %d mapped.\n", mapped, count);
    return mapped;
}

/* ═══════════════════════════════════════════════════════════════
   QUESTION ANSWERING VIA LLM
   ═══════════════════════════════════════════════════════════════ */

LLMResponse llm_answer_question(const char *question) {
    char prompt[1024];
    snprintf(prompt, sizeof(prompt),
        "Answer this question clearly and factually in 2-4 sentences: %s",
        question);

    printf("[LLM] Querying %s for: \"%s\"\n",
           provider_name(g_cfg.provider), question);

    LLMResponse r = llm_query(prompt);
    if (r.success)
        printf("[LLM] Answer received (%d chars).\n", (int)strlen(r.text));
    else
        printf("[LLM] Error: %s\n", r.error);

    return r;
}

/* ═══════════════════════════════════════════════════════════════
   FACT EXTRACTION
   ═══════════════════════════════════════════════════════════════ */

int llm_extract_facts(const char *subject, const char *text) {
    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "From this text about \"%s\", extract 5 key facts as simple "
        "subject-verb-object triplets. Format: one per line as "
        "SUBJECT|VERB|OBJECT. Text: %.800s",
        subject, text);

    LLMResponse r = llm_query(prompt);
    if (!r.success) return -1;

    int mapped = 0;
    char *line = strtok(r.text, "\n");
    while (line) {
        char subj[64], verb[64], obj[64];
        if (sscanf(line, "%63[^|]|%63[^|]|%63s", subj, verb, obj) == 3) {
            /* Map verb and object as concepts if not already mapped */
            char lv[64], lo[64];
            int i = 0;
            for (; verb[i] && i < 63; i++) lv[i] = tolower((unsigned char)verb[i]);
            lv[i] = '\0';
            for (i = 0; obj[i] && i < 63; i++) lo[i] = tolower((unsigned char)obj[i]);
            lo[i] = '\0';

            ConceptDef dv = {lv, TYPE_IDENTIFIER, CLASS_VERB, TRUST_VERIFIED_SOURCES,
                             SECTOR_GENERAL, PIN_ANY, PIN_ANY};
            ConceptDef dn = {lo, TYPE_IDENTIFIER, CLASS_NOUN, TRUST_VERIFIED_SOURCES,
                             SECTOR_GENERAL, PIN_ANY, PIN_ANY};
            map_word(&dv);
            map_word(&dn);
            mapped++;
        }
        line = strtok(NULL, "\n");
    }

    printf("[LLM FACTS] Extracted and mapped %d facts about \"%s\".\n",
           mapped, subject);
    return mapped;
}
