/**
 * Copyright 2024 Protocentral Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SH8601 AMOLED display driver.
 */
#ifndef ZEPHYR_DRIVERS_DISPLAY_DISPLAY_SH8601_H_
#define ZEPHYR_DRIVERS_DISPLAY_DISPLAY_SH8601_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

/*Configuration data struct.*/
struct sh8601_config
{
  struct mipi_dbi_config dbi_config;
  struct spi_dt_spec spi;
  struct gpio_dt_spec reset;
  uint8_t pixel_format;
  uint16_t rotation;
  uint16_t x_resolution;
  uint16_t y_resolution;
  bool inversion;
};

#define SH8601_MAXWIDTH 480  ///< SH8601 max TFT width
#define SH8601_MAXHEIGHT 480 ///< SH8601 max TFT width

#define SH8601_RST_DELAY 250    ///< delay ms wait for reset finish
#define SH8601_SLPIN_DELAY 120  ///< delay ms wait for sleep in finish
#define SH8601_SLPOUT_DELAY 120 ///< delay ms wait for sleep out finish

// User Command
#define SH8601_C_NOP 0x00          // nop
#define SH8601_C_SWRESET 0x01      // Software Reset
#define SH8601_R_RDID 0x04         // Read Display Identification Information ID/1/2/3
#define SH8601_R_RDNERRORSDSI 0x05 // Read Number of Errors on DSI
#define SH8601_R_RDPOWERMODE 0x0A  // Read Display Power Mode
#define SH8601_R_RDMADCTL 0x0B     // Read Display MADCTL
#define SH8601_R_RDPIXFMT 0x0C     // Read Display Pixel Format
#define SH8601_R_RDIMGFMT 0x0D     // Read Display Image Mode
#define SH8601_R_RDSIGMODE 0x0E    // Read Display Signal Mode
#define SH8601_R_RDSELFDIAG 0x0F   // Read Display Self-Diagnostic Result

#define SH8601_C_SLPIN 0x10  // Sleep In
#define SH8601_C_SLPOUT 0x11 // Sleep Out
#define SH8601_C_PTLON 0x12  // Partial Display On
#define SH8601_C_NORON 0x13  // Normal Display mode on

#define SH8601_C_INVOFF 0x20  // Inversion Off
#define SH8601_C_INVON 0x21   // Inversion On
#define SH8601_C_ALLPOFF 0x22 // All pixels off
#define SH8601_C_ALLPON 0x23  // All pixels on
#define SH8601_C_DISPOFF 0x28 // Display off
#define SH8601_C_DISPON 0x29  // Display on
#define SH8601_W_CASET 0x2A   // Column Address Set
#define SH8601_W_PASET 0x2B   // Page Address Set
#define SH8601_W_RAMWR 0x2C   // Memory Write Start

#define SH8601_W_PTLAR 0x30   // Partial Area Row Set
#define SH8601_W_PTLAC 0x31   // Partial Area Column Set
#define SH8601_C_TEAROFF 0x34 // Tearing effect off
#define SH8601_WC_TEARON 0x35 // Tearing effect on
#define SH8601_W_MADCTL 0x36  // Memory data access control
#define SH8601_C_IDLEOFF 0x38 // Idle Mode Off
#define SH8601_C_IDLEON 0x39  // Idle Mode On
#define SH8601_W_PIXFMT 0x3A  // Write Display Pixel Format
#define SH8601_W_WRMC 0x3C    // Memory Write Continue

#define SH8601_W_SETTSL 0x44             // Write Tearing Effect Scan Line
#define SH8601_R_GETSL 0x45              // Read Scan Line Number
#define SH8601_C_SPIROFF 0x46            // SPI read Off
#define SH8601_C_SPIRON 0x47             // SPI read On
#define SH8601_C_AODMOFF 0x48            // AOD Mode Off
#define SH8601_C_AODMON 0x49             // AOD Mode On
#define SH8601_W_WDBRIGHTNESSVALAOD 0x4A // Write Display Brightness Value in AOD Mode
#define SH8601_R_RDBRIGHTNESSVALAOD 0x4B // Read Display Brightness Value in AOD Mode
#define SH8601_W_DEEPSTMODE 0x4F         // Deep Standby Mode On

