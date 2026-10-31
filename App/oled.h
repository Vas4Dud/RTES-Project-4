/**
 ******************************************************************************
 * @file    oled.h
 * @brief   Text API for the on-board 0.96" 128x64 OLED (SSD1315, SPI1).
 *          This is the HAL port of the course driver, provided.
 *
 *          SWEN 563 / CMPE 663 — P2 "DG-30 DecuGuard" starter.
 *          PROVIDED DRIVER — do not change it. Read it to see how a raw-SPI
 *          register driver from P0/P1 becomes a HAL driver. The SSD1315
 *          command stream is the same, but the code calls
 *          HAL_SPI_Transmit() and HAL_GPIO_WritePin() against the
 *          CubeMX-generated hspi1, and not register writes.
 *
 *  Ownership: CubeMX configures SPI1 and the PH0/PC8/PC9 control pins
 *  (MX_SPI1_Init / MX_GPIO_Init). This driver only USES them, and it
 *  initializes no hardware. That division, where CubeMX owns init and the
 *  code owns behavior, is the full point of requirement R24.
 *
 *  Text grid: 8 rows (0 = top) x 21 columns, 5x7 font in a 6x8 cell.
 *  Cost: about 170 µs for each line at 8 MHz SPI. Main loop only, and not
 *  from an ISR.
 *
 *  Usage:
 *      oled_init();                          // after MX_SPI1_Init()
 *      oled_clear();
 *      oled_printf(2, "HOB %3d.%d deg", a / 10, a % 10);
 *      // careful: that idiom mangles NEGATIVE tenths (lateral tilt,
 *      // GR-A). Print the sign, then abs()/10 and abs()%10.
 ******************************************************************************
 */
#ifndef OLED_H
#define OLED_H

#include <stdint.h>

#define OLED_ROWS 8u   /* text rows, 0 = top */
#define OLED_COLS 21u  /* characters in a row */

/** Panel reset and init sequence. Call one time, after MX_SPI1_Init(). It
 *  takes about 120 ms. */
void oled_init(void);

/** Clear the full display. */
void oled_clear(void);

/** Write one full 21-column text row (0..7). The driver pads a short string. */
void oled_write_line(uint8_t row, const char *text);

/** printf-style wrapper around oled_write_line(). */
void oled_printf(uint8_t row, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** P4 addition: blit a full 1024-byte framebuffer (8 pages x 128 columns,
 *  page-major). Each byte holds 8 vertical pixels of one column. This is the
 *  raw primitive that the spectrum/VU renderer draws into. It is one
 *  full-frame SPI transfer of about 1-2 ms. UI task only, and not from the
 *  DSP task or an ISR. */
void oled_blit(const uint8_t *fb1024);

#endif /* OLED_H */
