#include "mca_bridge.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TRANSACTION_COUNT 20000u
static mca_bridge_t bridge;
static atomic_bool producer_done;
static atomic_uint_fast64_t clock_us;
static unsigned completed;
uint64_t time_us_64(void) { return atomic_fetch_add(&clock_us, 1); }

static void *producer_thread(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < TRANSACTION_COUNT; ++i) {
        const uint8_t request[] = {0xaa, 0x42, 2, 0, (uint8_t)i, (uint8_t)(i >> 8)};
        while (mca_bridge_state(&bridge) != MCA_BRIDGE_IDLE) sched_yield();
        memcpy(bridge.input, request, sizeof(request));
        assert(mca_bridge_submit(&bridge, sizeof(request)));
        while (mca_bridge_state(&bridge) != MCA_BRIDGE_RESPONSE) sched_yield();
        if (bridge.output_length == 2) {
            assert(bridge.output[0] == 0x55 && bridge.output[1] == 0xaf);
        } else {
            assert(bridge.output_length == 7);
            assert(bridge.output[0] == 0x55 && bridge.output[1] == 0x5a);
            assert(bridge.output[2] == 3 && bridge.output[3] == 0);
            assert(bridge.output[4] == request[4] && bridge.output[5] == request[5]);
            assert(bridge.output[6] == (uint8_t)(request[4] + request[5]));
        }
        mca_bridge_sending(&bridge);
        mca_bridge_release(&bridge);
        ++completed;
    }
    atomic_store(&producer_done, true);
    return NULL;
}
static void *consumer_thread(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < TRANSACTION_COUNT; ++i) {
        while (!mca_bridge_begin(&bridge)) sched_yield();
        uint8_t input[2];
        if (mca_bridge_receive(&bridge, &input[0]) && mca_bridge_receive(&bridge, &input[1])) {
            mca_bridge_respond(&bridge, input[0]);
            mca_bridge_respond(&bridge, input[1]);
            mca_bridge_respond(&bridge, (uint8_t)(input[0] + input[1]));
        }
        mca_bridge_finish(&bridge, true);
    }
    return NULL;
}
static void *canceller_thread(void *unused) {
    (void)unused;
    while (!atomic_load(&producer_done)) {
        critical_section_enter_blocking(&bridge.lock);
        bool active = bridge.state == MCA_BRIDGE_ACTIVE;
        uint64_t deadline = bridge.deadline;
        critical_section_exit(&bridge.lock);
        /* A newer request has a later deadline, so this cannot cancel an
           unclaimed operation and strand the consumer's transaction count. */
        if (active) mca_bridge_poll_timeout(&bridge, deadline);
        sched_yield();
    }
    return NULL;
}
int main(void) {
    alarm(30);
    pthread_t producer, consumer, canceller;
    mca_bridge_init(&bridge);
    assert(pthread_create(&producer, NULL, producer_thread, NULL) == 0);
    assert(pthread_create(&consumer, NULL, consumer_thread, NULL) == 0);
    assert(pthread_create(&canceller, NULL, canceller_thread, NULL) == 0);
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(pthread_join(canceller, NULL) == 0);
    assert(completed == TRANSACTION_COUNT && mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    critical_section_deinit(&bridge.lock);
    alarm(0);
    puts("bridge threaded stress passed");
    return 0;
}
