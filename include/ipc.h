#pragma once

#include "types.h"
#include "config.h"

// Generate IPC keys using ftok
// Returns 0 on success, -1 on error
int ipc_generate_keys(IPCKeys *keys, const char *path);

// Create all IPC resources (shared memory, semaphores, message queues)
// Should only be called by main process
// Returns 0 on success, -1 on error
int ipc_create(IPCResources *res, const IPCKeys *keys, const Config *cfg);

// Attach to existing IPC resources (for child processes)
// Returns 0 on success, -1 on error
int ipc_attach(IPCResources *res, const IPCKeys *keys);

// Detach from IPC resources (for child processes before exit)
void ipc_detach(IPCResources *res);

// Destroy all IPC resources (for main process cleanup)
void ipc_destroy(IPCResources *res);

// Semaphore operations with EINTR handling
// Atomically acquire/release 'count' slots in a single syscall
// Returns 0 on success, -1 on error
int sem_wait(int sem_id, int sem_num, int count);
int sem_post(int sem_id, int sem_num, int count);
int sem_trywait(int sem_id, int sem_num);  // Non-blocking, returns -1 with EAGAIN if would block

// Get current semaphore value
int sem_getval(int sem_id, int sem_num);

// Check pause state and block if paused (issue #11 fix - consolidated)
// Properly tracks pause_waiters for reliable wakeup
void ipc_check_pause(IPCResources *res);

// Wait for emergency stop to clear (issue #4 fix - replaces usleep polling)
// Properly tracks emergency_waiters for reliable wakeup
void ipc_wait_emergency_clear(IPCResources *res);

// Release all emergency waiters (called when emergency clears)
void ipc_release_emergency_waiters(IPCResources *res);

// Release all pause waiters (called on SIGCONT)
void ipc_release_pause_waiters(IPCResources *res);