#define SH8601_W_WDBRIGHTNESSVALNOR 0x51 // Write Display Brightness Value in Normal Mode
#define SH8601_R_RDBRIGHTNESSVALNOR 0x52 // Read display brightness value in Normal Mode
#define SH8601_W_WCTRLD1 0x53            // Write CTRL Display1
#define SH8601_R_RCTRLD1 0x54            // Read CTRL Display1
#define SH8601_W_WCTRLD2 0x55            // Write CTRL Display2
#define SH8601_R_RCTRLD2 0x56            // Read CTRL Display2
#define SH8601_W_WCE 0x58                // Write CE
#define SH8601_R_RCE 0x59                // Read CE

#define SH8601_W_WDBRIGHTNESSVALHBM 0x63 // Write Display Brightness Value in HBM Mode
#define SH8601_R_WDBRIGHTNESSVALHBM 0x64 // Read Display Brightness Value in HBM Mode
#define SH8601_W_WHBMCTL 0x66            // Write HBM Control

#define SH8601_R_RDDBSTART 0xA1         // Read DDB start
#define SH8601_R_DDBCONTINUE 0xA8       // Read DDB Continue
#define SH8601_R_RFIRCHECKSUN 0xAA      // Read First Checksum
#define SH8601_R_RCONTINUECHECKSUN 0xAF // Read Continue Checksum

#define SH8601_W_SPIMODECTL 0xC4 // SPI mode control

#define SH8601_R_RDID1 0xDA // Read ID1
#define SH8601_R_RDID2 0xDB // Read ID2
#define SH8601_R_RDID3 0xDC // Read ID3

// Flip
#define SH8601_MADCTL_X_AXIS_FLIP 0x40 // X-axis Flip
#define SH8601_MADCTL_Y_AXIS_FLIP 0xC0 // Y-axis Flip

// Color Order
#define SH8601_MADCTL_RGB 0x00                      // Red-Green-Blue pixel order
#define SH8601_MADCTL_BGR 0x08                      // Blue-Green-Red pixel order
#define SH8601_MADCTL_COLOR_ORDER SH8601_MADCTL_RGB // RGB

enum
{
  SH8601_ContrastOff = 0,
  SH8601_LowContrast,
  SH8601_MediumContrast,
  SH8601_HighContrast
};

/*
static const uint8_t sh8601_regs[] =
    {
        BEGIN_WRITE,

        WRITE_COMMAND_8, SH8601_C_SLPOUT,

        END_WRITE,

        DELAY, SH8601_SLPOUT_DELAY,

        BEGIN_WRITE,

        // WRITE_C8_D8, SH8601_WC_TEARON, 0x00,

        WRITE_COMMAND_8, SH8601_C_NORON,

        WRITE_COMMAND_8, SH8601_C_INVOFF,

        // WRITE_COMMAND_8, SH8601_C_ALLPON,

        // WRITE_C8_D8, SH8601_W_MADCTL, SH8601_MADCTL_COLOR_ORDER, // RGB/BGR

        WRITE_C8_D8, SH8601_W_PIXFMT, 0x05, // Interface Pixel Format 16bit/pixel
        // WRITE_C8_D8, SH8601_W_PIXFMT, 0x06, // Interface Pixel Format 18bit/pixel
        // WRITE_C8_D8, SH8601_W_PIXFMT, 0x07, // Interface Pixel Format 24bit/pixel

        WRITE_COMMAND_8, SH8601_C_DISPON,

        // WRITE_COMMAND_8, SH8601_W_WDBRIGHTNESSVALNOR,
        // WRITE_BYTES, 2,
        // 0x03, 0xFF,

        WRITE_C8_D8, SH8601_W_WCTRLD1, 0x28, // Brightness Control On and Display Dimming On

        WRITE_C8_D8, SH8601_W_WDBRIGHTNESSVALNOR, 0x00, // Brightness adjustment

        // High contrast mode (Sunlight Readability Enhancement)
        WRITE_C8_D8, SH8601_W_WCE, 0x00, // Off
        // WRITE_C8_D8, SH8601_W_WCE, 0x05, // On Low
        // WRITE_C8_D8, SH8601_W_WCE, 0x06, // On Medium
        // WRITE_C8_D8, SH8601_W_WCE, 0x07, // On High

        END_WRITE,

        DELAY, 10}
};
*/
int sh8601_transmit_cmd(const struct device *dev, uint8_t cmd,
                     const void *tx_data, size_t tx_len);

#endif
