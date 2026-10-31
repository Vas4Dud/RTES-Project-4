/**
 ******************************************************************************
 * @file    audio.c
 * @brief   SAI-PDM capture front-end (circular DMA double-buffer). See audio.h.
 *          P4 "PS-16 PhonoScope" starter — the capture-spike scaffold.
 *
 *          SWEN 563 / CMPE 663 — P4.
 ******************************************************************************
 */
#include "audio.h"
#include "main.h"
#include <stdint.h>
#include <string.h>
#include "pdm2pcm.h"
#include "pdm2pcm_glo.h"
/* CubeMX names the SAI1 sub-block-A handle 'hsai_BlockA1' and its DMA handle
 * 'hdma_sai1_a' by default. If your generated Core/Src/main.c uses different
 * names, change them here. */
extern SAI_HandleTypeDef hsai_BlockA1;
extern DMA_HandleTypeDef hdma_sai1_a;

/* Raw PDM double-buffer. It is static and out of the RTOS heap (spec R17).
 * The DMA fills [0..HALF) then [HALF..2*HALF) circularly. The callbacks hand
 * off the half that the DMA filled. */
static uint8_t   s_pdm[2u * AUDIO_PDM_HALF_BYTES];
static osThreadId_t s_consumer;
static volatile uint32_t s_frames;
static volatile uint32_t s_overruns;
static volatile uint8_t  s_busy[2];        /* half filled, consumer not done    */
PDM2PCM_Handler_t filter;

void audio_set_consumer(osThreadId_t consumer) { s_consumer = consumer; }
void filter_init(void);

const uint8_t *audio_half(uint8_t which)
{
    return &s_pdm[(which & 1u) * AUDIO_PDM_HALF_BYTES];
}

/* ---- PROVIDED (Patch 1): the SAI and DMA configuration ------------------
 * The CubeMX PDM template cannot express the configuration that this board
 * needs. The template enables no SAI slot, so the SAI receives no data and
 * the DMA never starts. Its PDM clock is also too slow for the mic, and its
 * DMA is word-wide.
 *
 * This function overrides the parameters of the two CubeMX handles with the
 * values from the ST WB5MM-DK BSP (MX_SAI1_Block_A_Init and SAI_MspInit in
 * stm32wb5mm_dk_audio.c). CubeMX continues to own the GPIO, the DMA channel,
 * the link and the IRQs. Do not change this function. */
