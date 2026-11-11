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
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "arm_math.h"          /* CMSIS-DSP — wired in CMakeLists (R16)         */
#include <math.h>
#include <stdio.h>
#include <string.h> 
#include <sys/_types.h>
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
#include "stm32_hal_legacy.h"
#define FS_HZ   16000u
#define BIN_HZ  (FS_HZ / AUDIO_PCM_N)               /* 62.5 Hz at N=256          */
#define T_MS    (AUDIO_PCM_N * 1000u / FS_HZ)       /* frame period = deadline   */
void fillup_oled(const uint8_t* spectrum, uint8_t *fb1024);
void dwt_init();
static uint32_t get_N();
volatile uint8_t ui_state = 0; 

/* Shared spike level (0..255, about 128 with no sound). Single-writer (DSP),
 * one-reader (UI). A stale read is harmless (spec R11-style unlocked
 * snapshot). */
// static volatile uint8_t g_spike_level = 128u;
static volatile uint8_t g_freeze;
static volatile uint8_t g_b1_flag, g_b2_flag;

static osThreadId_t g_dsp_id, g_ui_id, g_con_id;

static uint32_t N_value = 256;
static uint32_t N_request = 0;
volatile uint8_t win_request = 0;
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
    filter_init();
    dsp_init();                 /* FFT instance + Hann window (for the full path) */
    perf_mon_init();
    dwt_init();
    printf("POST done. Audio starts after the idle-baseline second.\n\n");
}

