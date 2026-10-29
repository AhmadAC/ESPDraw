#pragma once
#include "driver/gpio.h"

// Hardware Pin Definitions (ESP32-S3 Zero to L9110S)
#define MOTOR_IN_A (GPIO_NUM_4)
#define MOTOR_IN_B (GPIO_NUM_5)
#define BOOT_BUTTON_PIN (GPIO_NUM_0)

// Software Safety Limits to Prevent Motor Burnout
#define MOTOR_MAX_SAFE_DUTY_PERCENT 80   // Hard ceiling: motor PWM duty will never exceed 80%
#define MOTOR_MIN_START_DUTY_PERCENT 20  // Minimum duty to prevent motor stall coil overheating
#define MOTOR_DEFAULT_SPEED_PERCENT 60   // Default speed when using Open/Close buttons
#define MOTOR_MAX_RUNTIME_SEC 15         // Auto-stop safety timeout in seconds

// System Constants
#define MAXIMUM_RETRY 5