static int32_t sai_bsp_config(void)
{
    (void)HAL_SAI_DMAStop(&hsai_BlockA1);
    __HAL_SAI_DISABLE(&hsai_BlockA1);

    hsai_BlockA1.Instance            = SAI1_Block_A;
    hsai_BlockA1.Init.Protocol       = SAI_FREE_PROTOCOL;
    hsai_BlockA1.Init.AudioMode      = SAI_MODEMASTER_RX;
    hsai_BlockA1.Init.DataSize       = SAI_DATASIZE_32;
    hsai_BlockA1.Init.FirstBit       = SAI_FIRSTBIT_MSB;
    hsai_BlockA1.Init.ClockStrobing  = SAI_CLOCKSTROBING_FALLINGEDGE;
    hsai_BlockA1.Init.Synchro        = SAI_ASYNCHRONOUS;
    hsai_BlockA1.Init.OutputDrive    = SAI_OUTPUTDRIVE_DISABLE;
    hsai_BlockA1.Init.NoDivider      = SAI_MASTERDIVIDER_DISABLE;
    hsai_BlockA1.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
    hsai_BlockA1.Init.FIFOThreshold  = SAI_FIFOTHRESHOLD_EMPTY;
    hsai_BlockA1.Init.SynchroExt     = SAI_SYNCEXT_DISABLE;
    hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
    hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
    /* frame rate 160 kHz -> SCK 5.12 MHz -> CK2 = SCK/4 = 1.28 MHz (BSP:
     * AudioFrequency = PDM_FREQ*1000/FrameLength*4 = 1280000/32*4) */
    hsai_BlockA1.Init.AudioFrequency = 160000u;
    hsai_BlockA1.Init.PdmInit.Activation  = ENABLE;
    hsai_BlockA1.Init.PdmInit.MicPairsNbr = 2u;
    hsai_BlockA1.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK2_ENABLE;
    hsai_BlockA1.FrameInit.FrameLength       = 32u;
    hsai_BlockA1.FrameInit.ActiveFrameLength = 1u;
    hsai_BlockA1.FrameInit.FSDefinition      = SAI_FS_STARTFRAME;
    hsai_BlockA1.FrameInit.FSPolarity        = SAI_FS_ACTIVE_HIGH;
    hsai_BlockA1.FrameInit.FSOffset          = SAI_FS_FIRSTBIT;
    hsai_BlockA1.SlotInit.FirstBitOffset = 0u;
    hsai_BlockA1.SlotInit.SlotSize       = SAI_SLOTSIZE_DATASIZE;
    /* These two lines keep the BSP values. They are formally out-of-spec
     * (4x32 != FRL 32) but proven on this board. The hardware behaves as one
     * 32-bit slot in each frame, and the byte-wide DMA (below) selects the
     * microphone lane. Do not change them without a new test on hardware.
     * A formal reading of RM0434 Table 247 gives NBSLOT=1/0x1. */
    hsai_BlockA1.SlotInit.SlotNumber = 4u;
    hsai_BlockA1.SlotInit.SlotActive = 0x00000002u;
    if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK) return -1;

    /* The DMA is BYTE-wide on the peripheral side and on the memory side.
     * This is the lane filter. Each byte read pops one 32-bit FIFO word and
     * keeps the low byte, which is the pair-2 microphone lane. CubeMX
     * generates a WORD-wide DMA, so this code overrides it with the BSP
     * values. The __HAL_LINKDMA call in the CubeMX MSP stays in effect. */
    (void)HAL_DMA_DeInit(&hdma_sai1_a);
    hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_sai1_a.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_sai1_a.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_sai1_a.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_sai1_a.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_sai1_a.Init.Mode                = DMA_CIRCULAR;
    hdma_sai1_a.Init.Priority            = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_sai1_a) != HAL_OK) return -1;
    return 0;
}

int32_t audio_start(void)
{
    s_frames = 0u; s_overruns = 0u; s_busy[0] = 0u; s_busy[1] = 0u;
    if (sai_bsp_config() != 0) return -1;
    /* Receive the full 2*HALF buffer circularly. HAL raises HalfCplt at the
     * midpoint and Cplt at the end, then it wraps. The DMA is byte-wide, so
     * the transfer count is the byte count of the buffer. */
    return (HAL_SAI_Receive_DMA(&hsai_BlockA1, s_pdm, sizeof s_pdm) == HAL_OK)
               ? 0 : -1;
}

/* ---- DMA callbacks: O(1), notify only (spec R3) --------------------------*/
static void half_ready(uint8_t which)
{
    if (s_busy[which]) s_overruns++;       /* previous half not consumed        */
    s_busy[which] = 1u;
    s_frames++;
    if (s_consumer != NULL)
        osThreadFlagsSet(s_consumer, which ? AUDIO_FLAG_HALF1 : AUDIO_FLAG_HALF0);
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) half_ready(0u);
}
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) half_ready(1u);
}

/* The consumer marks a half done after it processes the half. This clears
 * the overrun flag. */
void audio_half_done(uint8_t which) { s_busy[which & 1u] = 0u; }

/* ---- MILESTONE 1: crude PDM-density level (no decimation) -----------------*/
/* Bytes in one density window: 40 bytes = 320 PDM bits = 0.25 ms. The window
 * is short, so it follows a tone of up to about 2 kHz. AUDIO_PDM_HALF_BYTES
 * (AUDIO_PCM_N * 10) is a multiple of 40 when AUDIO_PCM_N is a multiple of 4. */
