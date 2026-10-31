# P4 — PS-16 "PhonoScope" · Digital Stethoscope / Auscultation Analyzer — Starter

SWEN 563 / CMPE 663 / EEEE 663 — FreeRTOS + **CMSIS-DSP** project (STM32CubeMX HAL,
CMake + Ninja + VS Code). Spec: [`../RTES_Project4_Spec.md`](../RTES_Project4_Spec.md).

> **P4 starts from P3.**
> The FreeRTOS + CMSIS-RTOS2 + CMake scaffold, the OLED / console / RGB / idle-hook modules, and the **CMSIS-DSP library (vendored in P3)** come from the P3 solution without change.
> The new material in P4 is the **audio front end**.
> **SAI1 + circular DMA** captures the PDM microphone, PDM2PCM decimates the bitstream to PCM, and the DSP task calculates an FFT spectrum.
> The `.ioc` here is the P3 peripheral set with a new name, and you add SAI1 in CubeMX (§0).
> Make the capture spike (§0.2) operate first, because the SAI-PDM path is the primary risk of the project (spec §6.1).

---

## 0. One-time setup: add SAI1 (PDM) + DMA in CubeMX

Open `P4_PhonoScope.ioc`.
In P3, CubeMX configured FreeRTOS, SPI1 (OLED/RGB), USART1 (console), and the TIM17 HAL timebase.
Add the mic:

**A. SAI1 — PDM receive**

1. In *Multimedia → SAI1*, enable **sub-block A** as the PDM master receiver (SAI in
   **PDM** mode). The WB55 SAI has a hardware PDM front-end, so the SPI/I2S capture
   method is not necessary on this board.

2. Connect the mic lines: **CK2 = PA8** (clock out to the mic), **DI2 = PA9** (data in).
   These are the UM2825-verified board pins for the IMP34DT05.

3. Set the PDM bit-clock to **~2.048 MHz** (the mic accepts 1.2–3.25 MHz). With the
   PDM2PCM ÷128 decimation, this gives **16 kHz** PCM. The `.ioc` carries the PLLSAI1
   values (`PLLSAI1N=24`, `SAI1Freq=48 MHz`) from P3 — make sure that the clock tree
   gives a 2.048 MHz PDM clock.

**B. DMA — circular**

4. In the DMA settings of SAI1, add the **SAI1_A** request in **Circular** mode.
   Enable the SAI DMA IRQ. (The `.ioc` has `ForceEnableDMAVector` set.)

**C. FreeRTOS heap** (more space for the audio/FFT buffers)

5. *Middleware → FREERTOS → Config* → `configTOTAL_HEAP_SIZE` = **16384**.

*(Optional cleanup: the `.ioc` keeps the P3 QUADSPI / I2C3 / TSC configuration, and a
stethoscope does not use those peripherals. You can remove them in CubeMX, but removal
is not necessary. If they stay, the generated code initializes peripherals that P4
does not use.)*

Then **Generate Code**.

## 0.1 Generated-file touchpoints — carried from P3

These touchpoints come from the P3 solution, and they sit in `USER CODE` fences.
They are the two `main.c` calls (`#include "tasks.h"`, `app_post()`, `app_start()`) and the `app_freertos.c` idle-hook line (`g_idle_count++`).
CubeMX keeps them across the regeneration.
After you generate, make sure that they are in the files.

**One check:** `audio.c` has an extern for the SAI handle with the name **`hsai_BlockA1`**.
If CubeMX gave the handle a different name in your `main.c`, change the extern name so that the two names agree.

## 0.2 Build, then the CAPTURE SPIKE (milestone 1)

```
cmake --preset Debug && cmake --build --preset Debug
```

Flash the board and open the VCP at **115200 8N1**.
The starter runs the **capture spike**.
The spike path is SAI-PDM → DMA → the DSP task.
The DSP task calculates a coarse **PDM bit-density level** for each half (no FFT at this step).
The OLED and the console show the level.

**Play a tone at the mic — the `level` value must move, and the `frames` count must increase.**
This test proves the SAI-PDM path before you write the DSP code.
Type `STAT` for Fs/N/bin/deadline/frames/overruns/level/CPU.

If `frames` stays 0, or if `AUDIO START FAILED` prints, the SAI-PDM configuration is incorrect.
Iterate on §0.A until the spike operates.
This is expected. Capture is the part of P4 with the most risk.

---

## 1. What is in the box

**Provided (carried from P3 — use without change):**

- `App/oled.c` (+ new `oled_blit`), `console.c`, `rgb_led.c`, `perf_mon.c`.

