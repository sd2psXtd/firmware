#ifndef TEST_PICO_CRITICAL_SECTION_H
#define TEST_PICO_CRITICAL_SECTION_H

/* Host-only stand-in for the Pico SDK critical-section API. */
#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;
    int initialized;
} critical_section_t;

static inline void critical_section_init(critical_section_t *section) {
    (void)pthread_mutex_init(&section->mutex, NULL);
    section->initialized = 1;
}

static inline void critical_section_enter_blocking(critical_section_t *section) {
    (void)pthread_mutex_lock(&section->mutex);
}

static inline void critical_section_exit(critical_section_t *section) {
    (void)pthread_mutex_unlock(&section->mutex);
}

static inline void critical_section_deinit(critical_section_t *section) {
    if (section->initialized) {
        (void)pthread_mutex_destroy(&section->mutex);
        section->initialized = 0;
    }
}

#endif
