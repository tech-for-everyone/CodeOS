/* HyperDE notifications - C++ port of notify.rs */
#include "hyperde_internal.hpp"
#include <stdint.h>
extern "C" void kprintf(const char *fmt, ...);

enum { MAX_NOTIFS=8, MAX_MSG=120 };
struct Notification { uint8_t msg[MAX_MSG]; uint32_t len; uint64_t timestamp; int active; uint32_t color; };
static Notification notifs[MAX_NOTIFS];
static const uint64_t TOAST_TIMEOUT_MS = 4000;
static const int64_t TOAST_H=40, TOAST_GAP=6, TOAST_W=360;

static void push(const char *msg, uint32_t color){
    if(!msg) return;
    int slot=-1; uint64_t oldest=(uint64_t)-1; int oidx=0;
    for(int i=0;i<MAX_NOTIFS;i++){ if(!notifs[i].active){slot=i;break;} if(notifs[i].timestamp<oldest){oldest=notifs[i].timestamp;oidx=i;} }
    if(slot<0) slot=oidx;
    Notification *n=&notifs[slot]; uint32_t len=0;
    while(len<MAX_MSG-1 && msg[len]){ n->msg[len]=(uint8_t)msg[len]; len++; }
    n->msg[len]=0; n->len=len; n->timestamp=timer_get_milliseconds(); n->active=1;
    n->color = color ? color : hyperde_pal()->accent;
    kprintf("HYPERDE NOTIFY: %s\n", msg);
}
extern "C" void hyperde_notify(const char *msg, uint32_t color){ push(msg,color); }
extern "C" void hyperde_notify_success(const char *msg){ push(msg,0x0030D158); }
extern "C" void hyperde_notify_error(const char *msg){ push(msg,0x00FF5F57); }
extern "C" void hyperde_notify_clear(void){ for(int i=0;i<MAX_NOTIFS;i++) notifs[i].active=0; }

extern "C" void hyperde_notify_render(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h){
    if(!buf) return;
    uint64_t now=timer_get_milliseconds(); int64_t yoff=36;
    const Palette *p=hyperde_pal();
    for(int i=0;i<MAX_NOTIFS;i++){
        Notification *n=&notifs[i]; if(!n->active) continue;
        uint64_t age = now - n->timestamp;
        if(age > TOAST_TIMEOUT_MS){ n->active=0; continue; }
        uint32_t alpha = (age > TOAST_TIMEOUT_MS-500) ? (uint32_t)((TOAST_TIMEOUT_MS-age)*0xE0/500) : 0xE0;
        int64_t tx=(int64_t)w-TOAST_W-16, ty=yoff;
        hyperde_round_rect(buf,stride,w,h,tx,ty,tx+TOAST_W,ty+TOAST_H,10,p->glass_top,alpha);
        for(int64_t y=ty+4;y<ty+TOAST_H-4;y++) for(int64_t x=tx+4;x<tx+7;x++) hyperde_glass_px(buf,stride,w,h,x,y,n->color,alpha);
        hyperde_draw_text(buf,stride,w,h,tx+14,ty+(TOAST_H-8)/2,n->msg,n->len,p->text,1,alpha);
        yoff += TOAST_H+TOAST_GAP;
    }
}
extern "C" int hyperde_notify_active(void){ for(int i=0;i<MAX_NOTIFS;i++) if(notifs[i].active) return 1; return 0; }
extern "C" int hyperde_notify_drain_dirty(void){ return 0; }
extern "C" void hyperde_render_notifications(void){ /* called with no args in some contexts? provide safe no-op; real render needs fb */ }
