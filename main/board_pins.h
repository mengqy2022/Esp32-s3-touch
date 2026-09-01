#pragma once

// Pin map derived from the supplied E32R28T / ESP32-WROOM-32E schematic.
// LCD (ILI9341, 4-wire SPI)
#define PIN_LCD_SCK        14
#define PIN_LCD_MOSI       13
#define PIN_LCD_MISO       12
#define PIN_LCD_CS         15
#define PIN_LCD_DC          2
#define PIN_LCD_BL         21
// LCD reset is tied to the board RESET net, not to a dedicated GPIO.

// Resistive touch controller (XPT2046) - software SPI
#define PIN_TP_SCK         25
#define PIN_TP_MOSI        32
#define PIN_TP_MISO        39
#define PIN_TP_CS          33
#define PIN_TP_IRQ         36

// MicroSD (SPI)
#define PIN_SD_SCK         18
#define PIN_SD_MISO        19
#define PIN_SD_MOSI        23
#define PIN_SD_CS           5

// On-board common-anode RGB LED: active LOW
#define PIN_LED_R          17
#define PIN_LED_G          22
#define PIN_LED_B          16

#define LCD_H_RES         320
#define LCD_V_RES         240