#define SPIKE_WIN_BYTES  40u
#define SPIKE_GAIN       16u

uint8_t audio_spike_level(const uint8_t *pdm_half)
{
    /* In a PDM bitstream the local density of 1s tracks the instantaneous
     * amplitude (50% with no sound). A tone moves the density above and
     * below 50%, so the average over a full half stays near 50%. Thus count
     * the set bits in each short window, and add the distance of each count
     * from 50%.
     *
     * With no sound the result is about 128, and a tone moves it above 128.
     * This is NOT audio. It is a check that the mic is alive and hears the
     * tone, for the capture spike. (Patch 1) */
    const uint32_t nwin = AUDIO_PDM_HALF_BYTES / SPIKE_WIN_BYTES;
    const int32_t  mid  = (int32_t)(SPIKE_WIN_BYTES * 4u);   /* 50% of the bits */
    uint32_t acc = 0u;
    for (uint32_t w = 0u; w < nwin; w++) {
        int32_t ones = 0;
        const uint8_t *p = &pdm_half[w * SPIKE_WIN_BYTES];
        for (uint32_t i = 0u; i < SPIKE_WIN_BYTES; i++)
            ones += __builtin_popcount(p[i]);
        acc += (uint32_t)((ones > mid) ? (ones - mid) : (mid - ones));
    }
    uint32_t amp = acc * SPIKE_GAIN / nwin;
    return (uint8_t)(128u + ((amp > 127u) ? 127u : amp));
}

uint32_t audio_frames(void)   { return s_frames; }
uint32_t audio_overruns(void) { return s_overruns; }

/* ---- STUDENT TODO: PDM -> PCM decimation (PDM2PCM is pre-vendored) --------*/
int32_t audio_decimate(const uint8_t *pdm_half, int16_t *pcm_out)
{
    (void)pdm_half; (void)pcm_out;
    //int16_t pdm_decimate = (int16_t)(*pdm_half / AUDIO_DECIM);
    /* STUDENT TODO (spec R1): call the ST PDM2PCM library to decimate this PDM
     * half (÷80) into AUDIO_PCM_N int16 samples at 16 kHz. Init the filter one
     * time, then call PDM_Filter() one time for each 1 ms of audio.
     *
     * The provided constants for this board (from the ST WB5MM-DK BSP):
     *   - __HAL_RCC_CRC_CLK_ENABLE() before PDM_Filter_Init(), or the init fails
     *   - handler: PDM_FILTER_BIT_ORDER_LSB, PDM_FILTER_ENDIANNESS_LE,
     *     high_pass_tap 2122358088, in_ptr_channels 1, out_ptr_channels 1
     *   - config: PDM_FILTER_DEC_FACTOR_80, output_samples_number 16,
     *     mic_gain 24 (dB)
     *   - each call: 160 PDM bytes in, 16 int16 PCM samples out
     *
     * PDM2PCM ships pre-vendored (Middlewares/ST/STM32_Audio/PDM2PCM), and the
     * build connects it for you (spec R16). Your work is the init and filter
     * integration, not the library ceremony. Until you write this code, the
     * DSP task runs on the spike level. */
    return -1;
}

void filter_init(void)
{
    __HAL_RCC_CRC_CLK_ENABLE();
    filter.bit_order = PDM_FILTER_BIT_ORDER_LSB;
    filter.endianness = PDM_FILTER_ENDIANNESS_LE;
    filter.high_pass_tap = 2122358088;
    filter.in_ptr_channels = 1;
    filter.out_ptr_channels = 1;
    PDM_Filter_Init(&filter);
    
    PDM2PCM_Config_t config;
    config.decimation_factor = PDM_FILTER_DEC_FACTOR_80;
    config.output_samples_number = 16;
    config.mic_gain = 24;
    PDM_Filter_setConfig(&filter, &config);
}

