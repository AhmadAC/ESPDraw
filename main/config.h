#pragma once
#include "driver/gpio.h"

// Hardware Pin Definitions (ESP32-S3 Zero to L9110S)
#define MOTOR_IN_A (GPIO_NUM_4)
#define MOTOR_IN_B (GPIO_NUM_5)
#define BOOT_BUTTON_PIN (GPIO_NUM_0)

// System Constants
#define MAXIMUM_RETRY 5