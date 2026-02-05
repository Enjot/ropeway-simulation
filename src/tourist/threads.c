/**
 * @file tourist/threads.c
 * @brief Kid and bike thread management for family tourists.
 */

#include "tourist/threads.h"
#include "core/logger.h"

#include <stdio.h>

/**
 * @brief Thread function for kid simulation.
 *
 * @param arg Pointer to KidThreadData.
 * @return NULL.
 */
void *kid_thread_func(void *arg) {
    KidThreadData *td = (KidThreadData *)arg;
    FamilyState *family = td->family;
    int kid_idx = td->kid_index;

    log_info("KID", "Tourist %d's kid #%d started", family->parent_id, kid_idx + 1);
    // Thread exits immediately - no zombie, just waits for join
    log_info("KID", "Tourist %d's kid #%d exiting", family->parent_id, kid_idx + 1);

    return NULL;
}

/**
 * @brief Thread function for bike simulation.
 *
 * @param arg Pointer to BikeThreadData.
 * @return NULL.
 */
void *bike_thread_func(void *arg) {
    BikeThreadData *td = (BikeThreadData *)arg;

    log_info("BIKE", "Tourist %d's bike ready", td->tourist_id);
    // Thread exits immediately
    log_info("BIKE", "Tourist %d's bike stored", td->tourist_id);

    return NULL;
}

/**
 * @brief Create threads for kids and bike (if cyclist).
 *
 * @param data Tourist data.
 * @param family Family state for thread data.
 * @param kid_data Array of kid thread data.
 * @param kid_threads Array of kid thread handles.
 * @param bike_data Bike thread data.
 * @param bike_thread Bike thread handle.
 * @param bike_thread_created Output: 1 if bike thread was created.
 * @return 0 on success, -1 on error.
 */
int tourist_create_family_threads(const TouristData *data, FamilyState *family,
                                   KidThreadData kid_data[], pthread_t kid_threads[],
                                   BikeThreadData *bike_data, pthread_t *bike_thread,
                                   int *bike_thread_created) {
    *bike_thread_created = 0;

    // Spawn bike thread if cyclist (thread exits immediately after logging)
    if (data->type == TOURIST_CYCLIST) {
        bike_data->tourist_id = data->id;
        bike_data->family = family;
        if (pthread_create(bike_thread, NULL, bike_thread_func, bike_data) != 0) {
            perror("tourist: pthread_create bike");
            return -1;
        }
        *bike_thread_created = 1;
    }

    // Spawn kid threads (threads exit immediately after logging)
    if (data->kid_count > 0) {
        for (int i = 0; i < data->kid_count; i++) {
            kid_data[i].kid_index = i;
            kid_data[i].family = family;

            if (pthread_create(&kid_threads[i], NULL, kid_thread_func, &kid_data[i]) != 0) {
                perror("tourist: pthread_create kid");
                // Join already-created threads
                for (int j = 0; j < i; j++) {
                    pthread_join(kid_threads[j], NULL);
                }
                if (*bike_thread_created) {
                    pthread_join(*bike_thread, NULL);
                }
                return -1;
            }
        }
    }

    return 0;
}

/**
 * @brief Join all family threads (kids and bike).
 *
 * @param data Tourist data.
 * @param kid_threads Array of kid thread handles.
 * @param bike_thread Bike thread handle.
 * @param bike_thread_created 1 if bike thread was created.
 */
void tourist_join_family_threads(const TouristData *data, pthread_t kid_threads[],
                                  pthread_t bike_thread, int bike_thread_created) {
    // Join bike thread if created (returns immediately - thread already exited)
    if (bike_thread_created) {
        pthread_join(bike_thread, NULL);
    }

    // Join kid threads (returns immediately - threads already exited)
    for (int i = 0; i < data->kid_count; i++) {
        pthread_join(kid_threads[i], NULL);
    }
}
