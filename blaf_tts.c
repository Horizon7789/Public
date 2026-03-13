/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_tts.c                                          ║
 * ║  Text-To-Speech Engine                                      ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

#include "blaf_tts.h"

/* ═══════════════════════════════════════════════════════════════
   PHONEME NAME TABLE  (ARPAbet strings)
   ═══════════════════════════════════════════════════════════════ */

static const char* ph_names[PH_COUNT] = {
    "AA","AE","AH","AO","AW","AY","EH","ER","EY",
    "IH","IY","OW","OY","UH","UW",
    "B","CH","D","DH","F","G","HH","JH","K","L",
    "M","N","NG","P","R","S","SH","T","TH","V",
    "W","Y","Z","ZH","SIL"
};

const char* tts_phoneme_name(Phoneme p) {
    if (p < 0 || p >= PH_COUNT) return "?";
    return ph_names[p];
}

static Phoneme ph_from_name(const char *name) {
    for (int i = 0; i < PH_COUNT; i++)
        if (strcasecmp(ph_names[i], name) == 0) return (Phoneme)i;
    return PH_SIL;
}

/* ═══════════════════════════════════════════════════════════════
   CMU DICTIONARY
   Hash table: word → phoneme sequence
   ═══════════════════════════════════════════════════════════════ */

#define CMU_HASH_SIZE  65536
#define CMU_MAX_WORDS  150000

typedef struct CMUEntry {
    char           word[48];
    PhonemeSeq     seq;
    struct CMUEntry *next;  /* chaining for collisions */
} CMUEntry;

static CMUEntry  *cmu_pool  = NULL;
static CMUEntry **cmu_table = NULL;
static int        cmu_count = 0;
static int        cmu_loaded = 0;

static uint32_t cmu_hash(const char *s) {
    uint32_t h = 5381;
    while (*s) h = ((h<<5)+h) + (unsigned char)tolower(*s++);
    return h & (CMU_HASH_SIZE-1);
}

static void cmu_insert(const char *word, const PhonemeSeq *seq) {
    if (cmu_count >= CMU_MAX_WORDS) return;
    CMUEntry *e = &cmu_pool[cmu_count++];
    strncpy(e->word, word, 47);
    e->seq  = *seq;
    e->next = NULL;
    uint32_t slot = cmu_hash(word);
    e->next = cmu_table[slot];
    cmu_table[slot] = e;
}

static const PhonemeSeq* cmu_lookup(const char *word) {
    if (!cmu_table) return NULL;
    char l[48]; int i=0;
    while(word[i]&&i<47){l[i]=tolower((unsigned char)word[i]);i++;}l[i]='\0';
    uint32_t slot = cmu_hash(l);
    for (CMUEntry *e = cmu_table[slot]; e; e = e->next)
        if (strcmp(e->word, l) == 0) return &e->seq;
    return NULL;
}

int tts_load_cmu_dict(const char *dict_path) {
    if (!cmu_pool) {
        cmu_pool  = calloc(CMU_MAX_WORDS, sizeof(CMUEntry));
        cmu_table = calloc(CMU_HASH_SIZE, sizeof(CMUEntry*));
        if (!cmu_pool || !cmu_table) {
            fprintf(stderr, "[TTS] Out of memory for CMU dict.\n");
            return -1;
        }
    }

    FILE *f = fopen(dict_path, "r");
    if (!f) {
        printf("[TTS] CMU dict not found: %s\n", dict_path);
        printf("[TTS] Download from http://www.speech.cs.cmu.edu/cgi-bin/cmudict\n");
        printf("[TTS] G2P rule fallback will be used instead.\n");
        return 0;
    }

    char line[256];
    int  loaded = 0;
    while (fgets(line, sizeof(line), f) && loaded < CMU_MAX_WORDS) {
        /* Skip comments */
        if (line[0] == ';' || line[0] == '\n') continue;

        /* Parse: WORD  PH1 PH2 PH3... */
        char *p = strtok(line, " \t");
        if (!p) continue;
        char word[48]; int j=0;
        while(p[j]&&j<47){word[j]=tolower((unsigned char)p[j]);j++;}
        word[j]='\0';
        /* Strip alt-pronunciation suffix: "word(2)" → "word" */
        char *paren = strchr(word, '(');
        if (paren) *paren = '\0';

        PhonemeSeq seq = {{0},{0},0,{0}};
        strncpy(seq.word, word, 31);

        p = strtok(NULL, " \t");
        while (p && seq.count < MAX_PHONEMES_PER_WORD) {
            /* ARPAbet has stress digits: AH0 AH1 AH2 */
            char ph_name[8] = {0};
            uint8_t stress = STRESS_NONE;
            int k = 0;
            while (p[k] && !isdigit((unsigned char)p[k]) && k < 7)
                ph_name[k] = p[k]; k++;
            ph_name[k] = '\0';
            if (isdigit((unsigned char)p[k]))
                stress = p[k] - '0'; /* 0=none, 1=primary, 2=secondary */
            seq.ph[seq.count]     = ph_from_name(ph_name);
            seq.stress[seq.count] = stress;
            seq.count++;
            p = strtok(NULL, " \t\n\r");
        }

        if (seq.count > 0) {
            cmu_insert(word, &seq);
            loaded++;
        }
    }
    fclose(f);
    cmu_loaded = loaded;
    printf("[TTS] CMU dict loaded: %d entries.\n", loaded);
    return loaded;
}

