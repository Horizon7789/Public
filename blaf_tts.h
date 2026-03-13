/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  BLAF — blaf_tts.h                                          ║
 * ║  Text-To-Speech Engine                                      ║
 * ║                                                              ║
 * ║  Pipeline:                                                   ║
 * ║    text → words → phonemes (CMU dict) → diphones            ║
 * ║         → duration rules → pitch curve → PCM audio          ║
 * ║                                                              ║
 * ║  44 phonemes. 1936 diphone slots. ~6MB recording set.       ║
 * ║  Zero neural network. Fully deterministic.                  ║
 * ║                                                              ║
 * ║  Recording guide:                                           ║
 * ║    Record each diphone as a mono 16kHz 16-bit WAV.          ║
 * ║    Filename format: diphones/AA_B.wav  (from_to)            ║
 * ║    Or use Festival/espeak diphone sets (free, open).        ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#ifndef BLAF_TTS_H
#define BLAF_TTS_H

#include <stdint.h>
#include "blaf_grammar.h"   /* for QuestionType → intonation */

/* ═══════════════════════════════════════════════════════════════
   PHONEME SET  (ARPAbet — 44 symbols)
   This is the standard used by CMU Pronouncing Dictionary
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    /* Monophthong vowels (12) */
    PH_AA = 0,  /* f[a]ther        */
    PH_AE,      /* c[a]t           */
    PH_AH,      /* b[u]t           */
    PH_AO,      /* d[o]g           */
    PH_AW,      /* c[ow]           */  /* diphthong */
    PH_AY,      /* h[i]de          */  /* diphthong */
    PH_EH,      /* b[e]d           */
    PH_ER,      /* b[ir]d          */
    PH_EY,      /* b[a]ke          */  /* diphthong */
    PH_IH,      /* b[i]t           */
    PH_IY,      /* b[ee]t          */
    PH_OW,      /* b[oa]t          */  /* diphthong */
    PH_OY,      /* b[oy]           */  /* diphthong */
    PH_UH,      /* b[oo]k          */
    PH_UW,      /* b[oo]t          */
    /* Consonants (24) */
    PH_B,       /* [b]ad           */
    PH_CH,      /* [ch]eese        */
    PH_D,       /* [d]ig           */
    PH_DH,      /* [th]is          */
    PH_F,       /* [f]ig           */
    PH_G,       /* [g]ood          */
    PH_HH,      /* [h]at           */
    PH_JH,      /* [j]oy           */
    PH_K,       /* [k]ey           */
    PH_L,       /* [l]et           */
    PH_M,       /* [m]at           */
    PH_N,       /* [n]at           */
    PH_NG,      /* si[ng]          */
    PH_P,       /* [p]at           */
    PH_R,       /* [r]ed           */
    PH_S,       /* [s]it           */
    PH_SH,      /* [sh]oe          */
    PH_T,       /* [t]op           */
    PH_TH,      /* [th]in          */
    PH_V,       /* [v]at           */
    PH_W,       /* [w]et           */
    PH_Y,       /* [y]et           */
    PH_Z,       /* [z]oo           */
    PH_ZH,      /* plea[s]ure      */
    /* Special */
    PH_SIL,     /* silence/pause   */
    PH_COUNT    /* = 44            */
} Phoneme;

/* Stress markers on vowels (from CMU dict) */
#define STRESS_NONE    0
#define STRESS_PRIMARY 1
#define STRESS_SECONDARY 2

/* ═══════════════════════════════════════════════════════════════
   PHONEME SEQUENCE  (one word's pronunciation)
   ═══════════════════════════════════════════════════════════════ */

#define MAX_PHONEMES_PER_WORD 16
#define MAX_WORDS_PER_SENT   64

typedef struct {
    Phoneme  ph[MAX_PHONEMES_PER_WORD];
    uint8_t  stress[MAX_PHONEMES_PER_WORD]; /* STRESS_* */
    int      count;
    char     word[32];
} PhonemeSeq;

/* ═══════════════════════════════════════════════════════════════
   INTONATION PATTERNS
   ═══════════════════════════════════════════════════════════════ */

