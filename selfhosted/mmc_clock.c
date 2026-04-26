/*
 * ============================================================================
 *  mmc_clock.c  -  မြန်မာစံတော်ချိန် နောက်ဆုံး (Myanmar Standard Time Clock)
 *
 *  A standalone time-announcing clock using the Saing Waing audio system.
 *  Plays the MZT identification chime followed by N gong strikes for
 *  the current hour — like a Burmese temple bell tower.
 *
 *  Usage:
 *      ./mmc_clock              # Announce current time once
 *      ./mmc_clock 20 30        # Announce 8:30 PM (for testing)
 *      ./mmc_clock --loop       # Run continuously, announce every hour
 *
 *  Audio Pattern (example: 8:30 PM / ကွန် 8 နာရီ ၃၀ မိနစ်):
 *      [MZT 4-note chime] → [pause] → [မောင်း x 8] → [ဝါးလက်ခုပ် x 1]
 *
 *  Hour → Gong mapping:
 *      00:00/12:00 = 12 strikes  |  01:00/13:00 = 1 strike
 *      02:00/14:00 = 2 strikes  |  ...
 *      11:00/23:00 = 11 strikes
 *
 *  Myanmar Time Periods:
 *      00:00-04:59   နံရံ (night)
 *      05:00-11:59   မနက် (morning)
 *      12:00-17:59   ညနေ (afternoon)
 *      18:00-23:59   ကွန် (evening/night)
 *
 *  Compile:
 *      gcc -std=c99 -O2 -Wall -DMMC_CLOCK_MAIN \
 *          -o mmc_clock mmc_clock.c ia_bridge.c ia_audio.c -lm
 *
 *  Cross-compile for ARM64 (Termux / Snapdragon 4 Gen 2):
 *      aarch64-linux-android-gcc -std=c99 -O2 -Wall -DMMC_CLOCK_MAIN \
 *          -o mmc_clock mmc_clock.c ia_bridge.c ia_audio.c -lm
 *
 *  GitHub: meonnmi-ops/mmc-compiler
 *  License: MIT
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

/* Forward declarations from ia_audio.c */
typedef struct {
    int  enabled;
    int  backend;
    int  volume_pct;
    int  sample_rate;
    char device_name[128];
} MMC_Audio_Config;

extern int  mmc_audio_init(MMC_Audio_Config *config);
extern void mmc_audio_trigger(int event);
extern void mmc_audio_play_startup(void);
extern void mmc_audio_play_analyzing(void);
extern void mmc_audio_play_success(void);
extern void mmc_audio_play_critical(void);
extern void mmc_audio_play_complete(void);
extern void mmc_audio_set_volume(int pct);
extern void mmc_audio_enable(int enabled);
extern int  mmc_audio_cleanup(void);
extern int  mmc_audio_announce_time(int hour, int minute);

/* Audio event enum (matches ia_audio.c) */
#define MMC_AUDIO_SAIN_CHIME    0
#define MMC_AUDIO_WAING_CLAP    1
#define MMC_AUDIO_OBOE_MELODY   2
#define MMC_AUDIO_GONG_STRIKE   3
#define MMC_AUDIO_HNEE_TONE     4
#define MMC_AUDIO_SILENT        5

/* ============================================================================
 *  Myanmar Time Helpers
 * ============================================================================ */

static const char *myanmar_period(int hour)
{
    if (hour >= 0 && hour < 5)  return "နံရံ";
    if (hour >= 5 && hour < 12) return "မနက်";
    if (hour >= 12 && hour < 18) return "ညနေ";
    return "ကွန်";
}

static const char *myanmar_period_en(int hour)
{
    if (hour >= 0 && hour < 5)  return "Night";
    if (hour >= 5 && hour < 12) return "Morning";
    if (hour >= 12 && hour < 18) return "Afternoon";
    return "Evening";
}

static int hour_to_strikes(int hour)
{
    if (hour == 0 || hour == 12) return 12;
    if (hour > 12) return hour - 12;
    return hour;
}

/* ============================================================================
 *  Signal Handling (for --loop mode graceful exit)
 * ============================================================================ */

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n  [clock] Stopping...\n");
}

/* ============================================================================
 *  Display Art
 * ============================================================================ */