/* ═══════════════════════════════════════════════════════════════
   G2P RULE ENGINE  (Grapheme-to-Phoneme fallback)
   Used when a word isn't in the CMU dictionary.
   Covers the most common English spelling patterns.
   ═══════════════════════════════════════════════════════════════ */

typedef struct { const char *graph; const char *phones; } G2PRule;

/* Rules are tried in order — first match wins */
static const G2PRule g2p_rules[] = {
    /* Common digraphs first */
    {"tion",  "SH AH0 N"},
    {"sion",  "ZH AH0 N"},
    {"ough",  "AH0 F"},      /* rough, tough */
    {"ough",  "OW"},         /* though */
    {"igh",   "AY"},
    {"tch",   "CH"},
    {"dge",   "JH"},
    {"ck",    "K"},
    {"wh",    "W"},
    {"ph",    "F"},
    {"gh",    ""},           /* silent: night */
    {"kn",    "N"},
    {"wr",    "R"},
    {"qu",    "K W"},
    {"ch",    "CH"},
    {"sh",    "SH"},
    {"th",    "TH"},
    {"ng",    "NG"},
    /* Vowel patterns */
    {"ee",    "IY"},
    {"ea",    "IY"},
    {"oo",    "UW"},
    {"oa",    "OW"},
    {"ai",    "EY"},
    {"ay",    "EY"},
    {"oi",    "OY"},
    {"oy",    "OY"},
    {"ou",    "AW"},
    {"ow",    "AW"},
    {"ie",    "AY"},
    {"ue",    "UW"},
    {"ui",    "IH"},
    /* Silent e: make previous vowel long */
    {"a_e",   "EY"},
    {"i_e",   "AY"},
    {"o_e",   "OW"},
    {"u_e",   "UW"},
    /* Single letters */
    {"a",     "AE"},
    {"e",     "EH"},
    {"i",     "IH"},
    {"o",     "AO"},
    {"u",     "AH"},
    {"b",     "B"},
    {"c",     "K"},
    {"d",     "D"},
    {"f",     "F"},
    {"g",     "G"},
    {"h",     "HH"},
    {"j",     "JH"},
    {"k",     "K"},
    {"l",     "L"},
    {"m",     "M"},
    {"n",     "N"},
    {"p",     "P"},
    {"q",     "K"},
    {"r",     "R"},
    {"s",     "S"},
    {"t",     "T"},
    {"v",     "V"},
    {"w",     "W"},
    {"x",     "K S"},
    {"y",     "Y"},
    {"z",     "Z"},
    {NULL, NULL}
};

