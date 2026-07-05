#include "common.h"
#include <csignal>
#include <iostream>

// Global flag for graceful shutdown
std::atomic<bool> g_shutdown_requested{ false };

// Signal handler function
void signal_handler(int signum) {
    g_shutdown_requested = true;
    std::cout << "\n[!] Interrupt received (Signal " << signum << "). Initiating graceful shutdown...\n";
}

void print_help() {
    std::cout <<
        "Usage:\n"
        " MyCrackDB -h or --help                  : show help\n"
        " MyCrackDB -l or --lookup <hash>         : lookup hash\n"
        " MyCrackDB -ls or --list                 : display available hash algorithms\n"
        " MyCrackDB -c or --count                 : count the total number of words in the database\n"
        " MyCrackDB -g <text>                     : generate and store <text>\n"
        " MyCrackDB -g <text> -d <algo1,algo2,..> : generate, store and display hashes for <text>\n"
        "         (Don't add spaces between algos if no algorithm selected deafult is all)*\n"
        "                    (Example: MyCrackDB -g ahmed -d MD5,SHA1)*\n"
        " MyCrackDB -g -w wordlist.txt            : generate and store each line from file as a value\n"
        "\nExample: MyCrackDB -l 5d41402abc4b2a76b9719d911017c592\n";
}