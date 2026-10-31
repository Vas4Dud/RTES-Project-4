/**
 ******************************************************************************
 * @file    tasks.c
 * @brief   P4 "PS-16 PhonoScope" — the 3-task skeleton that runs the CAPTURE
 *          SPIKE (milestone 1). Build it into the full FFT analyzer
 *          (spec §1-§4).
 *
 *          SWEN 563 / CMPE 663 — P4.
 *
 *  What runs today (the spike): SAI-PDM capture, then the DSP task calculates
 *  a crude PDM-density level for each half. The UI then shows the level and
 *  the %CPU, and the console runs. This proves the capture path and the RTOS
 *  pipeline BEFORE PDM2PCM and the FFT. PDM2PCM ships pre-vendored and
 *  connected. When you write audio_decimate(), change the DSP task to
 *  audio_decimate() and dsp_process(), and draw dsp_spectrum() with
 *  oled_blit().
 *
 *  Tasks (spec R12): DSP (osPriorityHigh) > UI (osPriorityNormal) > Console
 *  (osPriorityBelowNormal), and the idle hook. The DMA is the sampler. The
 *  callbacks notify the DSP task (R3/R13).
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h> 
#include "main.h"
#include "tasks.h"
#include "audio.h"
#include "dsp.h"
#include "oled.h"
#include "console.h"
#include "cmd_table.h"
#include "rgb_led.h"
#include "perf_mon.h"
#include "cmsis_os2.h"
#include "pdm2pcm.h"
#include "pdm2pcm_glo.h"
#define FS_HZ   16000u
#define BIN_HZ  (FS_HZ / AUDIO_PCM_N)               /* 62.5 Hz at N=256          */
#define T_MS    (AUDIO_PCM_N * 1000u / FS_HZ)       /* frame period = deadline   */

/* Shared spike level (0..255, about 128 with no sound). Single-writer (DSP),
 * one-reader (UI). A stale read is harmless (spec R11-style unlocked
 * snapshot). */
static volatile uint8_t g_spike_level = 128u;
static volatile uint8_t g_freeze;
static volatile uint8_t g_b1_flag, g_b2_flag;

static osThreadId_t g_dsp_id, g_ui_id, g_con_id;

/* ============================ POST (pre-scheduler) ======================== */
void app_post(void)
{
    console_init();
    printf("\n=== PS-16 \"PhonoScope\" — P4 starter (capture spike) ===\n");
    printf("FW P4.0   build %s %s\n", __DATE__, __TIME__);
    printf("Fs=%u Hz  N=%u  bin=%u Hz  frame/deadline=%u ms\n",
           FS_HZ, (unsigned)AUDIO_PCM_N, (unsigned)BIN_HZ, (unsigned)T_MS);

    oled_init();
    oled_clear();
    oled_write_line(0, "PS-16 PhonoScope");
    oled_write_line(2, "capture spike");
    rgb_init();
    rgb_set(0u, 0u, 8u);
    //filter_init();
    dsp_init();                 /* FFT instance + Hann window (for the full path) */
    perf_mon_init();
    printf("POST done. Audio starts after the idle-baseline second.\n\n");
}

/* ============================ DSP task ==================================== */
static void DSPTask(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t fl = osThreadFlagsWait(AUDIO_FLAG_HALF0 | AUDIO_FLAG_HALF1,
                                        osFlagsWaitAny, osWaitForever);
        uint8_t which = (fl & AUDIO_FLAG_HALF1) ? 1u : 0u;
        const uint8_t *pdm = audio_half(which);

        /* MILESTONE 1 — the spike: crude PDM-density level (no decimation). */
        g_spike_level = audio_spike_level(pdm);

        /* FULL PATH (student, when audio_decimate() works — PDM2PCM is
         * pre-vendored and connected):
         *   static int16_t pcm[AUDIO_PCM_N];
         *   if (audio_decimate(pdm, pcm) == 0) dsp_process(pcm);
         * dsp_process obeys WCET < T_MS in all frames (spec R14). */
        static int16_t pcm[AUDIO_PCM_N];
        if (audio_decimate(pdm, pcm) == 0) dsp_process(pcm);

        audio_half_done(which);
    }
}

/* ============================ Console task =============================== */
/* Dispatch goes through the provided command-table module (cmd_table.c, the
 * P3 module carried — spec §6). HELP, STAT and FREEZE work today. The
 * analyzer commands are stubs, and to write them is the project
 * (spec §3/§4). */

static void cmd_stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int amp = (g_spike_level > 128) ? (g_spike_level - 128) : (128 - g_spike_level);
    printf("Fs=%u N=%u bin=%uHz T=%ums | frames=%lu overruns=%lu level=%d cpu=%u%%\n",
           FS_HZ, (unsigned)AUDIO_PCM_N, (unsigned)BIN_HZ, (unsigned)T_MS,
           (unsigned long)audio_frames(), (unsigned long)audio_overruns(),
           amp, perf_cpu_percent());
    /* STUDENT: build this into the full stat surface — WCET and jitter,
     * dropped=0, stack high-waters (R17: read at the bench off this
     * surface). */
}

static void cmd_freeze(int argc, char *argv[])
{
    (void)argc; (void)argv;
    g_freeze ^= 1u; printf("freeze=%u\n", g_freeze);
}

static void cmd_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("commands:\n");
    cmd_table_help();
}

