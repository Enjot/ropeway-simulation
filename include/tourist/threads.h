#pragma once

/**
 * @file tourist/threads.h
 * @brief Kid and bike thread management for family tourists.
 */

#include "tourist/types.h"

/**
 * @brief Kid thread entry point.
 *
 * Kids just log and exit immediately - parent handles all actions.
 * Thread resources are cleaned up via pthread_join() at parent cleanup.
 *
 * @param arg Pointer to KidThreadData
 * @return NULL
 */
void *kid_thread_func(void *arg);

/**
 * @brief Bike thread entry point (for cyclists).
 *
 * Bikes just log and exit immediately - parent handles all actions.
 * Thread resources are cleaned up via pthread_join() at parent cleanup.
 *
 * @param arg Pointer to BikeThreadData
 * @return NULL
 */
void *bike_thread_func(void *arg);

/**
 * @brief Create family threads (kids and bike if cyclist).
 *
 * @param data Tourist data
 * @param family Family state (initialized by caller)
 * @param kid_data Array of kid thread data (at least MAX_KIDS_PER_ADULT elements)
 * @param kid_threads Array to receive kid thread handles
 * @param bike_data Bike thread data (if cyclist)
 * @param bike_thread Pointer to receive bike thread handle
 * @param bike_thread_created Set to 1 if bike thread was created
 * @return 0 on success, -1 on error (partial threads are cleaned up)
 */
int tourist_create_family_threads(const TouristData *data, FamilyState *family,
                                   KidThreadData kid_data[], pthread_t kid_threads[],
                                   BikeThreadData *bike_data, pthread_t *bike_thread,
                                   int *bike_thread_created);

/**
 * @brief Join family threads (kids and bike).
 *
 * @param data Tourist data
 * @param kid_threads Array of kid thread handles
 * @param bike_thread Bike thread handle
 * @param bike_thread_created Whether bike thread was created
 */
void tourist_join_family_threads(const TouristData *data, pthread_t kid_threads[],
                                  pthread_t bike_thread, int bike_thread_created);
