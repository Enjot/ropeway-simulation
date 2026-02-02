#pragma once

#include "ipc/resources.h"
#include "config.h"

// Generate IPC keys using ftok
// Returns 0 on success, -1 on error
int ipc_generate_keys(IPCKeys *keys, const char *path);

// Clean up stale IPC resources from a previous crashed run
// Checks if shared memory exists and if the main_pid stored in it is dead
// Returns: 1 if stale resources cleaned, 0 if no stale resources, -1 on error
int ipc_cleanup_stale(const IPCKeys *keys);

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

// Signal-safe IPC cleanup (for use in signal handlers)
// Only uses async-signal-safe syscalls (msgctl, semctl, shmctl)
// No logging, no memory allocation
void ipc_cleanup_signal_safe(IPCResources *res);

// Semaphore operations with EINTR handling
// Atomically acquire/release 'count' slots in a single syscall
// Returns 0 on success, -1 on error
int sem_wait(int sem_id, int sem_num, int count);
int sem_post(int sem_id, int sem_num, int count);
int sem_trywait(int sem_id, int sem_num);  // Non-blocking, returns -1 with EAGAIN if would block

// Semaphore wait with EINTR handling (kernel handles SIGTSTP automatically)
// Only returns -1 on shutdown (EIDRM) or other errors
int sem_wait_pauseable(IPCResources *res, int sem_num, int count);

// Get current semaphore value
int sem_getval(int sem_id, int sem_num);

// Wait for emergency stop to clear (issue #4 fix - replaces usleep polling)
// Properly tracks emergency_waiters for reliable wakeup
void ipc_wait_emergency_clear(IPCResources *res);

// Release all emergency waiters (called when emergency clears)
void ipc_release_emergency_waiters(IPCResources *res);

// Startup synchronization barrier
// Workers call ipc_signal_worker_ready() after initialization
// Main calls ipc_wait_workers_ready() before spawning tourist generator
void ipc_signal_worker_ready(IPCResources *res);
int ipc_wait_workers_ready(IPCResources *res, int expected_count);