static void g2p_convert(const char *word, PhonemeSeq *seq) {
    char l[64]; int wi=0;
    while(word[wi]&&wi<63){l[wi]=tolower((unsigned char)word[wi]);wi++;}
    l[wi]='\0';

    seq->count = 0;
    strncpy(seq->word, word, 31);

    int pos = 0;
    int len = strlen(l);

    while (pos < len && seq->count < MAX_PHONEMES_PER_WORD) {
        int matched = 0;
        for (int r = 0; g2p_rules[r].graph; r++) {
            int gl = strlen(g2p_rules[r].graph);
            if (strncmp(l+pos, g2p_rules[r].graph, gl) == 0) {
                /* Add phonemes from this rule */
                char phones[64];
                strncpy(phones, g2p_rules[r].phones, 63);
                char *tok = strtok(phones, " ");
                while (tok && seq->count < MAX_PHONEMES_PER_WORD) {
                    seq->ph[seq->count]     = ph_from_name(tok);
                    seq->stress[seq->count] = STRESS_NONE;
                    seq->count++;
                    tok = strtok(NULL, " ");
                }
                pos += gl;
                matched = 1;
                break;
            }
        }
        if (!matched) pos++; /* skip unknown character */
    }
}

/* ═══════════════════════════════════════════════════════════════
   DIPHONE TABLE
   1936 slots (44×44). Each slot either has loaded audio or NULL.
   ═══════════════════════════════════════════════════════════════ */

static DiphoneAudio *diphone_table[PH_COUNT][PH_COUNT];
static int           diphone_count = 0;
static char          diphone_dir[256];

/* Load a single WAV file into a DiphoneAudio struct */
static int load_wav(const char *path, DiphoneAudio *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    /* Skip 44-byte WAV header */
    fseek(f, 44, SEEK_SET);

    int16_t buf[MAX_DIPHONE_SAMP];
    int n = (int)fread(buf, sizeof(int16_t), MAX_DIPHONE_SAMP, f);
    fclose(f);

    if (n <= 0) return 0;
    memcpy(out->samples, buf, n * sizeof(int16_t));
    out->count  = n;
    out->center = n / 2;
    return 1;
}

static void load_all_diphones(const char *dir) {
    int loaded = 0;
    for (int i = 0; i < PH_COUNT; i++) {
        for (int j = 0; j < PH_COUNT; j++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s_%s.wav",
                     dir, ph_names[i], ph_names[j]);
            DiphoneAudio *da = calloc(1, sizeof(DiphoneAudio));
            if (da && load_wav(path, da)) {
                diphone_table[i][j] = da;
                loaded++;
            } else {
                free(da);
                diphone_table[i][j] = NULL;
            }
        }
    }
    diphone_count = loaded;
    printf("[TTS] Diphones loaded: %d / %d\n", loaded, PH_COUNT*PH_COUNT);
}

/* ═══════════════════════════════════════════════════════════════
   DURATION MODEL
   Returns duration in samples for a phoneme in context.
   ═══════════════════════════════════════════════════════════════ */

static int phoneme_duration_samples(Phoneme ph, uint8_t stress,
                                    int is_final, float rate) {
    int base_ms;

    /* Vowels are longer than consonants */
    if (ph <= PH_UW) {
        /* Vowel */
        switch (stress) {
            case STRESS_PRIMARY:   base_ms = 120; break;
            case STRESS_SECONDARY: base_ms = 90;  break;
            default:               base_ms = 60;  break;
        }
    } else if (ph == PH_SIL) {
        base_ms = 200; /* inter-word pause */
    } else {
        /* Consonant */
        switch (ph) {
            case PH_M: case PH_N: case PH_NG:
            case PH_L: case PH_R:
                base_ms = 70; break;  /* sonorants */
            case PH_S: case PH_Z: case PH_SH:
            case PH_ZH: case PH_F: case PH_V:
            case PH_TH: case PH_DH:
                base_ms = 80; break;  /* fricatives */
            default:
                base_ms = 50; break;  /* stops, affricates */
        }
    }

    /* Final lengthening */
    if (is_final && ph <= PH_UW) base_ms += 40;

    float scaled = (float)base_ms / rate;
    return (int)(scaled * TTS_SAMPLE_RATE / 1000);
}

/* ═══════════════════════════════════════════════════════════════
   PITCH CURVE  (F0 envelope)
   Returns pitch multiplier (1.0 = base pitch) for a position
   in the utterance (pos 0.0 = start, 1.0 = end).
   ═══════════════════════════════════════════════════════════════ */

