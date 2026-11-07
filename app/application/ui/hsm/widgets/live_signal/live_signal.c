#include "live_signal.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("live_signal")

// y = round(20 - 15 * sin(2*pi*x/200)), sampled every 2 px.
// Flash-resident reference; each instance reuses its own shifted points.
static lv_point_precise_t const UiLiveSignal_points_[] = {
    {0, 20}, {2, 19}, {4, 18}, {6, 17}, {8, 16},
    {10, 15}, {12, 14}, {14, 14}, {16, 13}, {18, 12},
    {20, 11}, {22, 10}, {24, 10}, {26, 9}, {28, 8},
    {30, 8}, {32, 7}, {34, 7}, {36, 6}, {38, 6},
    {40, 6}, {42, 5}, {44, 5}, {46, 5}, {48, 5},
    {50, 5}, {52, 5}, {54, 5}, {56, 5}, {58, 5},
    {60, 6}, {62, 6}, {64, 6}, {66, 7}, {68, 7},
    {70, 8}, {72, 8}, {74, 9}, {76, 10}, {78, 10},
    {80, 11}, {82, 12}, {84, 13}, {86, 14}, {88, 14},
    {90, 15}, {92, 16}, {94, 17}, {96, 18}, {98, 19},
    {100, 20}, {102, 21}, {104, 22}, {106, 23}, {108, 24},
    {110, 25}, {112, 26}, {114, 26}, {116, 27}, {118, 28},
    {120, 29}, {122, 30}, {124, 30}, {126, 31}, {128, 32},
    {130, 32}, {132, 33}, {134, 33}, {136, 34}, {138, 34},
    {140, 34}, {142, 35}, {144, 35}, {146, 35}, {148, 35},
    {150, 35}, {152, 35}, {154, 35}, {156, 35}, {158, 35},
    {160, 34}, {162, 34}, {164, 34}, {166, 33}, {168, 33},
    {170, 32}, {172, 32}, {174, 31}, {176, 30}, {178, 30},
    {180, 29}, {182, 28}, {184, 27}, {186, 26}, {188, 26},
    {190, 25}, {192, 24}, {194, 23}, {196, 22}, {198, 21},
    {200, 20}
};

static lv_obj_t *UiLiveSignal_label_(lv_obj_t *parent,
                                    char const *text) {
    return Label_create(parent,
        &(LabelConfig){
            .style = &UiStyle_signalLabel,
            .longMode = LV_LABEL_LONG_MODE_CLIP, .text = text});
}

void UiLiveSignal_create(UiLiveSignal *me, lv_obj_t *parent,
                          int32_t width, int32_t height) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    me->container = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = width, .height = height, .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_CENTER,
            .padTop = 30, .padBottom = 20});
    me->header = FlexContainer_create(me->container,
        &(UiFlexConfig){
            .width = 204, .height = 17, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START});
    me->heading = UiLiveSignal_label_(me->header, "Signal / Phase");
    me->caption = UiLiveSignal_label_(me->header, "MOVE DEMO");
    me->wave = lv_line_create(me->container);
    lv_obj_remove_style_all(me->wave);
    lv_obj_remove_flag(me->wave,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(me->wave, &UiStyle_signalWave, LV_PART_MAIN);
    me->phases = FlexContainer_create(me->container,
        &(UiFlexConfig){
            .width = 128, .height = 4, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_START,
            .trackPlace = LV_FLEX_ALIGN_START, .padColumn = 7});
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t * const phase = lv_obj_create(me->phases);
        lv_obj_remove_style_all(phase);
        lv_obj_remove_flag(phase,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_style(phase, &UiStyle_signalPhase, LV_PART_MAIN);
        lv_obj_add_style(phase, &UiStyle_signalPhaseActive,
                          LV_PART_MAIN | LV_STATE_CHECKED);
    }
    UiLiveSignal_setProgress(me, 0);
}

void UiLiveSignal_setPhase(UiLiveSignal *me, uint8_t index) {
    SM_REQUIRE(me != NULL && me->phases != NULL);
    SM_REQUIRE(index < 5U);
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t * const phase = lv_obj_get_child(me->phases, (int32_t)i);
        if (i == index) {
            lv_obj_add_state(phase, LV_STATE_CHECKED);
        }
        else {
            lv_obj_remove_state(phase, LV_STATE_CHECKED);
        }
    }
}

void UiLiveSignal_setProgress(UiLiveSignal *me, uint8_t progress) {
    SM_REQUIRE(me != NULL && me->wave != NULL && me->phases != NULL);
    SM_REQUIRE(progress < 100U);
    for (unsigned i = 0; i < 101; ++i) {
        me->points[i].x = UiLiveSignal_points_[i].x;
        me->points[i].y = UiLiveSignal_points_[(i + progress) % 100U].y;
    }
    me->progress = progress;
    lv_line_set_points(me->wave, me->points, 101);
    UiLiveSignal_setPhase(me, progress / 20U);
}

void UiLiveSignal_destroy(UiLiveSignal *me) {
    SM_REQUIRE(me != NULL);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiLiveSignal){0};
}
