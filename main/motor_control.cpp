#include "motor_control.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "MOTOR";
char last_command_str[32] = "STOPPED";

void init_motors() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << MOTOR_IN_A) | (1ULL << MOTOR_IN_B);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Ensure motors are stopped on boot
    stop_bridge();
    ESP_LOGI(TAG, "Motors initialized on GPIO %d and %d", MOTOR_IN_A, MOTOR_IN_B);
}

void open_bridge() {
    ESP_LOGI(TAG, "Opening Bridge...");
    gpio_set_level(MOTOR_IN_A, 1);
    gpio_set_level(MOTOR_IN_B, 0);
    strcpy(last_command_str, "OPENING");
}

void close_bridge() {
    ESP_LOGI(TAG, "Closing Bridge...");
    gpio_set_level(MOTOR_IN_A, 0);
    gpio_set_level(MOTOR_IN_B, 1);
    strcpy(last_command_str, "CLOSING");
}

void stop_bridge() {
    ESP_LOGI(TAG, "Stopping Bridge...");
    gpio_set_level(MOTOR_IN_A, 0);
    gpio_set_level(MOTOR_IN_B, 0);
    strcpy(last_command_str, "STOPPED");
}

void execute_bridge_command(const char* cmd) {
    if (strcmp(cmd, "open") == 0) {
        open_bridge();
    } else if (strcmp(cmd, "close") == 0) {
        close_bridge();
    } else if (strcmp(cmd, "stop") == 0) {
        stop_bridge();
    } else {
        ESP_LOGW(TAG, "Unknown bridge command: %s", cmd);
    }
}