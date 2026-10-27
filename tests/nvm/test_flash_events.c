#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "sm_flash.h"
#include "bsp_flash.h"
#include "flash_priv.h"
#include "flash_req_priv.h"
#include "sst_pubsub.h"
#include "sst_priv.h"
#include "ui_evt_priv.h"
#include "ui_evt.h"
#include "flash_req.c"
#include "blinky.c"
unsigned Host_critical;
static Sm_Flash ao;
SST_Task *const AO_Flash=&ao.super;
static SST_Evt const *queue[16];
static unsigned head,tail,notified,ops,stage,published,uiReady;
SST_Task *SST_tasks_[SST_MAX_TASK+1];
static unsigned saveCount[NVM_ID_LIMIT];
static unsigned blinkyEdges,uiLed;
void Nvm_addBlinky(uint32_t edges){blinkyEdges+=edges;}
static SST_Signal expected;
static bool outcome=true,reuse;
static SST_Evt const *dispatching;
static Flash_Req reused;
static BSP_Flash dev;
void SST_Task_ctor(SST_Task *me,SST_Handler init,SST_Handler dispatch){
    me->init=init;me->dispatch=dispatch;
}
void SST_TimeEvt_ctor(SST_TimeEvt *me,SST_Signal sig,SST_Task *task){
    me->super.sig=sig;me->task=task;
}
void SST_TimeEvt_arm(SST_TimeEvt *me,SST_TCtr count,SST_TCtr interval){
    (void)me;(void)count;(void)interval;
}
void BSP_ledOn(void){}
void BSP_ledOff(void){}
void SM_onAssert(char const *module,int line){
    fprintf(stderr,"ASSERT %s:%d\n",module,line);abort();
}
void DBC_fault_handler(char const *m,int l){SM_onAssert(m,l);}
int xPortIsInsideInterrupt(void){return pdFALSE;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)2;}
void SST_Task_post(SST_Task *me,SST_Evt const *e){
    if(e->poolId){++((SST_Evt *)e)->refCtr;}
    if(me==AO_Blinky){
        assert(e->poolId==1 && e->refCtr==2);
        assert(e->sig==BRING_UP_READY_SIG ||
               e->sig==BRING_UP_ASSETS_LOADED_SIG ||
               e->sig==BRING_UP_NVM_READY_SIG);
        ++published;
        Blinky_dispatch(&Blinky_inst,e);
        SST_GC(e);return;
    }
    assert(me==AO_Flash && tail-head<FLASH_QUEUE_LEN);
    queue[tail++%16]=e;
}
static void step(void){
    assert(head<tail);SST_Evt const *e=queue[head++%16];
    dispatching=e;Sm_Flash_dispatch(AO_Flash,e);
    dispatching=NULL;SST_GC(e);
}
int xTaskNotifyGiveIndexed(TaskHandle_t me,unsigned i){
    if(me==(void *)3){
        assert(i==0);SST_Evt const *evt;assert(Ui_evtPop(&evt));
        assert(evt->poolId==1 && evt->refCtr==1);
        if(evt->sig==UI_LED_ON_SIG || evt->sig==UI_LED_OFF_SIG){
            ++uiLed;assert(blinkyEdges==uiLed);
        }else{
            assert(evt->sig==BRING_UP_READY_SIG ||
                   evt->sig==BRING_UP_ASSETS_LOADED_SIG);
            ++uiReady;
        }
        SST_GC(evt);return 1;
    }
    assert(me==(void *)2 && i==1);++notified;
    if(reuse){
        reuse=false;
        // A new request cannot reuse the envelope still owned by dispatch.
        SST_Evt const *previous=dispatching;
        assert(Flash_post_(FLASH_READ_SIG,&reused));
        assert(queue[(tail-1)%16]!=previous);
    }
    return 1;
}
unsigned ulTaskNotifyTakeIndexed(unsigned i,int clear,TickType_t wait){
    assert(i==1 && clear && wait==portMAX_DELAY);
    step();assert(notified);--notified;return 1;
}
BSP_Flash *BSP_flashDevice(void){return &dev;}
static bool operation(SST_Signal sig,uint32_t addr,void const *buf,
                      uint32_t len){
    assert(sig==expected && addr==0x1234);
    assert((sig==FLASH_ERASE_SIG) ? (!buf && !len) : (buf && len==4));
    ++ops;return outcome;
}
bool BSP_flashRead(BSP_Flash *d,uint32_t a,void *b,uint32_t n){
    assert(d==&dev);return operation(FLASH_READ_SIG,a,b,n);
}
bool BSP_flashWrite(BSP_Flash *d,uint32_t a,void const *b,uint32_t n){
    assert(d==&dev);return operation(FLASH_WRITE_SIG,a,b,n);
}
bool BSP_flashEraseSector(BSP_Flash *d,uint32_t a){
    assert(d==&dev);return operation(FLASH_ERASE_SIG,a,NULL,0);
}
void Flash_bindRuntime(void){assert(stage==0);++stage;}
void Asset_loadSplash(void){assert(stage==1);++stage;}
void Asset_loadRemaining(void){assert(stage==2);++stage;}
void Flash_initStorage(void){assert(stage==3);Flash_reqBind(AO_Flash);}
void Flash_writeNvm(Nvm_Id id){
    assert(stage==3);++saveCount[id];
}
void Flash_completeRequest(Flash_Req *const req){Flash_reqFinish(req);}
int main(int argc,char **argv){
    unsigned data=0;
    static STATIC_POOL_ELEM_TYPE(SST_Evt) small[48];
    static STATIC_POOL_ELEM_TYPE(Flash_Evt) medium[16];
    SST_EvtPool_init(small,sizeof small,sizeof(SST_Evt));
    SST_EvtPool_init(medium,sizeof medium,sizeof(Flash_Evt));
    bool const subscribed=argc==1 || atoi(argv[1])!=0;
    static SST_SubscrSet subscriptions[APPLICATION_PUB_SIG_MAX];
    SST_PubSub_init(subscriptions,APPLICATION_PUB_SIG_MAX);
    AO_Blinky->prio=1;SST_tasks_[1]=AO_Blinky;
    Ui_evtBind((void *)3);
    Blinky_ctor();
    Blinky_init(&Blinky_inst,NULL);
    SST_Evt beat={.sig=BLINKY_TIMEOUT_SIG};
    for(unsigned i=0;i<25;++i)Blinky_dispatch(&Blinky_inst,&beat);
    assert(!blinkyEdges && !uiLed && head==tail);
    if(!subscribed){
        SST_Task_unsubscribe(AO_Blinky,BRING_UP_READY_SIG);
        SST_Task_unsubscribe(AO_Blinky,BRING_UP_ASSETS_LOADED_SIG);
        SST_Task_unsubscribe(AO_Blinky,BRING_UP_NVM_READY_SIG);
    }
    ao.super.thread=(void *)1;
    assert(!Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BOOT_COUNT));
    Sm_Flash_init(AO_Flash,NULL);step();step();
    assert(stage==3 && published==(subscribed ? 3U : 0U));
    assert(uiReady==2);
    SST_Evt ready={.sig=BRING_UP_NVM_READY_SIG};
    if(!subscribed)Blinky_dispatch(&Blinky_inst,&ready);
    assert(blinkyEdges==1 && uiLed==1 && tail-head==1);
    step();assert(saveCount[NVM_BOOT_COUNT]==1);
    // Repeated ready does not reenter active or save the boot count again.
    Blinky_dispatch(&Blinky_inst,&ready);
    assert(head==tail && blinkyEdges==1);
    saveCount[NVM_BOOT_COUNT]=0;
    assert(!saveCount[NVM_BLINKY_EDGES]);
    for(unsigned fail=0;fail<2;++fail){
        outcome=!fail;
        expected=FLASH_READ_SIG;
        assert(Flash_readBlock(0x1234,&data,4)==outcome);
        expected=FLASH_WRITE_SIG;
        assert(Flash_writeBlock(0x1234,&data,4)==outcome);
        expected=FLASH_ERASE_SIG;
        assert(Flash_eraseSector(0x1234)==outcome);
    }
    outcome=true;
    Flash_Req pending[FLASH_REQUEST_CAPACITY]={0};
    for(unsigned i=0;i<FLASH_REQUEST_CAPACITY;++i){
        pending[i]=(Flash_Req){.address=0x1234,.dst=&data,.len=4,
                              .waiter=(void *)2,.waiterIndex=1};
        assert(Flash_post_(FLASH_READ_SIG,&pending[i]));
    }
    assert(!Flash_post_(FLASH_WRITE_SIG,&reused));
    assert(Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BOOT_COUNT));
    assert(Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BLINKY_EDGES));
    assert(tail-head==FLASH_QUEUE_LEN);
    assert(queue[(tail-1)%16] != queue[(tail-2)%16]);
    assert(queue[(tail-1)%16]->poolId==1);
    assert(queue[(tail-1)%16]->refCtr==1);
    expected=FLASH_READ_SIG;
    for(unsigned i=0;i<FLASH_REQUEST_CAPACITY;++i)step();
    step();step();
    assert(saveCount[NVM_BOOT_COUNT]==1 && saveCount[NVM_BLINKY_EDGES]==1);
    for(unsigned i=0;i<FLASH_REQUEST_CAPACITY;++i)assert(pending[i].ok);
    notified=0;
    reused=(Flash_Req){.address=0x1234,.dst=&data,.len=4,
                       .waiter=(void *)2,.waiterIndex=1};
    reuse=true;expected=FLASH_WRITE_SIG;
    assert(Flash_writeBlock(0x1234,&data,4));
    assert(head<tail);expected=FLASH_READ_SIG;step();assert(reused.ok);
    assert(Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BLINKY_EDGES));
    assert(saveCount[NVM_BLINKY_EDGES]==1);step();
    assert(saveCount[NVM_BLINKY_EDGES]==2);
    unsigned const saved=saveCount[NVM_BLINKY_EDGES];
    assert(Flash_postNvm(FLASH_NVM_WRITE_SIG,NVM_BLINKY_EDGES));
    assert(Flash_postNvm(FLASH_NVM_WRITE_SIG,NVM_BLINKY_EDGES));
    assert(tail-head==2);
    step();step();assert(saveCount[NVM_BLINKY_EDGES]==saved+2);
    // More requests than the pool can hold proves completed events return.
    for(unsigned i=0;i<32;++i){
        assert(Flash_postNvm(FLASH_NVM_WRITE_SIG,NVM_BLINKY_EDGES));step();
    }
    assert(head==tail);
    for(unsigned i=0;i<19;++i)Blinky_maintenance_();
    assert(head==tail);
    Blinky_maintenance_();assert(tail-head==1);
    step();
    assert(saveCount[NVM_BOOT_COUNT]==1 && saveCount[NVM_BLINKY_EDGES]==37);
    // Both transition directions update the counter before UI delivery.
    assert(blinkyEdges==1 && uiLed==1);
    Blinky_dispatch(&Blinky_inst,&beat);
    assert(blinkyEdges==2 && uiLed==2);
    Blinky_dispatch(&Blinky_inst,&beat);
    assert(blinkyEdges==3 && uiLed==3);
    assert(ops==16 && !Host_critical);
    // All event shapes must fit their native pool and return after GC.
    SST_Evt *smallEvents[48];Flash_Evt *mediumEvents[16];
    for(unsigned i=0;i<48;++i)smallEvents[i]=SST_NEW(SST_Evt);
    for(unsigned i=0;i<16;++i)mediumEvents[i]=SST_NEW(Flash_Evt);
    for(unsigned i=0;i<48;++i)SST_GC(smallEvents[i]);
    for(unsigned i=0;i<16;++i)SST_GC(mediumEvents[i]);
    puts("PASS flat Flash signals: startup, three raw RPCs success/failure, "
         "capacity, envelope lifetime through GC, dynamic item events, GC, "
         "queue bound, application beats and readiness publish/UI GC");
}