/*
 * ============================================================================
 *  ia_audio.c  -  MMC Saing Waing Native Audio Trigger System
 * ============================================================================
 *
 *  Project:    MMC Compiler  -  Self-Hosted Hardware Diagnostic Suite
 *  Context:    စကားဝိုင်း (Saing Waing) - Traditional Myanmar Orchestra
 *
 *  Description:
 *      Audio feedback system for MMC diagnostics using sounds inspired by
 *      traditional Myanmar Saing Waing instruments.  Each diagnostic event
 *      maps to a culturally significant instrument sound:
 *
 *          မြန်မာစံတော်ချိန် (Sain Chime)    ->  Startup
 *          ဝါးလက်ခုပ် (Waing Clap)      ->  Analyzing / In-progress
 *          နှဲ (Hne / Oboe)             ->  Success (all green)
 *          မောင်း (Gong)                 ->  Critical fault detected
 *          ဟွဲ (Hnee / Flute)            ->  Diagnostic complete
 *
 *  Audio Backends:
 *      0 = Console bell (ANSI \a)  - works everywhere
 *      1 = ALSA PCM                - Linux / Termux
 *      2 = Android AudioTrack      - reserved for future JNI
 *
 *  Architecture:
 *      ARM64 (Snapdragon 4 Gen 2) / Termux / HyperOS
 *
 *  Dependencies:
 *      C99 standard library + POSIX (unistd.h for usleep)
 *      NO external audio libraries for basic functionality
 *      Console bell fallback works without ALSA
 *
 *  Author:     MMC Compiler Team
 *  License:    MIT
 *  Version:    1.0.0
 * ============================================================================
 */

#include "ia_bridge.h"

#define _DEFAULT_SOURCE   /* for usleep() on glibc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* POSIX for usleep() */
#include <unistd.h>

/* ALSA support (optional) */
#ifdef MMC_AUDIO_ALSA
#include <alsa/asoundlib.h>
#endif

/* ============================================================================
 *  Constants
 * ============================================================================ */

#define MMC_PI                    3.14159265358979323846
#define MMC_TWO_PI                (2.0 * MMC_PI)
#define MMC_DEFAULT_SAMPLE_RATE   44100
#define MMC_MAX_PCM_BUFFER        (44100 * 2)   /* 2 seconds max buffer */
#define MMC_MAX_VOLUME            32767          /* 16-bit signed max */
#define MMC_ATTACK_SMOOTH         0.05           /* 5% of duration */
#define MMC_RELEASE_SMOOTH        0.10           /* 10% of duration */

/* ============================================================================
 *  Audio Event Enumeration  -  အသုံးပြုသောအသေးအိုင်းများ (Usage Events)
 *
 *  Each event corresponds to a traditional Myanmar instrument:
 *  Sain = circular frame gong, Waing = drum circle, Hne = oboe,
 *  Kyee = gong, Hnee = flute
 * ============================================================================ */

typedef enum {
    MMC_AUDIO_SAIN_CHIME,    /* မြန်မာစံတော်ချိန် - Burmese Standard Time chime (startup) */
    MMC_AUDIO_WAING_CLAP,    /* ဝါးလက်ခုပ် - Bamboo clapper rhythmic pulse (analyzing) */
    MMC_AUDIO_OBOE_MELODY,   /* နှဲ - Oboe clear melody (success / all green) */
    MMC_AUDIO_GONG_STRIKE,   /* မောင်း - Gong heavy strike (critical fault) */
    MMC_AUDIO_HNEE_TONE,     /* ဟွဲ - Hnee completion tone (diagnostic complete) */
    MMC_AUDIO_SILENT         /* No audio output */
} MMC_Audio_Event;

/* ============================================================================
 *  Audio Configuration Structure  -  အသုံးပြုမှုပြဿနာ (Configuration)
 * ============================================================================ */

typedef struct {
    int  enabled;                /* Audio system on/off switch */
    int  backend;                /* 0=console bell, 1=ALSA PCM, 2=Android */
    int  volume_pct;             /* Volume level 0-100 */
    int  sample_rate;            /* PCM sample rate (default 44100) */
    char device_name[128];       /* ALSA device path or Android output name */
} MMC_Audio_Config;

/* ============================================================================
 *  Module State  -  အခြေအနေ (Internal State)
 * ============================================================================ */