static void print_banner(void)
{
    printf("\n");
    printf("  ============================================================\n");
    printf("     ◉  မြန်မာစံတော်ချိန် နောက်ဆုံး  ◉\n");
    printf("     Myanmar Standard Time Clock  (Saing Waing Edition)\n");
    printf("  ============================================================\n");
    printf("     Audio: MZT Chime + မောင်း (Gong) + ဝါးလက်ခုပ် (Clapper)\n");
    printf("     Pattern: [Chime] -> [N gong strikes] -> [half-hour clap]\n");
    printf("     Timezone: Asia/Yangon (UTC+6:30)\n");
    printf("  ============================================================\n");
    printf("\n");
}

static void print_time_art(int hour, int minute)
{
    int h12 = hour_to_strikes(hour);
    int is_half = (minute >= 30);
    const char *period = myanmar_period(hour);

    printf("  ┌──────────────────────────────────────┐\n");
    printf("  │  မြန်မာစံတော်ချိန် Myanmar Standard Time  │\n");
    printf("  │                                      │\n");
    printf("  │      %s %2d : %02d %s             │\n",
           period, h12, minute, is_half ? " ၀၃၀" : "    ");
    printf("  │      %s                │\n",
           myanmar_period_en(hour));
    printf("  │                                      │\n");

    /* Gong strike visualization */
    int strikes = h12;
    printf("  │  မောင်း: ");
    int i;
    for (i = 0; i < strikes; i++) {
        printf(" ◎");
    }
    for (i = strikes; i < 12; i++) {
        printf(" ·");
    }
    printf("  │\n");

    if (is_half) {
        printf("  │  ဝါးလက်ခုပ်: ◇ (half-hour)             │\n");
    } else {
        printf("  │  ဝါးလက်ခုပ်:                         │\n");
    }

    printf("  │                                      │\n");
    printf("  │  Chime: 440→430→387→4033 Hz (accel) │\n");
    printf("  │  Gong:  80 Hz + 5 harmonics          │\n");
    printf("  └──────────────────────────────────────┘\n");
    printf("\n");
}

/* ============================================================================
 *  Main
 * ============================================================================ */

#ifdef MMC_CLOCK_MAIN
int main(int argc, char *argv[])
{
    MMC_Audio_Config cfg;
    int mode_loop = 0;
    int use_system_time = 1;
    int test_hour = -1;
    int test_minute = -1;

    /* Parse arguments */
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--loop") == 0) {
            mode_loop = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [hour] [minute] [--loop] [--help]\n", argv[0]);
            printf("  (no args)     Announce current system time once\n");
            printf("  20 30         Announce 8:30 PM (test mode)\n");
            printf("  --loop        Run continuously\n");
            printf("  --help        Show this help\n");
            return 0;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            use_system_time = 0;
            test_hour = atoi(argv[i]);
            if (i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9') {
                test_minute = atoi(argv[i+1]);
                i++;
            } else {
                test_minute = 0;
            }
        }
    }

    /* Initialize audio system */
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled     = 1;
    cfg.backend     = 0;   /* Console bell (works everywhere) */
    cfg.volume_pct  = 80;
    cfg.sample_rate = 44100;
    strncpy(cfg.device_name, "default", sizeof(cfg.device_name) - 1);

    if (mmc_audio_init(&cfg) != 0) {
        fprintf(stderr, "Failed to initialize audio system.\n");
        return 1;
    }

    /* Set Myanmar timezone */
    #ifdef _DEFAULT_SOURCE
    setenv("TZ", "Asia/Yangon", 1);
    tzset();
    #endif

    print_banner();

    if (mode_loop) {
        /* Continuous mode: announce at the top of every hour */
        printf("  [loop] Running continuously. Press Ctrl+C to stop.\n");
        printf("  [loop] Will announce at the start of every hour.\n\n");

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        int last_hour = -1;
        while (g_running) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            int current_hour = tm_info->tm_hour;

            /* Check if we just entered a new hour */
            if (current_hour != last_hour) {
                printf("  ─── New Hour ───\n");
                print_time_art(tm_info->tm_hour, tm_info->tm_min);
                mmc_audio_announce_time(tm_info->tm_hour, tm_info->tm_min);
                printf("\n");
                last_hour = current_hour;
            }

            /* Sleep 10 seconds between checks */
            sleep(10);
        }

        printf("  [clock] Loop ended.\n");
    } else {
        /* Single announce mode */
        int hour, minute;

        if (use_system_time) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            hour = tm_info->tm_hour;
            minute = tm_info->tm_min;
        } else {
            hour = test_hour;
            minute = test_minute;
        }

        print_time_art(hour, minute);
        mmc_audio_announce_time(hour, minute);
    }

    /* Cleanup */
    mmc_audio_cleanup();

    return 0;
}
#endif /* MMC_CLOCK_MAIN */