static float pitch_at(float pos, IntonationPattern inton) {
    switch (inton) {
        case INTON_STATEMENT:
            /* Gradual fall: start ~1.1, end ~0.8 */
            return 1.1f - (pos * 0.3f);

        case INTON_QUESTION:
            /* Rise at end: flat until 0.7, then rise */
            if (pos < 0.7f) return 1.0f;
            return 1.0f + ((pos - 0.7f) / 0.3f) * 0.4f;

        case INTON_EXCLAMATION:
            /* High start, quick fall */
            return 1.3f - (pos * 0.4f);

        case INTON_LIST_ITEM:
            /* Slight rise — not terminal */
            return 0.95f + (pos * 0.1f);

        case INTON_LIST_FINAL:
            /* Terminal fall */
            return 1.05f - (pos * 0.25f);

        default:
            return 1.0f;
    }
}

/* Apply pitch shift via simple resampling */
static void apply_pitch(int16_t *samples, int count, float pitch_mult) {
    if (fabsf(pitch_mult - 1.0f) < 0.01f) return;
    /* Time-domain pitch shift: resample at pitch_mult rate */
    int16_t tmp[MAX_DIPHONE_SAMP * 4];
    int new_count = (int)(count / pitch_mult);
    if (new_count > MAX_DIPHONE_SAMP * 4) new_count = MAX_DIPHONE_SAMP * 4;
    for (int i = 0; i < new_count; i++) {
        float src = i * pitch_mult;
        int   si  = (int)src;
        float frac = src - si;
        if (si + 1 < count)
            tmp[i] = (int16_t)(samples[si]*(1-frac) + samples[si+1]*frac);
        else if (si < count)
            tmp[i] = samples[si];
        else
            tmp[i] = 0;
    }
    memcpy(samples, tmp, new_count * sizeof(int16_t));
}

/* ═══════════════════════════════════════════════════════════════
   SYNTHESIZE SILENCE
   Used when no diphone recording is available.
   ═══════════════════════════════════════════════════════════════ */

static void synth_silence(int16_t *out, int count) {
    memset(out, 0, count * sizeof(int16_t));
}

/* ═══════════════════════════════════════════════════════════════
   INIT
   ═══════════════════════════════════════════════════════════════ */

static TTSConfig g_cfg = {{0}, 1.0f, 120.0f, 4.0f, 0, {0}};
static int       g_tts_init = 0;

int tts_init(const char *dphone_dir, float speaking_rate, float pitch_base) {
    strncpy(diphone_dir, dphone_dir, 255);
    g_cfg.speaking_rate = speaking_rate;
    g_cfg.pitch_base    = pitch_base;

    memset(diphone_table, 0, sizeof(diphone_table));
    load_all_diphones(dphone_dir);

    g_tts_init = 1;
    printf("[TTS] Engine ready. Rate:%.1f Pitch:%.0fHz Diphones:%d\n",
           speaking_rate, pitch_base, diphone_count);
    return diphone_count;
}

/* ═══════════════════════════════════════════════════════════════
   TEXT → PHONEMES
   ═══════════════════════════════════════════════════════════════ */

int tts_text_to_phonemes(const char *text, PhonemeSeq *out, int max_words) {
    int   word_count = 0;
    char  buf[2048];
    strncpy(buf, text, 2047);

    char *tok = strtok(buf, " \t\n.,!?;:");
    while (tok && word_count < max_words) {
        /* Clean word: remove non-alpha */
        char clean[48]; int j=0;
        for (int i=0; tok[i] && j<47; i++)
            if (isalpha((unsigned char)tok[i]))
                clean[j++] = tolower((unsigned char)tok[i]);
        clean[j]='\0';
        if (!clean[0]) { tok=strtok(NULL," \t\n.,!?;:"); continue; }

        /* CMU dict lookup first */
        const PhonemeSeq *cmu = cmu_lookup(clean);
        if (cmu) {
            out[word_count] = *cmu;
        } else {
            /* G2P fallback */
            g2p_convert(clean, &out[word_count]);
        }
        word_count++;
        tok = strtok(NULL, " \t\n.,!?;:");
    }
    return word_count;
}

