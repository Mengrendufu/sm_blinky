// One execution body for the NVS, and the only one: nvs_port.h requirement 7
// is satisfied by there being exactly one caller. The flash itself is still
// owned by the flash task - every read, write and erase here is a request into
// its queue.
//
// On the way up it mounts, reads the boot counter, writes it back one higher,
// and puts the cost of each step on the tracer as record 91, so the numbers can
// be read off the wire instead of guessed at. Payload: mount ms, write ms,
// read ms, boot count low, boot count high, tag.
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"
#include "flash.h"
#include "nvs.h"
#include "storage.h"
#include "sm_tracer.h"

#define STORAGE_STACK_WORDS 384U
#define STORAGE_PRIORITY    (tskIDLE_PRIORITY + 1U)
#define STORAGE_TRACE       91U   /* free; used: 12/13/20-24/38/45/66/67/79/90 */
#define STORAGE_KEY         1U

static StaticTask_t Storage_tcb_;
static StackType_t Storage_stack_[STORAGE_STACK_WORDS];
static struct nvs_fs Storage_fs_;
static uint32_t Storage_boot_;

// TIM1 is the HAL time base at 1 MHz - the same source ui_disp.c measures with.
static uint32_t Storage_us_(void) {
    return ((uint32_t)HAL_GetTick() * 1000U) + (uint32_t)TIM1->CNT;
}

static uint8_t Storage_ms_(uint32_t us) {
    uint32_t const ms = us / 1000U;

    return (ms > 255U) ? 255U : (uint8_t)ms;
}

static void Storage_thread_(void *arg) {
    uint32_t before = 0U;
    uint32_t t0;
    uint32_t mountUs;
    uint32_t readUs;
    uint32_t writeUs;
    bool have;

    (void)arg;

    t0 = Storage_us_();
    if (nvs_mount(&Storage_fs_) != 0) {
        (void)nvs_clear(&Storage_fs_);
        if (nvs_mount(&Storage_fs_) != 0) {
            vTaskDelete((TaskHandle_t)0);   /* nothing to report this boot */
        }
    }
    mountUs = Storage_us_() - t0;

    t0 = Storage_us_();
    have = (nvs_read(&Storage_fs_, STORAGE_KEY, &before, sizeof(before))
            == (ssize_t)sizeof(before));
    readUs = Storage_us_() - t0;

    Storage_boot_ = have ? (before + 1U) : 1U;

    t0 = Storage_us_();
    (void)nvs_write(&Storage_fs_, STORAGE_KEY, &Storage_boot_,
                    sizeof(Storage_boot_));
    writeUs = Storage_us_() - t0;

    SM_TRACE_BEGIN(STORAGE_TRACE, 6)
    SM_TRACE_PAYLOAD_BE(Storage_ms_(mountUs))
    SM_TRACE_PAYLOAD_BE(Storage_ms_(writeUs))
    SM_TRACE_PAYLOAD_BE(Storage_ms_(readUs))
    SM_TRACE_PAYLOAD_BE((unsigned char)(Storage_boot_ & 0xFFU))
    SM_TRACE_PAYLOAD_BE((unsigned char)((Storage_boot_ >> 8) & 0xFFU))
    SM_TRACE_PAYLOAD_BE(1U)
    SM_TRACE_END(0)

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void Storage_start(void) {
    Storage_fs_.offset = 0U;
    Storage_fs_.sector_size = 4096U;
    Storage_fs_.sector_count = 16U;
    Storage_fs_.flash_device = nvs_port_device();
    Storage_boot_ = 0U;
    (void)xTaskCreateStatic(Storage_thread_, "nvs", STORAGE_STACK_WORDS,
                            (void *)0, STORAGE_PRIORITY, Storage_stack_,
                            &Storage_tcb_);
}

uint32_t Storage_bootCount(void) {
    return Storage_boot_;
}
