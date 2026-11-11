/**
 ******************************************************************************
 * @file    audio.h
 * @brief   SAI-PDM microphone capture (circular DMA double-buffer) — P4
 *          "PS-16 PhonoScope" starter.
 *
 *          SWEN 563 / CMPE 663 — P4.
 *
 *  The capture front-end (spec R1-R4). The onboard IMP34DT05 PDM mic is on
 *  SAI1 (PA8 = SAI1_CK2 clock out, PA9 = SAI1_DI2 data in). CubeMX owns
 *  MX_SAI1_Init() and MX_DMA_Init(), but the CubeMX PDM template cannot
 *  express the configuration that this board needs. Thus audio_start() sets
 *  the SAI and DMA parameters again with the ST BSP values (Patch 1), then
 *  starts the circular DMA. This module also services the half-transfer and
 *  full-transfer callbacks.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │ MILESTONE 1 — THE CAPTURE SPIKE (do this FIRST, before any FFT).       │
 *  │ The SAI-PDM path is unproven on this board (spec §6.1). Prove that     │
 *  │ capture works before you build the DSP half. Start the DMA. In the     │
 *  │ callback, calculate a crude PDM bit-density level (audio_spike_level). │
 *  │ A tone at the mic must move it. Print it over the VCP. Only then       │
 *  │ connect PDM2PCM and the FFT.                                           │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  Sizing: these values agree with the SAI configuration in audio_start()
 *  and with the PDM2PCM configuration.
 *    SAI frame rate 160 kHz, SCK 5.12 MHz, PDM clock CK2 = SCK/4 = 1.28 MHz.
 *    PCM output = Fs 16 kHz, 16-bit, from the 1.28 MHz PDM clock ÷80.
 *    The byte-wide DMA keeps one byte (8 PDM bits of the mic) from each
 *    32-bit SAI word. One PCM frame of AUDIO_PCM_N samples needs
 *    AUDIO_PCM_N*80 PDM bits, which is AUDIO_PCM_N*10 bytes in each half.
 ******************************************************************************
 */
#ifndef AUDIO_H
#define AUDIO_H

#include "cmsis_os2.h"
#include <stdint.h>

/* Starter default: N=256 PCM samples in a frame (62.5 Hz bins) keeps the PDM
 * buffer modest for the spike. Increase it to 512 or 1024 when capture and
 * the FFT are proven. */
#define AUDIO_PCM_N           256
#define AUDIO_DECIM           80u                          /* PDM2PCM ÷80         */
#define AUDIO_PDM_HALF_BYTES  (AUDIO_PCM_N * AUDIO_DECIM / 8u)   /* per half        */

/* thread flags the DMA callbacks raise on the consumer (DSP) task */
#define AUDIO_FLAG_HALF0  0x01u
#define AUDIO_FLAG_HALF1  0x02u

/** Register the task woken on each ready half (the DSP task). */
void audio_set_consumer(osThreadId_t consumer);

/** Set the SAI and DMA parameters to the BSP values, then start circular
 *  SAI-PDM DMA capture. Precondition: MX_SAI1_Init() and MX_DMA_Init() ran,
 *  and audio_set_consumer() set the consumer. @retval 0 on HAL_OK, -1 on any
 *  other result. */
int32_t audio_start(void);

/** Pointer to the raw PDM bytes of the half that the DMA filled (0 or 1). */
const uint8_t *audio_half(uint8_t which);

/** The consumer calls this after it processes a half. Postcondition: the
 *  overrun flag for that half is clear. */
void audio_half_done(uint8_t which);

/** MILESTONE 1: crude amplitude of a raw PDM half from the 1-bit density in
 *  short windows, with no decimation. It proves capture. It returns 128..255.
 *  About 128 means no sound, and a tone gives a larger value. */
uint8_t audio_spike_level(const uint8_t *pdm_half);

/** Frames captured, and overruns. An overrun is a half that the DMA re-filled
 *  before the consumer read it. */
uint32_t audio_frames(void);
uint32_t audio_overruns(void);

/** STUDENT TODO (spec R1): decimate one PDM half to AUDIO_PCM_N int16 PCM
 *  through the pre-vendored ST PDM2PCM library
 *  (Middlewares/ST/STM32_Audio/PDM2PCM, which CMakeLists connects for you).
 *  @retval 0 on success, -1 while the function is a stub. */
int32_t audio_decimate(const uint8_t *pdm_half, int16_t *pcm_out);
void filter_init(void);
void audio_setN(uint32_t value);

#endif /* AUDIO_H */