typedef enum {
    INTON_STATEMENT,   /* falling — period          */
    INTON_QUESTION,    /* rising  — ?               */
    INTON_EXCLAMATION, /* high-flat then drop — !   */
    INTON_LIST_ITEM,   /* slight rise, not final    */
    INTON_LIST_FINAL,  /* fall — last item in list  */
} IntonationPattern;

/* ═══════════════════════════════════════════════════════════════
   AUDIO SEGMENT  (one diphone of PCM)
   ═══════════════════════════════════════════════════════════════ */

#define TTS_SAMPLE_RATE  16000   /* 16kHz mono                   */
#define TTS_BIT_DEPTH    16      /* 16-bit signed PCM            */
#define MAX_DIPHONE_MS   200     /* max diphone recording length */
#define MAX_DIPHONE_SAMP (TTS_SAMPLE_RATE * MAX_DIPHONE_MS / 1000)

typedef struct {
    int16_t samples[MAX_DIPHONE_SAMP];
    int     count;        /* actual sample count          */
    int     center;       /* index of phoneme boundary    */
} DiphoneAudio;

/* ═══════════════════════════════════════════════════════════════
   TTS CONFIGURATION
   ═══════════════════════════════════════════════════════════════ */

typedef struct {
    char   diphone_dir[256];  /* path to diphone WAV files    */
    float  speaking_rate;     /* 1.0 = normal, 0.5 = slow     */
    float  pitch_base;        /* base F0 in Hz (120=male)     */
    float  pitch_range;       /* semitones of variation       */
    int    output_to_file;    /* 1 = write WAV, 0 = play live */
    char   output_file[256];  /* path for WAV output          */
} TTSConfig;

/* ═══════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════ */

/*
 * tts_init()
 * Load CMU dictionary + diphone set from the given directory.
 * diphone_dir should contain files like "AH_B.wav", "B_AH.wav" etc.
 * Returns number of diphones loaded, -1 on error.
 *
 * Example:
 *   tts_init("./diphones", 1.0f, 120.0f);
 */
int tts_init(const char *diphone_dir, float speaking_rate, float pitch_base);

/*
 * tts_load_cmu_dict()
 * Load CMU Pronouncing Dictionary text file.
 * Download: http://www.speech.cs.cmu.edu/cgi-bin/cmudict
 * Returns number of entries loaded.
 */
int tts_load_cmu_dict(const char *dict_path);

/*
 * tts_speak()
 * Main function. Converts text to speech.
 * Uses question_type to select intonation pattern.
 * Plays audio or writes to WAV depending on config.
 *
 * Example:
 *   tts_speak("London is in England.", Q_STATEMENT);
 *   tts_speak("Who is Bill Gates?",    Q_WHO_IS);
 */
void tts_speak(const char *text, QuestionType qtype);

/*
 * tts_text_to_phonemes()
 * Converts text to a sequence of PhonemeSeq (one per word).
 * Uses CMU dict if loaded, falls back to G2P rules.
 * Returns number of words converted.
 */
int tts_text_to_phonemes(const char *text,
                          PhonemeSeq *out, int max_words);

/*
 * tts_phonemes_to_audio()
 * Converts phoneme sequences to raw PCM samples.
 * Applies duration rules and pitch curve.
 * Output is written to out_samples (caller must allocate).
 * Returns total sample count.
 */
int tts_phonemes_to_audio(const PhonemeSeq *seqs, int word_count,
                           IntonationPattern inton,
                           int16_t *out_samples, int max_samples);

/*
 * tts_write_wav()
 * Write PCM samples to a WAV file.
 */
int tts_write_wav(const char *path, const int16_t *samples, int count);

/*
 * tts_play_pcm()
 * Play PCM directly via ALSA (Linux) or AudioTrack (Android/Termux).
 * Falls back to writing /tmp/blaf_tts.wav + system("play ...").
 */
void tts_play_pcm(const int16_t *samples, int count);

/*
 * tts_phoneme_name()
 * Returns the ARPAbet string for a Phoneme enum.
 */
const char* tts_phoneme_name(Phoneme p);

/*
 * tts_status()
 * Print TTS config and load status.
 */
void tts_status(void);

#endif /* BLAF_TTS_H */
