
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp.h"
#include "sm_ui.h"
#include "ui.h"
#include "ui_evt.h"
#include "ui_evt_priv.h"
#include "time_evt.h"
#include "asset.h"
#include "nvm.h"
#include "nvs.h"
#include "flash.h"
#include "sm_flash_evt.h"
#include "aos/blinky/blinky.h"
#include "sm_tracer.h"
#include "ui_disp_priv.h"
// UI accesses RAM only; persistence must remain on the Flash AO.
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
static SST_Task flashTask,blinkyTask;
SST_Task *const AO_Flash=&flashTask;
SST_Task *const AO_Blinky=&blinkyTask;
static unsigned hostTick,backlight,scenario;
static unsigned idleCount,totalCount;
static Asset assets;
static uint16_t pixels[3][240*320];
static StackType_t taskStacks[5][256];
static unsigned samples;
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
void BSP_cpuSample(uint32_t *idle,uint32_t *total){
    idleCount+=30;totalCount+=50;*idle=idleCount;*total=totalCount;
}
UBaseType_t uxTaskGetSystemState(TaskStatus_t *tasks,unsigned cap,
                                uint32_t *runtime){
    assert(cap>=5);*runtime=0;++samples;
    char const *names[]={"ui","ao","ao","Tmr Svc","IDLE"};
    for(unsigned i=0;i<5;++i){
        tasks[i]=(TaskStatus_t){.xHandle=(void *)(uintptr_t)(i+1),
            .pcTaskName=names[i],.pxStackBase=taskStacks[i],
            .pxEndOfStack=taskStacks[i]+255,
            .usStackHighWaterMark=100+i};
    }
    return 5;
}
Asset const *Asset_get(void){return &assets;}
void const *Asset_slot(Asset const *a,uint32_t slot){
    return a->present && slot<3 ? pixels[slot] : NULL;
}
void const *Asset_splash(void){return scenario==3 ? NULL : pixels[2];}
void Ui_dispInit(void){}
void Ui_dispBacklight(bool on){assert(on);++backlight;}
uint32_t Ui_dispFps(void){return 30;}
TickType_t Ui_dispHeartbeat(void){return 1;}
// Include the actual thread binding/start/drain implementation. Its RTOS
// entry stays parked; the host drives the same drain on this single owner.
#include "../../app/application/ui/thread/ui.c"
extern SM_HsmState Sm_Ui_standby,Sm_Ui_readySplash,Sm_Ui_earlyReadyShow;
extern SM_HsmState Sm_Ui_splashing,Sm_Ui_readyShow,Sm_Ui_sys,Sm_Ui_live;
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
// Compare actual LVGL global coordinates against the exported design.
static void geometry(char const *name, lv_obj_t *obj){
    if(scenario)return;
    lv_obj_update_layout(obj);
    lv_area_t area;lv_obj_get_coords(obj,&area);
    int baseline=-1;
    if(lv_obj_check_type(obj,&lv_label_class)){
        lv_area_t content;lv_obj_get_content_coords(obj,&content);
        lv_font_t const *font=lv_obj_get_style_text_font(obj,0);
        baseline=content.y1+font->line_height-font->base_line;
    }
    printf("GEOMETRY %s %d %d %d %d %d\n",name,
           (int)area.x1,(int)area.y1,(int)lv_obj_get_width(obj),
           (int)lv_obj_get_height(obj),baseline);
}
static void uniformFont(lv_obj_t *obj){
    if(lv_obj_check_type(obj,&lv_label_class)){
        assert(lv_obj_get_style_text_font(obj,0)==LV_FONT_DEFAULT);
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i){
        uniformFont(lv_obj_get_child(obj,i));
    }
}
static void sysGeometry(void){
    geometry("sys.title",Ui_sm_.top.title);
    geometry("sys.led",Ui_sm_.top.led);
    geometry("sys.count",Ui_sm_.top.count);
    geometry("sys.fps",Ui_sm_.top.fps);
    geometry("sys.fpsUnit",Ui_sm_.top.fpsUnit);
    geometry("sys.cpu",Ui_sm_.top.cpu);
    geometry("sys.cpuUnit",Ui_sm_.top.cpuUnit);
    geometry("sys.heading",Ui_sm_.panel.heading);
    geometry("sys.cardCount",Ui_sm_.panel.count);
    geometry("sys.name0",Ui_sm_.panel.name[0]);
    geometry("sys.value0",Ui_sm_.panel.val[0]);
    geometry("sys.bar0",Ui_sm_.panel.bar[0]);
    geometry("sys.name1",Ui_sm_.panel.name[1]);
    geometry("sys.value1",Ui_sm_.panel.val[1]);
    geometry("sys.bar1",Ui_sm_.panel.bar[1]);
    geometry("sys.small",Ui_sm_.gauge.unit);
    geometry("sys.bytes",Ui_sm_.gauge.bytes);
    geometry("sys.byteUnit",Ui_sm_.gauge.byteUnit);
    geometry("sys.arc",Ui_sm_.gauge.arc);
    geometry("sys.min",Ui_sm_.gauge.min);
    geometry("sys.max",Ui_sm_.gauge.max);
    geometry("sys.percentValue",Ui_sm_.gauge.num);
    geometry("sys.percentUnit",Ui_sm_.gauge.percent);
    geometry("sys.bootCaption",Ui_sm_.nvm.caption);
    geometry("sys.bootCount",Ui_sm_.nvm.label);
    for(unsigned i=0;i<6;++i){
        char name[24];snprintf(name,sizeof name,"sys.dot%u",i);
        geometry(name,Ui_sm_.dots.dot[i]);
    }
}
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
int main(int argc,char **argv){
    assert(argc==2);scenario=(unsigned)atoi(argv[1]);
    static STATIC_POOL_ELEM_TYPE(SST_Evt) small[48];
    static STATIC_POOL_ELEM_TYPE(Flash_Evt) medium[16];
    SST_EvtPool_init(small,sizeof small,sizeof(SST_Evt));
    SST_EvtPool_init(medium,sizeof medium,sizeof(Flash_Evt));
    flashTask.thread=(void *)2;blinkyTask.thread=(void *)3;
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
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_standby && !Ui_sm_.top.title);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(Ui_sm_.headerTimeout.remaining==0);
    assert(Ui_sm_.stateTimeout.remaining==0);
    assert(Ui_sm_.dialTimeout.remaining==0);
    send(UI_LED_ON_SIG);send(UI_LED_OFF_SIG);
    assert(Ui_sm_.ledEdges==2 && !Ui_sm_.ledOn);
    Nvm_addBlinky(50); // Business snapshot is independent of UI events.
    send(BRING_UP_READY_SIG);
    if(scenario==2)send(BRING_UP_ASSETS_LOADED_SIG);
    if(scenario==5){
        SST_Evt const *go;assert(Ui_evtPop(&go));
        for(unsigned i=0;i<20;++i){hostTick+=10;TimeEvt_tick();}
        SST_Evt const *pending;assert(!Ui_evtPop(&pending));
        Sm_Ui_dispatch(&Ui_sm_,go);SST_GC(go);
    }
    Ui_drain_();render();capture("splash.rgb565");
    assert(backlight==1 && Ui_dbgSplashOk_==(scenario!=3));
    assert(!Ui_sm_.top.title && !samples);
    send(UI_LED_ON_SIG);send(UI_LED_OFF_SIG);
    assert(Ui_sm_.ledEdges==4 && !Ui_sm_.ledOn && !Ui_sm_.top.title);
    assert(Nvm_blinky()==50); // LED display events never update NVM.
    if(scenario==1){
        pulse(150);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_readyShow);
        assert(!Ui_sm_.top.title);send(BRING_UP_ASSETS_LOADED_SIG);
    }else if(scenario==2){pulse(150);}
    else if(scenario==3){pulse(1);send(BRING_UP_ASSETS_LOADED_SIG);}
    else if(scenario==5){
        assert(Ui_sm_.stateTimeout.remaining==150);
        send(BRING_UP_ASSETS_LOADED_SIG);pulse(140);
        for(unsigned i=0;i<20;++i){hostTick+=10;TimeEvt_tick();}
        assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_readyShow);
        Ui_drain_();assert(Ui_sm_.stateTimeout.remaining==500);
    }else{send(BRING_UP_ASSETS_LOADED_SIG);pulse(150);}
    render();
    verifyRepaint(display);
    capture("sys.rgb565");
    sysGeometry();
    uniformFont(lv_screen_active());
    if(!scenario){
        lv_font_t const *a=lv_obj_get_style_text_font(Ui_sm_.top.title,0);
        lv_font_t const *b=lv_obj_get_style_text_font(Ui_sm_.panel.heading,0);
        lv_point_t titleSize,headingSize;
        lv_text_get_size(&titleSize,"SysMonitor",a,0,0,1000,0);
        lv_text_get_size(&headingSize,"Blinky",b,0,0,1000,0);
        assert(titleSize.x<=lv_obj_get_content_width(Ui_sm_.top.title));
        assert(headingSize.x<=lv_obj_get_content_width(Ui_sm_.panel.heading));
    }

    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sys);
    assert(!strcmp(lv_label_get_text(Ui_sm_.top.title),"SysMonitor"));
    assert(Ui_sm_.metrics.snapshot.cpuPct==40);
    // A valid label must paint glyphs, not only retain its text string.
    // With compressed fonts disabled, a compressed subset paints nothing.
    if(scenario!=4){
        unsigned white=0;
        for(unsigned y=4;y<20;++y){
            for(unsigned x=80;x<100;++x){
                if(framebuffer[y*240+x]==0xFFFFU)++white;
            }
        }
        assert(white>5);
    }
    assert(!strcmp(lv_label_get_text(Ui_sm_.top.count),"50"));
    for(unsigned i=0;i<5;++i){
        assert(Ui_sm_.metrics.snapshot.rows[i].available);
        assert(Ui_sm_.metrics.snapshot.rows[i].free==100+i);
        assert(Ui_sm_.metrics.snapshot.rows[i].total==256);
    }
    assert(Ui_sm_.metrics.snapshot.rows[5].free==10);
    for(unsigned i=0;i<2;++i){
        SST_EvtPoolLevel level=SST_EvtPool_level(i+1);
        assert(Ui_sm_.metrics.snapshot.rows[7+i].free==level.min);
        assert(Ui_sm_.metrics.snapshot.rows[7+i].total==level.total);
    }
    assert(!strcmp(lv_label_get_text(Ui_sm_.panel.heading),"Blinky"));
    assert(!strcmp(lv_label_get_text(Ui_sm_.panel.val[0]),"102 w"));
    assert(!strcmp(lv_label_get_text(Ui_sm_.panel.val[1]),"154 w"));
    assert(Ui_sm_.nvm.shown==Nvm_bootCount());
    assert(Ui_sm_.metrics.snapshot.smallBlockBytes
           ==SST_EvtPool_level(1).blockSize);
    assert(lv_arc_get_bg_angle_start(Ui_sm_.gauge.arc)==210);
    assert(lv_arc_get_bg_angle_end(Ui_sm_.gauge.arc)==330);
    assert(!Ui_sm_.panel.name[7] && !Ui_sm_.panel.name[8]);
    lv_obj_update_layout(Ui_sm_.surface.root);
    assert(Ui_sm_.metrics.snapshot.ramUsed==0xE000);
    assert(Ui_sm_.metrics.snapshot.ramTotal==0xFFE0);
    Nvm_addBlinky(4); // Business progresses while Sys is visible.
    lv_obj_t *header=Ui_sm_.top.title;
    assert(lv_obj_get_child_count(lv_screen_active())==8);
    assert(lv_obj_get_child_count(Ui_sm_.surface.root)==26);
    assert(Ui_sm_.headerTimeout.interval==50);
    unsigned sampled=samples;
    for(unsigned i=0;i<100;++i)send(UI_REFRESH_SIG);
    // Render requests never sample or change deadlines.
    assert(samples==sampled);
    pulse(49);assert(samples==sampled);
    pulse(1);assert(samples==sampled+1);
    assert(Ui_sm_.sysRevision==Ui_sm_.metrics.snapshot.revision);
    pulse(449);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sys);
    pulse(1);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_live);
    render();
    verifyRepaint(display);
    capture("live-empty.rgb565");
    uniformFont(lv_screen_active());
    geometry("live.title",Ui_sm_.top.title);
    geometry("live.heading",Ui_sm_.dial.heading);
    geometry("live.arc",Ui_sm_.dial.arc);
    geometry("live.switch",Ui_sm_.sw.sw);
    assert(Ui_sm_.top.title==header && Ui_sm_.gauge.arc==NULL);
    assert(!strcmp(lv_label_get_text(header),"LiveShow"));
    assert(lv_obj_get_child_count(Ui_sm_.surface.root)==3);
    assert(Ui_sm_.dialTimeout.interval==3);
    assert(Ui_sm_.dial.value==0);
    assert(lv_arc_get_bg_angle_start(Ui_sm_.dial.arc)==180);
    assert(lv_arc_get_bg_angle_end(Ui_sm_.dial.arc)==90);
    assert(Nvm_blinky()==54 && !Ui_sm_.nvm.label);
    Nvm_addBlinky(1); // Blinky updates before notifying the UI.
    send(UI_LED_ON_SIG);Ui_drain_();
    assert(Ui_sm_.ledOn && Ui_sm_.ledEdges==5);
    assert(lv_obj_has_state(Ui_sm_.sw.sw,LV_STATE_CHECKED));
    assert(Nvm_blinky()==55 && !Ui_sm_.nvm.label);
    assert(!strcmp(lv_label_get_text(Ui_sm_.top.count),"55"));
    Nvm_addBlinky(1);
    send(UI_LED_OFF_SIG);Ui_drain_();
    assert(!Ui_sm_.ledOn && Ui_sm_.ledEdges==6);
    assert(!lv_obj_has_state(Ui_sm_.sw.sw,LV_STATE_CHECKED));
    assert(Nvm_blinky()==56 && !Ui_sm_.nvm.label);
    pulse(150);assert(Ui_sm_.dial.value==100);
    verifyRepaint(display);
    capture("live-full.rgb565");
    pulse(150);assert(Ui_sm_.dial.value==0);
    assert(Nvm_blinky()==56 && !Ui_sm_.nvm.label);
    unsigned before=samples;send(UI_HEADER_TIMEOUT_SIG);
    assert(samples==before+1 && Ui_sm_.top.title==header);
    pulse(699);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_live);
    pulse(1);assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sys);
    assert(!Ui_sm_.dial.arc && !Ui_sm_.dialTimeout.remaining);
    assert(!Ui_sm_.sw.sw && Ui_sm_.nvm.label);
    assert(!Ui_sm_.ram.lineA && !Ui_sm_.ram.lineB);
    send(UI_DIAL_TIMEOUT_SIG); // Committed old callback cannot touch dead UI.
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sys);
    for(unsigned i=0;i<3;++i){pulse(500);pulse(1000);}
    assert(Ui_sm_.sm_hsm_.curr==&Sm_Ui_sys && Ui_sm_.top.title==header);
    assert(lv_obj_get_child_count(lv_screen_active())==8);
    assert(lv_obj_get_child_count(Ui_sm_.surface.root)==26);
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    printf("PASS scenario %u: real LVGL trees, header lifetime, metrics, "
           "LED bubbling, NVM, timers, arc sweep; LVGL used=%zu peak=%zu\n",
           scenario,memory.total_size-memory.free_size,memory.max_used);
    // Exercise the actual page/parent exit actions in inside-out order.
    // No dispatch follows this teardown (the production graph stays Active).
    // A sibling borrowed from the same parent must survive component exits.
    lv_obj_t *sentinel=lv_obj_create(Ui_sm_.surface.root);
    Sm_Ui_sys.exit_(&Ui_sm_.sm_hsm_);
    assert(lv_obj_is_valid(sentinel));
    assert(lv_obj_get_child_count(Ui_sm_.surface.root)==1);
    assert(!Ui_sm_.gauge.arc && !Ui_sm_.gauge.num && !Ui_sm_.gauge.unit);
    assert(!Ui_sm_.dots.dot[0] && !Ui_sm_.dots.dot[1]);
    for(unsigned i=0;i<W_BARPANEL_ROWS;++i){
        assert(!Ui_sm_.panel.name[i] && !Ui_sm_.panel.bar[i]);
        assert(!Ui_sm_.panel.pct[i] && !Ui_sm_.panel.val[i]);
    }
    lv_obj_delete(sentinel);
    Sm_Ui_active.exit_(&Ui_sm_.sm_hsm_);
    assert(!Ui_sm_.headerTimeout.remaining);
    assert(!Ui_sm_.top.title && !Ui_sm_.gauge.arc);
    assert(lv_obj_get_child_count(lv_screen_active())==1);
    assert(!lv_obj_get_child_count(Ui_sm_.surface.root));
    assert(!Host_critical);
    SST_Evt *smallEvents[48];Flash_Evt *mediumEvents[16];
    for(unsigned i=0;i<48;++i)smallEvents[i]=SST_NEW(SST_Evt);
    for(unsigned i=0;i<16;++i)mediumEvents[i]=SST_NEW(Flash_Evt);
    for(unsigned i=0;i<48;++i)SST_GC(smallEvents[i]);
    for(unsigned i=0;i<16;++i)SST_GC(mediumEvents[i]);
    return 0;
}
