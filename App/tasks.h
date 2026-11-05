/**
 ******************************************************************************
 * @file    tasks.h
 * @brief   P4 "PS-16 PhonoScope" application entry points.
 *
 *          SWEN 563 / CMPE 663 — P4.
 *
 *  Two calls go into the CubeMX-generated main.c (see the README touchpoints):
 *    app_post()   — before the scheduler starts, this call initializes OLED,
 *                   console, RGB, DSP and perf. It prints Fs, N, the bin
 *                   width and the deadline. The UI task starts the audio DMA
 *                   after this call, so the meter reads the idle baseline
 *                   while the board is quiet.
 *    app_start()  — this call makes the UI task. The UI task calibrates the
 *                   idle baseline, then makes the DSP and Console tasks and
 *                   starts SAI capture.
 ******************************************************************************
 */
#ifndef TASKS_H
#define TASKS_H

void app_post(void);
void app_start(void);
void fillup_oled(const uint8_t* spectrum, uint8_t *fb1024);

#endif /* TASKS_H */
