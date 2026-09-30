#include <cstdio>
#include "console_line.h"
static size_t wrapper_limit(const char* line) { return sizeof(line) - 1; }
int main() { char line[meshroute::console::local_command_max_bytes + 1] = {}; printf("wrapper_limit=%zu actual_array_limit=%zu\n",wrapper_limit(line), sizeof(line)-1); }
