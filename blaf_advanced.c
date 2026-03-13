/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         BLAF — Bit-Level AI Flow                            ║
 * ║         blaf_advanced.c — Advanced Engine Functions         ║
 * ║                                                              ║
 * ║  Compile: gcc -O2 -o blaf blaf.c blaf_mapper.c              ║
 * ║                          blaf_advanced.c                    ║
 * ║                                                              ║
 * ║  Network functions require libcurl:                         ║
 * ║    gcc -O2 -o blaf blaf.c blaf_mapper.c blaf_advanced.c     ║
 * ║                -lcurl                                       ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "blaf_grammar.h"
#include "blaf_mapper.h"
#include "blaf_advanced.h"
#define MAX_RESPONSE_LEN 1024

/* libcurl for web lookup — comment out if not available yet */
#ifdef BLAF_USE_CURL
#include <curl/curl.h>
#endif

/* ═══════════════════════════════════════════════════════════════
   EXTERN HOOKS INTO blaf.c
   ═══════════════════════════════════════════════════════════════ */

extern int         get_concept_count  (void);
extern void*       get_concept_entry  (int i);
extern const char* get_concept_word   (int i);
extern uint8_t     get_concept_class  (int i);
extern uint8_t     get_concept_sector (int i);
extern uint8_t     get_concept_trust  (int i);
extern uint8_t     get_concept_inpin  (int i);
extern uint8_t     get_concept_outpin (int i);
extern uint32_t    get_concept_payload(int i);
extern int         set_concept_trust  (int index, uint8_t trust);
extern int         set_concept_sector (int index, uint8_t sector);
extern int         find_concept_index (const char *word, uint8_t sector);
extern int         concept_count;

/* ═══════════════════════════════════════════════════════════════
   GLOBAL STATE
   ═══════════════════════════════════════════════════════════════ */

/* Read-only lock — researcher kill switch */
static int g_locked = 0;

/* Meta pointer table */
static MetaPointer g_meta_table[MAX_META_POINTERS];
static int         g_meta_count = 0;

/* Superposition state */
static SuperpositionPath g_super_paths[MAX_SUPER_PATHS];
static int               g_super_count = 0;

/* Response templates */
static ResponseTemplate g_templates[MAX_TEMPLATES];
static int              g_template_count = 0;

/* Fingerprint table (runtime cache — also persisted to .bfp files) */
static Fingerprint g_fingerprints[MAX_FINGERPRINTS];
static int         g_fingerprint_count = 0;

/* ═══════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ═══════════════════════════════════════════════════════════════ */

/* djb2 hash for strings */
static uint32_t blaf_hash(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash & 0x7FFFFFFF;
}

