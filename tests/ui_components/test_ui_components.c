
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp.h"
#include "sm_ui.h"
#include "helpers/ui_splash.h"
#include "widgets/topbar/topbar.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/helpers/ui_bar.h"
#include "widgets/helpers/ui_arc.h"
#include "helpers/ui_sys_info.h"
#include "widgets/styles/ui_style.h"
#include "widgets/page_title/page_title.h"
#include "widgets/boot_count/boot_count.h"
#include "widgets/thread_monitor/thread_monitor.h"
#include "widgets/resource_monitor/resource_monitor.h"
#include "ui.h"
#include "ui_evt.h"
#include "ui_evt_priv.h"
#include "time_evt.h"
#include "asset.h"
#include "instance/nvm_boot.h"
#include "instance/nvm_blinky.h"
#include "nvs.h"
#include "flash.h"
#include "sm_flash_evt.h"
#include "sm_tracer.h"
#include "ui_disp_priv.h"
// UI accesses RAM only; persistence must remain on the Flash AO.
void *nvs_port_device(void){return (void *)1;}
int nvs_mount(struct nvs_fs *fs){
    (void)fs;assert(!"unexpected UI storage mount");return -1;
}
ssize_t nvs_read(struct nvs_fs *fs,uint16_t id,void *dst,size_t size){
    (void)fs;(void)id;(void)dst;(void)size;
    assert(!"unexpected UI storage read");return -1;
}
ssize_t nvs_write(struct nvs_fs *fs,uint16_t id,void const *src,size_t size){
    (void)fs;(void)id;(void)src;(void)size;
    assert(!"unexpected UI storage write");return -1;
}
unsigned Host_critical;
SM_Tracer SM_Tracer_inst;
// UI must not perform storage IO or emit its failure records.
unsigned char *SM_Trace_getBlock(SM_Tracer * const me) {
    (void)me;assert(!"unexpected UI trace");return NULL;
}
void SM_Trace_push(SM_Tracer * const me,unsigned char *frame) {
    (void)me;(void)frame;assert(!"unexpected UI trace");
}
static unsigned hostTick,backlight,scenario;
static uint32_t hostFps=30,hostIdle,hostTotal;
static char const *expectedCpu="0%";
static Asset assets;
static uint16_t pixels[3][240*320];
static uint8_t drawBuffer[240*32*2];
static uint16_t framebuffer[240*320];
void SM_onAssert(char const *name,int line){
    fprintf(stderr,"ASSERT %s:%d tick %u scenario %u\n",
            name,line,hostTick,scenario);
    abort();
}
void DBC_fault_handler(char const *module,int label){
    SM_onAssert(module,label);
}
TaskHandle_t xTaskCreateStatic(void (*entry)(void *),char const *name,
    unsigned words,void *arg,unsigned priority,StackType_t *stack,
    StaticTask_t *tcb){
    assert(entry && !strcmp(name,"ui") && words==UI_STACK_WORDS);
    assert(priority==23 && !arg && stack && tcb);return (void *)1;
}
unsigned ulTaskNotifyTakeIndexed(unsigned i,int c,TickType_t w){
    (void)i;(void)c;(void)w;abort();
}
int xTaskNotifyGiveIndexed(TaskHandle_t thread,unsigned index){
    assert(thread==(void *)1 && index==0);return 1;
}
TickType_t xTaskGetTickCount(void){return hostTick;}
Asset const *Asset_get(void){return &assets;}
void const *Asset_slot(Asset const *a,uint32_t slot){
    return a->present && slot<3 ? pixels[slot] : NULL;
}
void const *Asset_splash(void){return scenario==3 ? NULL : pixels[2];}
void Ui_dispInit(void){}
void Ui_dispBacklight(bool on){assert(on);++backlight;}
uint32_t Ui_dispFps(void){return hostFps;}
void BSP_cpuSample(uint32_t *idle,uint32_t *total){
    assert(Host_critical);
    *idle=hostIdle;*total=hostTotal;
}
TickType_t Ui_dispHeartbeat(void){return 1;}
// Include the actual thread binding/start/drain implementation. Its RTOS
// entry stays parked; the host drives the same drain on this single owner.
#include "../../app/application/ui/thread/ui.c"
extern SM_HsmState Sm_Ui_standby,Sm_Ui_readySplash,Sm_Ui_earlyReadyShow;
extern SM_HsmState Sm_Ui_splashing,Sm_Ui_readyShow,Sm_Ui_sysInfo;
extern SM_HsmState Sm_Ui_liveShow;
extern SM_HsmState Sm_Ui_active;
extern uint8_t Ui_dbgSplashOk_;
static uint32_t lvTick(void){return hostTick;}
static void flush(lv_display_t *d,lv_area_t const *a,uint8_t *map){
    unsigned width=(unsigned)(a->x2-a->x1+1);
    for(int y=a->y1;y<=a->y2;++y){
        memcpy(framebuffer+y*240+a->x1,
               map+(y-a->y1)*width*2,width*2);
    }
    lv_display_flush_ready(d);
}
static void render(void){(void)lv_timer_handler();}
static void capture(char const *path){
    if(scenario)return;
    FILE *file=fopen(path,"wb");assert(file);
    assert(fwrite(framebuffer,1,sizeof framebuffer,file)==sizeof framebuffer);
    fclose(file);
}
// Natural dirty regions must reconstruct the same image as a full repaint.
static void verifyRepaint(lv_display_t *display){
    static uint16_t before[240*320];
    lv_refr_now(display);
    memcpy(before,framebuffer,sizeof before);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
    assert(!memcmp(before,framebuffer,sizeof before));
}
static void send(unsigned sig){
    SST_Evt *event=SST_NEW(SST_Evt);event->sig=sig;
    Sm_Ui_dispatch(&Ui_sm_,event);SST_GC(event);
}
static void pulse(unsigned n){
    while(n--){hostTick+=10;TimeEvt_tick();Ui_drain_();render();}
}
// The root is the sole persistent screen child and always visible.
static void checkRootOnly(void){
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(lv_obj_get_parent(Ui_sm_.root.container)
           ==lv_screen_active());
    assert(!lv_obj_get_child_count(Ui_sm_.root.container));
    assert(!lv_obj_has_flag(Ui_sm_.root.container,LV_OBJ_FLAG_HIDDEN));
}
static void blank(void){
    checkRootOnly();
    assert(!lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0));
    for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
}
// Rightmost non-black pixel column inside an object's coords.
static int rightEdge_(lv_obj_t const *o){
    lv_area_t a;lv_obj_get_coords(o,&a);
    for(int x=a.x2;x>=a.x1;--x)
        for(int y=a.y1;y<=a.y2;++y)
            if(framebuffer[y*240+x])return x;
    return a.x1-1;
}
static unsigned countObjs_(lv_obj_t const *o){
    unsigned n=1;
    for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)
        n+=countObjs_(lv_obj_get_child(o,(int32_t)i));
    return n;
}
// Style-free child placed at pos; returns the object for coords.
static lv_obj_t *flexChild_(lv_obj_t *parent,int32_t w,int32_t h){
    lv_obj_t * const child=lv_obj_create(parent);
    lv_obj_remove_style_all(child);
    lv_obj_set_size(child,w,h);
    return child;
}
static void checkPads_(lv_obj_t const *o){
    assert(lv_obj_get_style_pad_top(o,0)==7);
    assert(lv_obj_get_style_pad_bottom(o,0)==9);
    assert(lv_obj_get_style_pad_left(o,0)==3);
    assert(lv_obj_get_style_pad_right(o,0)==5);
    assert(lv_obj_get_style_pad_row(o,0)==6);
    assert(lv_obj_get_style_pad_column(o,0)==4);
    assert(lv_obj_get_style_layout(o,0)==LV_LAYOUT_FLEX);
    assert(lv_obj_get_style_flex_flow(o,0)==LV_FLEX_FLOW_ROW
           ||lv_obj_get_style_flex_flow(o,0)
             ==LV_FLEX_FLOW_ROW_WRAP
           ||lv_obj_get_style_flex_flow(o,0)==LV_FLEX_FLOW_COLUMN);
    assert(!lv_obj_get_style_bg_opa(o,0));
    assert(!lv_obj_get_style_border_width(o,0));
    assert(!lv_obj_get_style_shadow_width(o,0));
    assert(!lv_obj_has_flag_any(o,
           LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
}
static int32_t xOf_(lv_obj_t const *o){
    lv_area_t a;lv_obj_get_coords(o,&a);return a.x1;
}
static int32_t yOf_(lv_obj_t const *o){
    lv_area_t a;lv_obj_get_coords(o,&a);return a.y1;
}
// Shared flex-layout helper: geometry for row, column and wrap.
static void testUiFlex_(lv_obj_t *root,lv_display_t *display){
    // A) row 100x40, all six pads, END alignment exercises edges.
    UiFlexConfig cfg={.width=100,.height=40,.flow=LV_FLEX_FLOW_ROW,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,
        .trackPlace=LV_FLEX_ALIGN_START,
        .padTop=7,.padBottom=9,.padLeft=3,.padRight=5,
        .padRow=6,.padColumn=4};
    lv_obj_t *box=FlexContainer_create(root,&cfg);
    lv_obj_t *c0=flexChild_(box,10,10),*c1=flexChild_(box,10,10);
    cfg.width=999; // Borrowed for the call only; not retained.
    lv_obj_update_layout(root);
    assert(lv_obj_get_width(box)==100);
    assert(xOf_(c0)==3&&yOf_(c0)==7);
    assert(xOf_(c1)==17&&yOf_(c1)==7);
    checkPads_(box);
    // Track-place END pushes the track to the bottom pad edge.
    lv_obj_set_flex_align(box,LV_FLEX_ALIGN_END,LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END);
    lv_obj_update_layout(root);
    assert(xOf_(c0)==71&&yOf_(c0)==21);
    assert(xOf_(c1)==85&&yOf_(c1)==21);
    lv_obj_delete(box);
    assert(lv_obj_get_child_count(root)==0);
    // B) column 100x60, padRow drives the vertical gap.
    box=FlexContainer_create(root,&(UiFlexConfig){
        .width=100,.height=60,.flow=LV_FLEX_FLOW_COLUMN,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,
        .trackPlace=LV_FLEX_ALIGN_START,
        .padTop=7,.padBottom=9,.padLeft=3,.padRight=5,
        .padRow=6,.padColumn=4});
    c0=flexChild_(box,10,10);c1=flexChild_(box,10,10);
    lv_obj_update_layout(root);
    assert(xOf_(c0)==3&&yOf_(c0)==7);
    assert(xOf_(c1)==3&&yOf_(c1)==23);
    checkPads_(box);
    lv_obj_delete(box);
    assert(lv_obj_get_child_count(root)==0);
    // C) row-wrap 40x60: second child wraps to track 2, row gap.
    box=FlexContainer_create(root,&(UiFlexConfig){
        .width=40,.height=60,.flow=LV_FLEX_FLOW_ROW_WRAP,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,
        .trackPlace=LV_FLEX_ALIGN_START,
        .padTop=7,.padBottom=9,.padLeft=3,.padRight=5,
        .padRow=6,.padColumn=4});
    c0=flexChild_(box,20,10);c1=flexChild_(box,20,10);
    lv_obj_update_layout(root);
    assert(xOf_(c0)==3&&yOf_(c0)==7);
    assert(xOf_(c1)==3&&yOf_(c1)==23);
    checkPads_(box);
    lv_obj_delete(box);
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    lv_refr_now(display);blank();
}
// Shared label helper: config, borrowed text, repaint on notify.
static void testLabel_(lv_obj_t *root,lv_display_t *display){
    static char text[32]="123";
    LabelConfig cfg={
        .width=90,.height=LV_SIZE_CONTENT,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text=text};
    lv_obj_t *label=Label_create(root,&cfg);
    cfg.width=1;cfg.text="X"; // Not retained.
    cfg.ignoreLayout=&(LabelIgnoreLayout){.x=7,.y=9}; // Ditto.
    lv_obj_add_style(label,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_update_layout(root);
    assert(lv_obj_get_parent(label)==root);
    assert(lv_obj_get_child_count(root)==1);
    assert(!lv_obj_has_flag(label,LV_OBJ_FLAG_IGNORE_LAYOUT));
    lv_area_t pa;lv_obj_get_coords(label,&pa);
    assert(pa.x1==0&&pa.y1==0); // Root content origin.
    assert(lv_label_get_text(label)==text); // Borrowed, no copy.
    assert(!strcmp(lv_label_get_text(label),"123"));
    lv_area_t a;lv_obj_get_coords(label,&a);
    assert(a.x2-a.x1+1==90);
    assert(a.y2-a.y1+1==lv_font_montserrat_14.line_height);
    assert(lv_obj_get_style_text_font(label,0)
           ==&lv_font_montserrat_14);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(label,0))
            &0xFFFFFFU)==0xFFFFFFU); // From UiStyle_topbarText.
    assert(lv_label_get_long_mode(label)==LV_LABEL_LONG_MODE_CLIP);
    assert(!lv_obj_get_style_bg_opa(label,0));
    assert(!lv_obj_get_style_border_width(label,0));
    assert(!lv_obj_get_style_shadow_width(label,0));
    lv_refr_now(display);
    // Buffer edit + notification refreshes the same borrowed label.
    static uint16_t before[240*320];
    (void)memcpy(before,framebuffer,sizeof before);
    text[0]='9';text[1]='\0';
    lv_label_set_text_static(label,text);
    assert(lv_label_get_text(label)==text);
    assert(!strcmp(lv_label_get_text(label),"9"));
    lv_refr_now(display);
    assert(memcmp(before,framebuffer,sizeof before)!=0);
    verifyRepaint(display);
    lv_obj_delete(label);
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
    // General config: different width/mode still wraps to content.
    lv_obj_t *wrap=Label_create(root,&(LabelConfig){
        .width=32,.height=LV_SIZE_CONTENT,
        .longMode=LV_LABEL_LONG_MODE_WRAP,
        .text="ABCD ABCD ABCD"});
    lv_obj_add_style(wrap,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_update_layout(root);
    lv_obj_get_coords(wrap,&a);
    assert(a.x2-a.x1+1==32);
    assert(lv_label_get_long_mode(wrap)==LV_LABEL_LONG_MODE_WRAP);
    assert(a.y2-a.y1+1>lv_font_montserrat_14.line_height);
    lv_obj_delete(wrap);
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    lv_refr_now(display);blank();
}
// textAlign aligns the text INSIDE the label box, not the object.
static void testLabelTextAlign_(lv_obj_t *root,
                                 lv_display_t *display){
    static char text[]="123";
    lv_point_t sz;
    lv_text_get_size(&sz,text,&lv_font_montserrat_14,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(sz.x<100&&sz.y<=16);
    lv_text_align_t const aligns[]={
        LV_TEXT_ALIGN_AUTO,LV_TEXT_ALIGN_LEFT,
        LV_TEXT_ALIGN_CENTER,LV_TEXT_ALIGN_RIGHT};
    int32_t autoEdge=-1;
    for(unsigned i=0;i<4;++i){
        LabelConfig cfg={
            .width=100,.height=16,
            .longMode=LV_LABEL_LONG_MODE_CLIP,
            .text=text};
        // The first case leaves textAlign at its zero default.
        if(i!=0)cfg.textAlign=aligns[i];
        lv_obj_t *label=Label_create(root,&cfg);
        lv_obj_add_style(label,&UiStyle_topbarText,LV_PART_MAIN);
        if(i!=0)cfg.textAlign=LV_TEXT_ALIGN_AUTO;
        lv_obj_set_pos(label,10,20); // Fixture only.
        lv_obj_update_layout(root);
        assert(lv_obj_get_style_text_align(label,0)
               ==aligns[i]);
        lv_area_t a;lv_obj_get_coords(label,&a);
        assert(a.x1==10&&a.y1==20);
        assert(a.x2-a.x1+1==100&&a.y2-a.y1+1==16);
        assert(!lv_obj_has_flag(label,
                 LV_OBJ_FLAG_IGNORE_LAYOUT));
        assert(lv_label_get_text(label)==text);
        lv_point_t pos;
        lv_label_get_letter_pos(label,0,&pos);
        int32_t const expected=i<2?0
            :(aligns[i]==LV_TEXT_ALIGN_CENTER
              ?(100-sz.x+1)/2:100-sz.x); // LVGL rounds up.
        assert(pos.x==expected&&pos.y==0);
        lv_refr_now(display);verifyRepaint(display);
        int32_t const edge=rightEdge_(label);
        // Ink shifts by the floored split: CENTER rounds the
        // letter-advance position up but the raster floor.
        int32_t const expectedInk=i<2?0
            :(aligns[i]==LV_TEXT_ALIGN_CENTER
              ?(100-sz.x)/2:100-sz.x);
        if(i==0){
            autoEdge=edge;
        }else{
            assert(edge==autoEdge+expectedInk);
        }
        lv_obj_delete(label);
        assert(lv_obj_get_child_count(root)==0);
        lv_refr_now(display);blank();
    }
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
}
// Label positioning: content-area origin, Flex bypass, border.
static void testLabelPosition_(lv_obj_t *root,
                               lv_display_t *display){
    lv_obj_t *box=FlexContainer_create(root,&(UiFlexConfig){
        .width=180,.height=100,.flow=LV_FLEX_FLOW_ROW,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,
        .trackPlace=LV_FLEX_ALIGN_START,
        .padTop=9,.padBottom=3,.padLeft=7,.padRight=5,
        .padRow=0,.padColumn=4});
    lv_obj_set_pos(box,20,30);
    lv_obj_set_style_border_width(box,2,0);
    // Content origin = (20+2+7, 30+2+9) = (29,41).
    lv_obj_t *a1=Label_create(box,&(LabelConfig){
        .width=10,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text="A"});
    lv_obj_add_style(a1,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_t *a2=Label_create(box,&(LabelConfig){
        .width=10,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text="B"});
    lv_obj_add_style(a2,&UiStyle_topbarText,LV_PART_MAIN);
    LabelIgnoreLayout pos={.x=11,.y=13};
    LabelConfig bcfg={
        .width=20,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text="C",
        .ignoreLayout=&pos};
    lv_obj_t *b1=Label_create(box,&bcfg);
    lv_obj_add_style(b1,&UiStyle_topbarText,LV_PART_MAIN);
    pos.x=99;pos.y=88; // Config+pointee not retained.
    bcfg.ignoreLayout=NULL;
    lv_obj_update_layout(root);
    lv_area_t r1,r2,r3;
    lv_obj_get_coords(a1,&r1);
    lv_obj_get_coords(a2,&r2);
    lv_obj_get_coords(b1,&r3);
    assert(r1.x1==29&&r1.y1==41); // Flex owns normals.
    assert(r2.x1==43&&r2.y1==41); // Ignored label takes no slot.
    assert(r3.x1==40&&r3.y1==54); // Origin + (11,13).
    assert(!lv_obj_has_flag(a1,LV_OBJ_FLAG_IGNORE_LAYOUT));
    assert(lv_obj_has_flag(b1,LV_OBJ_FLAG_IGNORE_LAYOUT));
    assert(lv_obj_get_style_align(b1,0)==LV_ALIGN_TOP_LEFT);
    // Move the parent: ignored label follows, normals re-flow.
    lv_obj_set_pos(box,25,37);
    lv_obj_update_layout(root);
    lv_obj_get_coords(a1,&r1);
    lv_obj_get_coords(a2,&r2);
    lv_obj_get_coords(b1,&r3);
    assert(r1.x1==34&&r1.y1==48);
    assert(r2.x1==48&&r2.y1==48);
    assert(r3.x1==45&&r3.y1==61); // New origin (34,48)+(11,13).
    lv_obj_delete(box);
    assert(lv_obj_get_child_count(root)==0);
    // No-Flex parent: requested coords apply verbatim.
    lv_obj_t *plain=lv_obj_create(root);
    lv_obj_remove_style_all(plain);
    lv_obj_set_size(plain,100,80);
    lv_obj_set_pos(plain,3,4);
    lv_obj_t *c1=Label_create(plain,&(LabelConfig){
        .width=20,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text="D"});
    lv_obj_add_style(c1,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_t *c2=Label_create(plain,&(LabelConfig){
        .width=20,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,
        .text="E",
        .ignoreLayout=&(LabelIgnoreLayout){.x=8,.y=6}});
    lv_obj_add_style(c2,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_update_layout(root);
    lv_obj_get_coords(c1,&r1);
    assert(r1.x1==3&&r1.y1==4); // NULL: default position.
    lv_obj_get_coords(c2,&r1);
    assert(r1.x1==11&&r1.y1==10); // Plain origin (3,4)+(8,6).
    assert(lv_obj_has_flag(c2,LV_OBJ_FLAG_IGNORE_LAYOUT));
    lv_obj_delete(plain);
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    lv_refr_now(display);blank();
}
static void testArc_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    ArcConfig config={.width=100,.height=100,
        .startAngle=207,.endAngle=333,
        .minValue=-10,.maxValue=10,.value=0};
    uint16_t const animations=lv_anim_count_running();
    lv_obj_t * const arc=Arc_create(root,&config);
    config.width=999;config.startAngle=0;config.value=10;
    lv_obj_update_layout(root);
    assert(lv_obj_check_type(arc,&lv_arc_class));
    assert(lv_obj_get_parent(arc)==root&&countObjs_(arc)==1);
    assert(lv_obj_get_width(arc)==100&&lv_obj_get_height(arc)==100);
    assert(lv_arc_get_bg_angle_start(arc)==207);
    assert(lv_arc_get_bg_angle_end(arc)==333);
    assert(lv_arc_get_min_value(arc)==-10);
    assert(lv_arc_get_max_value(arc)==10&&lv_arc_get_value(arc)==0);
    assert(lv_arc_get_mode(arc)==LV_ARC_MODE_NORMAL);
    assert(lv_arc_get_rotation(arc)==0);
    assert(!lv_obj_has_flag_any(arc,
        LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
        |LV_OBJ_FLAG_IGNORE_LAYOUT));
    assert(lv_obj_get_style_bg_opa(arc,LV_PART_KNOB)==LV_OPA_TRANSP);
    assert(lv_anim_count_running()==animations);
    lv_refr_now(display);
    for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
    lv_obj_add_style(arc,&UiStyle_arcTrack,LV_PART_MAIN);
    lv_obj_add_style(arc,&UiStyle_arcIndicator,LV_PART_INDICATOR);
    assert(lv_style_is_const(&UiStyle_arcTrack));
    assert(lv_style_is_const(&UiStyle_arcIndicator));
    lv_arc_set_range(arc,0,100);
    int32_t const values[]={0,50,100};
    for(unsigned i=0;i<3;++i){
        lv_arc_set_value(arc,values[i]);
        lv_refr_now(display);
        assert(lv_arc_get_value(arc)==values[i]);
        uint16_t const top=framebuffer[2*240+50];
        assert(top==(i?0xBF10U:0xFFFFU));
        verifyRepaint(display);
        if(i==1)capture("arc.rgb565");
    }
    lv_arc_set_bg_angles(arc,300,60);
    assert(lv_arc_get_bg_angle_start(arc)==300);
    assert(lv_arc_get_bg_angle_end(arc)==60);
    verifyRepaint(display);
    lv_obj_delete(arc);
    assert(lv_obj_get_child_count(root)==1);
    assert(lv_obj_get_child(root,0)==sibling);
    lv_obj_delete(sibling);
    lv_obj_t * const box=FlexContainer_create(root,&(UiFlexConfig){
        .width=100,.height=50,.flow=LV_FLEX_FLOW_ROW,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,.trackPlace=LV_FLEX_ALIGN_START});
    lv_obj_t * const child=Arc_create(box,&(ArcConfig){
        .width=lv_pct(50),.height=40,.startAngle=0,.endAngle=180,
        .minValue=0,.maxValue=100,.value=0});
    lv_obj_update_layout(root);
    assert(lv_obj_get_width(child)==50&&lv_obj_get_height(child)==40);
    assert(lv_obj_get_parent(child)==box);
    lv_obj_delete(box); // Parent owns the native arc's lifetime.
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
}
static void testBar_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    BarConfig config={.width=120,.height=6,
        .minValue=0,.maxValue=100,.value=25};
    uint16_t const animations=lv_anim_count_running();
    lv_obj_t *bar=Bar_create(root,&config);
    config.width=999;config.value=99; // Config is not retained.
    lv_obj_update_layout(root);
    assert(lv_obj_check_type(bar,&lv_bar_class));
    assert(lv_obj_get_parent(bar)==root);
    assert(countObjs_(bar)==1);
    assert(lv_obj_get_width(bar)==120&&lv_obj_get_height(bar)==6);
    assert(lv_bar_get_min_value(bar)==0&&lv_bar_get_max_value(bar)==100);
    assert(lv_bar_get_value(bar)==25);
    assert(lv_bar_get_mode(bar)==LV_BAR_MODE_NORMAL);
    assert(lv_bar_get_orientation(bar)==LV_BAR_ORIENTATION_AUTO);
    assert(lv_anim_count_running()==animations);
    assert(!lv_obj_has_flag_any(bar,
        LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    assert(lv_obj_get_style_bg_opa(bar,LV_PART_MAIN)==LV_OPA_TRANSP);
    assert(lv_obj_get_style_bg_opa(bar,LV_PART_INDICATOR)==LV_OPA_TRANSP);
    lv_refr_now(display);
    for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
    lv_style_t const *styles[]={&UiStyle_barTrack,&UiStyle_barIndicator};
    for(unsigned i=0;i<2;++i){
        assert(lv_style_is_const(styles[i]));
        lv_style_value_t value;
        lv_style_prop_t props[]={LV_STYLE_WIDTH,LV_STYLE_HEIGHT,
            LV_STYLE_LAYOUT,LV_STYLE_PAD_LEFT,LV_STYLE_PAD_TOP};
        for(unsigned j=0;j<5;++j)
            assert(lv_style_get_prop(styles[i],props[j],&value)
                   ==LV_STYLE_RES_NOT_FOUND);
    }
    lv_obj_add_style(bar,&UiStyle_barTrack,LV_PART_MAIN);
    lv_obj_add_style(bar,&UiStyle_barIndicator,LV_PART_INDICATOR);
    assert(lv_obj_get_style_bg_opa(bar,LV_PART_MAIN)==LV_OPA_30);
    assert(lv_obj_get_style_bg_opa(bar,LV_PART_INDICATOR)==LV_OPA_COVER);
    assert(lv_obj_get_style_radius(bar,LV_PART_MAIN)==LV_RADIUS_CIRCLE);
    assert(lv_obj_get_style_radius(bar,LV_PART_INDICATOR)==LV_RADIUS_CIRCLE);
    lv_refr_now(display);
    uint16_t const track=framebuffer[3*240+110];
    assert(track!=0&&track!=0xFFFFU);
    assert(framebuffer[3*240+10]==0xFFFFU);
    int32_t values[]={0,50,100};
    for(unsigned i=0;i<3;++i){
        lv_bar_set_value(bar,values[i],LV_ANIM_OFF);
        assert(lv_bar_get_value(bar)==values[i]);
        lv_refr_now(display);
        assert(framebuffer[3*240+10]==(i ? 0xFFFFU : track));
        assert(framebuffer[3*240+110]==(i==2 ? 0xFFFFU : track));
        for(int y=0;y<320;++y)
            for(int x=0;x<240;++x)
                if(x>=120||y>=6)assert(!framebuffer[y*240+x]);
        verifyRepaint(display);
        if(i==1)capture("bar.rgb565");
    }
    lv_bar_set_range(bar,-10,10);
    lv_bar_set_value(bar,0,LV_ANIM_OFF);
    assert(lv_bar_get_min_value(bar)==-10&&lv_bar_get_max_value(bar)==10);
    assert(lv_bar_get_value(bar)==0);
    lv_refr_now(display);
    assert(framebuffer[3*240+10]==0xFFFFU);
    assert(framebuffer[3*240+110]==track);
    verifyRepaint(display);
    lv_obj_delete(bar);
    assert(lv_obj_get_child_count(root)==1);
    assert(lv_obj_get_child(root,0)==sibling);
    lv_obj_delete(sibling);
    lv_obj_t *box=FlexContainer_create(root,&(UiFlexConfig){
        .width=100,.height=20,.flow=LV_FLEX_FLOW_ROW,
        .mainPlace=LV_FLEX_ALIGN_START,
        .crossPlace=LV_FLEX_ALIGN_START,.trackPlace=LV_FLEX_ALIGN_START});
    config=(BarConfig){.width=lv_pct(50),.height=6,
        .minValue=0,.maxValue=32,.value=16};
    lv_obj_t *first=Bar_create(box,&config);
    lv_obj_t *second=Bar_create(box,&config);
    lv_obj_update_layout(root);
    assert(lv_obj_get_width(first)==50&&lv_obj_get_width(second)==50);
    assert(xOf_(first)==0&&xOf_(second)==50);
    lv_obj_delete(box);
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
}
// Shared const style: two visual props, no layout leakage.
static void testTextStyle_(lv_obj_t *root,lv_display_t *display){
    assert(lv_style_is_const(&UiStyle_topbarText));
    lv_style_value_t v;
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_TEXT_FONT,&v)
           ==LV_STYLE_RES_FOUND
           &&v.ptr==&lv_font_montserrat_14);
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_TEXT_COLOR,&v)
           ==LV_STYLE_RES_FOUND
           &&(lv_color_to_u32(v.color)&0xFFFFFFU)==0xFFFFFFU);
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_WIDTH,&v)
           ==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_HEIGHT,&v)
           ==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_LAYOUT,&v)
           ==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_topbarText,LV_STYLE_PAD_COLUMN,&v)
           ==LV_STYLE_RES_NOT_FOUND);
    lv_obj_t *a=Label_create(root,&(LabelConfig){
        .width=30,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,.text="x"});
    lv_obj_t *b=Label_create(root,&(LabelConfig){
        .width=70,.height=32,
        .longMode=LV_LABEL_LONG_MODE_CLIP,.text="y"});
    lv_obj_add_style(a,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_add_style(b,&UiStyle_topbarText,LV_PART_MAIN);
    lv_obj_set_pos(a,0,0);lv_obj_set_pos(b,50,0);
    lv_obj_update_layout(root);
    lv_area_t r;
    lv_obj_get_coords(a,&r);
    assert(r.x2-r.x1+1==30&&r.y2-r.y1+1==16);
    lv_obj_get_coords(b,&r);
    assert(r.x2-r.x1+1==70&&r.y2-r.y1+1==32);
    assert(lv_obj_get_style_text_font(a,0)
           ==&lv_font_montserrat_14);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(a,0))
            &0xFFFFFFU)==0xFFFFFFU);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(b,0))
            &0xFFFFFFU)==0xFFFFFFU);
    // A component-level mutable accent overrides shared props.
    static lv_style_t accent;
    lv_style_init(&accent);
    lv_style_set_text_color(&accent,lv_color_hex(0x12AB34U));
    lv_obj_add_style(a,&accent,LV_PART_MAIN);
    lv_obj_add_style(b,&accent,LV_PART_MAIN);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(a,0))
            &0xFFFFFFU)==0x12AB34U);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(b,0))
            &0xFFFFFFU)==0x12AB34U);
    lv_style_set_text_color(&accent,lv_color_hex(0x40E080U));
    lv_obj_report_style_change(&accent);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(a,0))
            &0xFFFFFFU)==0x40E080U);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(b,0))
            &0xFFFFFFU)==0x40E080U);
    lv_refr_now(display);verifyRepaint(display);
    lv_obj_delete(a);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(b,0))
            &0xFFFFFFU)==0x40E080U);
    lv_obj_delete(b);
    lv_style_reset(&accent); // Only after users are gone.
    // Const base is untouched and reusable.
    lv_obj_t *c=Label_create(root,&(LabelConfig){
        .width=30,.height=16,
        .longMode=LV_LABEL_LONG_MODE_CLIP,.text="z"});
    lv_obj_add_style(c,&UiStyle_topbarText,LV_PART_MAIN);
    assert((lv_color_to_u32(lv_obj_get_style_text_color(c,0))
            &0xFFFFFFU)==0xFFFFFFU);
    lv_obj_delete(c);
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    lv_refr_now(display);blank();
}
// PageTitle + BootCount composition: two independent components
// mounted under the same strip, borrowed-vs-owned semantics,
// const styles, lifecycle.
static void testPageTitleAndBootCount_(lv_obj_t *root,
                                        lv_display_t *display){
    static char title[16]="SysMonitor";
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_set_size(sibling,10,10);
    lv_obj_set_pos(sibling,0,40);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiPageTitle pt={0};
    UiPageTitle_create(&pt,root,236,17,title);
    assert(lv_obj_get_parent(pt.container)==root);
    assert(lv_obj_get_child_count(root)==2);
    assert(countObjs_(pt.container)==2); // Box + title only.
    assert(!lv_obj_has_flag_any(pt.container,
        LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    assert(!lv_obj_get_style_border_width(pt.container,0));
    assert(lv_obj_get_style_layout(pt.container,0)
           ==LV_LAYOUT_NONE);
    UiBootCount bc={0};
    UiBootCount_create(&bc,pt.container,106,12);
    lv_obj_align(bc.container,LV_ALIGN_RIGHT_MID,0,0);
    assert(countObjs_(bc.container)==3);
    assert(countObjs_(pt.container)==5);
    lv_obj_update_layout(root);
    lv_area_t a,ta,ma,ba,ca;
    lv_obj_get_coords(pt.container,&a);
    assert(a.x2-a.x1+1==236&&a.y2-a.y1+1==17);
    lv_obj_get_coords(pt.title,&ta);
    assert(ta.x1-a.x1==0&&ta.y1-a.y1==0);
    assert(ta.x2-ta.x1+1==81&&ta.y2-ta.y1+1==17);
    assert(lv_label_get_text(pt.title)==title);
    assert(!strcmp(lv_label_get_text(pt.title),"SysMonitor"));
    assert(lv_obj_get_style_text_font(pt.title,0)
           ==&lv_font_montserrat_14);
    assert((lv_color_to_u32(
        lv_obj_get_style_text_color(pt.title,0))
        &0xFFFFFFU)==0xFFFFFFU);
    lv_obj_t * const meta=bc.container;
    lv_obj_get_coords(meta,&ma);
    assert(ma.x2-ma.x1+1==106&&ma.y2-ma.y1+1==12);
    assert(ma.x2==a.x2);
    assert(ma.x1-a.x1==130);
    int32_t const mid=(ma.y1+ma.y2)-(a.y1+a.y2);
    assert(mid>=-1&&mid<=1);
    assert(lv_obj_get_style_flex_flow(meta,0)
           ==LV_FLEX_FLOW_ROW);
    assert(lv_obj_get_style_pad_top(meta,0)==0);
    assert(lv_obj_get_style_pad_bottom(meta,0)==0);
    assert(lv_obj_get_style_pad_left(meta,0)==0);
    assert(lv_obj_get_style_pad_right(meta,0)==0);
    assert(lv_obj_get_style_pad_row(meta,0)==0);
    assert(lv_obj_get_style_pad_column(meta,0)==1);
    lv_obj_t * const boots=lv_obj_get_child(meta,0);
    lv_obj_get_coords(boots,&ba);
    assert(ba.x2-ba.x1+1==37&&ba.y2-ba.y1+1==12);
    assert(!strcmp(lv_label_get_text(boots),"BOOTS"));
    assert(lv_obj_get_style_text_font(boots,0)
           ==&lv_font_montserrat_10);
    assert((lv_color_to_u32(
        lv_obj_get_style_text_color(boots,0))
        &0xFFFFFFU)==0xFFFFFFU);
    lv_obj_get_coords(bc.count,&ca);
    assert(ca.x2-ca.x1+1==68&&ca.y2-ca.y1+1==12);
    assert(lv_obj_get_style_text_font(bc.count,0)
           ==&lv_font_montserrat_10);
    assert((lv_color_to_u32(
        lv_obj_get_style_text_color(bc.count,0))
        &0xFFFFFFU)==0xFFFFFFU);
    assert(ca.x1==ba.x2+2); // Inclusive coords: 1px gap.
    assert(ta.x2<ma.x1); // Title ends before meta starts.
    assert(lv_label_get_text(bc.count)==bc.countText);
    assert(!strcmp(lv_label_get_text(bc.count),"0"));
    // New const styles stay layout-free.
    assert(lv_style_is_const(&UiStyle_pageTitleText));
    assert(lv_style_is_const(&UiStyle_bootCountText));
    lv_style_value_t sv;
    assert(lv_style_get_prop(&UiStyle_pageTitleText,
        LV_STYLE_WIDTH,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_pageTitleText,
        LV_STYLE_HEIGHT,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_pageTitleText,
        LV_STYLE_LAYOUT,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_pageTitleText,
        LV_STYLE_PAD_COLUMN,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_bootCountText,
        LV_STYLE_WIDTH,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_bootCountText,
        LV_STYLE_HEIGHT,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_bootCountText,
        LV_STYLE_LAYOUT,&sv)==LV_STYLE_RES_NOT_FOUND);
    assert(lv_style_get_prop(&UiStyle_bootCountText,
        LV_STYLE_PAD_COLUMN,&sv)==LV_STYLE_RES_NOT_FOUND);
    // Thousands-comma formatting across the uint32 domain.
    struct{uint32_t n;char const *e;}cases[]={
        {0U,"0"},{1U,"1"},{999U,"999"},{1000U,"1,000"},
        {1000000U,"1,000,000"},
        {1000000000U,"1,000,000,000"},
        {UINT32_MAX,"4,294,967,295"}};
    for(unsigned i=0;i<7;++i){
        UiBootCount_setCount(&bc,cases[i].n);
        assert(!strcmp(lv_label_get_text(bc.count),
                       cases[i].e));
        assert(lv_label_get_text(bc.count)==bc.countText);
    }
    verifyRepaint(display);
    static uint16_t before[240*320];
    // Max-domain width fits its box; font height fits 12.
    lv_point_t sz;
    lv_text_get_size(&sz,"4,294,967,295",
                     &lv_font_montserrat_10,0,0,100,
                     LV_TEXT_FLAG_NONE);
    assert(sz.x<=68&&sz.y<=12);
    // Title must fit its box at the compiled font.
    lv_point_t titleSize;
    lv_text_get_size(&titleSize,"SysMonitor",
                     &lv_font_montserrat_14,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    fprintf(stderr,"PageTitle SysMonitor: %ldx%ld;"
        " label %ldx%ld\n",(long)titleSize.x,
        (long)titleSize.y,(long)lv_obj_get_width(pt.title),
        (long)lv_obj_get_height(pt.title));
    assert(titleSize.x<=lv_obj_get_width(pt.title));
    assert(titleSize.y<=lv_obj_get_height(pt.title));
    // Shrink clears old glyphs; geometry stable.
    lv_obj_get_coords(bc.count,&ca);
    lv_point_t small;
    lv_text_get_size(&small,"1",&lv_font_montserrat_10,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(small.x>0);
    (void)memcpy(before,framebuffer,sizeof before);
    UiBootCount_setCount(&bc,1U);
    lv_refr_now(display);
    assert(memcmp(before,framebuffer,sizeof before)!=0);
    lv_area_t ca2;
    lv_obj_get_coords(bc.count,&ca2);
    assert(ca2.x1==ca.x1&&ca2.y1==ca.y1
           &&ca2.x2==ca.x2&&ca2.y2==ca.y2);
    assert(rightEdge_(bc.count)<ca.x1+small.x);
    verifyRepaint(display);
    // setTitle rebinds the same label without rebuilding, and
    // never disturbs the boot-count group.
    static char const altTitle[]="LiveShow";
    lv_obj_t * const titleObj=pt.title;
    lv_obj_t * const countObj=bc.count;
    UiPageTitle_setTitle(&pt,altTitle);
    assert(lv_label_get_text(pt.title)==altTitle);
    assert(!strcmp(lv_label_get_text(pt.title),"LiveShow"));
    assert(pt.title==titleObj&&countObjs_(pt.container)==5);
    assert(bc.count==countObj
           &&lv_label_get_text(bc.count)==bc.countText);
    lv_refr_now(display);verifyRepaint(display);
    UiPageTitle_setTitle(&pt,title);
    UiBootCount_setCount(&bc,UINT32_MAX);
    lv_refr_now(display);capture("page-title.rgb565");
    verifyRepaint(display);
    // Destroy child first, then the parent component.
    UiBootCount_destroy(&bc);
    assert(!bc.container&&!bc.count);
    assert(bc.countText[0]=='\0');
    assert(countObjs_(pt.container)==2);
    assert(lv_obj_get_parent(pt.title)==pt.container);
    UiBootCount_destroy(&bc); // Idempotent when empty.
    UiPageTitle_destroy(&pt);
    assert(!pt.container&&!pt.title);
    assert(lv_obj_get_child_count(root)==1);
    UiPageTitle_destroy(&pt); // Idempotent when empty.
    for(unsigned i=0;i<10;++i){
        UiPageTitle_create(&pt,root,236,17,title);
        UiBootCount_create(&bc,pt.container,106,12);
        UiBootCount_setCount(&bc,i*123456789U);
        UiBootCount_destroy(&bc);
        UiPageTitle_destroy(&pt);
    }
    assert(lv_obj_get_child_count(root)==1);
    lv_obj_delete(sibling);
    assert(lv_obj_get_child_count(root)==0);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    lv_refr_now(display);blank();
}
static void checkThreadCardResources_(UiThreadCard const *card){
    for(unsigned i=0;i<UI_THREAD_CARD_RESOURCE_COUNT;++i){
        UiThreadCardResource const *row=&card->resources[i];
        assert(lv_obj_get_parent(row->container)==card->container);
        assert(countObjs_(row->container)==4);
        assert(lv_obj_get_width(row->container)==232);
        assert(lv_obj_get_height(row->container)==23);
        assert(xOf_(row->container)==xOf_(card->container));
        assert(yOf_(row->container)-yOf_(card->container)==27+(int)i*33);
        assert(lv_obj_get_style_layout(row->container,0)==LV_LAYOUT_NONE);
        assert(lv_obj_get_parent(row->value)==row->container);
        assert(lv_obj_get_parent(row->unit)==row->container);
        assert(lv_obj_get_parent(row->bar)==row->container);
        assert(lv_obj_get_width(row->value)==174);
        assert(lv_obj_get_width(row->unit)==58);
        assert(xOf_(row->value)==xOf_(row->container));
        assert(xOf_(row->unit)-xOf_(row->container)==174);
        assert(yOf_(row->value)==yOf_(row->container));
        assert(yOf_(row->unit)==yOf_(row->container));
        assert(lv_obj_get_style_text_align(row->value,0)
               ==LV_TEXT_ALIGN_LEFT);
        assert(lv_obj_get_style_text_align(row->unit,0)
               ==LV_TEXT_ALIGN_RIGHT);
        assert(lv_label_get_long_mode(row->value)==LV_LABEL_LONG_MODE_CLIP);
        assert(lv_label_get_long_mode(row->unit)==LV_LABEL_LONG_MODE_CLIP);
        assert(lv_obj_check_type(row->bar,&lv_bar_class));
        assert(lv_obj_get_width(row->bar)==232);
        assert(lv_obj_get_height(row->bar)==6);
        assert(xOf_(row->bar)==xOf_(row->container));
        assert(yOf_(row->bar)-yOf_(row->container)==17);
        assert(lv_bar_get_min_value(row->bar)==0);
        assert(lv_bar_get_max_value(row->bar)==100);
        assert(lv_obj_get_style_bg_opa(row->bar,LV_PART_MAIN)==LV_OPA_30);
        assert(lv_obj_get_style_bg_opa(row->bar,LV_PART_INDICATOR)
               ==LV_OPA_COVER);
    }
}
static void testThreadCard_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiThreadCard card={0};
    UiThreadCard_destroy(&card);
    for(unsigned pass=0;pass<3;++pass){
        UiThreadCard_create(&card,root,232,83);
        lv_obj_update_layout(root);
        assert(lv_obj_get_parent(card.container)==root);
        assert(lv_obj_get_child_count(root)==2);
        assert(countObjs_(card.container)==12);
        assert(lv_obj_get_width(card.container)==232);
        assert(lv_obj_get_height(card.container)==83);
        assert(!lv_obj_has_flag_any(card.container,
            LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
            |LV_OBJ_FLAG_OVERFLOW_VISIBLE));
        assert(lv_obj_get_style_layout(card.container,0)==LV_LAYOUT_FLEX);
        assert(lv_obj_get_style_flex_flow(card.container,0)
               ==LV_FLEX_FLOW_COLUMN);
        assert(lv_obj_get_style_pad_row(card.container,0)==10);
        assert(lv_obj_get_style_bg_opa(card.container,0)==LV_OPA_TRANSP);
        assert(lv_obj_get_style_border_width(card.container,0)==0);
        assert(lv_obj_get_parent(card.header)==card.container);
        assert(lv_obj_get_parent(card.threadName)==card.header);
        assert(lv_obj_get_parent(card.priority)==card.header);
        assert(lv_obj_get_child_count(card.header)==2);
        assert(lv_obj_get_width(card.header)==232);
        assert(lv_obj_get_height(card.header)==17);
        assert(xOf_(card.header)==xOf_(card.container));
        assert(yOf_(card.header)==yOf_(card.container));
        assert(lv_obj_get_style_layout(card.header,0)==LV_LAYOUT_NONE);
        assert(lv_obj_get_width(card.threadName)==174);
        assert(lv_obj_get_width(card.priority)==58);
        assert(xOf_(card.threadName)==xOf_(card.header));
        assert(xOf_(card.priority)==xOf_(card.header)+174);
        assert(yOf_(card.threadName)==yOf_(card.header));
        assert(yOf_(card.priority)==yOf_(card.header));
        assert(lv_obj_get_style_align(card.threadName,0)==LV_ALIGN_LEFT_MID);
        assert(lv_obj_get_style_align(card.priority,0)==LV_ALIGN_RIGHT_MID);
        assert(lv_obj_get_style_text_align(card.threadName,0)
               ==LV_TEXT_ALIGN_LEFT);
        assert(lv_obj_get_style_text_align(card.priority,0)
               ==LV_TEXT_ALIGN_RIGHT);
        assert(lv_obj_get_style_text_font(card.threadName,0)
               ==&lv_font_montserrat_14);
        assert(lv_obj_get_style_text_font(card.priority,0)
               ==&lv_font_montserrat_14);
        assert(lv_label_get_long_mode(card.threadName)
               ==LV_LABEL_LONG_MODE_CLIP);
        assert(lv_label_get_long_mode(card.priority)
               ==LV_LABEL_LONG_MODE_CLIP);
        assert(!strcmp(lv_label_get_text(card.threadName),""));
        assert(!strcmp(lv_label_get_text(card.priority),""));
        checkThreadCardResources_(&card);
        for(unsigned i=0;i<UI_THREAD_CARD_RESOURCE_COUNT;++i){
            assert(!strcmp(lv_label_get_text(card.resources[i].value),""));
            assert(!strcmp(lv_label_get_text(card.resources[i].unit),""));
            assert(lv_bar_get_value(card.resources[i].bar)==0);
        }
        lv_refr_now(display);
        for(int y=0;y<320;++y)
            for(int x=0;x<240;++x){
                bool const track=(y>=44&&y<50)||(y>=77&&y<83);
                if(x>=232||!track)assert(!framebuffer[y*240+x]);
            }
        assert(framebuffer[47*240+10]!=0);
        assert(framebuffer[80*240+10]!=0);
        static char const name[]="Blinky";
        static char const priority[]="233";
        UiThreadCard_setHeader(&card,name,priority);
        assert(lv_label_get_text(card.threadName)==name);
        assert(lv_label_get_text(card.priority)==priority);
        lv_refr_now(display);
        assert(rightEdge_(card.threadName)>xOf_(card.threadName));
        assert(rightEdge_(card.priority)>xOf_(card.priority)+43);
        capture("thread-card-header.rgb565");
        verifyRepaint(display);
        UiThreadCard_setHeader(&card,
            "A very long thread name that must clip","999999999999");
        lv_refr_now(display);
        for(int y=0;y<320;++y)
            for(int x=0;x<240;++x){
                bool const track=(y>=44&&y<50)||(y>=77&&y<83);
                if(x>=232||(y>=17&&!track))
                    assert(!framebuffer[y*240+x]);
            }
        verifyRepaint(display);
        UiThreadCard_setHeader(&card,"I","1");
        lv_refr_now(display);verifyRepaint(display);
        assert(lv_obj_get_width(card.threadName)==174);
        assert(lv_obj_get_width(card.priority)==58);
        assert(rightEdge_(card.threadName)<xOf_(card.threadName)+15);
        assert(rightEdge_(card.priority)>xOf_(card.priority)+43);
        static char const stackValue[]="2048 / 8192";
        static char const stackUnit[]="B";
        static char const inboxValue[]="26 / 32";
        static char const inboxUnit[]="slots";
        uint16_t const animations=lv_anim_count_running();
        UiThreadCard_setResource(&card,UI_THREAD_CARD_STACK,
                                  stackValue,stackUnit,25);
        assert(lv_label_get_text(card.resources[0].value)==stackValue);
        assert(lv_label_get_text(card.resources[0].unit)==stackUnit);
        assert(lv_bar_get_value(card.resources[0].bar)==25);
        assert(lv_bar_get_value(card.resources[1].bar)==0);
        UiThreadCard_setResource(&card,UI_THREAD_CARD_INBOX,
                                  inboxValue,inboxUnit,81);
        assert(lv_label_get_text(card.resources[1].value)==inboxValue);
        assert(lv_label_get_text(card.resources[1].unit)==inboxUnit);
        assert(lv_bar_get_value(card.resources[1].bar)==81);
        assert(lv_anim_count_running()==animations);
        UiThreadCard_setHeader(&card,name,priority);
        lv_refr_now(display);
        capture("thread-card-resources.rgb565");
        verifyRepaint(display);
        static uint16_t before[240*320];
        memcpy(before,framebuffer,sizeof before);
        UiThreadCard_setResource(&card,UI_THREAD_CARD_STACK,
                                  "123456789012345678901234567890",
                                  "BYTES-TOO-LONG",100);
        lv_refr_now(display);
        unsigned changed=0;
        for(int y=0;y<320;++y)
            for(int x=0;x<240;++x)
                if(before[y*240+x]!=framebuffer[y*240+x]){
                    assert(x<232&&y>=27&&y<50);
                    ++changed;
                }
        assert(changed>0);
        assert(lv_label_get_text(card.resources[1].value)==inboxValue);
        assert(lv_bar_get_value(card.resources[1].bar)==81);
        verifyRepaint(display);
        UiThreadCard_setResource(&card,UI_THREAD_CARD_STACK,"1","B",0);
        UiThreadCard_setResource(&card,UI_THREAD_CARD_INBOX,"N/A","",0);
        lv_refr_now(display);verifyRepaint(display);
        assert(rightEdge_(card.resources[0].value)
               <xOf_(card.resources[0].value)+15);
        assert(rightEdge_(card.resources[1].unit)
               ==xOf_(card.resources[1].unit)-1);
        assert(lv_bar_get_value(card.resources[0].bar)==0);
        assert(lv_bar_get_value(card.resources[1].bar)==0);
        checkThreadCardResources_(&card);
        assert(countObjs_(card.container)==12);
        UiThreadCard_destroy(&card);
        assert(!card.container&&!card.header);
        assert(!card.threadName&&!card.priority);
        for(unsigned i=0;i<UI_THREAD_CARD_RESOURCE_COUNT;++i){
            assert(!card.resources[i].container&&!card.resources[i].value);
            assert(!card.resources[i].unit&&!card.resources[i].bar);
        }
        assert(lv_obj_get_child_count(root)==1);
        assert(lv_obj_get_child(root,0)==sibling);
        UiThreadCard_destroy(&card);
    }
    lv_obj_delete(sibling);
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
}
static void checkDots_(UiDots const *dots,lv_obj_t *parent,uint8_t count){
    assert(dots->container!=NULL);
    assert(lv_obj_get_parent(dots->container)==parent);
    assert(countObjs_(dots->container)==(unsigned)count+1);
    assert(lv_obj_get_width(dots->container)==(int32_t)count*12-6);
    assert(lv_obj_get_height(dots->container)==6);
    assert(lv_obj_get_child_count(dots->container)==count);
    assert(!lv_obj_has_flag_any(dots->container,
        LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    for(unsigned i=0;i<count;++i){
        lv_obj_t * const dot=lv_obj_get_child(dots->container,(int32_t)i);
        assert(lv_obj_get_parent(dot)==dots->container);
        assert(lv_obj_get_width(dot)==6);
        assert(lv_obj_get_height(dot)==6);
        assert(xOf_(dot)-xOf_(dots->container)==(int32_t)i*12);
        assert(yOf_(dot)==yOf_(dots->container));
        assert(!lv_obj_has_flag_any(dot,
            LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
            |LV_OBJ_FLAG_OVERFLOW_VISIBLE));
        assert(lv_obj_get_style_radius(dot,0)==LV_RADIUS_CIRCLE);
        assert(lv_obj_get_style_bg_opa(dot,0)==LV_OPA_COVER);
        uint32_t const c=lv_color_to_u32(
            lv_obj_get_style_bg_color(dot,0))&0xFFFFFFU;
        assert(c==(i==0?0x6FE97BU:0x334155U));
    }
    assert(lv_style_is_const(&UiStyle_dot));
    assert(lv_style_is_const(&UiStyle_dotActive));
}
static void testDots_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiDots dots={0};
    UiDots_destroy(&dots);
    uint8_t const counts[]={1,3,4};
    for(unsigned pass=0;pass<3;++pass){
        UiDots_create(&dots,root,counts[pass]);
        lv_obj_set_pos(dots.container,11,13);
        lv_obj_update_layout(root);
        assert(lv_obj_get_child_count(root)==2);
        assert(xOf_(dots.container)==11&&yOf_(dots.container)==13);
        checkDots_(&dots,root,counts[pass]);
        verifyRepaint(display);
        UiDots_destroy(&dots);
        assert(!dots.container);
        assert(lv_obj_get_child_count(root)==1);
        assert(lv_obj_get_child(root,0)==sibling);
        UiDots_destroy(&dots);
    }
    lv_obj_delete(sibling);
    lv_refr_now(display);blank();
}
// The Dots component is a sibling of the viewport, never a card child.
static void checkMonitorDots_(UiThreadMonitor const *monitor,
                              int32_t groupX,int32_t groupY){
    checkDots_(&monitor->dots,monitor->container,3);
    assert(lv_obj_get_parent(monitor->dots.container)!=monitor->viewport);
    assert(xOf_(monitor->dots.container)-xOf_(monitor->container)==103);
    assert(yOf_(monitor->dots.container)-yOf_(monitor->container)==99);
    assert(xOf_(monitor->dots.container)==groupX);
    assert(yOf_(monitor->dots.container)==groupY);
}
// Pack an 8-bit-per-channel RGB triple into RGB565.
static uint16_t pack565_(unsigned r,unsigned g,unsigned b){
    return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
static void testThreadMonitor_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiThreadMonitor monitor={0};
    UiThreadMonitor_destroy(&monitor);
    for(unsigned pass=0;pass<3;++pass){
        UiThreadMonitor_create(&monitor,root,236,105);
        lv_obj_update_layout(root);
        assert(lv_obj_get_child_count(root)==2);
        assert(lv_obj_get_parent(monitor.container)==root);
        assert(lv_obj_get_child_count(monitor.container)==2);
        assert(countObjs_(monitor.container)==30);
        assert(lv_obj_get_parent(monitor.viewport)==monitor.container);
        assert(lv_obj_get_child_count(monitor.viewport)==2);
        assert(lv_obj_get_width(monitor.container)==236);
        assert(lv_obj_get_height(monitor.container)==105);
        assert(lv_obj_get_width(monitor.viewport)==232);
        assert(lv_obj_get_height(monitor.viewport)==83);
        assert(xOf_(monitor.viewport)-xOf_(monitor.container)==2);
        assert(yOf_(monitor.viewport)-yOf_(monitor.container)==6);
        assert(lv_obj_get_style_layout(monitor.viewport,0)
               ==LV_LAYOUT_NONE);
        lv_obj_t *objects[]={monitor.container,monitor.viewport,
                             monitor.cards[0].container,
                             monitor.cards[1].container};
        for(unsigned i=0;i<4;++i){
            assert(!lv_obj_has_flag_any(objects[i],
                LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
                |LV_OBJ_FLAG_OVERFLOW_VISIBLE));
            assert(lv_obj_get_style_bg_opa(objects[i],0)==LV_OPA_TRANSP);
            assert(lv_obj_get_style_border_width(objects[i],0)==0);
        }
        for(unsigned i=0;i<2;++i){
            lv_obj_t * const card=monitor.cards[i].container;
            assert(lv_obj_get_parent(card)==monitor.viewport);
            assert(lv_obj_get_child_count(card)==3);
            assert(lv_obj_get_width(card)==232);
            assert(lv_obj_get_height(card)==83);
            assert(xOf_(card)-xOf_(monitor.viewport)==(int)i*232);
            assert(yOf_(card)==yOf_(monitor.viewport));
            assert(lv_obj_get_style_layout(card,0)==LV_LAYOUT_FLEX);
            checkThreadCardResources_(&monitor.cards[i]);
        }
        checkMonitorDots_(&monitor,
                          xOf_(monitor.container)+103,
                          yOf_(monitor.container)+99);
        lv_area_t dotsArea;
        lv_obj_get_coords(monitor.dots.container,&dotsArea);
        // Recolor tracks too, so each slot is a solid clipping reference.
        lv_obj_set_style_bg_color(monitor.cards[0].container,
                                   lv_color_hex(0xFF0000),0);
        lv_obj_set_style_bg_color(monitor.cards[1].container,
                                   lv_color_hex(0x0000FF),0);
        lv_obj_set_style_bg_opa(monitor.cards[0].container,LV_OPA_COVER,0);
        lv_obj_set_style_bg_opa(monitor.cards[1].container,LV_OPA_COVER,0);
        for(unsigned i=0;i<2;++i)
            for(unsigned j=0;j<UI_THREAD_CARD_RESOURCE_COUNT;++j){
                lv_obj_t *bar=monitor.cards[i].resources[j].bar;
                lv_color_t const color=lv_color_hex(i ? 0x0000FF : 0xFF0000);
                lv_obj_set_style_bg_color(bar,color,LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar,color,LV_PART_INDICATOR);
            }
        lv_area_t viewport;
        lv_obj_get_coords(monitor.viewport,&viewport);
        uint16_t dotPixels[30*6];
        for(unsigned slot=0;slot<2;++slot){
            if(slot){
                lv_obj_set_x(monitor.cards[0].container,-232);
                lv_obj_set_x(monitor.cards[1].container,0);
            }
            lv_refr_now(display);
            checkMonitorDots_(&monitor,dotsArea.x1,dotsArea.y1);
            for(int y=0;y<6;++y)
                for(int x=0;x<30;++x){
                    uint16_t const pixel=framebuffer[
                        (dotsArea.y1+y)*240+dotsArea.x1+x];
                    if(slot)assert(dotPixels[y*30+x]==pixel);
                    else dotPixels[y*30+x]=pixel;
                }
            uint16_t const color=slot ? 0x001FU : 0xF800U;
            for(int y=0;y<320;++y){
                for(int x=0;x<240;++x){
                    bool const inView=x>=viewport.x1&&x<=viewport.x2
                        &&y>=viewport.y1&&y<=viewport.y2;
                    bool const inDots=x>=dotsArea.x1&&x<=dotsArea.x2
                        &&y>=dotsArea.y1&&y<=dotsArea.y2;
                    if(inDots)continue;
                    assert(framebuffer[y*240+x]==(inView ? color : 0));
                }
            }
            // Exact dot pixels: 3 circles active/inactive, gaps black.
            {
                uint16_t const active=pack565_(0x6F,0xE9,0x7B);
                uint16_t const inactive=pack565_(0x33,0x41,0x55);
                for(unsigned i=0;i<3;++i){
                    int32_t const cx=dotsArea.x1+(int32_t)i*12+3;
                    int32_t const cy=dotsArea.y1+3;
                    assert(framebuffer[cy*240+cx]
                           ==(i==0?active:inactive));
                }
                int32_t const cy=dotsArea.y1+3;
                for(unsigned g=0;g<2;++g){
                    int32_t const gx=dotsArea.x1+(int32_t)g*12+9;
                    assert(framebuffer[cy*240+gx]==0);
                }
            }
            assert(countObjs_(monitor.container)==30);
            verifyRepaint(display);
        }
        UiThreadMonitor_destroy(&monitor);
        assert(!monitor.container&&!monitor.viewport);
        assert(!monitor.cards[0].container&&!monitor.cards[1].container);
        assert(!monitor.dots.container);
        assert(lv_obj_get_child_count(root)==1);
        assert(lv_obj_get_child(root,0)==sibling);
        UiThreadMonitor_destroy(&monitor);
        lv_refr_now(display);
        for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
    }
    lv_obj_delete(sibling);
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
}
static void checkResourceCard_(UiResourceCard const *card){
    int32_t const x=xOf_(card->container);
    int32_t const y=yOf_(card->container);
    assert(lv_obj_get_width(card->container)==236);
    assert(lv_obj_get_height(card->container)==118);
    assert(countObjs_(card->container)==12);
    assert(lv_obj_get_parent(card->header)==card->container);
    assert(lv_obj_get_parent(card->sizeGroup)==card->header);
    assert(lv_obj_get_parent(card->gauge)==card->container);
    assert(lv_obj_get_parent(card->arc)==card->gauge);
    assert(lv_obj_get_parent(card->percent)==card->gauge);
    assert(lv_obj_get_parent(card->range)==card->container);
    assert(xOf_(card->name)==x+2&&yOf_(card->name)==y+1);
    assert(xOf_(card->sizeGroup)==x+207);
    assert(yOf_(card->sizeGroup)==y+1);
    assert(xOf_(card->gauge)==x+67&&yOf_(card->gauge)==y+32);
    assert(xOf_(card->arc)==x+68&&yOf_(card->arc)==y+38);
    assert(xOf_(card->percent)==x+100&&yOf_(card->percent)==y+53);
    assert(xOf_(card->range)==x&&yOf_(card->range)==y+71);
    assert(xOf_(card->minimum)==x+66);
    assert(xOf_(card->maximum)==x+136);
    assert(lv_obj_check_type(card->arc,&lv_arc_class));
    assert(lv_obj_get_width(card->arc)==100);
    assert(lv_obj_get_height(card->arc)==100);
    assert(lv_arc_get_bg_angle_start(card->arc)==207);
    assert(lv_arc_get_bg_angle_end(card->arc)==333);
    assert(lv_arc_get_value(card->arc)==0);
    assert(lv_obj_get_style_arc_width(card->arc,LV_PART_MAIN)==6);
    assert(lv_obj_get_style_arc_width(card->arc,LV_PART_INDICATOR)==6);
    assert(!lv_obj_has_flag(card->arc,LV_OBJ_FLAG_CLICKABLE));
    assert(!strcmp(lv_label_get_text(card->name),"--"));
    assert(!strcmp(lv_label_get_text(card->size),"--"));
    assert(!strcmp(lv_label_get_text(card->percent),"--%"));
    assert(!strcmp(lv_label_get_text(card->maximum),"--"));
}
static void checkResourceCardEmpty_(UiResourceCard const *card){
    assert(!card->container&&!card->header&&!card->sizeGroup);
    assert(!card->name&&!card->size&&!card->unit);
    assert(!card->gauge&&!card->arc&&!card->percent);
    assert(!card->range&&!card->minimum&&!card->maximum);
}
static void testResourceMonitor_(lv_obj_t *root,lv_display_t *display){
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiResourceCard card={0};
    UiResourceCard_destroy(&card);
    for(unsigned pass=0;pass<3;++pass){
        UiResourceCard_create(&card,root,236,118);
        lv_obj_update_layout(root);
        assert(lv_obj_get_parent(card.container)==root);
        checkResourceCard_(&card);
        assert(lv_obj_get_child_count(root)==2);
        assert(!lv_obj_has_flag_any(card.container,
            LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
            |LV_OBJ_FLAG_OVERFLOW_VISIBLE));
        lv_refr_now(display);
        unsigned white=0,green=0;
        for(unsigned i=0;i<240*320;++i){
            white+=framebuffer[i]==0xFFFFU;
            green+=framebuffer[i]==pack565_(0xBE,0xE0,0x86);
        }
        assert(white>0&&green==0);
        assert(framebuffer[40*240+118]==0xFFFFU);
        lv_arc_set_value(card.arc,50);
        lv_refr_now(display);
        green=0;
        for(unsigned i=0;i<240*320;++i)
            green+=framebuffer[i]==pack565_(0xBE,0xE0,0x86);
        assert(green>0);
        verifyRepaint(display);
        lv_arc_set_value(card.arc,100);
        verifyRepaint(display);
        lv_arc_set_value(card.arc,0);
        verifyRepaint(display);
        UiResourceCard_destroy(&card);
        checkResourceCardEmpty_(&card);
        assert(lv_obj_get_child_count(root)==1);
        assert(lv_obj_get_child(root,0)==sibling);
        UiResourceCard_destroy(&card);
    }
    UiResourceMonitor monitor={0};
    UiResourceMonitor_destroy(&monitor);
    for(unsigned pass=0;pass<3;++pass){
        UiResourceMonitor_create(&monitor,root,236,144);
        lv_obj_set_pos(monitor.container,2,174);
        lv_obj_update_layout(root);
        assert(lv_obj_get_parent(monitor.container)==root);
        assert(lv_obj_get_width(monitor.container)==236);
        assert(lv_obj_get_height(monitor.container)==144);
        assert(countObjs_(monitor.container)==32);
        assert(lv_obj_get_child_count(monitor.container)==2);
        assert(lv_obj_get_child_count(monitor.viewport)==2);
        assert(lv_obj_get_parent(monitor.viewport)==monitor.container);
        assert(lv_obj_get_width(monitor.viewport)==236);
        assert(lv_obj_get_height(monitor.viewport)==118);
        assert(xOf_(monitor.viewport)==2&&yOf_(monitor.viewport)==200);
        assert(!lv_obj_has_flag_any(monitor.viewport,
            LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE
            |LV_OBJ_FLAG_OVERFLOW_VISIBLE));
        assert(lv_obj_get_parent(monitor.indicatorContainer)
               ==monitor.container);
        assert(xOf_(monitor.indicatorContainer)==2);
        assert(yOf_(monitor.indicatorContainer)==271);
        checkDots_(&monitor.dots,monitor.indicatorContainer,4);
        assert(xOf_(monitor.dots.container)==99);
        assert(yOf_(monitor.dots.container)==287);
        for(unsigned i=0;i<2;++i){
            lv_obj_t *box=monitor.cards[i].container;
            assert(lv_obj_get_parent(box)==monitor.viewport);
            checkResourceCard_(&monitor.cards[i]);
            assert(xOf_(box)-xOf_(monitor.viewport)==(int32_t)i*236);
            assert(yOf_(box)==yOf_(monitor.viewport));
            for(uint32_t j=0;j<lv_obj_get_child_count(box);++j)
                lv_obj_add_flag(lv_obj_get_child(box,(int32_t)j),
                                 LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(box,
                lv_color_hex(i?0x0000FF:0xFF0000),0);
            lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);
        }
        for(unsigned slot=0;slot<2;++slot){
            if(slot){
                lv_obj_set_x(monitor.cards[0].container,-236);
                lv_obj_set_x(monitor.cards[1].container,0);
            }
            lv_refr_now(display);
            assert(countObjs_(monitor.container)==32);
            checkDots_(&monitor.dots,monitor.indicatorContainer,4);
            assert(xOf_(monitor.dots.container)==99);
            assert(yOf_(monitor.dots.container)==287);
            uint16_t const color=slot?0x001FU:0xF800U;
            for(int y=0;y<320;++y)
                for(int x=0;x<240;++x){
                    bool const inView=x>=2&&x<238&&y>=200&&y<318;
                    bool const inDots=x>=99&&x<141&&y>=287&&y<293;
                    if(!inDots)
                        assert(framebuffer[y*240+x]==(inView?color:0));
                }
            for(unsigned i=0;i<4;++i){
                assert(framebuffer[290*240+102+i*12]
                       ==(i?pack565_(0x33,0x41,0x55)
                           :pack565_(0x6F,0xE9,0x7B)));
                if(i<3)assert(framebuffer[290*240+108+i*12]==color);
            }
            if(!pass&&!slot)capture("resource-monitor.rgb565");
            verifyRepaint(display);
        }
        UiResourceMonitor_destroy(&monitor);
        assert(!monitor.container&&!monitor.viewport);
        assert(!monitor.indicatorContainer);
        checkResourceCardEmpty_(&monitor.cards[0]);
        checkResourceCardEmpty_(&monitor.cards[1]);
        assert(!monitor.dots.container);
        assert(lv_obj_get_child_count(root)==1);
        assert(lv_obj_get_child(root,0)==sibling);
        UiResourceMonitor_destroy(&monitor);
        lv_refr_now(display);
        for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
    }
    lv_obj_delete(sibling);
    assert(lv_obj_get_child_count(root)==0);
    lv_refr_now(display);blank();
}
static void testMonitorFill_(lv_obj_t *root){
    lv_obj_t * const parent=lv_obj_create(root);
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent,236,144);
    UiThreadMonitor thread={0};
    UiResourceMonitor resource={0};
    UiThreadMonitor_create(&thread,parent,lv_pct(100),lv_pct(100));
    UiResourceMonitor_create(&resource,parent,lv_pct(100),lv_pct(100));
    for(unsigned pass=0;pass<2;++pass){
        int32_t const width=pass?276:236,height=pass?164:144;
        lv_obj_set_size(parent,width,height);
        lv_obj_update_layout(root);
        assert(lv_obj_get_width(thread.container)==width);
        assert(lv_obj_get_height(thread.container)==height);
        assert(lv_obj_get_width(thread.viewport)==width-4);
        assert(lv_obj_get_height(thread.viewport)==height-22);
        assert(lv_obj_get_width(thread.cards[0].header)==width-4);
        assert(lv_obj_get_width(thread.cards[0].resources[0].bar)==width-4);
        assert(xOf_(thread.cards[1].container)-xOf_(thread.viewport)
               ==width-4);
        assert(lv_obj_get_width(resource.container)==width);
        assert(lv_obj_get_height(resource.container)==height);
        assert(lv_obj_get_height(resource.viewport)==height-26);
        assert(lv_obj_get_width(resource.cards[0].container)==width);
        assert(lv_obj_get_height(resource.cards[0].container)==height-26);
        assert(lv_obj_get_width(resource.cards[0].header)==width);
        assert(xOf_(resource.cards[1].container)-xOf_(resource.viewport)
               ==width);
        assert(xOf_(resource.dots.container)-xOf_(resource.container)
               ==(width-42)/2);
    }
    UiThreadMonitor_destroy(&thread);
    UiResourceMonitor_destroy(&resource);
    lv_obj_delete(parent);
}
static void checkSplash(bool image){
    checkRootOnly();
    assert(!lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0)
           ==!image);
    if(image){
        assert(lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0)
               ==&Ui_sm_.root.background);
    }
    assert(Ui_sm_.topbar.container==NULL);
    assert(Ui_sm_.pageTitle.container==NULL);
    assert(Ui_sm_.bootCount.container==NULL);
    assert(Ui_sm_.threadMonitor.container==NULL);
    assert(Ui_sm_.resourceMonitor.container==NULL);
}
// Figma's 2 px page spacing belongs to Root, not the Topbar.
static void checkTopbarPads_(lv_obj_t const *o){
    assert(lv_obj_get_style_pad_top(o,0)==0);
    assert(lv_obj_get_style_pad_bottom(o,0)==0);
    assert(lv_obj_get_style_pad_left(o,0)==0);
    assert(lv_obj_get_style_pad_right(o,0)==0);
    assert(lv_obj_get_style_pad_row(o,0)==0);
    assert(lv_obj_get_style_pad_column(o,0)==0);
}
static void checkTopbarLayout_(UiTopbar const *bar){
    lv_obj_t * const box=bar->container;
    int32_t const x=xOf_(box),y=yOf_(box);
    assert(lv_obj_get_width(box)==236&&lv_obj_get_height(box)==17);
    checkTopbarPads_(box);
    lv_obj_t * const left=lv_obj_get_parent(bar->led);
    lv_obj_t * const right=lv_obj_get_parent(bar->fps);
    assert(lv_obj_get_parent(left)==box);
    assert(lv_obj_get_parent(right)==box);
    assert(xOf_(left)==x&&yOf_(left)==y);
    assert(lv_obj_get_width(left)==99&&lv_obj_get_height(left)==17);
    assert(lv_obj_get_width(right)==93&&lv_obj_get_height(right)==17);
    assert(xOf_(bar->led)==x&&yOf_(bar->led)==y+4);
    assert(xOf_(bar->blinkyCount)==x+13);
    assert(xOf_(bar->fps)==x+143);
    assert(xOf_(bar->cpu)==x+200);
    // Figma's exclusive right edges are 101/200/238 on a 240 px page.
    assert(xOf_(bar->blinkyCount)+lv_obj_get_width(bar->blinkyCount)==x+99);
    assert(xOf_(bar->fps)+lv_obj_get_width(bar->fps)==x+198);
    assert(xOf_(bar->cpu)+lv_obj_get_width(bar->cpu)==x+236);
    assert(xOf_(bar->cpu)-xOf_(bar->fps)-lv_obj_get_width(bar->fps)==2);
    lv_obj_t * const labels[]={bar->blinkyCount,bar->fps,bar->cpu};
    for(unsigned i=0;i<3;++i){
        assert(yOf_(labels[i])==y&&lv_obj_get_height(labels[i])==17);
        assert(lv_obj_get_style_text_align(labels[i],0)==LV_TEXT_ALIGN_RIGHT);
    }
}
static void checkMountedMonitor_(UiThreadMonitor const *monitor){
    lv_obj_t * const box=monitor->container;
    assert(box!=NULL&&monitor->viewport!=NULL);
    assert(xOf_(box)==2&&yOf_(box)==59);
    assert(lv_obj_get_width(box)==236&&lv_obj_get_height(box)==105);
    assert(lv_obj_get_style_margin_left(box,0)==0);
    assert(lv_obj_get_style_margin_top(box,0)==2);
    assert(!lv_obj_has_flag(box,LV_OBJ_FLAG_IGNORE_LAYOUT));
    assert(xOf_(monitor->viewport)==4&&yOf_(monitor->viewport)==65);
    assert(countObjs_(box)==30);
    checkMonitorDots_(monitor,105,158);
    assert(xOf_(monitor->cards[0].container)==4);
    assert(xOf_(monitor->cards[1].container)==236);
    assert(yOf_(monitor->cards[0].container)==65);
    assert(yOf_(monitor->cards[1].container)==65);
    assert(!strcmp(lv_label_get_text(monitor->cards[0].threadName),"Thread"));
    assert(!strcmp(lv_label_get_text(monitor->cards[0].priority),"--"));
    for(unsigned i=0;i<2;++i){
        checkThreadCardResources_(&monitor->cards[i]);
        assert(!strcmp(lv_label_get_text(
            monitor->cards[0].resources[i].value),"--"));
        assert(lv_bar_get_value(monitor->cards[0].resources[i].bar)==0);
    }
}
// Figma lower slot: viewport (2,200), independent dots (99,287).
static void checkMountedResourceMonitor_(
    UiResourceMonitor const *monitor){
    lv_obj_t * const box=monitor->container;
    assert(box!=NULL&&monitor->viewport!=NULL);
    assert(xOf_(box)==2&&yOf_(box)==174);
    assert(lv_obj_get_width(box)==236
           &&lv_obj_get_height(box)==144);
    assert(lv_obj_get_style_margin_left(box,0)==0);
    assert(!lv_obj_has_flag(box,LV_OBJ_FLAG_IGNORE_LAYOUT));
    assert(countObjs_(box)==32);
    assert(xOf_(monitor->viewport)==2);
    assert(yOf_(monitor->viewport)==200);
    assert(lv_obj_get_width(monitor->viewport)==236);
    assert(lv_obj_get_height(monitor->viewport)==118);
    for(unsigned i=0;i<2;++i){
        lv_obj_t * const card=monitor->cards[i].container;
        assert(lv_obj_get_parent(card)==monitor->viewport);
        assert(xOf_(card)==(int32_t)(2+i*236));
        assert(yOf_(card)==200);
        checkResourceCard_(&monitor->cards[i]);
    }
    assert(lv_obj_get_parent(monitor->indicatorContainer)==box);
    checkDots_(&monitor->dots,monitor->indicatorContainer,4);
    assert(xOf_(monitor->dots.container)==99);
    assert(yOf_(monitor->dots.container)==287);
}
// Active leaves placement to the parent; edge spacing is Flex padding.
// Outside mounted page regions, framebuffer matches the background.
static void checkActiveTopbar(uint16_t const *bg,
                               bool expectPageTitle){
    lv_obj_t const * const root=Ui_sm_.root.container;
    lv_obj_t const * const top=Ui_sm_.topbar.container;
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(top&&lv_obj_get_child(root,0)==top);
    assert(lv_obj_get_parent(top)==root);
    assert(!lv_obj_has_flag(top,LV_OBJ_FLAG_HIDDEN));
    checkTopbarPads_(top);
    lv_obj_update_layout(root);
    lv_area_t a;lv_obj_get_coords(top,&a);
    assert(a.x1==2&&a.y1==2);
    checkTopbarLayout_(&Ui_sm_.topbar);
    assert(lv_obj_get_style_pad_top(root,0)==2);
    assert(lv_obj_get_style_pad_bottom(root,0)==2);
    assert(lv_obj_get_style_pad_left(root,0)==2);
    assert(lv_obj_get_style_pad_right(root,0)==2);
    lv_area_t ta={0,0,0,0};
    lv_area_t ma={0,0,0,0};
    lv_area_t ra={0,0,0,0};
    if(expectPageTitle){
        assert(Ui_sm_.pageTitle.container!=NULL);
        assert(lv_obj_get_parent(Ui_sm_.pageTitle.container)
               ==root);
        assert(countObjs_(Ui_sm_.pageTitle.container)==5);
        assert(lv_obj_get_child_count(root)==4);
        assert(lv_obj_get_child(root,1)
               ==Ui_sm_.pageTitle.container);
        assert(!strcmp(lv_label_get_text(
            Ui_sm_.pageTitle.title),"SysMonitor"));
        assert(Ui_sm_.bootCount.container!=NULL);
        assert(lv_obj_get_parent(Ui_sm_.bootCount.container)
               ==Ui_sm_.pageTitle.container);
        assert(countObjs_(Ui_sm_.bootCount.container)==3);
        assert(!strcmp(lv_label_get_text(
            Ui_sm_.bootCount.count),"1"));
        assert(lv_obj_get_style_flex_flow(root,0)
               ==LV_FLEX_FLOW_COLUMN);
        assert(lv_obj_get_style_pad_row(root,0)==10);
        lv_obj_get_coords(Ui_sm_.pageTitle.container,&ta);
        assert(ta.x2-ta.x1+1==236&&ta.y2-ta.y1+1==17);
        assert(ta.x1==a.x1);
        assert(ta.y1==30&&ta.y1-a.y2-1==11);
        checkMountedMonitor_(&Ui_sm_.threadMonitor);
        assert(lv_obj_get_parent(Ui_sm_.threadMonitor.container)==root);
        assert(lv_obj_get_child(root,2)==Ui_sm_.threadMonitor.container);
        lv_obj_get_coords(Ui_sm_.threadMonitor.container,&ma);
        checkMountedResourceMonitor_(&Ui_sm_.resourceMonitor);
        assert(lv_obj_get_parent(Ui_sm_.resourceMonitor.container)
               ==root);
        assert(lv_obj_get_child(root,3)
               ==Ui_sm_.resourceMonitor.container);
        lv_obj_get_coords(Ui_sm_.resourceMonitor.container,&ra);
    }
    else{
        assert(Ui_sm_.pageTitle.container==NULL);
        assert(Ui_sm_.bootCount.container==NULL);
        assert(Ui_sm_.bootCount.count==NULL);
        assert(Ui_sm_.threadMonitor.container==NULL);
        assert(Ui_sm_.threadMonitor.viewport==NULL);
        assert(Ui_sm_.resourceMonitor.container==NULL);
        assert(Ui_sm_.resourceMonitor.viewport==NULL);
        assert(lv_obj_get_child_count(root)==1);
    }
    char expectedCount[11];
    snprintf(expectedCount,sizeof expectedCount,"%lu",
              (unsigned long)NvmBlinky_get()->edges);
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.blinkyCount),
                   expectedCount));
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.fps),"30FPS"));
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.cpu),expectedCpu));
    if(bg!=NULL){
        assert(lv_obj_get_style_bg_image_src(root,0)
               ==&Ui_sm_.root.background);
        lv_image_dsc_t const *d=&Ui_sm_.root.background;
        assert(d->header.cf==LV_COLOR_FORMAT_RGB565);
        assert(d->header.w==240&&d->header.h==320);
        assert(d->header.stride==480);
        assert(d->data_size==ASSET_SLOT_BYTES);
        assert(d->data==(uint8_t const *)bg);
    }
    else{
        assert(!lv_obj_get_style_bg_image_src(root,0));
    }
    for(int32_t y=0;y<320;++y){
        for(int32_t x=0;x<240;++x){
            bool const inTop=x>=a.x1&&x<=a.x2
                              &&y>=a.y1&&y<=a.y2;
            bool const inTitle=expectPageTitle
                               &&x>=ta.x1&&x<=ta.x2
                               &&y>=ta.y1&&y<=ta.y2;
            bool const inMonitor=expectPageTitle
                                 &&x>=ma.x1&&x<=ma.x2
                                 &&y>=ma.y1&&y<=ma.y2;
            bool const inRes=expectPageTitle
                              &&x>=ra.x1&&x<=ra.x2
                              &&y>=ra.y1&&y<=ra.y2;
            if(inTop||inTitle||inMonitor||inRes)continue;
            size_t const i=(size_t)y*240U+(size_t)x;
            assert(framebuffer[i]==(bg!=NULL?bg[i]:0U));
        }
    }
}
// UiSysInfo helper: page background + PageTitle ops; never
// touches the shared Topbar, siblings, or the root object.
static void testUiSysInfo_(UiRoot *root,lv_display_t *display){
    lv_obj_t * const rc=root->container;
    // Restore UiRoot's layout after the preceding isolated fixtures.
    lv_obj_set_flex_flow(rc,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rc,LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(rc,2,LV_PART_MAIN);
    lv_obj_set_style_pad_row(rc,10,LV_PART_MAIN);
    UiTopbar bar={0};
    UiTopbar_create(&bar,rc,lv_pct(100),17);
    lv_obj_t * const barbox=bar.container;
    lv_obj_t * const sibling=lv_obj_create(rc);
    lv_obj_add_flag(sibling,LV_OBJ_FLAG_HIDDEN);
    UiPageTitle title={0};
    UiBootCount bc={0};
    UiThreadMonitor monitor={0};
    UiResourceMonitor res={0};
    UiSysInfo_build(root,&title,&bc,&monitor,&res,pixels[0]);
    assert(lv_obj_get_child_count(rc)==5);
    assert(lv_obj_get_parent(title.container)==rc);
    assert(lv_obj_get_parent(bc.container)==title.container);
    assert(countObjs_(bc.container)==3);
    assert(countObjs_(title.container)==5);
    assert(bar.container==barbox);
    assert(root->background.data==(uint8_t const *)pixels[0]);
    lv_obj_update_layout(rc);
    lv_area_t ba,ta;
    lv_obj_get_coords(barbox,&ba);
    lv_obj_get_coords(title.container,&ta);
    assert(ta.y1-ba.y2-1==11);
    checkTopbarLayout_(&bar);
    checkMountedMonitor_(&monitor);
    checkMountedResourceMonitor_(&res);
    verifyRepaint(display);
    UiSysInfo_hide(root,&title,&bc,&monitor,&res);
    assert(!title.container&&!title.title);
    assert(!bc.container&&!bc.count);
    assert(!monitor.container&&!monitor.viewport);
    assert(!res.container&&!res.viewport);
    assert(lv_obj_get_child_count(rc)==2);
    assert(lv_obj_get_parent(barbox)==rc);
    assert(!lv_obj_get_style_bg_image_src(rc,0));
    UiSysInfo_hide(root,&title,&bc,&monitor,&res); // Idempotent when empty.
    assert(lv_obj_get_child_count(rc)==2);
    // Rebuild only after hide; NULL pixels are a legitimate
    // black fallback and still mount the title.
    UiSysInfo_build(root,&title,&bc,&monitor,&res,NULL);
    assert(title.container!=NULL&&bc.container!=NULL);
    assert(root->background.data==NULL);
    assert(lv_obj_get_child_count(rc)==5);
    lv_obj_update_layout(rc);checkMountedMonitor_(&monitor);
    UiSysInfo_hide(root,&title,&bc,&monitor,&res);
    UiSysInfo_build(root,&title,&bc,&monitor,&res,pixels[1]);
    assert(root->background.data==(uint8_t const *)pixels[1]);
    assert(lv_obj_get_child_count(rc)==5);
    UiSysInfo_hide(root,&title,&bc,&monitor,&res);
    // Fixture teardown only -- not a production page exit.
    UiTopbar_destroy(&bar);
    assert(!bar.container&&!bar.blinkyCount);
    lv_obj_delete(sibling);
    assert(root->container==rc&&lv_obj_get_child_count(rc)==0);
    lv_obj_set_layout(rc,LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(rc,0,LV_PART_MAIN);
    lv_obj_set_style_pad_row(rc,0,LV_PART_MAIN);
    lv_refr_now(display);blank();
}
int main(int argc,char **argv){
    assert(argc==2);scenario=(unsigned)atoi(argv[1]);
    static NvmManager manager;
    NvmManager_ctor(&manager);
    NvmBoot_ctor(&manager);
    NvmBlinky_ctor(&manager);
    NvmBoot_inc();
    static STATIC_POOL_ELEM_TYPE(SST_Evt) small[48];
    static STATIC_POOL_ELEM_TYPE(Flash_Evt) medium[16];
    SST_EvtPool_init(small,sizeof small,sizeof(SST_Evt));
    SST_EvtPool_init(medium,sizeof medium,sizeof(Flash_Evt));
    SM_Tracer_inst.nTot=16;SM_Tracer_inst.nMin=10;
    assets=(Asset){.present=scenario!=4,.len=460800,.slots=3};
    char const *files[]={"figma/sys-page.rgb565", "figma/live-page.rgb565",
                         "bg-logo.rgb565"};
    for(unsigned i=0;i<3;++i){
        char path[512];
        snprintf(path,sizeof path,UI_TEST_ROOT "/bg/%s",files[i]);
        FILE *file=fopen(path,"rb");assert(file);
        assert(fread(pixels[i],1,sizeof pixels[i],file)==sizeof pixels[i]);
        fclose(file);
    }
    lv_init();lv_tick_set_cb(lvTick);
    lv_display_t *display=lv_display_create(240,320);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,drawBuffer,NULL,sizeof drawBuffer,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    Ui_start();Ui_bindDisplay_();Sm_Ui_init(&Ui_sm_);
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_standby);
    assert(lv_obj_get_style_flex_flow(Ui_sm_.root.container,0)
           ==LV_FLEX_FLOW_COLUMN);
    assert(lv_obj_get_style_pad_row(Ui_sm_.root.container,0)==10);
    checkRootOnly();
    assert(!lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0));
    assert(Ui_sm_.root.background.data==NULL);
    assert(Ui_sm_.topbar.container==NULL);
    assert(Ui_sm_.stateTimeout.remaining==0);
    SST_Evt const * const timeoutEvt=Ui_sm_.stateTimeout.event;
    assert(timeoutEvt!=NULL);
    assert(timeoutEvt->sig==UI_STATE_TIMEOUT_SIG);
    assert(timeoutEvt->poolId==0U);
    assert(timeoutEvt->refCtr==0U);
    TimeEvt_arm(&Ui_sm_.stateTimeout,1U,0U);
    TimeEvt_tick();
    SST_Evt const *timeout;
    assert(Ui_evtPop(&timeout));
    assert(timeout==timeoutEvt);
    SST_GC(timeout);
    assert(timeoutEvt->poolId==0U);
    assert(timeoutEvt->refCtr==0U);
    assert(!Ui_evtPop(&timeout));
    send(UI_LED_ON_SIG);send(UI_LED_OFF_SIG);
    assert(lv_obj_get_child_count(Ui_sm_.root.container)==0);
    NvmBlinky_add(50); // Business snapshot is independent of UI events.
    send(BRING_UP_READY_SIG);
    if(scenario==2)send(BRING_UP_ASSETS_LOADED_SIG);
    if(scenario==5){
        SST_Evt const *go;assert(Ui_evtPop(&go));
        for(unsigned i=0;i<20;++i){hostTick+=10;TimeEvt_tick();}
        SST_Evt const *pending;assert(!Ui_evtPop(&pending));
        Sm_Ui_dispatch(&Ui_sm_,go);SST_GC(go);
    }
    Ui_drain_();verifyRepaint(display);capture("splash.rgb565");
    checkSplash(scenario!=3);
    if(scenario!=3){
        assert(!memcmp(framebuffer,pixels[2],sizeof framebuffer));
        assert(Ui_sm_.root.background.data
               ==(uint8_t const *)pixels[2]);
    }else{
        assert(!lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0));
        for(unsigned i=0;i<240*320;++i)assert(!framebuffer[i]);
    }
    assert(backlight==1 && Ui_dbgSplashOk_==(scenario!=3));
    send(UI_LED_ON_SIG);send(UI_LED_OFF_SIG);
    assert(lv_obj_get_child_count(Ui_sm_.root.container)==0);
    assert(NvmBlinky_get()->edges==50); // UI events never update NVM.
    if(scenario==1){
        pulse(150);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_readyShow);
        checkSplash(true); // Splash survives the waiting half.
        send(BRING_UP_ASSETS_LOADED_SIG);
    }else if(scenario==2){pulse(150);}
    else if(scenario==3){pulse(1);send(BRING_UP_ASSETS_LOADED_SIG);}
    else if(scenario==5){
        assert(Ui_sm_.stateTimeout.remaining==150);
        send(BRING_UP_ASSETS_LOADED_SIG);pulse(140);
        for(unsigned i=0;i<20;++i){hostTick+=10;TimeEvt_tick();}
        assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_readyShow);
        checkSplash(true); // Splash survives the waiting half.
        Ui_drain_();assert(Ui_sm_.stateTimeout.remaining==0);
    }else{send(BRING_UP_ASSETS_LOADED_SIG);pulse(150);}
    render();
    verifyRepaint(display);
    capture("sys.rgb565");
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sysInfo);
    // Slot-0 page background mounted; NULL falls back to black.
    uint16_t const * const sysBg=scenario==4?NULL:pixels[0];
    uint16_t const * const liveBg=scenario==4?NULL:pixels[1];
    checkActiveTopbar(sysBg,true);
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.blinkyCount),"50"));
    NvmBlinky_add(1);
    send(UI_LED_ON_SIG);Ui_drain_();
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.blinkyCount),"51"));
    assert((lv_color_to_u32(lv_obj_get_style_bg_color(
        Ui_sm_.topbar.led,0))&0xFFFFFFU)==0x40E080U);
    send(UI_LED_OFF_SIG);Ui_drain_();
    assert((lv_color_to_u32(lv_obj_get_style_bg_color(
        Ui_sm_.topbar.led,0))&0xFFFFFFU)==0xD9D9D9U);
    // Independent periodic statistics update, without any LED event.
    assert(Ui_sm_.statsTimeout.interval==UI_STATS_TICKS);
    hostIdle+=25;hostTotal+=100;hostFps=27;
    pulse(UI_STATS_TICKS);
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.fps),"27FPS"));
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.cpu),"75%"));
    expectedCpu="75%";
    hostFps=30;send(UI_STATS_TIMEOUT_SIG);
    // No elapsed CPU sample retains the last valid reading.
    assert(!strcmp(lv_label_get_text(Ui_sm_.topbar.cpu),"75%"));
    verifyRepaint(display);
    // Native proof a live data update repaints the count inside
    // the real HSM page -- no ticks, no NVM, count restore after.
    lv_obj_t const * const persistent=Ui_sm_.topbar.container;
    lv_obj_t * const titleObj=Ui_sm_.pageTitle.title;
    lv_obj_t * const titleBox=Ui_sm_.pageTitle.container;
    UiBootCount_setCount(&Ui_sm_.bootCount,UINT32_MAX);
    assert(!strcmp(lv_label_get_text(Ui_sm_.bootCount.count),
                   "4,294,967,295"));
    lv_refr_now(display);verifyRepaint(display);
    capture("boot-count-live.rgb565");
    assert(Ui_sm_.topbar.container==persistent);
    assert(Ui_sm_.pageTitle.title==titleObj);
    assert(Ui_sm_.pageTitle.container==titleBox);
    UiBootCount_setCount(&Ui_sm_.bootCount,NvmBoot_get()->count);
    assert(!strcmp(lv_label_get_text(Ui_sm_.bootCount.count),"1"));
    lv_refr_now(display);verifyRepaint(display);
    pulse(499);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sysInfo);
    pulse(1);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sysInfo);
    send(UI_STATE_TIMEOUT_SIG);
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_liveShow);
    verifyRepaint(display);checkActiveTopbar(liveBg,false);
    capture("live.rgb565");
    assert(Ui_sm_.topbar.container==persistent); // Pages share it.
    send(UI_LED_ON_SIG);send(UI_LED_OFF_SIG);Ui_drain_();
    verifyRepaint(display);checkActiveTopbar(liveBg,false);
    assert(NvmBlinky_get()->edges==51);
    pulse(999);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_liveShow);
    pulse(1);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sysInfo);
    verifyRepaint(display);checkActiveTopbar(sysBg,true);
    for(unsigned i=0;i<3;++i){
        send(UI_STATE_TIMEOUT_SIG);pulse(1000);
    }
    verifyRepaint(display);checkActiveTopbar(sysBg,true);
    assert(Ui_sm_.topbar.container==persistent);
    // No production transition exits Active yet; invoke the page
    // action handlers directly to cover teardown. Direct entry
    // does not enter SysInfo.
    Sm_Ui_sysInfo.exit_(&Ui_sm_.sm_hsm_);
    // Isolated LiveShow teardown: its own exit must clear the bg
    // independently of SysInfo overwriting it.
    Sm_Ui_liveShow.entry_(&Ui_sm_.sm_hsm_);
    lv_refr_now(display);checkActiveTopbar(liveBg,false);
    Sm_Ui_liveShow.exit_(&Ui_sm_.sm_hsm_);
    assert(!lv_obj_get_style_bg_image_src(Ui_sm_.root.container,0));
    lv_refr_now(display);checkActiveTopbar(NULL,false);
    // No production exit path out of Active: the bar is constant.
    assert(Sm_Ui_active.exit_==NULL);
    assert(Ui_sm_.topbar.container==persistent);
    // TEST FIXTURE teardown only -- the whole UI test is ending,
    // this is not a production page exit.
    (void)TimeEvt_disarm(&Ui_sm_.statsTimeout);
    UiTopbar_destroy(&Ui_sm_.topbar);
    assert(!Ui_sm_.topbar.container&&!Ui_sm_.topbar.blinkyCount);
    assert(!Ui_sm_.pageTitle.container);
    assert(!Ui_sm_.bootCount.container);
    assert(!Ui_sm_.threadMonitor.container);
    assert(lv_obj_get_child_count(Ui_sm_.root.container)==0);
    // Reset the root layout so later isolated
    // component fixtures see a normal parent again.
    lv_obj_set_layout(Ui_sm_.root.container,LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(Ui_sm_.root.container,0,LV_PART_MAIN);
    lv_obj_set_style_pad_row(Ui_sm_.root.container,0,
                             LV_PART_MAIN);
    lv_refr_now(display);blank();
    // Root lifecycle: borrow, replace and hide on the same object.
    lv_obj_t * const root=Ui_sm_.root.container;
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(UiSplash_show(&Ui_sm_.root,pixels[0]));
    lv_refr_now(display);
    assert(!memcmp(framebuffer,pixels[0],sizeof framebuffer));
    UiRoot_setBackground(&Ui_sm_.root,pixels[1]);
    assert(lv_obj_get_style_bg_image_src(root,0)
           ==&Ui_sm_.root.background);
    assert(Ui_sm_.root.background.data==(uint8_t const *)pixels[1]);
    lv_refr_now(display);
    assert(!memcmp(framebuffer,pixels[1],sizeof framebuffer));
    UiSplash_hide(&Ui_sm_.root);
    lv_refr_now(display);blank();
    assert(!UiSplash_show(&Ui_sm_.root,NULL));
    lv_refr_now(display);blank();
    // Business children may coexist with background replacement.
    lv_obj_t * const child=lv_obj_create(root);
    UiRoot_setBackground(&Ui_sm_.root,pixels[0]);
    UiRoot_setBackground(&Ui_sm_.root,pixels[1]);
    assert(lv_obj_get_child_count(root)==1);
    assert(lv_obj_get_child(root,0)==child);
    lv_obj_delete(child);
    UiSplash_hide(&Ui_sm_.root);
    lv_refr_now(display);blank();
    assert(root==Ui_sm_.root.container);
    testUiSysInfo_(&Ui_sm_.root,display);
    // Passive Topbar component under the persistent root.
    lv_obj_t * const sibling=lv_obj_create(root);
    lv_obj_set_size(sibling,10,10);
    lv_obj_set_pos(sibling,0,40); // Below the strip, out of the shot.
    UiTopbar bar={0};
    UiTopbar_create(&bar,root,236,17);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(lv_obj_get_child_count(root)==2);
    assert(lv_obj_get_parent(bar.container)==root);
    lv_area_t a,barArea,ledA,countA,fpsA,cpuA;
    lv_obj_update_layout(root);
    lv_obj_get_coords(bar.container,&barArea);
    assert(barArea.x2-barArea.x1+1==236);
    assert(barArea.y2-barArea.y1+1==17);
    checkTopbarLayout_(&bar);
    checkTopbarPads_(bar.container);
    assert(lv_obj_get_style_flex_flow(bar.container,0)
           ==LV_FLEX_FLOW_ROW);
    assert(countObjs_(bar.container)==7); // bar+2 groups+LED+3 labels
    assert(!lv_obj_has_flag_any(bar.container,
           LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    assert(!lv_obj_has_flag_any(bar.led,
           LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    lv_obj_get_coords(bar.led,&a);
    assert(a.x2-a.x1+1==10 && a.y2-a.y1+1==10);
    assert(!strcmp(lv_label_get_text(bar.blinkyCount),"0"));
    assert(!strcmp(lv_label_get_text(bar.fps),"0FPS"));
    assert(!strcmp(lv_label_get_text(bar.cpu),"0%"));
    uint32_t const ledOff=
        lv_color_to_u32(lv_obj_get_style_bg_color(bar.led,0));
    assert((ledOff&0xFFFFFFU)==0xD9D9D9U);
    // Four pieces never overlap and stay put across value changes.
    lv_obj_get_coords(bar.led,&ledA);
    lv_obj_get_coords(bar.blinkyCount,&countA);
    lv_obj_get_coords(bar.fps,&fpsA);
    lv_obj_get_coords(bar.cpu,&cpuA);
    assert(ledA.x2<countA.x1 && countA.x2<fpsA.x1
           && fpsA.x2<cpuA.x1);
    // Isolated setter refresh: each change alone must repaint.
    static uint16_t prev[240*320];
    lv_refr_now(display); // Settle the initial "0" texts.
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setCount(&bar,UINT32_MAX);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setFps(&bar,999U);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setCpu(&bar,100U);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    // Maximum-domain values fit their labels inside the strip.
    lv_point_t sz;
    lv_obj_t const *labels[3]={bar.blinkyCount,bar.fps,bar.cpu};
    for(unsigned i=0;i<3;++i){
        char const *t=lv_label_get_text(labels[i]);
        lv_text_get_size(&sz,t,&lv_font_montserrat_14,0,0,
                         LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        lv_obj_get_coords(labels[i],&a);
        assert(sz.x<=lv_obj_get_width(labels[i]));
        assert(a.x1>=barArea.x1 && a.x2<=barArea.x2);
        assert(a.y1>=barArea.y1 && a.y2<=barArea.y2);
    }
    UiTopbar_setLed(&bar,true);
    lv_refr_now(display);capture("topbar.rgb565");
    uint32_t const ledOn=
        lv_color_to_u32(lv_obj_get_style_bg_color(bar.led,0));
    assert((ledOn&0xFFFFFFU)==0x40E080U);
    UiTopbar_setLed(&bar,false);
    // Shrinking clears old glyphs and never moves the pieces.
    lv_text_get_size(&sz,"1",&lv_font_montserrat_14,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setCount(&bar,1U);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    assert(rightEdge_(bar.blinkyCount)>=countA.x2-2);
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setFps(&bar,1U);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    lv_text_get_size(&sz,"1FPS",&lv_font_montserrat_14,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(rightEdge_(bar.fps)>=fpsA.x2-2);
    memcpy(prev,framebuffer,sizeof prev);
    UiTopbar_setCpu(&bar,1U);
    lv_refr_now(display);
    assert(memcmp(prev,framebuffer,sizeof prev));
    verifyRepaint(display);
    lv_text_get_size(&sz,"1%",&lv_font_montserrat_14,0,0,
                     LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(rightEdge_(bar.cpu)>=cpuA.x2-2);
    lv_obj_get_coords(bar.led,&a);
    assert(a.x1==ledA.x1 && a.x2==ledA.x2);
    lv_obj_get_coords(bar.blinkyCount,&a);
    assert(a.x1==countA.x1 && a.x2==countA.x2);
    lv_obj_get_coords(bar.fps,&a);
    assert(a.x1==fpsA.x1 && a.x2==fpsA.x2);
    lv_obj_get_coords(bar.cpu,&a);
    assert(a.x1==cpuA.x1 && a.x2==cpuA.x2);
    // Value sweep updates the text buffers.
    char expect[16];
    uint32_t const counts[]={0,1,99,100,999,UINT32_MAX};
    for(unsigned i=0;i<6;++i){
        UiTopbar_setCount(&bar,counts[i]);
        (void)lv_snprintf(expect,sizeof expect,"%lu",
                          (unsigned long)counts[i]);
        assert(!strcmp(lv_label_get_text(bar.blinkyCount),expect));
    }
    unsigned const fpsVals[]={0,1,99,100,999};
    for(unsigned i=0;i<5;++i){
        UiTopbar_setFps(&bar,(uint16_t)fpsVals[i]);
        (void)lv_snprintf(expect,sizeof expect,"%uFPS",fpsVals[i]);
        assert(!strcmp(lv_label_get_text(bar.fps),expect));
    }
    unsigned const cpuVals[]={0,1,99,100};
    for(unsigned i=0;i<4;++i){
        UiTopbar_setCpu(&bar,(uint8_t)cpuVals[i]);
        (void)lv_snprintf(expect,sizeof expect,"%u%%",cpuVals[i]);
        assert(!strcmp(lv_label_get_text(bar.cpu),expect));
    }
    verifyRepaint(display); // Natural dirty render equals forced.
    // Destroy removes only its own subtree and zeroes the instance.
    UiTopbar_destroy(&bar);
    assert(!bar.container && !bar.led && !bar.blinkyCount);
    assert(lv_obj_get_child_count(root)==1);
    assert(lv_obj_get_child(root,0)==sibling);
    UiTopbar_destroy(&bar); // Destroy is idempotent when empty.
    for(unsigned i=0;i<10;++i){
        UiTopbar_create(&bar,root,236,17);
        UiTopbar_setCount(&bar,i);
        UiTopbar_setLed(&bar,(i&1U)!=0);
        UiTopbar_destroy(&bar);
    }
    assert(lv_obj_get_child_count(root)==1);
    lv_obj_delete(sibling);
    lv_refr_now(display);blank();
    testUiFlex_(root,display);
    testLabel_(root,display);
    testLabelTextAlign_(root,display);
    testLabelPosition_(root,display);
    testBar_(root,display);
    testArc_(root,display);
    testTextStyle_(root,display);
    testPageTitleAndBootCount_(root,display);
    testThreadCard_(root,display);
    testDots_(root,display);
    testThreadMonitor_(root,display);
    testResourceMonitor_(root,display);
    testMonitorFill_(root);
    assert(!Host_critical);
    SST_Evt *smallEvents[48];Flash_Evt *mediumEvents[16];
    for(unsigned i=0;i<48;++i)smallEvents[i]=SST_NEW(SST_Evt);
    for(unsigned i=0;i<16;++i)mediumEvents[i]=SST_NEW(Flash_Evt);
    for(unsigned i=0;i<48;++i)SST_GC(smallEvents[i]);
    for(unsigned i=0;i<16;++i)SST_GC(mediumEvents[i]);
    printf("PASS scenario %u: Splash, startup ordering, blank pages, "
           "page deadlines, repaint equivalence and event-pool reclamation\n",
           scenario);
    return 0;
}
