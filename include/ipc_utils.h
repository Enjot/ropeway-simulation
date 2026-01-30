#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <sys/types.h>
#include "shared_state.h"
#include "semaphores.h"

/* IPC resource handles */
typedef struct {
    int shm_id;             /* Shared memory ID */
    int sem_id;             /* Semaphore set ID */
    SharedState *state;     /* Pointer to attached shared memory */
    int is_owner;           /* 1 if this process created the resources */
} IpcResources;

/* Create IPC resources (called by main process) */
int ipc_create(IpcResources *ipc);

/* Attach to existing IPC resources (called by child processes) */
int ipc_attach(IpcResources *ipc);

/* Detach from shared memory (called before exit by children) */
int ipc_detach(IpcResources *ipc);

/* Destroy IPC resources (called by main process at cleanup) */
int ipc_destroy(IpcResources *ipc);

/* Initialize semaphores to their starting values */
int ipc_init_semaphores(IpcResources *ipc);

/* Initialize shared state to default values */
void ipc_init_state(SharedState *state);

/* Get IPC key for shared memory */
key_t get_shm_key(void);

/* Get IPC key for semaphores */
key_t get_sem_key(void);

#endif /* IPC_UTILS_H */
