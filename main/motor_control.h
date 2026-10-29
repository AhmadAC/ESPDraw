#pragma once

#include <stdint.h>
#include <stdbool.h>

extern char last_command_str[32];
extern int current_speed_pct;

void init_motors();
void execute_bridge_command(const char* cmd);
void set_bridge_speed(int speed_pct);
void open_bridge();
void close_bridge();
void stop_bridge();