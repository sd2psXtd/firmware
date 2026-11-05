#include "mca_bridge.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TRANSACTION_COUNT 20000u

static mca_bridge_t bridge;
static pthread_mutex_t progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned completed;
static bool producer_done;

static void *producer_thread(void *unused) {
    (void)unused;

    for (unsigned i = 0; i < TRANSACTION_COUNT; ++i) {
        uint8_t request[2] = {(uint8_t)i, (uint8_t)(i >> 8)};
        while (!mca_bridge_submit(&bridge, request, sizeof(request)))
            sched_yield();

        for (;;) {
            mca_bridge_state_t state = mca_bridge_state(&bridge);
            if (state == MCA_BRIDGE_RESPONSE) {
                uint8_t response[3];
                size_t length = 0;
                assert(mca_bridge_take_response(&bridge, response,
                                                sizeof(response), &length));
                assert(length == sizeof(response));
                assert(response[0] == request[0]);
                assert(response[1] == request[1]);
                assert(response[2] == (uint8_t)(request[0] + request[1]));
                break;
            }
            if (state == MCA_BRIDGE_IDLE)
                break; /* Core 1 acknowledged a cancellation. */
            sched_yield();
        }

        (void)pthread_mutex_lock(&progress_mutex);
        ++completed;
        (void)pthread_mutex_unlock(&progress_mutex);
    }

    (void)pthread_mutex_lock(&progress_mutex);
    producer_done = true;
    (void)pthread_mutex_unlock(&progress_mutex);
    return NULL;
}

static void *consumer_thread(void *unused) {
    (void)unused;

    for (unsigned i = 0; i < TRANSACTION_COUNT; ++i) {
        while (!mca_bridge_core1_begin(&bridge))
            sched_yield();

        uint8_t input[2];
        bool received_all = mca_bridge_core1_receive(&bridge, &input[0]);
        if (received_all)
            received_all = mca_bridge_core1_receive(&bridge, &input[1]);
        if (received_all) {
            received_all = mca_bridge_core1_respond(&bridge, input[0]) &&
                           mca_bridge_core1_respond(&bridge, input[1]) &&
                           mca_bridge_core1_respond(
                               &bridge, (uint8_t)(input[0] + input[1]));
        }

        (void)mca_bridge_core1_finish(&bridge);
    }
    return NULL;
}

static void *canceller_thread(void *unused) {
    (void)unused;

    for (;;) {
        bool done;
        (void)pthread_mutex_lock(&progress_mutex);
        done = producer_done;
        (void)pthread_mutex_unlock(&progress_mutex);
        if (done)
            break;

        (void)mca_bridge_cancel(&bridge);
        sched_yield();
    }
    return NULL;
}

int main(void) {
    pthread_t producer;
    pthread_t consumer;
    pthread_t canceller;

    mca_bridge_init(&bridge);
    assert(pthread_create(&producer, NULL, producer_thread, NULL) == 0);
    assert(pthread_create(&consumer, NULL, consumer_thread, NULL) == 0);
    assert(pthread_create(&canceller, NULL, canceller_thread, NULL) == 0);

    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(pthread_join(canceller, NULL) == 0);

    assert(completed == TRANSACTION_COUNT);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    critical_section_deinit(&bridge.lock);
    puts("bridge threaded stress passed");
    return 0;
}