/* ---- STUDENT TODO handlers (the analyzer commands, stubbed) -------------- */
static void cmd_gain(int argc, char *argv[])
{ (void)argc; (void)argv; printf("TODO(student): GAIN <dB> — mic gain, spec §9 D6\n"); }
static void cmd_band(int argc, char *argv[])
{ (void)argc; (void)argv; printf("TODO(student): BAND <lo> <hi> — display band/zoom, spec R10\n"); }
static void cmd_fft(int argc, char *argv[])
{ (void)argc; (void)argv; printf("TODO(student): FFT <256|512|1024> — re-init without dropping capture, spec R8\n"); }
static void cmd_win(int argc, char *argv[])
{ (void)argc; (void)argv; printf("TODO(student): WIN <hann|hamming|rect> — spec R5, demo D3\n"); }
static void cmd_run(int argc, char *argv[])
{ (void)argc; (void)argv; printf("TODO(student): RUN <vu|spec|heart|lung> — mode switch, spec §3\n"); }

static const cmd_entry_t g_cmds[] = {
    { "HELP",   cmd_help,   "HELP — this list" },
    { "STAT",   cmd_stat,   "STAT — Fs/N/bin/T, frames, overruns, level, %CPU" },
    { "FREEZE", cmd_freeze, "FREEZE — toggle display hold (B2 does the same)" },
    { "GAIN",   cmd_gain,   "GAIN <dB> — mic gain (student TODO)" },
    { "BAND",   cmd_band,   "BAND <lo> <hi> — display band (student TODO)" },
    { "FFT",    cmd_fft,    "FFT <256|512|1024> — FFT size (student TODO)" },
    { "WIN",    cmd_win,    "WIN <hann|hamming|rect> — window (student TODO)" },
    { "RUN",    cmd_run,    "RUN <vu|spec|heart|lung> — mode (student TODO)" },
};

static void ConsoleTask(void *arg)
{
    (void)arg;
    cmd_table_init(g_cmds, sizeof g_cmds / sizeof g_cmds[0]);
    for (;;) {
        cmd_table_poll();          /* non-blocking: drains arrived chars only */
        osDelay(20u);
    }
}

/* ============================ UI task ==================================== */
static void start_workers(void);

static void UITask(void *arg)
{
    (void)arg;
    g_ui_id = osThreadGetId();

    /* Calibrate the idle-hook %CPU baseline while quiet — audio + DSP not up. */
    g_idle_count = 0u;
    osDelay(1000u);
    perf_mon_tick_1s();
    printf("perf: idle baseline = %lu/s\n", (unsigned long)perf_baseline());

    start_workers();                        /* DSP + Console                     */
    audio_set_consumer(g_dsp_id);
    if (audio_start() != 0)
        printf("!! AUDIO START FAILED — check SAI1/DMA config (spec 6.1)\n");
    else
        printf("audio capture running.\n");

    uint32_t tick = osKernelGetTickCount();
    uint8_t sub = 0u, led_prev = 0xFFu;
    oled_clear();
    for (;;) {
        tick += 40u;
        osDelayUntil(tick);                 /* ~25 fps, decoupled from frames    */

        if (g_b1_flag) { g_b1_flag = 0u; /* TODO: cycle mode (VU/SPEC/HEART/LUNG) */ }
        if (g_b2_flag) { g_b2_flag = 0u; g_freeze ^= 1u; }

        uint8_t lv = g_spike_level;
        int amp = (lv > 128) ? (lv - 128) : (128 - lv);

        if (!g_freeze) {
            oled_printf(0, "PS-16 PhonoScope");
            oled_printf(2, "frames %lu", (unsigned long)audio_frames());
            oled_printf(4, "level %-3d ovr %lu", amp, (unsigned long)audio_overruns());
            oled_printf(6, "CPU %u%%", perf_cpu_percent());
            /* TODO: once dsp_process runs, oled_blit() the spectrum + VU bar. */
        }

        if (++sub >= 25u) {                 /* ~1 s */
            sub = 0u;
            perf_mon_tick_1s();
            uint8_t zone = (amp < 4) ? 0u : (amp < 24) ? 1u : (amp < 48) ? 2u : 3u;
            if (zone != led_prev) {         /* RGB on change only (blocks ~11 ms) */
                if (zone == 0u)      rgb_set(0u, 0u, 8u);   /* quiet  */
                else if (zone == 1u) rgb_set(0u, 15u, 0u);  /* good   */
                else if (zone == 2u) rgb_set(30u, 20u, 0u); /* hot    */
                else                 rgb_set(40u, 0u, 0u);  /* clip   */
                led_prev = zone;
            }
        }
    }
}

/* ============================ app_start ================================== */
void app_start(void)
{
    /* Only the UI task here. It calibrates the baseline, then it makes the
     * DSP and Console tasks and starts capture, so the baseline second is
     * quiet. */
    const osThreadAttr_t ui_a = { .name = "ui", .stack_size = 3072u, .priority = osPriorityNormal };
    g_ui_id = osThreadNew(UITask, NULL, &ui_a);
}

static void start_workers(void)
{
    const osThreadAttr_t dsp_a = { .name = "dsp", .stack_size = 2048u, .priority = osPriorityHigh };
    const osThreadAttr_t con_a = { .name = "con", .stack_size = 1024u, .priority = osPriorityBelowNormal };
    g_dsp_id = osThreadNew(DSPTask,     NULL, &dsp_a);
    g_con_id = osThreadNew(ConsoleTask, NULL, &con_a);
}

/* ==================== buttons: PC12=B1 (mode), PC13=B2 (freeze) =========== */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_12)      g_b1_flag = 1u;
    else if (GPIO_Pin == GPIO_PIN_13) g_b2_flag = 1u;
}
