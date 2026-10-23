#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// NUBO "Osito" (prototipo v0) sobre Seeed XIAO ESP32S3 Sense.
// Pinout: docs/02-hardware.md del proyecto NUBO.

// Audio: micrófono PDM de la placa Sense + amplificador MAX98357A por I2S
#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_42   // PDM CLK (Sense)
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_41   // PDM DATA (Sense)
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_43  // D6
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_44  // D7
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_8   // D9

// Pantalla redonda GC9A01 240x240. CS va a GND y RST a una red RC.
#define DISPLAY_SPI_SCLK_PIN GPIO_NUM_7  // D8
#define DISPLAY_SPI_MOSI_PIN GPIO_NUM_9  // D10
#define DISPLAY_SPI_DC_PIN GPIO_NUM_3    // D2
#define DISPLAY_SPI_CS_PIN GPIO_NUM_NC
#define DISPLAY_SPI_RESET_PIN GPIO_NUM_NC
#define DISPLAY_SPI_SCLK_HZ (40 * 1000 * 1000)

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_4  // D3
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
// Si la cara sale espejada o de cabeza, cambia estos valores
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

// Bus I2C de sensores: IMU LSM6DS3TR-C y medidor de batería MAX17048
#define SENSOR_I2C_PORT I2C_NUM_1
#define SENSOR_I2C_SDA_PIN GPIO_NUM_5  // D4
#define SENSOR_I2C_SCL_PIN GPIO_NUM_6  // D5
#define IMU_I2C_ADDR 0x6A              // 0x6B si SA0 está a 3V3
#define FUEL_GAUGE_I2C_ADDR 0x36

// Servos de la cabeza (PWM 50 Hz)
#define SERVO_PAN_PIN GPIO_NUM_1   // D0, giro
#define SERVO_TILT_PIN GPIO_NUM_2  // D1, cabeceo
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2500
#define SERVO_PAN_INVERT false
#define SERVO_TILT_INVERT false

// Límites mecánicos comprobados en el CAD (cad/nubo.scad)
#define HEAD_PAN_MIN_DEG -75
#define HEAD_PAN_MAX_DEG 75
#define HEAD_TILT_MIN_DEG -25  // mira arriba
#define HEAD_TILT_MAX_DEG 12   // mira abajo

// Cámara de la placa Sense (OV2640/OV3660), escondida en la oreja derecha
#define CAMERA_PIN_PWDN GPIO_NUM_NC
#define CAMERA_PIN_RESET GPIO_NUM_NC
#define CAMERA_PIN_XCLK GPIO_NUM_10
#define CAMERA_PIN_SIOD GPIO_NUM_40
#define CAMERA_PIN_SIOC GPIO_NUM_39
#define CAMERA_PIN_D0 GPIO_NUM_15  // Y2
#define CAMERA_PIN_D1 GPIO_NUM_17  // Y3
#define CAMERA_PIN_D2 GPIO_NUM_18  // Y4
#define CAMERA_PIN_D3 GPIO_NUM_16  // Y5
#define CAMERA_PIN_D4 GPIO_NUM_14  // Y6
#define CAMERA_PIN_D5 GPIO_NUM_12  // Y7
#define CAMERA_PIN_D6 GPIO_NUM_11  // Y8
#define CAMERA_PIN_D7 GPIO_NUM_48  // Y9
#define CAMERA_PIN_VSYNC GPIO_NUM_38
#define CAMERA_PIN_HREF GPIO_NUM_47
#define CAMERA_PIN_PCLK GPIO_NUM_13
#define XCLK_FREQ_HZ 20000000

#define BOOT_BUTTON_GPIO GPIO_NUM_0

#endif  // _BOARD_CONFIG_H_
