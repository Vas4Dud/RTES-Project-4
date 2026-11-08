/**
 ******************************************************************************
 * @file    dsp.c
 * @brief   CMSIS-DSP frame pipeline (window -> rFFT -> mag -> VU). P4
 *          starter. The starter gives the correct call sequence. The
 *          band-fold is a STUDENT TODO.
 *
 *          SWEN 563 / CMPE 663 — P4 "PS-16 PhonoScope".
 ******************************************************************************
 */

#include "arm_math.h"          /* CMSIS-DSP — wired in CMakeLists (R16)         */
#include <math.h>
#include <stdint.h>
#include "tasks.h"
#include "dsp.h"
#include "audio.h"
/* N is the number of samples in a frame. The starter uses 256, and you can
 * raise it. */
#define N  AUDIO_PCM_N

void set_frequency(float32_t low, float32_t high);
/* All DSP buffers are static. None is on the RTOS heap or a task stack
 * (spec R17). */
static arm_rfft_fast_instance_f32 s_rfft;
static float32_t s_win[N];             /* precomputed Hann window               */
static float32_t s_frame[N];           /* PCM -> f32                            */
static float32_t s_windowed[N];        /* frame * window                        */
static float32_t s_fft[N];             /* rFFT packed output                    */
static float32_t s_mag[N / 2u];        /* magnitude bins                        */

static uint8_t   s_spectrum[DSP_SPECTRUM_COLS];
static volatile float   s_level_dbfs = -120.0f, s_peak_dbfs = -120.0f;
static volatile uint8_t s_clip;

static volatile float32_t high_freq = 8000;
static volatile float32_t low_freq = 0;

void dsp_init(void)
{
    (void)arm_rfft_fast_init_f32(&s_rfft, N);
    for (uint32_t i = 0u; i < N; i++)
        s_win[i] = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)(N - 1u)));
}

void dsp_process(const int16_t *pcm)
{
    /* int16 PCM -> normalized f32 in [-1, 1). */
    arm_q15_to_float((q15_t *)pcm, s_frame, N);

    /* VU: RMS and abs-peak give dBFS and the clipping flag (spec R7). This
     * CMSIS-DSP build has no arm_absmax_f32, so take max(|max|, |min|). */
    float32_t rms = 0.0f, vmax = 0.0f, vmin = 0.0f;
    uint32_t idx = 0u;
    arm_rms_f32(s_frame, N, &rms);
    arm_max_f32(s_frame, N, &vmax, &idx);
    arm_min_f32(s_frame, N, &vmin, &idx);
    float32_t peak = (vmax > -vmin) ? vmax : -vmin;
    s_level_dbfs = 20.0f * log10f(rms + 1e-9f);
    s_peak_dbfs  = 20.0f * log10f(peak + 1e-9f);
    s_clip = (peak > 0.98f) ? 1u : 0u;

    /* Hann window -> rFFT -> magnitude (spec R5/R6). */
    arm_mult_f32(s_frame, s_win, s_windowed, N);
    arm_rfft_fast_f32(&s_rfft, s_windowed, s_fft, 0);
    s_fft[1] = 0.0f;                     /* rFFT packs Nyquist into bin[1] — drop */
    arm_cmplx_mag_f32(s_fft, s_mag, N / 2u);

    /* STUDENT TODO (spec R9/R10): fold s_mag[0..N/2) into s_spectrum[128] as
     * log-dB bar heights (0..DSP_BAR_MAX_PX). The fold is peak-preserving,
     * which is the maximum of each column group, and it must honor the
     * selected band and zoom. Map −60..0 dBFS -> 0..47 px. The starter keeps
     * the bars at 0. */
    
    float32_t hz_per_bin = 16000.0f / N;
    float32_t upper_bin = high_freq / hz_per_bin;
    float32_t lower_bin = low_freq / hz_per_bin;

    float32_t total_columns = (upper_bin - lower_bin) / DSP_SPECTRUM_COLS;
    float32_t offset = (20.0f * log10f( N/4.0f ));

    float32_t position = lower_bin;
    for (uint32_t c = 0u; c < DSP_SPECTRUM_COLS; c++)
    {
        float32_t step = position + total_columns;
        
        uint32_t start = (uint32_t) position;
        uint32_t end = (uint32_t) step;
        if (end > (N / 2) - 1)
        {
            end = (N / 2) - 1; 
        }
        float32_t largest = 0;
        for (int i = start; i <= end; i++)
        {
            if (s_mag[i] > largest)
            {
                largest = s_mag[i];
            }
        }
        position = step;

        float32_t db = (20.0f * log10f(largest + 1e-9f));
        db -= offset;
        if (db < -60.0f)
        {
            db = -60.0f;
        }
        else if (db > 0.0)
        {
            db = 0.0;
        }
        float32_t pixel = (db + 60.0f) * 47.0f / 60.0f;
        s_spectrum[c] = (uint8_t) pixel;
    }
}

void set_frequency(float32_t low, float32_t high)
{
    high_freq = high;
    low_freq = low;
}

float          dsp_level_dbfs(void) { return s_level_dbfs; }
float          dsp_peak_dbfs(void)  { return s_peak_dbfs; }
uint8_t        dsp_clip(void)       { return s_clip; }
const uint8_t *dsp_spectrum(void)   { return s_spectrum; }
