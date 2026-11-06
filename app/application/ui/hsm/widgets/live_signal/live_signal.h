#ifndef LIVE_SIGNAL_H_
#define LIVE_SIGNAL_H_

#include "lvgl.h"

// Waveform and five-phase display; UI thread only.
// Zero-initialize before create. Parent owns the subtree and must outlive
// this instance. Do not move/copy live instances: Line borrows points.
// No timers, sampling, hardware access or autonomous animation.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *header;
    lv_obj_t *heading;
    lv_obj_t *caption;
    lv_obj_t *wave;
    lv_obj_t *phases;
    lv_point_precise_t points[101];
    uint8_t progress;
} UiLiveSignal;

// Pixel or lv_pct() dimensions; outer box needs at least 204x113 px.
// Creates one sine period and selects phase zero.
void UiLiveSignal_create(UiLiveSignal *me, lv_obj_t *parent,
                          int32_t width, int32_t height);
// Requires index 0..4; changes only the highlighted phase.
void UiLiveSignal_setPhase(UiLiveSignal *me, uint8_t index);
// Requires 0..99 percent of a cycle. Shifts the waveform and selects its
// corresponding phase segment; reuses points without allocation.
void UiLiveSignal_setProgress(UiLiveSignal *me, uint8_t progress);
// Deletes the subtree and clears me; idempotent when empty.
void UiLiveSignal_destroy(UiLiveSignal *me);

#endif // LIVE_SIGNAL_H_
