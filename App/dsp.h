/**
 ******************************************************************************
 * @file    dsp.h
 * @brief   Frame DSP pipeline: window -> rFFT -> magnitude -> VU and
 *          band-fold (CMSIS-DSP). P4 "PS-16 PhonoScope" starter.
 *
 *          SWEN 563 / CMPE 663 — P4.
 *
 *  This is the deadline-bearing work (spec R5-R8). It must finish in one
 *  frame period T = N/Fs. The buffers are static (spec R17). The starter
 *  gives the correct CMSIS-DSP call sequence. The band-fold to OLED columns
 *  (R10) is the work of the student.
 ******************************************************************************
 */
#ifndef DSP_H
#define DSP_H

#include <stdint.h>

#define DSP_SPECTRUM_COLS 128u    /* OLED columns                              */
#define DSP_BAR_MAX_PX    47u     /* pages 1..6 => 48 px of vertical range     */

/** Init the FFT instance and precompute the Hann window. Call one time,
 *  before the scheduler starts. */
void dsp_init(void);

/** Run the pipeline on one PCM frame (AUDIO_PCM_N int16 samples). It gives
 *  the VU values (RMS and peak dBFS) and folds the windowed magnitude
 *  spectrum into the column bar heights. It runs in the DSP task, and it
 *  must obey WCET < frame period. */
void dsp_process(const int16_t *pcm);

/** Latest results. The UI task reads them (single-writer, one-reader
 *  snapshot). */
float   dsp_level_dbfs(void);
float   dsp_peak_dbfs(void);
uint8_t dsp_clip(void);
/** Column bar heights, 0..DSP_BAR_MAX_PX, DSP_SPECTRUM_COLS entries. */
const uint8_t *dsp_spectrum(void);
// clinical flags
uint8_t dsp_murmur(void);
uint8_t dsp_wheeze(void);
uint8_t dsp_rhonchi(void);
uint8_t dsp_crackle(void);
void set_frequency(float32_t low, float32_t high);
void dsp_setN(uint32_t value);
void set_window(uint8_t type);

#endif /* DSP_H */