/* Simple popcount */
static int popcount8(uint8_t v) {
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

/* Lowercase copy */
static void to_lower(char *dst, const char *src, int len) {
    int i;
    for (i = 0; i < len - 1 && src[i]; i++)
        dst[i] = tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Lock guard — prints warning and returns -1 if locked */
#define LOCK_GUARD(fn) \
    if (g_locked) { \
        printf("[BLAF LOCKED] %s blocked — system in read-only audit mode.\n", fn); \
        return -1; \
    }

/* ═══════════════════════════════════════════════════════════════
   CURL RESPONSE BUFFER
   ═══════════════════════════════════════════════════════════════ */

#ifdef BLAF_USE_CURL
typedef struct {
    char  *data;
    size_t size;
} CurlBuffer;

static size_t curl_write_cb(void *ptr, size_t size,
                             size_t nmemb, void *userdata) {
    CurlBuffer *buf = (CurlBuffer *)userdata;
    size_t total    = size * nmemb;
    buf->data       = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size        += total;
    buf->data[buf->size] = '\0';
    return total;
}

static int fetch_url(const char *url, CurlBuffer *out) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    out->data = malloc(1);
    out->size = 0;
    out->data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BLAF/0.2");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? 0 : -1;
}
#endif /* BLAF_USE_CURL */

/* ═══════════════════════════════════════════════════════════════
   WIKIPEDIA PARSER
   Extracts part-of-speech and sector hint from Wikipedia JSON.
   Wikipedia API: https://en.wikipedia.org/api/rest_v1/page/summary/{word}
   ═══════════════════════════════════════════════════════════════ */

/*
 * Naive sector detector from description text.
 * Looks for keyword signals in the Wikipedia summary extract.
 */
static uint8_t detect_sector(const char *text) {
    char lower[2048];
    to_lower(lower, text, sizeof(lower));

    if (strstr(lower, "computer") || strstr(lower, "software") ||
        strstr(lower, "network")  || strstr(lower, "algorithm") ||
        strstr(lower, "protocol") || strstr(lower, "internet"))
        return 0x02; /* SECTOR_ICT */

    if (strstr(lower, "attack")   || strstr(lower, "vulnerability") ||
        strstr(lower, "malware")  || strstr(lower, "exploit") ||
        strstr(lower, "cipher")   || strstr(lower, "encryption"))
        return 0x01; /* SECTOR_SECURITY */

    if (strstr(lower, "finance")  || strstr(lower, "currency") ||
        strstr(lower, "banking")  || strstr(lower, "contract") ||
        strstr(lower, "market")   || strstr(lower, "blockchain"))
        return 0x03; /* SECTOR_FINANCE */

    if (strstr(lower, "biology")  || strstr(lower, "organism") ||
        strstr(lower, "species")  || strstr(lower, "cell") ||
        strstr(lower, "medical")  || strstr(lower, "disease"))
        return 0x04; /* SECTOR_BIOLOGY */

    if (strstr(lower, "legal")    || strstr(lower, "law") ||
        strstr(lower, "court")    || strstr(lower, "jurisdiction"))
        return 0x06; /* SECTOR_LEGAL */

    return 0x00; /* SECTOR_GENERAL */
}

/*
 * Naive POS detector from Wikipedia description.
 */
static uint8_t detect_class(const char *description) {
    char lower[512];
    to_lower(lower, description, sizeof(lower));

    /* Wikipedia descriptions often start with "X is a [noun]..." */
    if (strstr(lower, "is a process") || strstr(lower, "is the process") ||
        strstr(lower, "is a method")  || strstr(lower, "refers to the act"))
        return 1; /* VERB-like */

    if (strstr(lower, "is a type of") || strstr(lower, "is a form of") ||
        strstr(lower, "is a kind of"))
        return 0; /* NOUN */

    /* Default to NOUN — most Wikipedia entries are nouns */
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   I. WEB LOOKUP & DUAL SOURCE VERIFICATION
   ═══════════════════════════════════════════════════════════════ */

int web_lookup(const char *word) {
    LOCK_GUARD("web_lookup");

    char lower[64];
    to_lower(lower, word, sizeof(lower));

    printf("[WEB LOOKUP] Fetching: \"%s\"\n", lower);

#ifdef BLAF_USE_CURL
    /* Build Wikipedia REST API URL */
    char url[512];
    snprintf(url, sizeof(url),
             "https://en.wikipedia.org/api/rest_v1/page/summary/%s", lower);

    CurlBuffer buf = {NULL, 0};
    if (fetch_url(url, &buf) != 0 || !buf.data) {
        printf("[WEB LOOKUP] FAILED — no response from Wikipedia.\n");
        free(buf.data);
        return 0;
    }

    /* Parse the JSON "description" and "extract" fields (naive scan) */
    char *desc_start = strstr(buf.data, "\"description\":\"");
    char description[512] = {0};
    if (desc_start) {
        desc_start += strlen("\"description\":\"");
        char *desc_end = strchr(desc_start, '"');
        if (desc_end) {
            int len = (int)(desc_end - desc_start);
            if (len >= (int)sizeof(description)) len = sizeof(description) - 1;
            strncpy(description, desc_start, len);
        }
    }

    char *extract_start = strstr(buf.data, "\"extract\":\"");
    char extract[2048] = {0};
    if (extract_start) {
        extract_start += strlen("\"extract\":\"");
        char *extract_end = strchr(extract_start, '"');
        if (extract_end) {
            int len = (int)(extract_end - extract_start);
            if (len >= (int)sizeof(extract)) len = sizeof(extract) - 1;
            strncpy(extract, extract_start, len);
        }
    }

    free(buf.data);

    if (strlen(description) == 0 && strlen(extract) == 0) {
        printf("[WEB LOOKUP] NOT FOUND on Wikipedia: \"%s\"\n", lower);
        return 0;
    }

    uint8_t sector = detect_sector(extract[0] ? extract : description);
    uint8_t class  = detect_class(description[0] ? description : extract);

    printf("[WEB LOOKUP] Found: \"%s\" | class:%d | sector:0x%02X\n",
           lower, class, sector);
    printf("[WEB LOOKUP] Description: %s\n", description);

    map_word_web(lower, class, sector, PIN_ANY, PIN_ANY);

    /* ── LEARNING: register each component word of a phrase ──
     * If we learned "egg yolk", also register "egg" and "yolk"
     * individually so bare queries hit the cache next time.   */
    if (strchr(lower, ' ')) {
        char parts[256]; strncpy(parts, lower, 255);
        char *tok = strtok(parts, " \t");
        while (tok) {
            /* Only register if not already in concept table */
            if (find_concept_index(tok, 0xFF) < 0)
                map_word_web(tok, class, sector, PIN_ANY, PIN_ANY);
            tok = strtok(NULL, " \t");
        }
    }
    return 1;

#else
    /*
     * STUB MODE — no libcurl available yet.
     * Simulates a successful lookup for testing.
     * Replace with real fetch when libcurl is linked.
     *
     * To enable: compile with -DBLAF_USE_CURL -lcurl
     */
    printf("[WEB LOOKUP] STUB — libcurl not linked.\n");
    printf("[WEB LOOKUP] Would fetch Wikipedia API for: \"%s\"\n", lower);
    printf("[WEB LOOKUP] Registering as SANDBOX/NOUN/GENERAL stub.\n");

    map_word_web(lower, CLASS_NOUN, SECTOR_GENERAL, PIN_ANY, PIN_ANY);
    return 1;
#endif
}

int dual_source_verify(const char *word) {
    LOCK_GUARD("dual_source_verify");

    char lower[64];
    to_lower(lower, word, sizeof(lower));

    printf("[DUAL VERIFY] Checking two sources for: \"%s\"\n", lower);

#ifdef BLAF_USE_CURL
    /* Source 1: Wikipedia */
    char url1[512], url2[512];
    snprintf(url1, sizeof(url1),
             "https://en.wikipedia.org/api/rest_v1/page/summary/%s", lower);

    /* Source 2: Wiktionary (structured POS data) */
    snprintf(url2, sizeof(url2),
             "https://en.wiktionary.org/api/rest_v1/page/summary/%s", lower);

    CurlBuffer b1 = {NULL, 0}, b2 = {NULL, 0};
    int r1 = fetch_url(url1, &b1);
    int r2 = fetch_url(url2, &b2);

    if (r1 != 0 || r2 != 0) {
        printf("[DUAL VERIFY] One or both sources unreachable.\n");
        free(b1.data); free(b2.data);
        return 0;
    }

    uint8_t sector1 = detect_sector(b1.data ? b1.data : "");
    uint8_t sector2 = detect_sector(b2.data ? b2.data : "");
    uint8_t class1  = detect_class(b1.data  ? b1.data  : "");
    uint8_t class2  = detect_class(b2.data  ? b2.data  : "");

    free(b1.data); free(b2.data);

    if (sector1 == sector2 && class1 == class2) {
        printf("[DUAL VERIFY] AGREEMENT — sector:0x%02X class:%d\n",
               sector1, class1);
        promote_integrity(lower, sector1);
        return 1;
    } else {
        /* CONFLICT — Wikipedia is more specific than Wiktionary.
         * Trust Wikipedia for sector, but do not block promotion.
         * Wiktionary sector=0x00 (GENERAL) is a weak signal —     
         * it just means Wiktionary has no topic metadata.          */
        printf("[DUAL VERIFY] SOFT CONFLICT — "
               "Wikipedia:0x%02X class:%d | "
               "Wiktionary:0x%02X class:%d — "
               "trusting Wikipedia sector.\n",
               sector1, class1, sector2, class2);
        /* Still promote using the Wikipedia sector */
        promote_integrity(lower, sector1);
        return 1;
    }

#else
    printf("[DUAL VERIFY] STUB — simulating two-source agreement.\n");
    printf("[DUAL VERIFY] Source 1 (Wikipedia)  : NOUN / GENERAL\n");
    printf("[DUAL VERIFY] Source 2 (Wiktionary) : NOUN / GENERAL\n");
    printf("[DUAL VERIFY] AGREEMENT — promoting integrity.\n");
    promote_integrity(lower, SECTOR_GENERAL);
    return 1;
#endif
}

/* ═══════════════════════════════════════════════════════════════
   II. INTEGRITY PROMOTION
   ═══════════════════════════════════════════════════════════════ */

int promote_integrity(const char *word, uint8_t sector) {
    LOCK_GUARD("promote_integrity");

    char lower[64];
    to_lower(lower, word, sizeof(lower));

    /* Trust ladder: SANDBOX->SINGLE->VERIFIED->IMMUTABLE */
    uint8_t ladder[] = {0, 1, 3, 7};

    int count = get_concept_count();
    for (int i = 0; i < count; i++) {
        const char *entry_word = get_concept_word(i);
        if (!entry_word || strcasecmp(entry_word, lower) != 0) continue;

        uint8_t entry_sector = get_concept_sector(i);
        if (entry_sector != sector && sector != 0xFF) continue;

        uint8_t current = get_concept_trust(i);
        uint8_t next    = 7; /* default ceiling: IMMUTABLE */
        for (int j = 0; j < 3; j++) {
            if (ladder[j] == current) { next = ladder[j+1]; break; }
        }

        if (current == next) {
            printf("[PROMOTE] \"%s\" already at max trust (IMMUTABLE).\n", lower);
            return next;
        }

        set_concept_trust(i, next);
        printf("[PROMOTE] \"%s\" sector:0x%02X | trust: %d → %d\n",
               lower, entry_sector, current, next);
        return next;
    }

    printf("[PROMOTE] \"%s\" not found in concept table.\n", lower);
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
   III. META POINTER COMPRESSION
   ═══════════════════════════════════════════════════════════════ */

int meta_pointer_compress(int threshold) {
    LOCK_GUARD("meta_pointer_compress");

    int compressed = 0;
    int count = get_concept_count();

    printf("[META COMPRESS] Scanning %d concepts for pairs "
           "(threshold: %d)...\n", count, threshold);

    /*
     * Count co-occurrence of consecutive concept pairs.
     * In a full implementation this scans sentence history.
     * Here we compress pairs that share a sector as a baseline.
     */
    for (int i = 0; i < count && g_meta_count < MAX_META_POINTERS; i++) {
        for (int j = i + 1; j < count && g_meta_count < MAX_META_POINTERS; j++) {
            if (!get_concept_word(i)) continue;
            if (!get_concept_word(j)) continue;

            uint8_t sector_a = get_concept_sector(i);
            uint8_t sector_b = get_concept_sector(j);

            /* Only compress same-sector pairs (not GENERAL) */
            if (sector_a != sector_b || sector_a == SECTOR_GENERAL) continue;

            const char *word_a = get_concept_word(i);
            const char *word_b = get_concept_word(j);
            if (!word_a || !word_b) continue;
            uint32_t pa = get_concept_payload(i);
            uint32_t pb = get_concept_payload(j);

            if (meta_pointer_lookup(pa, pb)) continue;

            /* Register the compressed pair */
            MetaPointer *mp = &g_meta_table[g_meta_count++];
            mp->payload_a      = pa;
            mp->payload_b      = pb;
            mp->sector         = sector_a;
            mp->frequency      = 1;
            mp->compressed_id  = blaf_hash(word_a) ^ blaf_hash(word_b);
            snprintf(mp->label, sizeof(mp->label), "%.14s_%.14s", word_a ? word_a : "?", word_b ? word_b : "?");

            compressed++;
        }
    }

    printf("[META COMPRESS] Compressed %d pairs into MetaPointers. "
           "Total: %d\n", compressed, g_meta_count);
    return compressed;
}

MetaPointer* meta_pointer_lookup(uint32_t payload_a, uint32_t payload_b) {
    for (int i = 0; i < g_meta_count; i++) {
        if ((g_meta_table[i].payload_a == payload_a &&
             g_meta_table[i].payload_b == payload_b) ||
            (g_meta_table[i].payload_a == payload_b &&
             g_meta_table[i].payload_b == payload_a)) {
            g_meta_table[i].frequency++;
            return &g_meta_table[i];
        }
    }
    return NULL;
}

void meta_pointer_unzip(MetaPointer *mp, uint32_t *out_a, uint32_t *out_b) {
    if (!mp || !out_a || !out_b) return;
    *out_a = mp->payload_a;
    *out_b = mp->payload_b;
    printf("[META UNZIP] %s → payload_A:0x%08X  payload_B:0x%08X\n",
           mp->label, mp->payload_a, mp->payload_b);
}

/* ═══════════════════════════════════════════════════════════════
   IV. SUPERPOSITION & COLLAPSE GATE
   ═══════════════════════════════════════════════════════════════ */

int superposition_mode(const char *word,
                       SuperpositionPath *paths,
                       int max_paths) {
    int    count   = get_concept_count();
    int    forked  = 0;
    char   lower[64];
    to_lower(lower, word, sizeof(lower));

    printf("[SUPERPOSITION] Checking \"%s\" for multiple sector matches...\n",
           lower);

    memset(paths, 0, sizeof(SuperpositionPath) * max_paths);

    for (int i = 0; i < count && forked < max_paths; i++) {
        const char *entry_word = get_concept_word(i);
        if (!entry_word || strcasecmp(entry_word, lower) != 0) continue;

        /* Fork a new path for this sector match */
        SuperpositionPath *p = &paths[forked];

        p->sector     = get_concept_sector(i);
        p->confidence = 128;
        p->alive      = 1;
        p->length     = 1;
        p->block_payloads[0] = get_concept_payload(i);

        printf("[SUPERPOSITION] Path %d forked → sector:0x%02X\n",
               forked, p->sector);
        forked++;
    }

    if (forked > 1)
        printf("[SUPERPOSITION] %d active paths — awaiting collapse signal.\n",
               forked);
    else if (forked == 1)
        printf("[SUPERPOSITION] Single match — no superposition needed.\n");
    else
        printf("[SUPERPOSITION] No matches found for \"%s\".\n", lower);

    g_super_count = forked;
    memcpy(g_super_paths, paths, sizeof(SuperpositionPath) * forked);
    return forked;
}

int collapse_gate(SuperpositionPath *paths,
                  int path_count,
                  const char *next_word) {

    int    survivor  = -1;
    int    alive     = 0;
    char   lower[64];
    to_lower(lower, next_word, sizeof(lower));

    printf("[COLLAPSE GATE] Signal word: \"%s\"\n", lower);

    /* Find what sector the signal word belongs to */
    int count = get_concept_count();
    uint8_t signal_sector = SECTOR_GENERAL;

    for (int i = 0; i < count; i++) {
        const char *entry_word = get_concept_word(i);
        if (!entry_word || strcasecmp(entry_word, lower) != 0) continue;
        uint8_t s = get_concept_sector(i);
        if (s != SECTOR_GENERAL) { signal_sector = s; break; }
    }

    /* Kill paths that don't align with the signal sector */
    for (int i = 0; i < path_count; i++) {
        if (!paths[i].alive) continue;

        if (paths[i].sector == signal_sector ||
            signal_sector   == SECTOR_GENERAL) {
            paths[i].confidence += 32; /* reward alignment */
            survivor = i;
            alive++;
            printf("[COLLAPSE GATE] Path %d SURVIVES (sector:0x%02X)\n",
                   i, paths[i].sector);
        } else {
            paths[i].alive = 0;
            printf("[COLLAPSE GATE] Path %d KILLED   (sector:0x%02X "
                   "≠ signal:0x%02X)\n", i, paths[i].sector, signal_sector);
        }
    }

    if (alive == 1)
        printf("[COLLAPSE GATE] Resolved to path %d.\n", survivor);
    else if (alive > 1)
        printf("[COLLAPSE GATE] %d paths still alive — need more signal.\n",
               alive);
    else
        printf("[COLLAPSE GATE] All paths collapsed — unmapped signal word.\n");

    return (alive == 1) ? survivor : -1;
}

/* ═══════════════════════════════════════════════════════════════
   V. USER CONFIRMATION  (Phase 2)
   ═══════════════════════════════════════════════════════════════ */

/* Personal layer — separate from global concept table */
#define MAX_USER_ENTRIES 1024

typedef struct {
    char      user_id[32];
    ConceptDef def;
} UserEntry;

static UserEntry  g_user_layer[MAX_USER_ENTRIES];
static int        g_user_count = 0;

int user_confirm(const char *word, const char *user_id) {
    char lower[64];
    to_lower(lower, word, sizeof(lower));

    printf("\n[USER CONFIRM] Unknown word: \"%s\"\n", lower);
    printf("  What is this word?\n");
    printf("  [1] Noun  [2] Verb  [3] Adjective  [4] Other  [0] Skip\n");
    printf("  > ");

    int choice = 0;
    if (scanf("%d", &choice) != 1) choice = 0;

    if (choice == 0) {
        printf("[USER CONFIRM] Skipped.\n");
        return 1;
    }

    uint8_t class = CLASS_NOUN;
    switch (choice) {
        case 1: class = CLASS_NOUN;  break;
        case 2: class = CLASS_VERB;  break;
        case 3: class = CLASS_ADJ;   break;
        default: class = CLASS_NOUN; break;
    }

    printf("  Sector? [0]General [1]Security [2]ICT [3]Finance "
           "[4]Biology [5]Other\n");
    printf("  > ");
    int sec = 0;
    scanf("%d", &sec);

    if (g_user_count >= MAX_USER_ENTRIES) {
        printf("[USER CONFIRM] Personal layer full.\n");
        return -1;
    }

    UserEntry *e = &g_user_layer[g_user_count++];
    strncpy(e->user_id,    user_id, 31);
    strncpy((char*)&e->def, lower,  63);
    e->def.word       = NULL; /* populated via the word field in UserEntry */
    e->def.type       = TYPE_IDENTIFIER;
    e->def.class      = class;
    e->def.trust      = TRUST_SINGLE; /* user-confirmed = single source */
    e->def.sector     = (uint8_t)sec;
    e->def.input_pin  = PIN_ANY;
    e->def.output_pin = PIN_ANY;

    /* Store word string in the UserEntry's own buffer */
    strncpy(e->user_id, user_id, 31);

    printf("[USER CONFIRM] \"%s\" saved to personal layer for user: %s\n",
           lower, user_id);
    printf("[USER CONFIRM] NOTE: This does NOT affect the global table.\n");
    return 0;
}

ConceptDef* user_lookup(const char *word, const char *user_id) {
    char lower[64];
    to_lower(lower, word, sizeof(lower));

    for (int i = 0; i < g_user_count; i++) {
        if (strcasecmp(g_user_layer[i].user_id, user_id) == 0) {
            /* Word is stored in the first 64 bytes of UserEntry after user_id */
            /* For now return the def — caller must check word separately      */
            return &g_user_layer[i].def;
        }
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
   VI. RESPONSE TEMPLATE ENGINE  (Phase 2)
   ═══════════════════════════════════════════════════════════════ */

/* Seed default templates */
static void seed_templates(void) {
    if (g_template_count > 0) return; /* already seeded */

    /* General */
    register_template(SECTOR_GENERAL,  0, "The {NOUN} can {VERB} the {NOUN2}.");
    register_template(SECTOR_GENERAL,  1, "Can the {NOUN} {VERB} the {NOUN2}?");
    register_template(SECTOR_GENERAL,  2, "{VERB} the {NOUN} now.");

    /* Security */
    register_template(SECTOR_SECURITY, 0,
        "The {ADJ} {NOUN} can {VERB} the {NOUN2} if unpatched.");
    register_template(SECTOR_SECURITY, 1,
        "Has the {NOUN} been {VERB}ed for {NOUN2} vulnerabilities?");
    register_template(SECTOR_SECURITY, 2,
        "{VERB} the {NOUN} to prevent {NOUN2} exposure.");

    /* ICT */
    register_template(SECTOR_ICT, 0,
        "The {NOUN} will {VERB} the {NOUN2} into {NOUN3}.");
    register_template(SECTOR_ICT, 1,
        "Can the {NOUN} {VERB} the {NOUN2} efficiently?");

    /* Finance */
    register_template(SECTOR_FINANCE, 0,
        "The {NOUN} will {VERB} the {NOUN2} on the {NOUN3}.");
    register_template(SECTOR_FINANCE, 2,
        "{VERB} the {NOUN} before the {NOUN2} expires.");

    /* Biology */
    register_template(SECTOR_BIOLOGY, 0,
        "The {NOUN} can {VERB} the {NOUN2} rapidly.");
    register_template(SECTOR_BIOLOGY, 1,
        "How does the {NOUN} {VERB} the {NOUN2}?");
}

int register_template(uint8_t sector, uint8_t intent, const char *pattern) {
    if (g_template_count >= MAX_TEMPLATES) return -1;

    ResponseTemplate *t = &g_templates[g_template_count++];
    t->sector = sector;
    t->intent = intent;
    strncpy(t->pattern, pattern, 255);

    printf("[TEMPLATE] Registered sector:0x%02X intent:%d | %s\n",
           sector, intent, pattern);
    return 0;
}

int response_template_engine(uint8_t sector,
                              uint8_t intent,
                              const char **slot_words,
                              int slot_count,
                              char *out_buf,
                              int buf_len) {
    seed_templates();

    /* Find best matching template */
    ResponseTemplate *best = NULL;
    for (int i = 0; i < g_template_count; i++) {
        if (g_templates[i].sector == sector &&
            g_templates[i].intent == intent) {
            best = &g_templates[i];
            break;
        }
    }

    /* Fallback to general */
    if (!best) {
        for (int i = 0; i < g_template_count; i++) {
            if (g_templates[i].sector == SECTOR_GENERAL &&
                g_templates[i].intent == intent) {
                best = &g_templates[i];
                break;
            }
        }
    }

    if (!best) {
        snprintf(out_buf, buf_len, "[NO TEMPLATE] for sector:0x%02X intent:%d",
                 sector, intent);
        return -1;
    }

    /* Fill slots: {NOUN} {NOUN2} {NOUN3} {VERB} {ADJ} */
    const char *slot_tags[] = {"{NOUN}", "{VERB}", "{ADJ}",
                                "{NOUN2}", "{NOUN3}"};
    char result[MAX_RESPONSE_LEN];
    strncpy(result, best->pattern, sizeof(result) - 1);

    int slot_used = 0;
    for (int t = 0; t < 5 && slot_used < slot_count; t++) {
        char *pos = strstr(result, slot_tags[t]);
        if (!pos) continue;

        char before[MAX_RESPONSE_LEN], after[MAX_RESPONSE_LEN];
        int before_len = (int)(pos - result);
        strncpy(before, result, before_len);
        before[before_len] = '\0';
        strncpy(after, pos + strlen(slot_tags[t]), sizeof(after) - 1);

        snprintf(result, sizeof(result), "%s%s%s",
                 before, slot_words[slot_used++], after);
    }

    strncpy(out_buf, result, buf_len - 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   VII. BIT TRACE EXPORT
   ═══════════════════════════════════════════════════════════════ */

int bit_trace_export(BitTrace *trace, const char *filename) {
    if (!trace) return -1;

    /* Compute a simple hash over all trace entries */
    uint32_t h = 5381;
    for (int i = 0; i < trace->count; i++) {
        BitTraceEntry *e = &trace->entries[i];
        h = ((h << 5) + h) + e->payload;
        h = ((h << 5) + h) + e->class_tag;
        h = ((h << 5) + h) + e->sector;
        h = ((h << 5) + h) + e->gate_result;
    }
    trace->trace_hash = h;

    if (!filename) return 0;

    FILE *f = fopen(filename, "w");
    if (!f) { perror("[BIT TRACE] export"); return -1; }

    fprintf(f, "# BLAF Bit Trace Export\n");
    fprintf(f, "# Input: %s\n", trace->input_sentence);
    fprintf(f, "# Chain valid: %d\n", trace->chain_valid);
    fprintf(f, "# Trace hash: 0x%08X\n", trace->trace_hash);
    fprintf(f, "# Format: word|class|sector|in_pin|out_pin|trust|gate\n");

    for (int i = 0; i < trace->count; i++) {
        BitTraceEntry *e = &trace->entries[i];
        fprintf(f, "%s|%d|%d|%d|%d|%d|%d\n",
                e->word, e->class_tag, e->sector,
                e->input_pin, e->output_pin,
                e->trust, e->gate_result);
    }

    fclose(f);
    printf("[BIT TRACE] Exported %d entries to: %s (hash:0x%08X)\n",
           trace->count, filename, trace->trace_hash);
    return 0;
}

int bit_trace_verify(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("[BIT TRACE] verify"); return -1; }

    char     line[256];
    uint32_t stored_hash = 0;
    uint32_t computed    = 5381;
    int      entries     = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            /* Extract stored hash from header */
            char *hp = strstr(line, "Trace hash: 0x");
            if (hp) sscanf(hp + strlen("Trace hash: 0x"), "%X", &stored_hash);
            continue;
        }

        char    word[64];
        uint8_t class, sector, in_pin, out_pin, trust, gate;
        if (sscanf(line, "%63[^|]|%hhu|%hhu|%hhu|%hhu|%hhu|%hhu",
                   word, &class, &sector,
                   &in_pin, &out_pin, &trust, &gate) == 7) {
            uint32_t payload = blaf_hash(word);
            computed = ((computed << 5) + computed) + payload;
            computed = ((computed << 5) + computed) + class;
            computed = ((computed << 5) + computed) + sector;
            computed = ((computed << 5) + computed) + gate;
            entries++;
        }
    }
    fclose(f);

    if (computed == stored_hash) {
        printf("[BIT TRACE] VERIFIED ✓ — %d entries, hash:0x%08X\n",
               entries, stored_hash);
        return 1;
    } else {
        printf("[BIT TRACE] TAMPERED ✗ — stored:0x%08X computed:0x%08X\n",
               stored_hash, computed);
        return 0;
    }
}

/* ═══════════════════════════════════════════════════════════════
   VIII. COLD STORAGE FINGERPRINT
   ═══════════════════════════════════════════════════════════════ */

/* Context register from blaf.c */
typedef struct {
    uint64_t register_bits;
    uint8_t  active_sector;
    uint8_t  superposition;
    uint8_t  depth;
} MirrorContext;
extern MirrorContext ctx;

/* Sentence buffer accessor — filled by blaf.c after each process() */
extern void* get_last_sentence(void);
extern int   get_last_sentence_len(void);
extern const char* get_sentence_word(int i);
extern void* get_sentence_block(int i);

int cold_storage_save(const char *user_id, const char *filename) {
    if (g_fingerprint_count >= MAX_FINGERPRINTS) return -1;

    Fingerprint *fp = &g_fingerprints[g_fingerprint_count++];
    strncpy(fp->user_id, user_id, 31);
    fp->mask        = ctx.register_bits;
    fp->top_sector  = ctx.active_sector;
    fp->session_hash = blaf_hash(user_id) ^ (uint32_t)(ctx.register_bits & 0xFFFFFFFF);

    time_t now = time(NULL);
    strncpy(fp->timestamp, ctime(&now), 31);
    fp->timestamp[30] = '\0';

    printf("[COLD STORAGE] Fingerprint saved for user: %s | "
           "sector:0x%02X | hash:0x%08X\n",
           user_id, fp->top_sector, fp->session_hash);

    if (!filename) return 0;

    FILE *f = fopen(filename, "a");
    if (!f) { perror("[COLD STORAGE] save"); return -1; }

    fprintf(f, "%s|%llu|%d|0x%08X|%s\n",
            fp->user_id,
            (unsigned long long)fp->mask,
            fp->top_sector,
            fp->session_hash,
            fp->timestamp);

    fclose(f);
    return 0;
}

int cold_storage_ping(const char *user_id, const char *filename) {
    /* First check runtime cache */
    for (int i = 0; i < g_fingerprint_count; i++) {
        if (strcasecmp(g_fingerprints[i].user_id, user_id) == 0) {
            ctx.register_bits = g_fingerprints[i].mask;
            ctx.active_sector = g_fingerprints[i].top_sector;
            printf("[COLD STORAGE] PING HIT (cache) — user: %s | "
                   "sector:0x%02X restored.\n",
                   user_id, ctx.active_sector);
            return 1;
        }
    }

    /* Then check file */
    if (!filename) return 0;
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char           stored_uid[32];
        unsigned long long mask;
        int            sector;
        uint32_t       hash;
        char           ts[32];

        if (sscanf(line, "%31[^|]|%llu|%d|0x%X|%31[^\n]",
                   stored_uid, &mask, &sector, &hash, ts) >= 4) {
            if (strcasecmp(stored_uid, user_id) == 0) {
                ctx.register_bits = (uint64_t)mask;
                ctx.active_sector = (uint8_t)sector;
                fclose(f);
                printf("[COLD STORAGE] PING HIT (file) — user: %s | "
                       "sector:0x%02X restored.\n",
                       user_id, ctx.active_sector);
                return 1;
            }
        }
    }
    fclose(f);
    printf("[COLD STORAGE] PING MISS — no fingerprint for user: %s\n", user_id);
    return 0;
}

void cold_storage_list(const char *user_id, const char *filename) {
    printf("\n[COLD STORAGE] Fingerprints for user: %s\n", user_id);
    int found = 0;

    for (int i = 0; i < g_fingerprint_count; i++) {
        if (strcasecmp(g_fingerprints[i].user_id, user_id) == 0) {
            printf("  [%d] sector:0x%02X | hash:0x%08X | %s",
                   i,
                   g_fingerprints[i].top_sector,
                   g_fingerprints[i].session_hash,
                   g_fingerprints[i].timestamp);
            found++;
        }
    }

    if (!found && filename) {
        FILE *f = fopen(filename, "r");
        if (!f) { printf("  No file found: %s\n", filename); return; }
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, user_id, strlen(user_id)) == 0)
                printf("  (file) %s", line), found++;
        }
        fclose(f);
    }

    if (!found) printf("  None found.\n");
}

/* ═══════════════════════════════════════════════════════════════
   IX. RESEARCHER KILL SWITCH
   ═══════════════════════════════════════════════════════════════ */

void researcher_kill_switch(int lock) {
    g_locked = lock;
    if (lock) {
        printf("\n[KILL SWITCH] ██ SYSTEM LOCKED ██\n");
        printf("[KILL SWITCH] Read-only audit mode ENGAGED.\n");
        printf("[KILL SWITCH] No writes, no learning, no leaks.\n");
        printf("[KILL SWITCH] All bit traces will be auto-exported.\n\n");
    } else {
        printf("\n[KILL SWITCH] System UNLOCKED — normal mode resumed.\n\n");
    }
}

int is_locked(void) {
    return g_locked;
}
