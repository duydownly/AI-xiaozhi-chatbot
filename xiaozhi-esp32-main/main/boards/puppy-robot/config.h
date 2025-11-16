#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* =======================
   Power Management
   ======================= */
#define POWER_CHARGE_DETECT_PIN GPIO_NUM_21
#define POWER_ADC_UNIT          ADC_UNIT_2
#define POWER_ADC_CHANNEL       ADC_CHANNEL_3

/* =======================
   Servo Control (5 servo)
   ======================= */
#define LEFT_LEG_PIN      GPIO_NUM_17
#define RIGHT_LEG_PIN     GPIO_NUM_39
#define LEFT_FOOT_PIN     GPIO_NUM_18
#define RIGHT_FOOT_PIN    GPIO_NUM_38
#define TAIL_SERVO_PIN    GPIO_NUM_8   // Servo đuôi

// Không có tay → disable hoàn toàn
#define LEFT_HAND_PIN     -1
#define RIGHT_HAND_PIN    -1

/* =======================
   Audio (loa + mic) – XiaoZhi cần
   ======================= */
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

/* =======================
   Display ST7789 (LCD 1.3"/1.28")
   ======================= */
#define DISPLAY_BACKLIGHT_PIN        GPIO_NUM_3
#define DISPLAY_MOSI_PIN             GPIO_NUM_10
#define DISPLAY_CLK_PIN              GPIO_NUM_9
#define DISPLAY_DC_PIN               GPIO_NUM_46
#define DISPLAY_RST_PIN              GPIO_NUM_11
#define DISPLAY_CS_PIN               GPIO_NUM_12

#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER      LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_SWAP_XY        false
#define DISPLAY_MIRROR_X       false
#define DISPLAY_MIRROR_Y       false
#define DISPLAY_OFFSET_X       0
#define DISPLAY_OFFSET_Y       0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE       3

/* =======================
   Boot Button
   ======================= */
#define BOOT_BUTTON_GPIO GPIO_NUM_0

/* =======================
   Version
   ======================= */
#define PUPPY_ROBOT_VERSION "2.0.5"

#endif