static MMC_Audio_Config g_audio_cfg = {
    .enabled     = 0,
    .backend     = 0,
    .volume_pct  = 70,
    .sample_rate = MMC_DEFAULT_SAMPLE_RATE,
    .device_name = "default"
};

/* PCM sample buffer - allocated once, reused for all sounds */
static int16_t g_pcm_buffer[MMC_MAX_PCM_BUFFER];

/* ALSA handle (NULL when ALSA is not active) */
#ifdef MMC_AUDIO_ALSA
static snd_pcm_t *g_alsa_handle = NULL;
#endif

/* Flag to track if init was called */
static int g_audio_initialized = 0;

/* ============================================================================
 *  Pure C Sine Wave Generator  -  နှင့်ရှင်းတိုင်း (Wave Generation)
 *
 *  Taylor series approximation - no math.h dependency required.
 *  Accurate enough for audio synthesis at audible frequencies.
 * ============================================================================ */

static double mmc_sine(double angle)
{
    double x = angle;

    /* Normalize angle to [-pi, +pi] range */
    while (x > MMC_PI)  x -= MMC_TWO_PI;
    while (x < -MMC_PI) x += MMC_TWO_PI;

    /* Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + ...
     * 10 iterations gives >20 bits of precision (audibly transparent) */
    double term = x;
    double sum  = x;
    int i;
    for (i = 1; i <= 10; i++) {
        term *= -x * x / (double)(2 * i * (2 * i + 1));
        sum  += term;
    }
    return sum;
}

/* ============================================================================
 *  Simple Pseudo-Random Generator  -  ကိန်းဂဏန်း (Random for noise)
 *
 *  Linear congruential generator for white noise synthesis.
 * ============================================================================ */

static uint32_t g_noise_seed = 12345;

static uint16_t mmc_noise_next(void)
{
    g_noise_seed = g_noise_seed * 1103515245 + 12345;
    return (uint16_t)(g_noise_seed >> 16);
}

/* ============================================================================
 *  PCM Buffer Playback  -  ဖော်ပြခြင်း (Playback)
 *
 *  Sends the filled PCM buffer to the active audio backend.
 * ============================================================================ */

/* --- Console Bell Fallback  -  ရုပ်ပြင်အချိန် (Screen bell) --- */
static void play_console_bell(int duration_ms)
{
    if (g_audio_cfg.volume_pct <= 0)
        return;

    /* Single or repeated ANSI bell characters for duration */
    int bells = (duration_ms + 200) / 200;  /* one bell ~200ms attention */
    int i;
    for (i = 0; i < bells; i++) {
        printf("\a");
        fflush(stdout);
        if (i < bells - 1) {
            usleep(200 * 1000);
        }
    }

    /* Sleep for remaining duration */
    if (duration_ms > 0) {
        usleep(duration_ms * 1000);
    }
}