/* ═══════════════════════════════════════════════════════════════
   PHONEMES → AUDIO
   ═══════════════════════════════════════════════════════════════ */

int tts_phonemes_to_audio(const PhonemeSeq *seqs, int word_count,
                           IntonationPattern inton,
                           int16_t *out_samples, int max_samples) {
    int total_samples = 0;
    int total_phonemes = 0;

    /* Count total phonemes for position tracking */
    for (int w = 0; w < word_count; w++)
        total_phonemes += seqs[w].count;

    int ph_pos = 0;

    for (int w = 0; w < word_count; w++) {
        const PhonemeSeq *seq = &seqs[w];

        for (int p = 0; p < seq->count; p++) {
            Phoneme  cur  = seq->ph[p];
            Phoneme  next = PH_SIL;
            if (p+1 < seq->count)
                next = seq->ph[p+1];
            else if (w+1 < word_count && seqs[w+1].count > 0)
                next = seqs[w+1].ph[0];

            int is_final = (w == word_count-1 && p == seq->count-1);

            /* Duration */
            int dur = phoneme_duration_samples(cur, seq->stress[p],
                                               is_final, g_cfg.speaking_rate);

            /* Pitch at this position */
            float pos        = (float)ph_pos / (float)(total_phonemes + 1);
            float pitch_mult = pitch_at(pos, inton);
            ph_pos++;

            if (total_samples + dur > max_samples) dur = max_samples - total_samples;
            if (dur <= 0) break;

            /* Get diphone audio */
            DiphoneAudio *da = diphone_table[cur][next];
            if (da && da->count > 0) {
                /* Resample diphone to target duration */
                float step = (float)da->count / (float)dur;
                for (int i = 0; i < dur; i++) {
                    int src = (int)(i * step);
                    if (src >= da->count) src = da->count - 1;
                    out_samples[total_samples + i] = da->samples[src];
                }
                /* Apply pitch */
                apply_pitch(out_samples + total_samples, dur, pitch_mult);
            } else {
                /* No diphone — synthesize a simple tone for vowels, silence for consonants */
                if (cur <= PH_UW && cur != PH_SIL) {
                    /* Simple sine wave approximation */
                    float freq = g_cfg.pitch_base * pitch_mult;
                    for (int i = 0; i < dur; i++) {
                        float t = (float)(total_samples + i) / TTS_SAMPLE_RATE;
                        float amp = 8000.0f;
                        /* Fade in/out to avoid clicks */
                        if (i < 80)  amp *= (float)i / 80.0f;
                        if (i > dur-80) amp *= (float)(dur-i) / 80.0f;
                        out_samples[total_samples + i] =
                            (int16_t)(amp * sinf(2.0f * 3.14159f * freq * t));
                    }
                } else {
                    synth_silence(out_samples + total_samples, dur);
                }
            }
            total_samples += dur;
        }

        /* Inter-word pause */
        int pause = (int)(0.05f * TTS_SAMPLE_RATE / g_cfg.speaking_rate);
        if (total_samples + pause < max_samples) {
            synth_silence(out_samples + total_samples, pause);
            total_samples += pause;
        }
    }

    return total_samples;
}

/* ═══════════════════════════════════════════════════════════════
   WAV FILE WRITER
   ═══════════════════════════════════════════════════════════════ */

int tts_write_wav(const char *path, const int16_t *samples, int count) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int data_size   = count * 2;
    int chunk_size  = 36 + data_size;
    int sample_rate = TTS_SAMPLE_RATE;
    int byte_rate   = sample_rate * 2;
    int16_t block_align = 2, bits = 16, channels = 1;
    int32_t fmt_size = 16;
    int16_t fmt_type = 1;

    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size,  4, 1, f);
    fwrite("WAVE", 1, 4, f);
    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size,   4, 1, f);
    fwrite(&fmt_type,   2, 1, f);
    fwrite(&channels,   2, 1, f);
    fwrite(&sample_rate,4, 1, f);
    fwrite(&byte_rate,  4, 1, f);
    fwrite(&block_align,2, 1, f);
    fwrite(&bits,       2, 1, f);
    /* data chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(samples, 2, count, f);
    fclose(f);

    printf("[TTS] WAV written: %s (%d samples, %.1fs)\n",
           path, count, (float)count/TTS_SAMPLE_RATE);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   AUDIO PLAYBACK
   ═══════════════════════════════════════════════════════════════ */

