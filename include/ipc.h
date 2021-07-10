#ifndef KANSHI_IPC_H
#define KANSHI_IPC_H

#include <stdbool.h>

#include "kanshi.h"

int kanshi_init_ipc(struct kanshi_state *state);
void kanshi_free_ipc(struct kanshi_state *state);

bool check_env(void);
int get_ipc_address(char *address, size_t size);

#endif