/* ============================ DSP task ==================================== */
static void DSPTask(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t fl = osThreadFlagsWait(AUDIO_FLAG_HALF0 | AUDIO_FLAG_HALF1,
                                        osFlagsWaitAny, osWaitForever);
        if (N_request != 0)
        {
            N_value = N_request;
            N_request = 0;
            audio_setN(N_value);
            dsp_setN(N_value);
        }
        if (win_request != 0)
        {
            set_window(win_request);
            win_request = 0;
        }                                
        uint8_t which = (fl & AUDIO_FLAG_HALF1) ? 1u : 0u;
        const uint8_t *pdm = audio_half(which);

        /* MILESTONE 1 — the spike: crude PDM-density level (no decimation). */
        // g_spike_level = audio_spike_level(pdm);

        /* FULL PATH (student, when audio_decimate() works — PDM2PCM is
         * pre-vendored and connected):
         *   static int16_t pcm[AUDIO_PCM_N];
         *   if (audio_decimate(pdm, pcm) == 0) dsp_process(pcm);
         * dsp_process obeys WCET < T_MS in all frames (spec R14). */
        static int16_t pcm[2048];
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
    // int amp = (g_spike_level > 128) ? (g_spike_level - 128) : (128 - g_spike_level);
    printf("Fs=%u N=%u bin=%uHz T=%ums | frames=%lu overruns=%lu level=%d peak=%d cpu=%u%%\n",
           FS_HZ, (unsigned)N_value, (unsigned)(FS_HZ / N_value), (unsigned)(N_value * 1000 / FS_HZ),
           (unsigned long)audio_frames(), (unsigned long)audio_overruns(),
           (int)dsp_level_dbfs(), (int)dsp_peak_dbfs(), perf_cpu_percent());

    /* STUDENT: build this into the full stat surface — WCET and jitter,
     * dropped=0, stack high-waters (R17: read at the bench off this
     * surface). */
    
    //GRAD SECTION
     float32_t deadline = (float32_t)N_value * 1000.0f / (float32_t) FS_HZ;
     printf("\n\nDeadline: %d.%dms\n", (int)deadline / 10, (int)deadline % 10);
     printf("Stack dsp %lu, ui %lu, console %lu\n", (unsigned long)(osThreadGetStackSpace(g_dsp_id)),
                (unsigned long)(osThreadGetStackSpace(g_ui_id)), (unsigned long)(osThreadGetStackSpace(g_con_id)));

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
{ 
    (void)argc; (void)argv; printf("TODO(student): GAIN <dB> — mic gain, spec §9 D6\n"); 

}

static void cmd_band(int argc, char *argv[])
{ 
    (void)argc; 
    (void)argv; 
    float32_t argument1 = atof(argv[1]);
    float32_t argument2 = atof(argv[2]);
    set_frequency(argument1, argument2);
}

static void cmd_fft(int argc, char *argv[])
{ 
    (void)argc; (void)argv; 
    uint32_t argument = atoi(argv[1]);
    if(argument > 2048)
    {
        argument = 2048;
    }
    else if (argument < 0 )
    {
        argument = 256;
    }
    
    N_request = argument;
}
static void cmd_win(int argc, char *argv[])
{ 
    (void)argc; (void)argv; 
    printf("TODO(student): WIN <hann|hamming|rect> — spec R5, demo D3\n"); 
    if (strcmp(argv[1], "hann") == 0)
    {
        win_request = 1;
    }
    else if (strcmp(argv[1], "hamming") == 0) 
    {
        win_request = 2;
    }
    else if (strcmp(argv[1], "rect") == 0) 
    {
        win_request = 3;
    }
}
static void cmd_run(int argc, char *argv[])
{ 
    (void)argc; (void)argv; 

    /*if (strcmp(argv[1], "vu") == 0)
    {
        ui_state = 0;
    }
    else if (strcmp(argv[1], "spec") == 0) 
    {
        ui_state = 1;
    }
    else if (strcmp(argv[1], "heart") == 0) 
    {
        win_request = 2;
    }
    else if (strcmp(argv[1], "lung") == 0) 
    {
        win_request =  3;
    }*/
    printf("TODO(student): RUN <vu|spec|heart|lung> — mode switch, spec §3\n"); 
}

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
        if (g_b1_flag) 
        { 
            g_b1_flag = 0;
            oled_clear();
            ui_state++;
            if (ui_state == 4)
            {
                ui_state = 0;
            } /* TODO: cycle mode (VU/SPEC/HEART/LUNG) */ 
        }
        if (g_b2_flag) { g_b2_flag = 0u; g_freeze ^= 1u; }

        // uint8_t lv = g_spike_level;
        // int amp = (lv > 128) ? (lv - 128) : (128 - lv);

        if (!g_freeze) {
            if (ui_state == 0)
            {
                oled_printf(0, "PS-16 PhonoScope");
                oled_printf(2, "frames %lu", (unsigned long)audio_frames());
                oled_printf(4, "level %d ovr %lu", (int)dsp_level_dbfs(), (unsigned long)audio_overruns());
                oled_printf(6, "CPU %u%%", perf_cpu_percent());
            }
            else
            {
                switch (ui_state) 
                {
                    case(1):
                        set_frequency(0.0f, 8000.0f);
                        break;
                    case(2):
                        set_frequency(0.0f, 300.0f);
                        break;
                    case(3):
                        set_frequency(100.0f, 1000.0f);
                        break;
                }

                uint8_t* spectrum = dsp_spectrum();
                static uint8_t vu_bar[1024];
                fillup_oled(spectrum, vu_bar);
                oled_blit(vu_bar);
            }
            /* TODO: once dsp_process runs, oled_blit() the spectrum + VU bar. */

        }

        if (++sub >= 25u) {                 /* ~1 s */
            sub = 0u;
            perf_mon_tick_1s();
            // uint8_t zone = (amp < 4) ? 0u : (amp < 24) ? 1u : (amp < 48) ? 2u : 3u;
            float32_t currPeak = dsp_peak_dbfs();
            uint8_t zone = (dsp_clip()) ? 3u : (currPeak > -12.0f) ? 2u : (currPeak > -40.0f) ? 1u : 0u;
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

void fillup_oled(const uint8_t* spectrum, uint8_t *fb1024)
{
    for (int i = 0; i < 1024; i++)
    {
        fb1024[i] = 0;
    }
    for (int i = 0; i < 128; i++)
    {
        uint8_t pixels = 0;
        for (int j = 0; j < 64; j++)
        {
            int y = 55 - j;
            if(spectrum[i] > j)
            {
                pixels |= 1 << (y % 8);
            }
            else if (pixels != 0) {
                fb1024[(y / 8) * 128 + i] = pixels;
                pixels = 0;
            }
            if (pixels == 0xFF)
            {
                fb1024[(y / 8) * 128 + i] = pixels;
                pixels = 0;
            }
            
        }
    }
    
    //draw total audio bar
    float audio = dsp_peak_dbfs();
    uint16_t width = (uint16_t) ((audio + 60.0) * (127.0 / 60.0));
    if (width > 127)
    {
        width = 127;
    }
    else if (width < 0)
    {
        width = 0;
    }

    for (int j = 0; j < 128; j++)
    {
        int y = 63;
        if(width > j)
        {
            uint8_t pixels = 0b10000000;
            fb1024[(y / 8) * 128 + j] = pixels;
        }  
    }
}

void dwt_init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}