void tts_play_pcm(const int16_t *samples, int count) {
    /* Write to temp file then play */
    const char *tmp = "/tmp/blaf_tts.wav";
    if (tts_write_wav(tmp, samples, count) == 0) {
        /* Try common players in order */
        char cmd[512];
        /* Termux / Android */
        snprintf(cmd, sizeof(cmd), "play-audio %s 2>/dev/null", tmp);
        if (system(cmd) != 0) {
            /* ALSA */
            snprintf(cmd, sizeof(cmd), "aplay %s 2>/dev/null", tmp);
            if (system(cmd) != 0) {
                /* SoX */
                snprintf(cmd, sizeof(cmd), "play %s 2>/dev/null", tmp);
                if (system(cmd) != 0) {
                    printf("[TTS] Audio saved to: %s\n", tmp);
                    printf("[TTS] Play with: aplay %s\n", tmp);
                }
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
   MAIN SPEAK FUNCTION
   ═══════════════════════════════════════════════════════════════ */

void tts_speak(const char *text, QuestionType qtype) {
    if (!text || !text[0]) return;

    printf("[TTS] Speaking: \"%s\"\n", text);

    /* Map question type to intonation */
    IntonationPattern inton;
    switch (qtype) {
        case Q_WHO_IS:
        case Q_WHAT_IS:
        case Q_WHERE_IS:
        case Q_WHEN_IS:
        case Q_WHY:
        case Q_HOW:
        case Q_IS:
        case Q_DO_DOES:
        case Q_CAN:
            inton = INTON_QUESTION; break;
        default:
            inton = INTON_STATEMENT; break;
    }

    /* 1. Text → phonemes */
    PhonemeSeq seqs[MAX_WORDS_PER_SENT];
    int word_count = tts_text_to_phonemes(text, seqs, MAX_WORDS_PER_SENT);
    if (word_count == 0) {
        printf("[TTS] No phonemes generated.\n");
        return;
    }

    printf("[TTS] %d words → %d phoneme sequences. Intonation: %s\n",
           word_count, word_count,
           inton == INTON_QUESTION ? "RISING" : "FALLING");

    /* 2. Phonemes → PCM */
    int max_samples = TTS_SAMPLE_RATE * 30; /* 30 second max */
    int16_t *pcm = malloc(max_samples * sizeof(int16_t));
    if (!pcm) { fprintf(stderr,"[TTS] OOM\n"); return; }

    int n_samples = tts_phonemes_to_audio(seqs, word_count, inton,
                                          pcm, max_samples);

    printf("[TTS] Generated %.2fs of audio (%d samples).\n",
           (float)n_samples/TTS_SAMPLE_RATE, n_samples);

    /* 3. Output */
    if (g_cfg.output_to_file && g_cfg.output_file[0]) {
        tts_write_wav(g_cfg.output_file, pcm, n_samples);
    } else {
        tts_play_pcm(pcm, n_samples);
    }

    free(pcm);
}

/* ═══════════════════════════════════════════════════════════════
   STATUS
   ═══════════════════════════════════════════════════════════════ */

void tts_status(void) {
    printf("\n[TTS STATUS]\n");
    printf("  Init      : %s\n",  g_tts_init ? "YES" : "NO");
    printf("  CMU dict  : %d entries\n", cmu_loaded);
    printf("  Diphones  : %d / %d loaded\n", diphone_count, PH_COUNT*PH_COUNT);
    printf("  Rate      : %.1fx\n", g_cfg.speaking_rate);
    printf("  Base pitch: %.0f Hz\n", g_cfg.pitch_base);
    printf("  Phonemes  : %d (ARPAbet)\n", PH_COUNT-1);
    printf("  Sample rate: %d Hz\n", TTS_SAMPLE_RATE);
    printf("  Bit depth : %d-bit\n", TTS_BIT_DEPTH);
    printf("\n");
}
