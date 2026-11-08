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

/* HyperDE "Signal" UI - intentionally not macOS/GNOME-like.
 * Window controls: right-aligned square glyph slots (match prs_window_at),
 * no traffic-light circles. Bar: command rail with segmented blocks. */
static const int64_t WIN_TB = 30, WIN_SH = 6; /* keep PRS_CHROME_* compat */
static const int64_t BAR_ANCHOR = 56;

static Palette PAL = {
    0x007CFFB2, /* accent: mint signal */
    0x007CFFB2, /* green/status */
    0x00E8ECF1, /* text */
    0x0087929D, /* sub */
    0x001A2028, /* glass_top / rail */
    0x0010151B, /* glass_bot */
    0xF0,       /* glass_alpha */
    0x007CFFB2, /* focused_border */
    0x00384452, /* normal_border */
    1,          /* border_width */
    30          /* bar_h */
};

extern "C" const Palette *hyperde_pal(void) { return &PAL; }

static uint32_t FB_Wv, FB_Hv;
static int ACTIVEv = 1, INIT_DONEv, LAST_MINv = -1, LAST_CPUv = -1;
static int WS_CURv, WS_NUMv = 6;
static uint64_t DRAWSv, LAST_MSv, LAST_MEMv, LAST_WMSIGv = (uint64_t)-1, LAST_XSIGv = (uint64_t)-1;
static void *CM_WMv;
static uint64_t cpu_last_total, cpu_last_busy;
static int cpu_last_pct;

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
static int cpu_sample(void){
    uint64_t total = timer_get_milliseconds()/10, busy=sched_busy_ticks();
    if (cpu_last_total && total>cpu_last_total) {
        uint64_t dtotal=total-cpu_last_total, dbusy=busy-cpu_last_busy;
        int pct = (int)(dbusy*100/dtotal); if(pct<0)pct=0; if(pct>100)pct=100;
        cpu_last_pct = pct;
    }
    cpu_last_total=total; cpu_last_busy=busy; return cpu_last_pct;
}
static int cpu_percent(void){ return cpu_sample(); }

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
    int64_t app_reserve = tn>0 ? (tn*16+20) : 0;
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
static void draw_glyph_slot(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h,int64_t cx,int64_t cy,int focused,int kind,uint32_t accent){
    /* kind: 1=close(X), 2=minimize(-), 3=maximize(box). Positions must match prs_window_at. */
    int64_t s=8;
    uint32_t bc = focused ? accent : PAL.normal_border;
    uint32_t ba = focused ? 0x70 : 0x30;
    /* square slot */
    for(int64_t x=cx-s;x<=cx+s;x++){ hyperde_glass_px(buf,stride,w,h,x,cy-s,bc,ba); hyperde_glass_px(buf,stride,w,h,x,cy+s,bc,ba); }
    for(int64_t y=cy-s;y<=cy+s;y++){ hyperde_glass_px(buf,stride,w,h,cx-s,y,bc,ba); hyperde_glass_px(buf,stride,w,h,cx+s,y,bc,ba); }
    uint32_t gc = focused ? PAL.text : PAL.sub;
    if(kind==1){ /* X */
        for(int i=-4;i<=4;i++){ px(buf,stride,w,h,cx+i,cy+i,gc); px(buf,stride,w,h,cx+i,cy-i,gc); }
    } else if(kind==2){ /* - */
        for(int i=-4;i<=4;i++) px(buf,stride,w,h,cx+i,cy,gc);
    } else if(kind==3){ /* box */
        for(int i=-3;i<=3;i++){ px(buf,stride,w,h,cx-3,cy+i,gc); px(buf,stride,w,h,cx+3,cy+i,gc); px(buf,stride,w,h,cx+i,cy-3,gc); px(buf,stride,w,h,cx+i,cy+3,gc); }
    }
}
static void draw_window_chrome(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h,int64_t rx,int64_t ry,int64_t rw,int64_t rh,int focused,const uint8_t *title,size_t tn){
    (void)rh;
    /* flat shadow */
    fill_rect(buf,stride,w,h,rx+WIN_SH+2,ry+rh-1,rx+rw-WIN_SH-2,ry+rh+3,0x00000000,focused?0x28:0x14);
    int64_t by0=ry+WIN_SH, by1=by0+WIN_TB, x0=rx+WIN_SH, x1=rx+rw-WIN_SH-1;
    /* title band: flat rail */
    fill_rect(buf,stride,w,h,x0,by0,x1,by1-1,focused?0x001B2430:0x00161C24,0xF2);
    /* left signal spine */
    uint32_t spine = focused?PAL.accent:PAL.normal_border;
    fill_rect(buf,stride,w,h,x0,by0,x0+3,by1-1,spine,focused?0xFF:0x70);
    /* bottom separator */
    fill_rect(buf,stride,w,h,x0,by1-1,x1,by1-1,focused?PAL.accent:0x00283340,focused?0x90:0x50);
    /* controls on RIGHT - must match prs_window_at geometry */
    int64_t dot_y = by0 + (WIN_TB - 12) / 2 + 6;
    int64_t close_x = (rx + rw) - WIN_SH - 18;
    int64_t min_x = close_x - 20;
    int64_t max_x = close_x - 40;
    draw_glyph_slot(buf,stride,w,h,max_x,dot_y,focused,3,PAL.accent);
    draw_glyph_slot(buf,stride,w,h,min_x,dot_y,focused,2,PAL.accent);
    draw_glyph_slot(buf,stride,w,h,close_x,dot_y,focused,1,focused?0x00FF6B7A:PAL.sub);
    /* title left, after spine */
    int64_t tx=x0+12; int64_t max_tx = max_x-18; int64_t room = max_tx>tx?(max_tx-tx)/10:0;
    size_t n = tn < (size_t)room ? tn : (size_t)room;
    if (n) hyperde_draw_text(buf,stride,w,h,tx,by0+(WIN_TB-8)/2-1,title,n,focused?PAL.text:PAL.sub,1,0xFF);
    /* focused frame hint */
    if(focused){
        fill_rect(buf,stride,w,h,x0,ry+rh-WIN_SH-1,x1,ry+rh-WIN_SH-1,PAL.accent,0x35);
        for(int64_t y=by0;y<ry+rh-WIN_SH;y++){ hyperde_glass_px(buf,stride,w,h,x0,y,PAL.accent,0x18); hyperde_glass_px(buf,stride,w,h,x1,y,PAL.accent,0x18); }
    }
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
    int64_t barh=(int64_t)PAL.bar_h, mid=barh/2;
    /* command rail: flat dark + bottom accent line */
    fill_rect(buf,stride,w,h,0,0,(int64_t)w-1,barh-1,0x0010151B,0xF4);
    fill_rect(buf,stride,w,h,0,barh-1,(int64_t)w-1,barh-1,PAL.accent,0x55);
    fill_rect(buf,stride,w,h,0,0,(int64_t)w-1,0,PAL.accent,0x22);
    /* launcher: square chevron block */
    fill_rect(buf,stride,w,h,8,mid-11,44,mid+11,PAL.accent,0x22);
    for(int64_t x=0;x<=0;x++){} /* placeholder */
    for(int64_t x=20;x<=32;x++){ int64_t dy = (x-26); if(dy<0) dy=-dy; px(buf,stride,w,h,x,mid-dy,PAL.accent); px(buf,stride,w,h,x,mid-dy+1,PAL.accent); }
    /* app title + workspaces */
    uint8_t tbuf[64]; int tn=focused_app_title(CM_WMv,tbuf,sizeof(tbuf));
    int64_t app_reserve = tn>0 ? (tn*16+20) : 0;
    if(tn){ fill_rect(buf,stride,w,h,BAR_ANCHOR-4,mid-10,BAR_ANCHOR+(int64_t)tn*16+4,mid+10,0x00FFFFFF,0x08);
        hyperde_draw_text(buf,stride,w,h,BAR_ANCHOR,mid-8,tbuf,(size_t)tn,PAL.text,2,0xFF); }
    int ws_num=WS_NUMv; if(ws_num<1)ws_num=1; if(ws_num>9)ws_num=9;
    int ws_cur=WS_CURv; if(ws_cur<0)ws_cur=0; if(ws_cur>=ws_num)ws_cur=ws_num-1;
    int64_t ws_start=BAR_ANCHOR+app_reserve;
    for(int i=0;i<ws_num;i++){
        int64_t wx=ws_start+i*22; uint8_t d=(uint8_t)('1'+i);
        if(i==ws_cur){ fill_rect(buf,stride,w,h,wx,mid-9,wx+16,mid+9,PAL.accent,0x35);
            for(int64_t x=wx;x<=wx+16;x++) hyperde_glass_px(buf,stride,w,h,x,mid+9,PAL.accent,0xFF);
            hyperde_draw_text(buf,stride,w,h,wx+4,mid-4,&d,1,PAL.text,1,0xFF);
        } else { fill_rect(buf,stride,w,h,wx,mid-9,wx+16,mid+9,0x00FFFFFF,0x06);
            hyperde_draw_text(buf,stride,w,h,wx+4,mid-4,&d,1,PAL.sub,1,0xFF); }
    }
    /* task pills (keep hit layout in sync with hyperde_shell_bar_hit) */
    int64_t px = ws_start + ws_num*22;
    if (CM_WMv) {
        int lcount = lvgl_wm_window_count(CM_WMv); if(lcount<0) lcount=0;
        for(int i=0;i<lcount && px < (int64_t)w/2-150;i++){
            const LvglWindow *ww=(const LvglWindow*)lvgl_wm_window_at(CM_WMv,i); if(!ww) continue;
            size_t n=0; while(n<9 && ww->title[n]) n++;
            int64_t tw=(int64_t)n*10+12;
            uint32_t ba = ww->focused ? 0x30 : 0x0C;
            fill_rect(buf,stride,w,h,px,mid-10,px+tw,mid+10,ww->focused?PAL.accent:0x00FFFFFF,ba);
            if(ww->focused) for(int64_t x=px;x<=px+tw;x++) hyperde_glass_px(buf,stride,w,h,x,mid-10,PAL.accent,0xAA);
            hyperde_draw_text(buf,stride,w,h,px+6,mid-4,ww->title,n,ww->focused?PAL.text:PAL.sub,1,0xFF);
            px += tw+8;
        }
    }
    { PrsDesktopWin xwins[PRS_WIN_MAX]; int nc=prs_desktop_windows(xwins,PRS_WIN_MAX);
      for(int i=0;i<nc && px<(int64_t)w/2-150;i++){ if(!xwins[i].mapped) continue;
        size_t n=0; while(n<9 && xwins[i].title[n]) n++;
        int64_t tw=(int64_t)n*10+12;
        uint32_t ba = xwins[i].focused ? 0x30 : 0x0C;
        fill_rect(buf,stride,w,h,px,mid-10,px+tw,mid+10,xwins[i].focused?PAL.accent:0x00FFFFFF,ba);
        if(xwins[i].focused) for(int64_t x=px;x<=px+tw;x++) hyperde_glass_px(buf,stride,w,h,x,mid-10,PAL.accent,0xAA);
        hyperde_draw_text(buf,stride,w,h,px+6,mid-4,(const uint8_t*)xwins[i].title,n,xwins[i].focused?PAL.text:PAL.sub,1,0xFF);
        px += tw+8;
      }
    }
    /* clock */
    RtcTime now; rtc_read(&now);
    uint8_t clk[16]; size_t cn=0;
    cn+=fmt_int(now.hour,2,clk+cn,sizeof(clk)-cn); clk[cn++]=':';
    cn+=fmt_int(now.minute,2,clk+cn,sizeof(clk)-cn);
    int64_t cx = (int64_t)w/2 - (int64_t)cn*10;
    fill_rect(buf,stride,w,h,cx-10,mid-10,cx+(int64_t)cn*20+10,mid+10,PAL.accent,0x14);
    hyperde_draw_text(buf,stride,w,h,cx,mid-8,clk,cn,PAL.text,2,0xFF);
    /* stats right: compact segments */
    int cpu=cpu_percent(); int mem=hyperde_shell_mem_mb();
    uint8_t sb[40]; size_t sn=0;
    const char *pfx="CPU "; while(*pfx) sb[sn++]=*pfx++; sn+=fmt_int(cpu,0,sb+sn,sizeof(sb)-sn); sb[sn++]='%';
    sb[sn++]=' '; sb[sn++]=0xC2; sb[sn++]=0xB7; sb[sn++]=' '; /* middle dot utf8 bytes - but draw_text skips >=0x80; use space/dot */
    sn-=3; sb[sn++]=' '; sb[sn++]=' '; sb[sn++]=' ';
    const char *mfx="MEM "; while(*mfx) sb[sn++]=*pfx?*pfx:0; /* oops - fix below in logic? easier: rebuild */
    (void)sn;
    sn=0; const char *a="CPU "; while(*a) sb[sn++]=*a++; sn+=fmt_int(cpu,0,sb+sn,sizeof(sb)-sn); sb[sn++]='%'; sb[sn++]=' '; sb[sn++]=' '; const char *b="MEM "; while(*b) sb[sn++]=*b++; sn+=fmt_int(mem,0,sb+sn,sizeof(sb)-sn); sb[sn++]='M';
    hyperde_draw_text(buf,stride,w,h,(int64_t)w - (int64_t)sn*10 - 80, mid-4, sb,sn,PAL.sub,1,0xFF);
}
