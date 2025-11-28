#pragma once
#include <stddef.h>
#include <sel4/shared_types_gen.h>
#include <stdbool.h>
#include <elf/elf.h>
#include "../threads.h"

int handle_sos_process_create();

/**
 * Create a process return true if successful.
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 * 
 * @param 
 * @returns
 */
bool create_process(sos_thread_t *assigned_worker_thread, char *app_name, pid_t pid, elf_t* elf_file, 
                    struct nfsfh* elf_fh);

