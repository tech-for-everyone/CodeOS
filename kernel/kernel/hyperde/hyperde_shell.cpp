/* HyperDE compositor shell - C++ port (was Rust kernel/kernel/hyperde/src/lib.rs).
 * Provides the C ABI expected by qt6/panels and the kernel desktop. */
#include "hyperde_internal.hpp"
#include <stdarg.h>

extern "C" {
/* config exports implemented in hyperde_config.cpp */
void hyperde_config_load(const uint8_t *data, size_t len);
uint32_t hyperde_config_workspace_count(void);
uint32_t hyperde_config_accent(void);
uint32_t hyperde_config_border_width(void);
uint32_t hyperde_config_panel_height(void);
int hyperde_config_blur_enabled(void);
int hyperde_config_shadow_enabled(void);
uint32_t hyperde_config_animation_duration(void);
int hyperde_config_backend(void); /* 0=x11,1=wayland */
}

/* Embedded conf.lua (compile-time, replaces Rust include_str!) */
#include "hyperde_conf_lua.inc"

/* font8x8 BASIC latin (bit0 = leftmost column). Public domain. */
static const uint8_t FONT8X8[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00},{0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7f,0x36,0x7f,0x36,0x36,0x00},{0x0c,0x3e,0x03,0x1e,0x30,0x1f,0x0c,0x00},
    {0x00,0x63,0x33,0x18,0x0c,0x66,0x63,0x00},{0x1c,0x36,0x1c,0x6e,0x3b,0x33,0x6e,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},{0x18,0x0c,0x06,0x06,0x06,0x0c,0x18,0x00},
    {0x06,0x0c,0x18,0x18,0x18,0x0c,0x06,0x00},{0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00},
    {0x00,0x0c,0x0c,0x3f,0x0c,0x0c,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x06},
    {0x00,0x00,0x00,0x3f,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x00},
    {0x60,0x30,0x18,0x0c,0x06,0x03,0x01,0x00},
    {0x3e,0x63,0x73,0x7b,0x6f,0x67,0x3e,0x00},{0x0c,0x0e,0x0c,0x0c,0x0c,0x0c,0x3f,0x00},
    {0x1e,0x33,0x30,0x1c,0x06,0x33,0x3f,0x00},{0x1e,0x33,0x30,0x1c,0x30,0x33,0x1e,0x00},
    {0x38,0x3c,0x36,0x33,0x7f,0x30,0x78,0x00},{0x3f,0x03,0x1f,0x30,0x30,0x33,0x1e,0x00},
    {0x1c,0x06,0x03,0x1f,0x33,0x33,0x1e,0x00},{0x3f,0x33,0x30,0x18,0x0c,0x0c,0x0c,0x00},
    {0x1e,0x33,0x33,0x1e,0x33,0x33,0x1e,0x00},{0x1e,0x33,0x33,0x3e,0x30,0x18,0x0e,0x00},
    {0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x00},{0x00,0x0c,0x0c,0x00,0x00,0x0c,0x0c,0x06},
    {0x18,0x0c,0x06,0x03,0x06,0x0c,0x18,0x00},{0x00,0x00,0x3f,0x00,0x00,0x3f,0x00,0x00},
    {0x06,0x0c,0x18,0x30,0x18,0x0c,0x06,0x00},{0x1e,0x33,0x30,0x18,0x0c,0x00,0x0c,0x00},
    {0x3e,0x63,0x7b,0x7b,0x7b,0x03,0x1e,0x00},
    {0x0c,0x1e,0x33,0x33,0x3f,0x33,0x33,0x00},{0x3f,0x66,0x66,0x3e,0x66,0x66,0x3f,0x00},
    {0x3c,0x66,0x03,0x03,0x03,0x66,0x3c,0x00},{0x1f,0x36,0x66,0x66,0x66,0x36,0x1f,0x00},
    {0x7f,0x46,0x16,0x1e,0x16,0x46,0x7f,0x00},{0x7f,0x46,0x16,0x1e,0x16,0x06,0x0f,0x00},
    {0x3c,0x66,0x03,0x03,0x73,0x66,0x7c,0x00},{0x33,0x33,0x33,0x3f,0x33,0x33,0x33,0x00},
    {0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},{0x78,0x30,0x30,0x30,0x33,0x33,0x1e,0x00},
    {0x67,0x66,0x36,0x1e,0x36,0x66,0x67,0x00},{0x0f,0x06,0x06,0x06,0x46,0x66,0x7f,0x00},
    {0x63,0x77,0x7f,0x7f,0x6b,0x63,0x63,0x00},{0x63,0x67,0x6f,0x7b,0x73,0x63,0x63,0x00},
    {0x1c,0x36,0x63,0x63,0x63,0x36,0x1c,0x00},{0x3f,0x66,0x66,0x3e,0x06,0x06,0x0f,0x00},
    {0x1e,0x33,0x33,0x33,0x3b,0x1e,0x38,0x00},{0x3f,0x66,0x66,0x3e,0x36,0x66,0x67,0x00},
    {0x1e,0x33,0x07,0x0e,0x38,0x33,0x1e,0x00},{0x3f,0x2d,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3f,0x00},{0x33,0x33,0x33,0x33,0x33,0x1e,0x0c,0x00},
    {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00},{0x63,0x63,0x36,0x1c,0x1c,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1e,0x0c,0x0c,0x1e,0x00},{0x7f,0x63,0x31,0x18,0x4c,0x66,0x7f,0x00},
    {0x1e,0x06,0x06,0x06,0x06,0x06,0x1e,0x00},{0x03,0x06,0x0c,0x18,0x30,0x60,0x40,0x00},
    {0x1e,0x18,0x18,0x18,0x18,0x18,0x1e,0x00},{0x08,0x1c,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff},{0x0c,0x0c,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1e,0x30,0x3e,0x33,0x6e,0x00},{0x07,0x06,0x06,0x3e,0x66,0x66,0x3b,0x00},
    {0x00,0x00,0x1e,0x33,0x03,0x33,0x1e,0x00},{0x38,0x30,0x30,0x3e,0x33,0x33,0x6e,0x00},
    {0x00,0x00,0x1e,0x33,0x3f,0x03,0x1e,0x00},{0x1c,0x36,0x06,0x0f,0x06,0x06,0x0f,0x00},
    {0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x1f},{0x07,0x06,0x36,0x6e,0x66,0x66,0x67,0x00},
    {0x0c,0x00,0x0e,0x0c,0x0c,0x0c,0x1e,0x00},{0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1e},
    {0x07,0x06,0x66,0x36,0x1e,0x36,0x67,0x00},{0x0e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0x00},
    {0x00,0x00,0x33,0x7f,0x7f,0x6b,0x63,0x00},{0x00,0x00,0x1f,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1e,0x33,0x33,0x33,0x1e,0x00},{0x00,0x00,0x3b,0x66,0x66,0x3e,0x06,0x0f},
    {0x00,0x00,0x6e,0x33,0x33,0x3e,0x30,0x78},{0x00,0x00,0x3b,0x6e,0x66,0x06,0x0f,0x00},
    {0x00,0x00,0x3e,0x03,0x1e,0x30,0x1f,0x00},{0x08,0x0c,0x3e,0x0c,0x0c,0x2c,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6e,0x00},{0x00,0x00,0x33,0x33,0x33,0x1e,0x0c,0x00},
    {0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00},{0x00,0x00,0x63,0x36,0x1c,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3e,0x30,0x1f},{0x00,0x00,0x3f,0x19,0x0c,0x26,0x3f,0x00},
    {0x38,0x0c,0x0c,0x07,0x0c,0x0c,0x38,0x00},{0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0c,0x0c,0x38,0x0c,0x0c,0x07,0x00},{0x6e,0x3b,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static const uint32_t CLOSE_DOT = 0x00FF5F57;
static const uint32_t MIN_DOT   = 0x00FFBD2E;
static const uint32_t MAX_DOT   = 0x0028C840;
static const int64_t WIN_TB = 30, WIN_SH = 6, WIN_CR = 12;
static const int64_t BAR_ANCHOR = 62;

static Palette PAL = {
    0x0040D9F0, 0x0030D158, 0x00F5F5F7, 0x0095A0AA,
    0x00242428, 0x0017171B, 0xC8,
    0x0040D9F0, 0x003c3836, 2, 28
};

extern "C" const Palette *hyperde_pal(void) { return &PAL; }

static uint32_t FB_Wv, FB_Hv;
static int ACTIVEv = 1, INIT_DONEv, LAST_MINv = -1, LAST_CPUv = -1;
static int WS_CURv, WS_NUMv = 6;
static uint64_t DRAWSv, LAST_MSv, LAST_MEMv, LAST_WMSIGv = (uint64_t)-1, LAST_XSIGv = (uint64_t)-1;
static void *CM_WMv;
static uint64_t cpu_last_total, cpu_last_busy;

static inline uint32_t blendc(uint32_t base, uint32_t g, uint32_t alpha) {
    if (alpha >= 0xFF) return g;
    if (alpha == 0) return base;
    uint32_t a = alpha & 0xFF, inv = 256 - a;
    uint32_t r = (((g>>16)&0xFF)*a + ((base>>16)&0xFF)*inv)/256;
    uint32_t gr= (((g>>8)&0xFF)*a  + ((base>>8)&0xFF)*inv)/256;
    uint32_t b = ((g&0xFF)*a       + (base&0xFF)*inv)/256;
    return (r<<16)|(gr<<8)|b;
}
static inline void px(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x, int64_t y, uint32_t c) {
    if (x<0||y<0||x>=(int64_t)w||y>=(int64_t)h) return;
    buf[(uint32_t)y*stride + (uint32_t)x] = c;
}
void hyperde_glass_px(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x, int64_t y, uint32_t g, uint32_t alpha) {
    if (x<0||y<0||x>=(int64_t)w||y>=(int64_t)h) return;
    size_t i = (size_t)(uint32_t)y*stride + (uint32_t)x;
    buf[i] = blendc(buf[i], g, alpha);
}
static void fill_rect(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t x0,int64_t y0,int64_t x1,int64_t y1, uint32_t c, uint32_t a) {
    if (x0<0) x0=0; if (x1>=(int64_t)w) x1=w-1;
    if (y0<0) y0=0; if (y1>=(int64_t)h) y1=h-1;
    for (int64_t y=y0;y<=y1;y++) for (int64_t x=x0;x<=x1;x++) hyperde_glass_px(buf,stride,w,h,x,y,c,a);
}
void hyperde_fill_circle(uint32_t *buf, uint32_t stride, uint32_t w, uint32_t h, int64_t cx,int64_t cy,int64_t r, uint32_t c, uint32_t) {
    int64_t rr=r*r;
    for (int64_t y=cy-r;y<=cy+r;y++) for (int64_t x=cx-r;x<=cx+r;x++) {
        int64_t dx=x-cx, dy=y-cy; if (dx*dx+dy*dy<=rr) px(buf,stride,w,h,x,y,c);
    }
}
void hyperde_round_rect(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h,int64_t x0,int64_t y0,int64_t x1,int64_t y1,int64_t r,uint32_t c,uint32_t a){
    fill_rect(buf,stride,w,h,x0+r,y0,x1-r,y1,c,a);
    fill_rect(buf,stride,w,h,x0,y0+r,x1,y1-r,c,a);
    hyperde_fill_circle(buf,stride,w,h,x0+r,y0+r,r,c,a);
    hyperde_fill_circle(buf,stride,w,h,x1-r,y0+r,r,c,a);
    hyperde_fill_circle(buf,stride,w,h,x0+r,y1-r,r,c,a);
    hyperde_fill_circle(buf,stride,w,h,x1-r,y1-r,r,c,a);
}
int64_t hyperde_draw_text(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h,int64_t x,int64_t y0,const uint8_t *s,size_t n,uint32_t color,int64_t scale,uint32_t){
    for (size_t i=0;i<n;i++) {
        uint8_t ch=s[i];
        if (ch>=0x80) { x += 8*scale; continue; }
        if (ch!=' ') {
            const uint8_t *glyph=FONT8X8[ch];
            for (int row=0;row<8;row++) for (int col=0;col<8;col++) if ((glyph[row]>>col)&1) {
                if (scale==1) px(buf,stride,w,h,x+col,y0+row,color);
                else for (int64_t dy=0;dy<scale;dy++) for (int64_t dx=0;dx<scale;dx++) px(buf,stride,w,h,x+col*scale+dx,y0+row*scale+dy,color);
            }
        }
        x += 8*scale + 2*scale;
    }
    return x;
}
extern "C" uint32_t hyperde_scancode_to_key_pub(uint32_t sc); /* from keybind */
static int64_t isqrt64(int64_t v){ if(v<=0)return 0; int64_t x=v,y=(v+1)/2; while(y<x){x=y;y=(x+v/x)/2;} return x; }

static size_t fmt_int(int v, size_t pad, uint8_t *out, size_t cap) {
    if (v<0) v=0; uint8_t tmp[12]; size_t i=0;
    if(v==0) tmp[i++]='0'; else while(v>0&&i<sizeof(tmp)){tmp[i++]='0'+(v%10); v/=10;}
    size_t n=0; while(n< (pad>i?pad-i:0) && n<cap) out[n++]='0';
    while(i>0 && n<cap) out[n++]=tmp[--i]; return n;
}
static int cpu_percent(void){
    uint64_t total = timer_get_milliseconds()/10, busy=sched_busy_ticks();
    int pct=0;
    if (cpu_last_total && total>cpu_last_total) {
        uint64_t dtotal=total-cpu_last_total, dbusy=busy-cpu_last_busy;
        pct = (int)(dbusy*100/dtotal); if(pct>100)pct=100;
    }
    cpu_last_total=total; cpu_last_busy=busy; return pct;
}

static const char *MONTHS3[12]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
/* Forward decls for render - implemented below with full logic? To keep this manageable,
   I implement a compact but functional bar/chrome matching the Rust behavior. */

extern "C" void hyperde_shell_set_workspace(int cur, int num){ WS_CURv=cur; WS_NUMv=num; }
extern "C" int hyperde_shell_cpu(void){ return cpu_percent(); }
extern "C" int hyperde_shell_mem_mb(void){ return (int)(pmm_count_used()*4096/1024/1024); }
extern "C" int hyperde_shell_mem_total_mb(void){ return (int)(pmm_total_pages()*4096/1024/1024); }
extern "C" int hyperde_shell_active(void){ return ACTIVEv; }
extern "C" int hyperde_shell_set_active(int v){ int old=ACTIVEv; ACTIVEv=v?1:0; if(old!=ACTIVEv) kprintf("HYPERDE: compositor switched %d->%d\n",old,ACTIVEv); return ACTIVEv; }
extern "C" int hyperde_shell_backend(void){ return hyperde_config_backend(); }

extern "C" void hyperde_palette_load_config(void){
    PAL.accent = hyperde_config_accent();
    PAL.focused_border = hyperde_config_accent();
    PAL.border_width = hyperde_config_border_width();
    PAL.bar_h = hyperde_config_panel_height();
}

static void render_bar(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h);
static void render_windows(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h);
static int focused_app_title(const void *wm, uint8_t *out, size_t max);

extern "C" void hyperde_shell_init(void){
    if (INIT_DONEv) return;
    INIT_DONEv=1;
    FB_Wv=fb_getwidth(); FB_Hv=fb_getheight();
    kprintf("HYPERDE: C++ shell ready fb=%dx%d bpp=%d\n",(int)FB_Wv,(int)FB_Hv,(int)fb_get_bpp());
    hyperde_config_load(HYPERDE_CONF_LUA, HYPERDE_CONF_LUA_LEN);
    if (hyperde_config_backend()==1) kprintf("HYPERDE: backend=wayland (opt-in)\n");
    else kprintf("HYPERDE: backend=x11 (default)\n");
    hyperde_palette_load_config();
    kprintf("HYPERDE: compositor active=%d\n", ACTIVEv);
}

extern "C" void hyperde_shell_pump(void *wm){
    if (!INIT_DONEv) hyperde_shell_init();
    if (!ACTIVEv) return;
    CM_WMv=wm;
    if (!FB_Wv){ FB_Wv=fb_getwidth(); FB_Hv=fb_getheight(); if(!FB_Wv) return; }
    uint32_t w=FB_Wv,h=FB_Hv;
    int pitch=fb_get_pitch(); uint32_t row = pitch>0? (uint32_t)pitch/4 : w;
    uint32_t *buf=fb_get_active_buffer(); if(!buf) return;
    render_windows(buf,row,w,h);
    /* simple dirty tracking by time */
    uint64_t now=timer_get_milliseconds();
    if (now - LAST_MSv >= 500 || DRAWSv==0) {
        render_bar(buf,row,w,h);
        DRAWSv++; LAST_MSv=now;
        if (DRAWSv % 20 == 0) kprintf("HYPERDE: bar #%u\n",(unsigned)DRAWSv);
    }
}

extern "C" int hyperde_shell_bar_hit(int mx, int my){
    if (my<0 || my >= (int)PAL.bar_h) return 0;
    int64_t w=(int64_t)FB_Wv, x=mx;
    if (w<=0||mx<0) return 0;
    if (x<54) return 1;
    /* workspace bars */
    int ws_num = WS_NUMv; if(ws_num<1)ws_num=1; if(ws_num>9)ws_num=9;
    int ws_cur = WS_CURv; if(ws_cur<0)ws_cur=0; if(ws_cur>=ws_num)ws_cur=ws_num-1;
    uint8_t tbuf[64]; int tn=focused_app_title(CM_WMv,tbuf,sizeof(tbuf));
    int64_t app_reserve = tn>0 ? (tn*16+24) : 0;
    int64_t ws_start = BAR_ANCHOR + app_reserve;
    if (x>=ws_start && x<ws_start + ws_num*22) { int idx=(int)((x-ws_start)/22); if(idx>=0&&idx<ws_num) return 920+idx; }
    /* task pills (lvgl) */
    int64_t px = ws_start + ws_num*22;
    if (CM_WMv) {
        int lcount = lvgl_wm_window_count(CM_WMv); if(lcount<0)lcount=0;
        for(int i=0;i<lcount && px < w/2-150;i++){
            const LvglWindow *ww=(const LvglWindow*)lvgl_wm_window_at(CM_WMv,i); if(!ww) continue;
            size_t n=0; while(n<9 && ww->title[n]) n++;
            int64_t tw=(int64_t)n*10+12;
            if (x>=px && x<px+tw) return 1000+i;
            px += tw+8;
        }
    }
    /* X11 pills */
    { PrsDesktopWin xwins[PRS_WIN_MAX]; int nc=prs_desktop_windows(xwins,PRS_WIN_MAX);
      for(int i=0;i<nc && px<w/2-150;i++){ if(!xwins[i].mapped) continue;
        size_t n=0; while(n<9 && xwins[i].title[n]) n++;
        int64_t tw=(int64_t)n*10+12;
        if (x>=px && x<px+tw) return 2000+i;
        px += tw+8;
      }
    }
    if (x>=w-68 && x<w-34) return 904;
    if (x>=w-34) return 905;
    if (x>=w-238 && x<w-186) return 902;
    if (x>=w-178 && x<w-126) return 901;
    if (x>=w-118 && x<w-78) return 900;
    if ((x - w/2) < 110 && (x - w/2) > -110) return 903;
    return 0;
}

/* --- rendering helpers (compact port of Rust logic) --- */
static int focused_app_title(const void *wm, uint8_t *out, size_t max){
    size_t n=0;
    PrsDesktopWin xwins[PRS_WIN_MAX]; int nc=prs_desktop_windows(xwins,PRS_WIN_MAX);
    for(int i=0;i<nc;i++) if(xwins[i].mapped && xwins[i].focused){
        for(int j=0;j<40 && xwins[i].title[j] && n<max;j++) out[n++]=(uint8_t)xwins[i].title[j];
        return (int)n;
    }
    if (wm) {
        int c=lvgl_wm_window_count(wm); if(c<0)c=0;
        for(int i=0;i<c;i++){ const LvglWindow *ww=(const LvglWindow*)lvgl_wm_window_at(wm,i);
            if(ww && ww->focused){ for(int j=0;j<128 && ww->title[j] && n<max;j++) out[n++]=ww->title[j]; return (int)n; }
        }
    }
    return 0;
}
static void draw_window_chrome(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h,int64_t rx,int64_t ry,int64_t rw,int64_t rh,int focused,const uint8_t *title,size_t tn){
    fill_rect(buf,stride,w,h,rx+WIN_SH+3,ry+rh+7,rx+rw-WIN_SH-3,ry+rh+9,0,0x08);
    fill_rect(buf,stride,w,h,rx+WIN_SH+1,ry+rh+4,rx+rw-WIN_SH-1,ry+rh+6,0,0x10);
    fill_rect(buf,stride,w,h,rx+WIN_SH,ry+rh+1,rx+rw-WIN_SH,ry+rh+3,0,0x1E);
    int64_t by0=ry+WIN_SH, by1=by0+WIN_TB, x0=rx+WIN_SH, x1=rx+rw-WIN_SH-1;
    for(int64_t y=by0;y<by1;y++){
        if(y<(int64_t)PAL.bar_h) continue;
        uint32_t t=(uint32_t)((y-by0)*255/WIN_TB);
        uint32_t tr=((PAL.glass_top>>16)&0xFF)*(255-t)+((PAL.glass_bot>>16)&0xFF)*t;
        uint32_t tg=((PAL.glass_top>>8)&0xFF)*(255-t)+((PAL.glass_bot>>8)&0xFF)*t;
        uint32_t tb=(PAL.glass_top&0xFF)*(255-t)+(PAL.glass_bot&0xFF)*t;
        uint32_t gcol=((tr/255)<<16)|((tg/255)<<8)|(tb/255);
        int64_t dy=y-by0, cl=0,cr=0;
        if(dy<WIN_CR){ int64_t side=isqrt64(WIN_CR*WIN_CR - (WIN_CR-dy)*(WIN_CR-dy)); cl=cr=WIN_CR-side; }
        for(int64_t x=x0+cl;x<=x1-cr;x++) hyperde_glass_px(buf,stride,w,h,x,y,gcol,focused?0xE8:0xB0);
    }
    for(int64_t x=x0;x<=x1;x++){
        hyperde_glass_px(buf,stride,w,h,x,by0,0x00FFFFFF,0x22);
        hyperde_glass_px(buf,stride,w,h,x,by0+1,0x00FFFFFF,0x12);
        hyperde_glass_px(buf,stride,w,h,x,by1-1,focused?PAL.accent:0, focused?0x88:0x33);
    }
    int64_t dot_y=by0+(WIN_TB-12)/2, gap=20, close_x=x0+14;
    uint32_t dc=focused?CLOSE_DOT:0x005A5A5E, dm=focused?MIN_DOT:0x005A5A5E, dx=focused?MAX_DOT:0x005A5A5E;
    hyperde_fill_circle(buf,stride,w,h,close_x,dot_y+6,6,dc,0xFF);
    hyperde_fill_circle(buf,stride,w,h,close_x+gap,dot_y+6,6,dm,0xFF);
    hyperde_fill_circle(buf,stride,w,h,close_x+gap*2,dot_y+6,6,dx,0xFF);
    int64_t tx=close_x+gap*2+16; int64_t room = x1-tx>0?(x1-tx)/10:0;
    size_t n = tn < (size_t)room ? tn : (size_t)room;
    if (n) hyperde_draw_text(buf,stride,w,h,tx,by0+(WIN_TB-8)/2-1,title,n,focused?PAL.text:PAL.sub,1,0xFF);
    if(focused){ for(int64_t x=x0;x<=x1;x++){ hyperde_glass_px(buf,stride,w,h,x,by1-1,PAL.accent,0x88); hyperde_glass_px(buf,stride,w,h,x,ry+rh-WIN_SH,PAL.accent,0x38);} 
        for(int64_t y=by0;y<ry+rh-WIN_SH;y++){ hyperde_glass_px(buf,stride,w,h,x0,y,PAL.accent,0x30); hyperde_glass_px(buf,stride,w,h,x1,y,PAL.accent,0x30);} }
}
static void render_windows(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h){
    (void)h;
    if (CM_WMv) {
        int c=lvgl_wm_window_count(CM_WMv); if(c<0)c=0;
        for(int i=c-1;i>=0;i--){ const LvglWindow *ww=(const LvglWindow*)lvgl_wm_window_at(CM_WMv,i);
            if(!ww||!ww->visible||ww->rect.w<=0||ww->rect.h<=0) continue;
            if(ww->rect.y+ww->rect.h <= (int)PAL.bar_h) continue;
            size_t n=0; while(n<40 && ww->title[n]) n++;
            draw_window_chrome(buf,stride,w,h,ww->rect.x,ww->rect.y,ww->rect.w,ww->rect.h,ww->focused!=0,ww->title,n);
        }
    }
    PrsDesktopWin xwins[PRS_WIN_MAX]; int nc=prs_desktop_windows(xwins,PRS_WIN_MAX);
    for(int i=nc-1;i>=0;i--){ if(!xwins[i].mapped||xwins[i].w<=0||xwins[i].h<=0) continue;
        if(xwins[i].y+xwins[i].h <= (int16_t)PAL.bar_h) continue;
        size_t n=0; while(n<40 && xwins[i].title[n]) n++;
        draw_window_chrome(buf,stride,w,h,xwins[i].x,xwins[i].y,xwins[i].w,xwins[i].h,xwins[i].focused!=0,(const uint8_t*)xwins[i].title,n);
    }
}
static void render_bar(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h){
    int64_t mid=(int64_t)PAL.bar_h/2;
    for(int64_t y=0;y<(int64_t)PAL.bar_h;y++){
        uint32_t t=(uint32_t)(y*255/PAL.bar_h);
        uint32_t r=((PAL.glass_top>>16)&0xFF)*(255-t)+((PAL.glass_bot>>16)&0xFF)*t;
        uint32_t g2=((PAL.glass_top>>8)&0xFF)*(255-t)+((PAL.glass_bot>>8)&0xFF)*t;
        uint32_t b=(PAL.glass_top&0xFF)*(255-t)+(PAL.glass_bot&0xFF)*t;
        uint32_t gcol=((r/255)<<16)|((g2/255)<<8)|(b/255);
        for(uint32_t x=0;x<w;x++) hyperde_glass_px(buf,stride,w,h,x,y,gcol,PAL.glass_alpha);
    }
    /* launcher dots */
    hyperde_round_rect(buf,stride,w,h,10,mid-11,46,mid+11,10,0x00FFFFFF,0x1A);
    for(int gy=-1;gy<=1;gy++) for(int gx=-1;gx<=1;gx++) hyperde_fill_circle(buf,stride,w,h,28+gx*6,mid+gy*6,2,PAL.accent,0xFF);
    /* app title + workspaces */
    uint8_t tbuf[64]; int tn=focused_app_title(CM_WMv,tbuf,sizeof(tbuf));
    int64_t app_reserve = tn>0 ? (tn*16+24) : 0;
    if(tn) hyperde_draw_text(buf,stride,w,h,BAR_ANCHOR,mid-8,tbuf,(size_t)tn,PAL.text,2,0xFF);
    int ws_num=WS_NUMv; if(ws_num<1)ws_num=1; if(ws_num>9)ws_num=9;
    int ws_cur=WS_CURv; if(ws_cur<0)ws_cur=0; if(ws_cur>=ws_num)ws_cur=ws_num-1;
    int64_t ws_start=BAR_ANCHOR+app_reserve;
    for(int i=0;i<ws_num;i++){ int64_t wx=ws_start+i*22,wy=mid-2;
        if(i==ws_cur) hyperde_round_rect(buf,stride,w,h,wx,wy-1,wx+14,wy+5,3,PAL.accent,0xFF);
        else hyperde_round_rect(buf,stride,w,h,wx,wy,wx+14,wy+4,2,0x00FFFFFF,0x26);
    }
    /* clock center */
    RtcTime now; rtc_read(&now);
    uint8_t clk[16]; size_t cn=0;
    cn+=fmt_int(now.hour,0,clk+cn,sizeof(clk)-cn); clk[cn++]=':';
    cn+=fmt_int(now.minute,2,clk+cn,sizeof(clk)-cn);
    hyperde_draw_text(buf,stride,w,h,(int64_t)w/2 - (int64_t)cn*10, mid-8, clk,cn,PAL.text,2,0xFF);
    /* stats right */
    int cpu=cpu_percent(); int mem=hyperde_shell_mem_mb();
    uint8_t sb[32]; size_t sn=0; const char *cl="cpu"; while(*cl) sb[sn++]=*cl++; sb[sn++]=' '; sn+=fmt_int(cpu,0,sb+sn,sizeof(sb)-sn);
    sb[sn++]=' '; sb[sn++]='m'; sb[sn++]='e'; sb[sn++]='m'; sb[sn++]=' '; sn+=fmt_int(mem,0,sb+sn,sizeof(sb)-sn);
    hyperde_draw_text(buf,stride,w,h,(int64_t)w - (int64_t)sn*10 - 80, mid-4, sb,sn,PAL.sub,1,0xFF);
}