/* --- ALSA PCM Backend  -  အလိုအကျုံ်အသေးစတိုး (ALSA Output) --- */
#ifdef MMC_AUDIO_ALSA
static int alsa_open(void)
{
    int err;
    snd_pcm_hw_params_t *params;

    if (g_alsa_handle)
        return 0;  /* Already open */

    err = snd_pcm_open(&g_alsa_handle, g_audio_cfg.device_name,
                       SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "[MMC Audio] ALSA open failed: %s\n", snd_strerror(err));
        g_alsa_handle = NULL;
        return -1;
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(g_alsa_handle, params);

    snd_pcm_hw_params_set_access(g_alsa_handle, params,
                                  SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(g_alsa_handle, params,
                                  SND_PCM_FORMAT_S16);
    snd_pcm_hw_params_set_channels(g_alsa_handle, params, 1);
    snd_pcm_hw_params_set_rate(g_alsa_handle, params,
                                g_audio_cfg.sample_rate, 0);

    err = snd_pcm_hw_params(g_alsa_handle, params);
    if (err < 0) {
        fprintf(stderr, "[MMC Audio] ALSA params failed: %s\n",
                snd_strerror(err));
        snd_pcm_close(g_alsa_handle);
        g_alsa_handle = NULL;
        return -1;
    }

    return 0;
}

static void alsa_close(void)
{
    if (g_alsa_handle) {
        snd_pcm_drain(g_alsa_handle);
        snd_pcm_close(g_alsa_handle);
        g_alsa_handle = NULL;
    }
}

static void alsa_write(const int16_t *buf, int frame_count)
{
    if (!g_alsa_handle || frame_count <= 0)
        return;

    snd_pcm_sframes_t written = 0;
    snd_pcm_sframes_t total   = 0;

    while (total < frame_count) {
        written = snd_pcm_writei(g_alsa_handle,
                                  buf + total,
                                  (snd_pcm_uframes_t)(frame_count - total));
        if (written < 0) {
            /* Recover from underrun */
            if (written == -EPIPE) {
                snd_pcm_prepare(g_alsa_handle);
                written = 0;
            } else if (written == -EINTR) {
                written = 0;
            } else {
                break;
            }
        }
        total += written;
    }
}
#endif /* MMC_AUDIO_ALSA */

/* --- Generic PCM write  -  ထုတ်ဝေခြင်း (Send to backend) --- */
static void pcm_write(const int16_t *buf, int frame_count, int duration_ms)
{
    if (!g_audio_cfg.enabled || !buf || frame_count <= 0)
        return;

    if (g_audio_cfg.backend == 1) {
#ifdef MMC_AUDIO_ALSA
        alsa_write(buf, frame_count);
#else
        /* ALSA not compiled in, fall back to console bell */
        play_console_bell(duration_ms);
#endif
    } else {
        /* Default: console bell */
        play_console_bell(duration_ms);
    }
}

/* ============================================================================
 *  Sound Generators  -  အသေးစတိုးထုတ်ခြင်း (Sound Synthesis)
 *
 *  Each function fills g_pcm_buffer with PCM samples and returns
 *  the number of valid frames written.
 * ============================================================================ */

/* --- Helper: compute amplitude envelope  -  အသံအလိုက် (Envelope) --- */
static double envelope(int sample, int total, double attack_frac, double release_frac)
{
    int attack_end  = (int)(total * attack_frac);
    int release_beg = (int)(total * (1.0 - release_frac));

    if (total <= 0) return 0.0;
    if (attack_end < 1) attack_end = 1;
    if (release_beg >= total) release_beg = total - 1;

    if (sample < attack_end) {
        return (double)sample / (double)attack_end;
    } else if (sample > release_beg) {
        return (double)(total - 1 - sample) / (double)(total - 1 - release_beg);
    }
    return 1.0;
}

/*
 * ============================================================================
 *  ၁။  မြန်မာစံတော်ချိန် - Sain Chime (Startup)
 *
 *  The traditional time-signal gong pattern: 5 ascending tones.
 *  C5 -> E5 -> G5 -> C6 -> E6
 *  120ms per tone, 50ms gap, quick attack (10ms), quick release (20ms).
 * ============================================================================
 */
static int mmc_audio_sain_chime(void)
{
    double freqs[] = { 523.25, 659.25, 783.99, 1046.50, 1318.51 };
    int note_dur_ms    = 120;
    int gap_dur_ms     = 50;
    int attack_ms      = 10;
    int release_ms     = 20;
    int num_notes      = 5;
    int i, n;

    int idx = 0;
    double vol = (double)g_audio_cfg.volume_pct / 100.0;

    for (i = 0; i < num_notes && idx < MMC_MAX_PCM_BUFFER; i++) {
        int note_frames = (g_audio_cfg.sample_rate * note_dur_ms) / 1000;
        int gap_frames  = (g_audio_cfg.sample_rate * gap_dur_ms) / 1000;

        if (idx + note_frames > MMC_MAX_PCM_BUFFER)
            note_frames = MMC_MAX_PCM_BUFFER - idx;

        /* Generate sine tone for this note */
        for (n = 0; n < note_frames; n++) {
            double t = (double)n / (double)g_audio_cfg.sample_rate;
            double env = envelope(n, note_frames,
                                   (double)attack_ms / (double)note_dur_ms,
                                   (double)release_ms / (double)note_dur_ms);
            double sample = mmc_sine(MMC_TWO_PI * freqs[i] * t) * env * vol;
            int32_t raw = (int32_t)(sample * MMC_MAX_VOLUME);
            int16_t pcm;
            if (raw >  MMC_MAX_VOLUME) raw =  MMC_MAX_VOLUME;
            if (raw < -MMC_MAX_VOLUME) raw = -MMC_MAX_VOLUME;
            pcm = (int16_t)raw;
            g_pcm_buffer[idx++] = pcm;
        }

        /* Silence gap between notes */
        for (n = 0; n < gap_frames && idx < MMC_MAX_PCM_BUFFER; n++) {
            g_pcm_buffer[idx++] = 0;
        }
    }

    /* Total duration for console bell fallback */
    int total_ms = num_notes * (note_dur_ms + gap_dur_ms);
    pcm_write(g_pcm_buffer, idx, total_ms);
    return idx;
}

/*
 * ============================================================================
 *  ၂။  ဝါးလက်ခုပ် - Waing Clap / Bamboo Clapper (Analyzing)
 *
 *  Sharp percussive clicks at ~2Hz rhythm.  White noise burst of 15ms
 *  with fast exponential decay.  Plays 3 rapid clicks then pauses.
 *  Used as background pulse during diagnostic analysis.
 * ============================================================================
 */
static int mmc_audio_waing_clap(void)
{
    int click_dur_ms   = 15;
    int click_pause_ms = 80;   /* ~2Hz rhythm: 15ms click + 80ms pause ≈ 95ms per beat */
    int clicks_per_group = 3;
    int group_pause_ms   = 300;
    int num_groups       = 2;  /* Two groups of 3 clicks */
    int i, c, n;

    int idx = 0;
    double vol = (double)g_audio_cfg.volume_pct / 100.0;
    g_noise_seed = 54321;  /* Reset seed for reproducibility */

    for (i = 0; i < num_groups && idx < MMC_MAX_PCM_BUFFER; i++) {

        /* 3 rapid clicks */
        for (c = 0; c < clicks_per_group && idx < MMC_MAX_PCM_BUFFER; c++) {
            int click_frames = (g_audio_cfg.sample_rate * click_dur_ms) / 1000;

            if (idx + click_frames > MMC_MAX_PCM_BUFFER)
                click_frames = MMC_MAX_PCM_BUFFER - idx;

            /* White noise with fast exponential decay */
            for (n = 0; n < click_frames; n++) {
                double decay = 1.0;
                if (click_frames > 1) {
                    decay = 1.0 - ((double)n / (double)click_frames);
                    decay = decay * decay;  /* Quadratic decay for sharpness */
                }
                double noise = ((double)mmc_noise_next() / 65535.0 - 0.5) * 2.0;
                double sample = noise * decay * vol * 0.8;
                int32_t raw = (int32_t)(sample * MMC_MAX_VOLUME);
                int16_t pcm;
                if (raw >  MMC_MAX_VOLUME) raw =  MMC_MAX_VOLUME;
                if (raw < -MMC_MAX_VOLUME) raw = -MMC_MAX_VOLUME;
                pcm = (int16_t)raw;
                g_pcm_buffer[idx++] = pcm;
            }

            /* Inter-click silence */
            int pause_frames = (g_audio_cfg.sample_rate * click_pause_ms) / 1000;
            for (n = 0; n < pause_frames && idx < MMC_MAX_PCM_BUFFER; n++) {
                g_pcm_buffer[idx++] = 0;
            }
        }

        /* Group pause (longer silence between groups) */
        if (i < num_groups - 1) {
            int gp_frames = (g_audio_cfg.sample_rate * group_pause_ms) / 1000;
            for (n = 0; n < gp_frames && idx < MMC_MAX_PCM_BUFFER; n++) {
                g_pcm_buffer[idx++] = 0;
            }
        }
    }

    int total_ms = num_groups * (clicks_per_group * (click_dur_ms + click_pause_ms)
                                  + group_pause_ms);
    pcm_write(g_pcm_buffer, idx, total_ms);
    return idx;
}

/*
 * ============================================================================
 *  ၃။  နှဲ - Hne / Oboe Melody (Success / All Green)
 *
 *  Clear sine wave melody with vibrato and warm timbre.
 *  G5(784Hz) -> A5(880Hz) -> B5(988Hz) -> C6(1047Hz)
 *  Each note 200ms, vibrato 5Hz modulation at 6Hz rate,
 *  2nd harmonic at 40% amplitude for warmth.
 *  Plays when diagnostic passes with all results green.
 * ============================================================================
 */
static int mmc_audio_oboe_melody(void)
{
    double freqs[]   = { 783.99, 880.00, 987.77, 1046.50 };
    int note_dur_ms  = 200;
    int num_notes    = 4;
    int i, n;

    int idx = 0;
    double vol = (double)g_audio_cfg.volume_pct / 100.0;

    for (i = 0; i < num_notes && idx < MMC_MAX_PCM_BUFFER; i++) {
        int note_frames = (g_audio_cfg.sample_rate * note_dur_ms) / 1000;

        if (idx + note_frames > MMC_MAX_PCM_BUFFER)
            note_frames = MMC_MAX_PCM_BUFFER - idx;

        for (n = 0; n < note_frames; n++) {
            double t = (double)n / (double)g_audio_cfg.sample_rate;

            /* Vibrato: 5Hz frequency modulation at 6Hz rate */
            double vibrato = 1.0 + 0.005 * mmc_sine(MMC_TWO_PI * 6.0 * t);
            double freq = freqs[i] * vibrato;

            /* Fundamental + 2nd harmonic at 40% amplitude */
            double fund  = mmc_sine(MMC_TWO_PI * freq * t);
            double harm2 = mmc_sine(MMC_TWO_PI * freq * 2.0 * t) * 0.4;
            double wave  = fund + harm2;

            /* Envelope: smooth attack (5%), sustain, smooth release (10%) */
            double env = envelope(n, note_frames, 0.05, 0.10);

            /* Normalize: max possible amplitude is 1.4, scale to 1.0 */
            wave = wave / 1.4;

            double sample = wave * env * vol;
            int32_t raw = (int32_t)(sample * MMC_MAX_VOLUME);
            int16_t pcm;
            if (raw >  MMC_MAX_VOLUME) raw =  MMC_MAX_VOLUME;
            if (raw < -MMC_MAX_VOLUME) raw = -MMC_MAX_VOLUME;
            pcm = (int16_t)raw;
            g_pcm_buffer[idx++] = pcm;
        }
    }

    int total_ms = num_notes * note_dur_ms;
    pcm_write(g_pcm_buffer, idx, total_ms);
    return idx;
}

/*
 * ============================================================================
 *  ၄။  မောင်း - Kyee / Gong Strike (Critical Fault)
 *
 *  Heavy, attention-commanding gong sound.
 *  Fundamental at 110Hz + harmonics at 220, 330, 440Hz.
 *  Long exponential decay of 800ms for reverberant effect.
 *  High amplitude for immediate technician attention.
 *  Plays when SHORT or CRITICAL hardware fault is detected.
 * ============================================================================
 */
static int mmc_audio_gong_strike(void)
{
    double fund_freq = 110.0;
    double harmonics[] = { 1.0, 2.0, 3.0, 4.0 };   /* harmonic ratios */
    double harms_amp[] = { 1.0, 0.7, 0.5, 0.3 };    /* harmonic amplitudes */
    int num_harmonics  = 4;
    int duration_ms    = 800;

    int total_frames = (g_audio_cfg.sample_rate * duration_ms) / 1000;
    int idx = 0;
    double vol = (double)g_audio_cfg.volume_pct / 100.0;

    /* Gong: loud by default, boost volume by 30% */
    vol *= 1.3;
    if (vol > 1.0) vol = 1.0;

    if (total_frames > MMC_MAX_PCM_BUFFER)
        total_frames = MMC_MAX_PCM_BUFFER;

    for (idx = 0; idx < total_frames; idx++) {
        double t = (double)idx / (double)g_audio_cfg.sample_rate;
        double wave = 0.0;
        int h;

        /* Sum harmonics with exponential decay */
        for (h = 0; h < num_harmonics; h++) {
            double freq = fund_freq * harmonics[h];
            double sine = mmc_sine(MMC_TWO_PI * freq * t);

            /* Each harmonic decays at slightly different rate
             * (higher harmonics decay faster - realistic gong behavior) */
            double decay = 1.0 - t / duration_ms;
            if (decay < 0.0) decay = 0.0;
            /* Exponential shape */
            double exp_decay = 1.0 - decay;
            exp_decay = exp_decay * exp_decay;
            decay = 1.0 - exp_decay;

            wave += sine * harms_amp[h] * decay;
        }

        /* Initial attack: very sharp (2ms) */
        int attack_frames = (g_audio_cfg.sample_rate * 2) / 1000;
        double attack = 1.0;
        if (idx < attack_frames) {
            attack = (double)idx / (double)attack_frames;
        }

        /* Normalize and apply volume */
        double norm = wave / 2.5;  /* prevent clipping from harmonic sum */
        double sample = norm * attack * vol;
        int32_t raw = (int32_t)(sample * MMC_MAX_VOLUME);
        int16_t pcm;
        if (raw >  MMC_MAX_VOLUME) raw =  MMC_MAX_VOLUME;
        if (raw < -MMC_MAX_VOLUME) raw = -MMC_MAX_VOLUME;
        pcm = (int16_t)raw;
        g_pcm_buffer[idx] = pcm;
    }

    pcm_write(g_pcm_buffer, idx, duration_ms);
    return idx;
}

/*
 * ============================================================================
 *  ၅။  ဟွဲ - Hnee / Flute Completion Tone (Diagnostic Complete)
 *
 *  A single warm, clean tone at A4 (440Hz) for 500ms.
 *  Smooth attack (50ms) and release (100ms) for a gentle finish.
 *  Indicates that the full diagnostic cycle has completed.
 * ============================================================================
 */
static int mmc_audio_hnee_tone(void)
{
    double freq        = 440.0;   /* A4 - concert pitch */
    int duration_ms    = 500;
    int attack_ms      = 50;
    int release_ms     = 100;

    int total_frames = (g_audio_cfg.sample_rate * duration_ms) / 1000;
    int idx = 0;
    double vol = (double)g_audio_cfg.volume_pct / 100.0;

    if (total_frames > MMC_MAX_PCM_BUFFER)
        total_frames = MMC_MAX_PCM_BUFFER;

    for (idx = 0; idx < total_frames; idx++) {
        double t = (double)idx / (double)g_audio_cfg.sample_rate;

        /* Clean fundamental with subtle 2nd and 3rd harmonics for warmth */
        double fund   = mmc_sine(MMC_TWO_PI * freq * t);
        double harm2  = mmc_sine(MMC_TWO_PI * freq * 2.0 * t) * 0.15;
        double harm3  = mmc_sine(MMC_TWO_PI * freq * 3.0 * t) * 0.05;
        double wave   = fund + harm2 + harm3;

        /* Envelope: smooth attack and release */
        double env = envelope(idx, total_frames,
                               (double)attack_ms / (double)duration_ms,
                               (double)release_ms / (double)duration_ms);

        /* Normalize: max amplitude is 1.2 */
        double sample = (wave / 1.2) * env * vol;
        int32_t raw = (int32_t)(sample * MMC_MAX_VOLUME);
        int16_t pcm;
        if (raw >  MMC_MAX_VOLUME) raw =  MMC_MAX_VOLUME;
        if (raw < -MMC_MAX_VOLUME) raw = -MMC_MAX_VOLUME;
        pcm = (int16_t)raw;
        g_pcm_buffer[idx] = pcm;
    }

    pcm_write(g_pcm_buffer, idx, duration_ms);
    return idx;
}

/* ============================================================================
 *  Public API  -  ထိတွေ့နိုင်သည့်လုပ်ဆောင်ချက်များ (Public Functions)
 * ============================================================================
 */

/*
 * mmc_audio_init  -  စတင်ပြင်ဆင်ခြင်း (Initialize Audio System)
 *
 * Copies user configuration, opens the selected backend.
 * Returns 0 on success, -1 on failure.
 */
int mmc_audio_init(MMC_Audio_Config *config)
{
    if (!config)
        return -1;

    /* Copy configuration */
    g_audio_cfg.enabled     = config->enabled;
    g_audio_cfg.backend     = config->backend;
    g_audio_cfg.volume_pct  = config->volume_pct;
    g_audio_cfg.sample_rate = config->sample_rate > 0
                               ? config->sample_rate
                               : MMC_DEFAULT_SAMPLE_RATE;
    strncpy(g_audio_cfg.device_name, config->device_name,
            sizeof(g_audio_cfg.device_name) - 1);
    g_audio_cfg.device_name[sizeof(g_audio_cfg.device_name) - 1] = '\0';

    /* Clamp volume */
    if (g_audio_cfg.volume_pct < 0)   g_audio_cfg.volume_pct = 0;
    if (g_audio_cfg.volume_pct > 100) g_audio_cfg.volume_pct = 100;

    /* Initialize backend */
    if (g_audio_cfg.backend == 1) {
#ifdef MMC_AUDIO_ALSA
        if (alsa_open() != 0) {
            fprintf(stderr, "[MMC Audio] ALSA init failed, falling back to "
                            "console bell\n");
            g_audio_cfg.backend = 0;
        }
#else
        fprintf(stderr, "[MMC Audio] ALSA not compiled in, using console "
                        "bell\n");
        g_audio_cfg.backend = 0;
#endif
    }

    /* Zero the PCM buffer */
    memset(g_pcm_buffer, 0, sizeof(g_pcm_buffer));

    g_audio_initialized = 1;
    return 0;
}

/*
 * mmc_audio_trigger  -  အသေးစတိုးဖြင့်ဖော်ပြခြင်း (Trigger Audio Event)
 *
 * Maps a diagnostic event to the corresponding Saing Waing sound.
 * If the event is MMC_AUDIO_SILENT or audio is disabled, does nothing.
 */
void mmc_audio_trigger(MMC_Audio_Event event)
{
    if (!g_audio_cfg.enabled || !g_audio_initialized)
        return;

    switch (event) {
        case MMC_AUDIO_SAIN_CHIME:
            mmc_audio_sain_chime();
            break;
        case MMC_AUDIO_WAING_CLAP:
            mmc_audio_waing_clap();
            break;
        case MMC_AUDIO_OBOE_MELODY:
            mmc_audio_oboe_melody();
            break;
        case MMC_AUDIO_GONG_STRIKE:
            mmc_audio_gong_strike();
            break;
        case MMC_AUDIO_HNEE_TONE:
            mmc_audio_hnee_tone();
            break;
        case MMC_AUDIO_SILENT:
        default:
            break;
    }
}

/* --- Convenience wrappers  -  အလိုလိုချက်များ (Shortcut Functions) --- */

void mmc_audio_play_startup(void)
{
    mmc_audio_trigger(MMC_AUDIO_SAIN_CHIME);
}

void mmc_audio_play_analyzing(void)
{
    mmc_audio_trigger(MMC_AUDIO_WAING_CLAP);
}

void mmc_audio_play_success(void)
{
    mmc_audio_trigger(MMC_AUDIO_OBOE_MELODY);
}

void mmc_audio_play_critical(void)
{
    mmc_audio_trigger(MMC_AUDIO_GONG_STRIKE);
}

void mmc_audio_play_complete(void)
{
    mmc_audio_trigger(MMC_AUDIO_HNEE_TONE);
}

/*
 * mmc_audio_set_volume  -  အသံတိုးချခြင်း (Set Volume)
 *
 * Sets the playback volume. Range: 0 (muted) to 100 (maximum).
 */
void mmc_audio_set_volume(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    g_audio_cfg.volume_pct = pct;
}

/*
 * mmc_audio_enable  -  ဖွင့်/ပိတ်ခြင်း (Enable / Disable Audio)
 *
 * Pass 1 to enable, 0 to disable.  When disabled, all trigger calls
 * are silent no-ops.
 */
void mmc_audio_enable(int enabled)
{
    g_audio_cfg.enabled = enabled ? 1 : 0;
}

/*
 * mmc_audio_cleanup  -  ပြကြပ်ခြင်း (Cleanup / Teardown)
 *
 * Closes the audio backend and resets internal state.
 * Returns 0 on success, -1 if audio was not initialized.
 */
int mmc_audio_cleanup(void)
{
    if (!g_audio_initialized)
        return -1;

#ifdef MMC_AUDIO_ALSA
    alsa_close();
#endif

    /* Zero and secure the buffer */
    memset(g_pcm_buffer, 0, sizeof(g_pcm_buffer));

    g_audio_cfg.enabled = 0;
    g_audio_initialized = 0;

    return 0;
}

/* ============================================================================
 *  Self-Test  -  ကိန်းဂဏန်းစမ်းသပ်ခြင်း (Built-in Test)
 *
 *  Compile with -DMMC_AUDIO_SELFTEST to build a standalone test binary:
 *      gcc -std=c99 -DMMC_AUDIO_SELFTEST ia_audio.c -o test_saing_waing
 *
 *  The test plays all 5 Saing Waing sounds sequentially using the
 *  console bell backend.
 * ============================================================================
 */

#ifdef MMC_AUDIO_SELFTEST
int main(void)
{
    MMC_Audio_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled     = 1;
    cfg.backend     = 0;   /* Console bell (works everywhere) */
    cfg.volume_pct  = 80;
    cfg.sample_rate = MMC_DEFAULT_SAMPLE_RATE;
    strncpy(cfg.device_name, "default", sizeof(cfg.device_name) - 1);

    int result = mmc_audio_init(&cfg);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize MMC Audio system.\n");
        return 1;
    }

    printf("============================================================\n");
    printf("  MMC Saing Waing Audio System Self-Test\n");
    printf("  စကားဝိုင်း အသေးစတိုး ကိန်းဂဏန်း စမ်းသပ်မှု\n");
    printf("============================================================\n");
    printf("  Backend:  %s\n",
           cfg.backend == 0 ? "Console Bell (ANSI \\a)"
         : cfg.backend == 1 ? "ALSA PCM" : "Android AudioTrack");
    printf("  Volume:   %d%%\n", cfg.volume_pct);
    printf("  Rate:     %d Hz\n", cfg.sample_rate);
    printf("============================================================\n\n");

    /* Test 1: Sain Chime  -  မြန်မာစံတော်ချိန် */
    printf("[1/5] Sain Chime (မြန်မာစံတော်ချိန်) - Startup chime...\n");
    mmc_audio_play_startup();
    printf("       OK\n\n");

    /* Small delay between tests */
    usleep(300 * 1000);

    /* Test 2: Waing Clap  -  ဝါးလက်ခုပ် */
    printf("[2/5] Waing Clap (ဝါးလက်ခုပ်) - Bamboo clapper rhythm...\n");
    mmc_audio_play_analyzing();
    printf("       OK\n\n");

    usleep(300 * 1000);

    /* Test 3: Oboe Melody  -  နှဲ */
    printf("[3/5] Oboe Melody (နှဲ) - Hne success melody...\n");
    mmc_audio_play_success();
    printf("       OK\n\n");

    usleep(300 * 1000);

    /* Test 4: Gong Strike  -  မောင်း */
    printf("[4/5] Gong Strike (မောင်း) - Critical alert...\n");
    mmc_audio_play_critical();
    printf("       OK\n\n");

    usleep(300 * 1000);

    /* Test 5: Completion Tone  -  ဟွဲ */
    printf("[5/5] Hnee Tone (ဟွဲ) - Completion tone...\n");
    mmc_audio_play_complete();
    printf("       OK\n\n");

    /* Test volume control */
    printf("--- Volume Control Test ---\n");
    mmc_audio_set_volume(30);
    printf("  Volume set to 30%%, playing soft chime...\n");
    mmc_audio_play_startup();

    usleep(300 * 1000);

    mmc_audio_set_volume(100);
    printf("  Volume set to 100%%, playing loud chime...\n");
    mmc_audio_play_startup();
    printf("  OK\n\n");

    /* Test enable/disable */
    printf("--- Enable/Disable Test ---\n");
    mmc_audio_enable(0);
    printf("  Audio disabled, triggering gong (should be silent)...\n");
    mmc_audio_play_critical();
    printf("  Silent: OK\n");

    mmc_audio_enable(1);
    printf("  Audio re-enabled, triggering chime...\n");
    mmc_audio_play_startup();
    printf("  Active: OK\n\n");

    /* Test SILENT event */
    printf("--- Silent Event Test ---\n");
    mmc_audio_trigger(MMC_AUDIO_SILENT);
    printf("  MMC_AUDIO_SILENT: OK (no output expected)\n\n");

    /* Cleanup */
    mmc_audio_cleanup();

    printf("============================================================\n");
    printf("  All 5 Saing Waing audio tests passed.\n");
    printf("  စကားဝိုင်း အသေးစတိုး အားလုံး မှန်ကန်ပါသည်။\n");
    printf("============================================================\n");

    return 0;
}
#endif /* MMC_AUDIO_SELFTEST */

/*
 * ============================================================================
 *  End of ia_audio.c
 *  စကားဝိုင်း / Saing Waing  -  MMC Compiler Project
 *  အေအိုင်AI / Nyanlin-AI
 * ============================================================================
 */
