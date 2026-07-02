#pragma once

extern char last_command_str[32];

void init_motors();
void execute_bridge_command(const char* cmd);
void open_bridge();
void close_bridge();
void stop_bridge();