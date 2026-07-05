#pragma once
#include <atomic>

extern std::atomic<bool> g_shutdown_requested;

void signal_handler(int signum);
void print_help();