- **`cmd_table.c`, the P3 command-table console dispatcher, carried. The console task
  routes `GAIN`/`BAND`/`FFT`/`WIN`/`STAT`/`RUN` through it — you write the
  handlers.**

- The FreeRTOS/CubeMX/CMake scaffold.

- The **CMSIS-DSP** library in `Drivers/CMSIS/DSP/`, **wired** in `CMakeLists.txt`
  (include + `libarm_cortexM4lf_math.a` + `ARM_MATH_CM4`/`__FPU_PRESENT`). P3
  vendored this library without the build wiring — P4 wires it.

- **PDM2PCM, pre-vendored** in `Middlewares/ST/STM32_Audio/PDM2PCM/` (from
  STM32CubeWB, with `libPDMFilter_CM4_GCC_wc32.a` + LICENSE) and wired into the
  build. The library ceremony is complete. The init/filter *integration*
  (`audio_decimate()`) is your work.

**Assets:** the **instructor clip pack** (S1/S2, murmur, wheeze, crackle — PHI-free,
known ground truth) goes on myCourses before the P4 window opens.
Download the clips to `Assets/` (see `Assets/README.md`).
Do not commit audio.

**Provided scaffold (complete for the spike, extend it for the full analyzer):**

| File | Role |
|---|---|
| `App/audio.c` | SAI-PDM circular-DMA capture, half/full callbacks → DSP-task notify, the spike level, overrun counting. `audio_decimate()` (PDM→PCM) is the **student TODO** (PDM2PCM is pre-vendored and wired — you write the init/filter calls). |
| `App/dsp.c` | The per-frame CMSIS-DSP pipeline: q15→f32, RMS/peak VU, Hann window, `arm_rfft_fast_f32`, `arm_cmplx_mag_f32`. The **band-fold to OLED columns** (R10) is the student TODO. |
| `App/tasks.c` | The 3 tasks (DSP High / UI Normal / Console BelowNormal) + idle hook + `app_post`/`app_start`. Runs the spike today; grow it into the analyzer. |

**Student TODO (the project):**

- Write `audio_decimate()` against the pre-vendored **PDM2PCM**.
- Switch the DSP task to `audio_decimate()` + `dsp_process()`.
- Complete the band-fold, and draw the spectrum with `oled_blit()`.
- Complete the stubbed `fft`/`band`/`gain`/`win`/`run` command handlers
  (`cmd_table.c` provides the dispatch) and the HEART/LUNG modes.
- (663) Write the schedulability/WCET/jitter analysis.

---

## 2. What changed from P3 → P4

| Area | P3 (AmbuLog) | P4 (PhonoScope) |
|---|---|---|
| **Data source** | I²C sensors @ ≤12 Hz | **PDM mic @ 16 kHz** via SAI1 + circular DMA |
| **Sampler** | DRDY interrupt / 1 Hz task | the **DMA** is the sampler; half/full callbacks notify the DSP task |
| **Compute** | posture/threshold logic | **CMSIS-DSP FFT** per frame — the hard-deadline (WCET < N/Fs) workload |
| **New peripheral** | QUADSPI flash | **SAI1 (PDM) + DMA** |
| **New library** | — | **CMSIS-DSP wired** (was vendored, un-wired in P3); **PDM2PCM pre-vendored + wired** |
| **Display** | text pages | text **+ `oled_blit` full-frame** spectrum/VU |
| **Removed** | — | flash/sensor component drivers, `log_store`, `flash_guard`, `vitals_bus` |
| **Heap** | 12288 | **16384** (audio/FFT buffers are static, on top) |

You know the content of the "carried" rows from P3.
The new material is SAI-PDM capture, DMA double-buffering, and the CMSIS-DSP FFT under a hard deadline.
Lectures **L16 (DSP/FFT/double-buffering)** and **L18 (timing/WCET/jitter)** give this new material.

## 3. Notes

- **Static buffers.** The FFT buffers in `dsp.c` and the PDM buffer in `audio.c` are
  `static` — never on the RTOS heap or a task stack (spec R17).

- **`rgb_set()` blocks for ~11 ms.** Call it from the UI task only, on a zone change,
  and never from the DSP task.

- **PDM2PCM** ships **pre-vendored** in this starter
  (`Middlewares/ST/STM32_Audio/PDM2PCM`, precompiled for each core/toolchain, with
  LICENSE), and `CMakeLists.txt` wires it into the build. Keep it wired (spec R16).

- **The deadline is the lesson.** At N=256/Fs=16 kHz a frame is 16 ms, and the DSP work is sub-millisecond.
  The margin is large on purpose.
  The real-time lesson (L18 and the graduate requirement) is jitter and interference, and the *continuous PDM2PCM decimation* is the dominant load, not the FFT.
