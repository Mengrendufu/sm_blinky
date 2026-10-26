// Static registry, registered delivery, one-shot/periodic and cancellation.
// See docs/ui-time-events.md for the host build command.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "time_evt.h"
#include "sst.h"

static unsigned critical;
static bool expectAssert;
static jmp_buf asserted;
static void (*afterUnlock)(void);
static TimeEvt oneShot, periodic;

typedef struct {
    unsigned count;
    SST_Evt const *last;
} Receiver;
static Receiver receiverA, receiverB;
static SST_Evt const eventA = { .sig = 10, .poolId = 0, .refCtr = 0 };
static SST_Evt const eventB = { .sig = 11, .poolId = 0, .refCtr = 0 };

void Host_timeEnter(void) { ++critical; }
void Host_timeExit(void) {
    assert(critical > 0U);
    --critical;
    if (!critical && afterUnlock != NULL) {
        void (*hook)(void) = afterUnlock;
        afterUnlock = NULL;
        hook();
    }
}
void SM_onAssert(char const *module, int line) {
    assert(strcmp(module, "time_evt") == 0);
    if (expectAssert) { longjmp(asserted, 1); }
    fprintf(stderr, "unexpected assertion %s:%d\n", module, line);
    abort();
}
static void receiveA(void *context, void const *event) {
    SST_Evt const *evt = event;
    Receiver *receiver = context;
    assert(!critical && receiver == &receiverA);
    assert(evt == &eventA && evt->sig == 10);
    assert(evt->poolId == 0 && evt->refCtr == 0);
    ++receiver->count;
    receiver->last = evt;
}
static void receiveB(void *context, void const *event) {
    SST_Evt const *evt = event;
    Receiver *receiver = context;
    assert(!critical && receiver == &receiverB);
    assert(evt == &eventB && evt->sig == 11);
    assert(evt->poolId == 0 && evt->refCtr == 0);
    ++receiver->count;
    receiver->last = evt;
}
static void pulses(unsigned n) { while (n--) { TimeEvt_tick(); } }
static void cancelExpired(void) {
    // tick has committed this periodic expiration and reloaded the count.
    assert(TimeEvt_disarm(&periodic));
}
#define EXPECT_ASSERT(action) do { \
    expectAssert = true; \
    if (setjmp(asserted) == 0) { action; abort(); } \
    expectAssert = false; \
} while (0)

int main(void) {
    static TimeEvt_PostOperations const a = {&receiverA, receiveA};
    static TimeEvt_PostOperations const b = {&receiverB, receiveB};
    TimeEvt_tick(); // Empty registry before thread startup is legal.
    TimeEvt_ctor(&oneShot, &eventA, &a);
    TimeEvt_ctor(&periodic, &eventB, &b);
    pulses(10);
    assert(!receiverA.count && !receiverB.count);

    TimeEvt_arm(&oneShot, 3, 0);
    TimeEvt_arm(&periodic, 2, 4);
    pulses(2);
    assert(receiverA.count == 0 && receiverB.count == 1);
    pulses(1);
    assert(receiverA.count == 1 && receiverB.count == 1);
    pulses(3);
    assert(receiverA.count == 1 && receiverB.count == 2);
    assert(!TimeEvt_disarm(&oneShot));
    assert(TimeEvt_disarm(&periodic));
    pulses(10);
    assert(receiverA.count == 1 && receiverB.count == 2);

    TimeEvt_arm(&oneShot, 10, 0);
    pulses(2);
    TimeEvt_arm(&oneShot, 2, 0); // Re-arm replaces the outstanding count.
    pulses(1);
    assert(receiverA.count == 1);
    pulses(1);
    assert(receiverA.count == 2);
    TimeEvt_arm(&oneShot, 1, 0);
    assert(TimeEvt_disarm(&oneShot));
    pulses(3);
    assert(receiverA.count == 2);

    TimeEvt_arm(&periodic, 1, 3);
    afterUnlock = cancelExpired;
    pulses(1);
    assert(receiverB.count == 3 && periodic.remaining == 0);
    pulses(10);
    assert(receiverB.count == 3); // Committed post survives disarm once.
    assert(receiverB.last == &eventB);

    EXPECT_ASSERT(TimeEvt_ctor(&oneShot, &eventA, &a));
    EXPECT_ASSERT(TimeEvt_arm(&oneShot, 0, 0));
    EXPECT_ASSERT(TimeEvt_disarm(NULL));
    TimeEvt_arm(&oneShot, 1, 0); // Assertions did not damage the registry.
    pulses(1);
    assert(receiverA.count == 3 && critical == 0);
    puts("PASS time events: registered targets, timing, re-arm, disarm, "
         "expiration race and static-event contracts");
    return 0;
}
