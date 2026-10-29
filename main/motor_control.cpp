#include "motor_control.h"
#include "config.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "MOTOR";
char last_command_str[32] = "STOPPED";
int current_speed_pct = 0;
static TimerHandle_t s_safety_timer = NULL;

static void safety_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGW(TAG, "Safety runtime limit reached (%d s). Auto-stopping motor to prevent burnout.", MOTOR_MAX_RUNTIME_SEC);
    stop_bridge();
    strncpy(last_command_str, "TIMEOUT STOP", sizeof(last_command_str) - 1);
    last_command_str[sizeof(last_command_str) - 1] = '\0';
}

static uint32_t calculate_safe_duty(int speed_pct) {
    int abs_speed = abs(speed_pct);
    if (abs_speed == 0) {
        return 0;
    }
    if (abs_speed > 100) {
        abs_speed = 100;
    }

    // Scale requested speed (1..100) across safe range [MIN_START..MAX_SAFE]
    int effective_pct = MOTOR_MIN_START_DUTY_PERCENT +
        (abs_speed * (MOTOR_MAX_SAFE_DUTY_PERCENT - MOTOR_MIN_START_DUTY_PERCENT)) / 100;

    if (effective_pct > MOTOR_MAX_SAFE_DUTY_PERCENT) {
        effective_pct = MOTOR_MAX_SAFE_DUTY_PERCENT;
    }

    return (uint32_t)((effective_pct * 255) / 100);
}

void init_motors() {
    // 1. Configure LEDC Timer (8-bit resolution, 5 kHz)
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Configure Channel 0 on MOTOR_IN_A
    ledc_channel_config_t ch_a = {};
    ch_a.gpio_num = MOTOR_IN_A;
    ch_a.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_a.channel = LEDC_CHANNEL_0;
    ch_a.intr_type = LEDC_INTR_DISABLE;
    ch_a.timer_sel = LEDC_TIMER_0;
    ch_a.duty = 0;
    ch_a.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_a));

    // 3. Configure Channel 1 on MOTOR_IN_B
    ledc_channel_config_t ch_b = {};
    ch_b.gpio_num = MOTOR_IN_B;
    ch_b.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_b.channel = LEDC_CHANNEL_1;
    ch_b.intr_type = LEDC_INTR_DISABLE;
    ch_b.timer_sel = LEDC_TIMER_0;
    ch_b.duty = 0;
    ch_b.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_b));

    // 4. Initialize FreeRTOS One-Shot Safety Watchdog Timer
    s_safety_timer = xTimerCreate(
        "motor_safety_timer",
        pdMS_TO_TICKS(MOTOR_MAX_RUNTIME_SEC * 1000),
        pdFALSE,
        NULL,
        safety_timer_callback
    );

    stop_bridge();
    ESP_LOGI(TAG, "Motors initialized with PWM on GPIO %d & %d (Duty ceiling: %d%%)",
             MOTOR_IN_A, MOTOR_IN_B, MOTOR_MAX_SAFE_DUTY_PERCENT);
}

void stop_bridge() {
    if (s_safety_timer != NULL && xTimerIsTimerActive(s_safety_timer) != pdFALSE) {
        xTimerStop(s_safety_timer, 0);
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

    current_speed_pct = 0;
    strcpy(last_command_str, "STOPPED");
    ESP_LOGI(TAG, "Bridge Stopped.");
}

void set_bridge_speed(int speed_pct) {
    if (speed_pct > 100) speed_pct = 100;
    if (speed_pct < -100) speed_pct = -100;

    current_speed_pct = speed_pct;

    if (speed_pct == 0) {
        stop_bridge();
        return;
    }

    uint32_t duty = calculate_safe_duty(speed_pct);

    if (speed_pct > 0) {
        // Forward / Opening
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        snprintf(last_command_str, sizeof(last_command_str), "OPENING (%d%%)", speed_pct);
        ESP_LOGI(TAG, "Bridge Opening: %d%% speed, Duty: %lu/255", speed_pct, (unsigned long)duty);
    } else {
        // Reverse / Closing
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

        snprintf(last_command_str, sizeof(last_command_str), "CLOSING (%d%%)", abs(speed_pct));
        ESP_LOGI(TAG, "Bridge Closing: %d%% speed, Duty: %lu/255", abs(speed_pct), (unsigned long)duty);
    }

    // Arm or reset the auto-stop timer
    if (s_safety_timer != NULL) {
        xTimerReset(s_safety_timer, 0);
    }
}

void open_bridge() {
    int spd = (current_speed_pct > 0) ? current_speed_pct : MOTOR_DEFAULT_SPEED_PERCENT;
    set_bridge_speed(spd);
}

void close_bridge() {
    int spd = (current_speed_pct < 0) ? current_speed_pct : -MOTOR_DEFAULT_SPEED_PERCENT;
    set_bridge_speed(spd);
}

void execute_bridge_command(const char* cmd) {
    if (cmd == NULL) return;

    if (strcmp(cmd, "open") == 0) {
        open_bridge();
    } else if (strcmp(cmd, "close") == 0) {
        close_bridge();
    } else if (strcmp(cmd, "stop") == 0) {
        stop_bridge();
    } else if (strncmp(cmd, "speed=", 6) == 0) {
        set_bridge_speed(atoi(cmd + 6));
    } else if ((cmd[0] >= '0' && cmd[0] <= '9') || (cmd[0] == '-' && cmd[1] >= '0' && cmd[1] <= '9')) {
        set_bridge_speed(atoi(cmd));
    } else {
        ESP_LOGW(TAG, "Unknown bridge command: %s", cmd);
    }
}