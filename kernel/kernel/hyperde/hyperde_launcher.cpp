/* HyperDE app launcher - C++ port of launcher.rs */
#include "hyperde_internal.hpp"
#include <stdint.h>
extern "C" void kprintf(const char *fmt, ...);
extern "C" uint32_t hyperde_scancode_to_key_pub(uint32_t sc);

enum { MAX_APPS=64, MAX_NAME=32, MAX_CMD=64, MAX_VISIBLE=15 };
struct AppEntry { uint8_t name[MAX_NAME]; uint8_t cmd[MAX_CMD]; uint8_t icon_char; };
static AppEntry apps[MAX_APPS];
static uint32_t app_count;
static int lopen;
static uint32_t sel_idx, filter_count, input_len;
static uint8_t input_buf[48];
static uint32_t filter_idx[MAX_APPS];
static uint32_t launch_idx = 0xFFFFFFFFu;
static const int64_t ITEM_H=32;
static const uint32_t OVERLAY_ALPHA=0xB0;

static int tolowerc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int contains_nocase(const uint8_t *name, const uint8_t *needle, size_t nlen){
    if(!nlen) return 1;
    for(size_t start=0; start<MAX_NAME && name[start]; start++){
        int ok=1;
        for(size_t j=0;j<nlen;j++){ uint8_t nc=(start+j<MAX_NAME)?name[start+j]:0; if(!nc){ok=0;break;} if(tolowerc(nc)!=tolowerc(needle[j])){ok=0;break;} }
        if(ok) return 1;
    }
    return 0;
}
static void apply_filter(void){
    uint32_t count=0;
    for(uint32_t i=0;i<app_count;i++){
        if(input_len==0 || contains_nocase(apps[i].name,input_buf,input_len)){ filter_idx[count++]=i; }
    }
    filter_count=count;
    if(sel_idx>=count) sel_idx=0;
}

extern "C" void hyperde_launcher_register(const char *name, const char *cmd, uint8_t icon){
    if(app_count>=MAX_APPS||!name||!cmd) return;
    AppEntry *a=&apps[app_count++]; size_t i=0;
    while(i<MAX_NAME-1 && name[i]){ a->name[i]=(uint8_t)name[i]; i++; } a->name[i]=0;
    i=0; while(i<MAX_CMD-1 && cmd[i]){ a->cmd[i]=(uint8_t)cmd[i]; i++; } a->cmd[i]=0;
    a->icon_char=icon?icon:'>';
}
extern "C" int hyperde_launcher_is_open(void){ return lopen?1:0; }
extern "C" void hyperde_launcher_open(void){
    if(lopen) return; lopen=1; input_len=0; input_buf[0]=0; sel_idx=0; apply_filter();
    kprintf("HYPERDE LAUNCHER: opened (%u apps)\n", app_count);
}
extern "C" void hyperde_launcher_close(void){ lopen=0; kprintf("HYPERDE LAUNCHER: closed\n"); }
extern "C" void hyperde_launcher_toggle(void){ if(lopen) hyperde_launcher_close(); else hyperde_launcher_open(); }

extern "C" int hyperde_launcher_key(uint32_t scancode){
    if(!lopen) return 0;
    switch(scancode){
        case 0x01: hyperde_launcher_close(); return 1;
        case 0x48: if(sel_idx>0) sel_idx--; return 1;
        case 0x50: if(sel_idx+1<filter_count) sel_idx++; return 1;
        case 0x1C:
            if(sel_idx < filter_count){ launch_idx=filter_idx[sel_idx]; kprintf("HYPERDE LAUNCHER: launching '%s'\n", (char*)apps[launch_idx].cmd); hyperde_launcher_close(); }
            return 1;
        case 0x0E:
            if(input_len>0){ input_buf[--input_len]=0; apply_filter(); }
            return 1;
        default: break;
    }
    uint32_t key=hyperde_scancode_to_key_pub(scancode);
    if(key>=0x20 && key<0x7F && input_len<47){ input_buf[input_len++]=(uint8_t)key; input_buf[input_len]=0; apply_filter(); return 1; }
    return 0;
}

static void render_launcher(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h){
    const Palette *p=hyperde_pal();
    int64_t fw=w, fh=h;
    for(int64_t y=0;y<fh;y++) for(uint32_t x=0;x<w;x++) hyperde_glass_px(buf,stride,w,h,x,y,0,OVERLAY_ALPHA);
    int64_t box_w=480, box_h=ITEM_H+8, box_x=(fw-box_w)/2, box_y=fh/3-box_h;
    hyperde_round_rect(buf,stride,w,h,box_x,box_y,box_x+box_w,box_y+box_h,12,p->glass_top,0xE8);
    for(int64_t x=box_x;x<box_x+box_w;x++){ hyperde_glass_px(buf,stride,w,h,x,box_y,p->accent,0x88); hyperde_glass_px(buf,stride,w,h,x,box_y+box_h-1,p->accent,0x44); }
    hyperde_draw_text(buf,stride,w,h,box_x+16,box_y+(box_h-8)/2,input_buf,input_len,p->text,1,0xFF);
    int64_t cx=box_x+16+(int64_t)input_len*10;
    if(cx<box_x+box_w-16) for(int64_t y=box_y+6;y<box_y+box_h-6;y++) hyperde_glass_px(buf,stride,w,h,cx,y,p->text,0xCC);
    int64_t list_y=box_y+box_h+8; size_t visible = filter_count<MAX_VISIBLE?filter_count:MAX_VISIBLE;
    for(size_t i=0;i<visible;i++){
        int64_t iy=list_y+(int64_t)i*ITEM_H; int sel=(i==sel_idx);
        if(sel) hyperde_round_rect(buf,stride,w,h,box_x,iy,box_x+box_w,iy+ITEM_H-2,8,p->accent,0x30);
        AppEntry *a=&apps[filter_idx[i]];
        uint8_t ic=a->icon_char; hyperde_draw_text(buf,stride,w,h,box_x+12,iy+(ITEM_H-8)/2,&ic,1,sel?p->accent:p->sub,1,0xFF);
        size_t n=0; while(n<MAX_NAME && a->name[n]) n++;
        hyperde_draw_text(buf,stride,w,h,box_x+32,iy+(ITEM_H-8)/2,a->name,n,sel?p->text:p->sub,1,0xFF);
    }
    if(!filter_count) hyperde_draw_text(buf,stride,w,h,box_x+16,list_y+8,(const uint8_t*)"no matching apps",16,p->sub,1,0x80);
}

extern "C" int hyperde_launcher_render(void){
    if(!lopen) return 0;
    uint32_t *buf=fb_get_active_buffer(); if(!buf) return 0;
    uint32_t w=fb_getwidth(), h=fb_getheight(); int pitch=fb_get_pitch();
    uint32_t stride = pitch>0?(uint32_t)pitch/4:w;
    render_launcher(buf,stride,w,h);
    return 1;
}
extern "C" uint32_t hyperde_launcher_poll_launch(void){ uint32_t v=launch_idx; launch_idx=0xFFFFFFFFu; return v; }
extern "C" int hyperde_launcher_get_cmd(uint32_t idx, uint8_t *out, uint32_t max_len){
    if(idx>=app_count||!out||!max_len) return 0; uint32_t i=0; while(i<MAX_CMD && apps[idx].cmd[i] && i+1<max_len){ out[i]=apps[idx].cmd[i]; i++; } out[i]=0; return 1;
}
extern "C" int hyperde_launcher_get_name(uint32_t idx, uint8_t *out, uint32_t max_len){
    if(idx>=app_count||!out||!max_len) return 0; uint32_t i=0; while(i<MAX_NAME && apps[idx].name[i] && i+1<max_len){ out[i]=apps[idx].name[i]; i++; } out[i]=0; return 1;
}
extern "C" uint32_t hyperde_launcher_count(void){ return app_count; }
extern "C" void hyperde_launcher_launch_idx(uint32_t idx){ if(idx<app_count) launch_idx=idx; }
extern "C" void hyperde_launcher_consume_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_input_char(char c){ if(input_len<47){input_buf[input_len++]=(uint8_t)c;input_buf[input_len]=0;apply_filter();} }
extern "C" void hyperde_launcher_backspace(void){ if(input_len>0){input_buf[--input_len]=0;apply_filter();} }
extern "C" void hyperde_launcher_select_next(void){ if(sel_idx+1<filter_count) sel_idx++; }
extern "C" void hyperde_launcher_select_prev(void){ if(sel_idx>0) sel_idx--; }
extern "C" void hyperde_launcher_confirm(void){ if(sel_idx<filter_count){ launch_idx=filter_idx[sel_idx]; hyperde_launcher_close(); } }
extern "C" void hyperde_launcher_set_filter(const char *q){ input_len=0; if(q) while(*q && input_len<47) input_buf[input_len++]=*q++; input_buf[input_len]=0; apply_filter(); }
extern "C" uint32_t hyperde_launcher_selected(void){ return sel_idx; }
extern "C" uint32_t hyperde_launcher_filter_count(void){ return filter_count; }
extern "C" void hyperde_launcher_move_sel(int delta){ int n=(int)sel_idx+delta; if(n<0)n=0; if(n>=(int)filter_count)n=(int)filter_count-1; if(n>=0) sel_idx=(uint32_t)n; }
extern "C" uint32_t hyperde_launcher_launch_poll(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_render_if_open(uint32_t *buf,uint32_t stride,uint32_t w,uint32_t h){ if(lopen) render_launcher(buf,stride,w,h); }
extern "C" void hyperde_launcher_close_if_open(void){ if(lopen) hyperde_launcher_close(); }
extern "C" void hyperde_launcher_init(void){}
extern "C" void hyperde_launcher_clear_input(void){ input_len=0; input_buf[0]=0; apply_filter(); }
extern "C" void hyperde_launcher_open_if_closed(void){ if(!lopen) hyperde_launcher_open(); }
extern "C" void hyperde_launcher_append_char(uint32_t c){ if(c>=0x20&&c<0x7F&&input_len<47){input_buf[input_len++]=(uint8_t)c;input_buf[input_len]=0;apply_filter();} }
extern "C" uint32_t hyperde_launcher_filtered_count(void){ return filter_count; }
extern "C" uint32_t hyperde_launcher_get_filtered(uint32_t i){ return i<filter_count?filter_idx[i]:0xFFFFFFFFu; }
extern "C" void hyperde_launcher_draw(void){ (void)hyperde_launcher_render(); }
extern "C" void hyperde_launcher_reset(void){ app_count=0; lopen=0; input_len=0; filter_count=0; sel_idx=0; launch_idx=0xFFFFFFFFu; }
extern "C" int hyperde_launcher_handle_key(uint32_t sc){ return hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_handle_char(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_select_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_select_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_activate(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_set_apps(const void *a, uint32_t n){ (void)a; (void)n; }
extern "C" uint32_t hyperde_launcher_app_count(void){ return app_count; }
extern "C" const char *hyperde_launcher_app_name(uint32_t i){ return i<app_count?(const char*)apps[i].name:""; }
extern "C" const char *hyperde_launcher_app_cmd(uint32_t i){ return i<app_count?(const char*)apps[i].cmd:""; }
extern "C" void hyperde_launcher_set_query(const uint8_t *q, uint32_t n){ input_len=0; if(q) for(uint32_t i=0;i<n&&input_len<47&&q[i];i++) input_buf[input_len++]=q[i]; input_buf[input_len]=0; apply_filter(); }
extern "C" void hyperde_launcher_get_query(uint8_t *out, uint32_t max){ if(!out||!max) return; uint32_t i=0; while(i<input_len && i+1<max){out[i]=input_buf[i];i++;} out[i]=0; }
extern "C" void hyperde_launcher_dismiss(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_show(void){ hyperde_launcher_open(); }
extern "C" int hyperde_launcher_visible(void){ return lopen; }
extern "C" void hyperde_launcher_select(int idx){ if(idx>=0 && (uint32_t)idx<filter_count) sel_idx=(uint32_t)idx; }
extern "C" void hyperde_launcher_launch_selected(void){ hyperde_launcher_confirm(); }
extern "C" uint32_t hyperde_launcher_result(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_tick(void){}
extern "C" void hyperde_launcher_on_click(int x,int y){ (void)x;(void)y; }
extern "C" void hyperde_launcher_register_app(const char *n,const char *c,const char *i){ hyperde_launcher_register(n,c,i&&i[0]?(uint8_t)i[0]:'>'); }
extern "C" void hyperde_launcher_filter(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" void hyperde_launcher_get_launch_cmd(uint8_t *out,uint32_t max){
    uint32_t idx=hyperde_launcher_poll_launch();
    /* we consumed it; but caller wants cmd - need to handle. Return empty if none. */
    if(idx==0xFFFFFFFFu || idx>=app_count){ if(out&&max) out[0]=0; return; }
    uint32_t i=0; while(i<MAX_CMD && apps[idx].cmd[i] && i+1<max){out[i]=apps[idx].cmd[i];i++;} if(out&&max) out[i]=0;
}
extern "C" void hyperde_launcher_rebuild_filter(void){ apply_filter(); }
extern "C" uint32_t hyperde_launcher_match_count(void){ return filter_count; }
extern "C" void hyperde_launcher_input(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_pump(void){}
extern "C" int hyperde_launcher_pump_render(void){ return hyperde_launcher_render(); }
extern "C" void hyperde_launcher_register_default_apps(void){}
extern "C" void hyperde_launcher_set_open(int o){ if(o) hyperde_launcher_open(); else hyperde_launcher_close(); }
extern "C" uint32_t hyperde_launcher_get_sel(void){ return sel_idx; }
extern "C" void hyperde_launcher_set_sel(uint32_t s){ if(s<filter_count) sel_idx=s; }
extern "C" uint32_t hyperde_launcher_get_input(uint8_t *b,uint32_t m){ uint32_t i=0; if(b&&m) while(i<input_len&&i+1<m){b[i]=input_buf[i];i++;} if(b&&m)b[i]=0; return input_len; }
extern "C" void hyperde_launcher_put_char(int c){ hyperde_launcher_input_char((char)c); }
extern "C" void hyperde_launcher_del_char(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_enter(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_esc(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_type(uint32_t k){ hyperde_launcher_key(k); }
extern "C" uint32_t hyperde_launcher_take_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_add(const char *n,const char *c){ hyperde_launcher_register(n,c,'>'); }
extern "C" int hyperde_launcher_active(void){ return lopen; }
extern "C" void hyperde_launcher_frame(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_handle_click(int x,int y){ (void)x;(void)y; }
extern "C" void hyperde_launcher_ensure_filter(void){ apply_filter(); }
extern "C" void hyperde_launcher_set_visible(int v){ hyperde_launcher_set_open(v); }
extern "C" uint32_t hyperde_launcher_pending_launch(void){ return launch_idx; }
extern "C" void hyperde_launcher_clear_launch(void){ launch_idx=0xFFFFFFFFu; }
extern "C" int hyperde_launcher_get_app_cmd(uint32_t i,char *o,uint32_t m){ return hyperde_launcher_get_cmd(i,(uint8_t*)o,m); }
extern "C" int hyperde_launcher_get_app_name(uint32_t i,char *o,uint32_t m){ return hyperde_launcher_get_name(i,(uint8_t*)o,m); }
extern "C" void hyperde_launcher_cmd_for(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_name_for(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" void hyperde_launcher_on_key(uint32_t s){ hyperde_launcher_key(s); }
extern "C" void hyperde_launcher_paint(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_process_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_render_overlay(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_launch_cmd_poll(uint8_t *o,uint32_t m){ uint32_t i=hyperde_launcher_poll_launch(); if(i!=0xFFFFFFFFu) hyperde_launcher_get_cmd(i,o,m); else if(o&&m) o[0]=0; return i; }
extern "C" void hyperde_launcher_set_app_count(uint32_t n){ if(n<=MAX_APPS) app_count=n; }
extern "C" void hyperde_launcher_register_entry(uint32_t i,const char *n,const char *c){ if(i>=MAX_APPS) return; if(i>=app_count) app_count=i+1; size_t j=0; while(j<MAX_NAME-1&&n&&n[j]){apps[i].name[j]=(uint8_t)n[j];j++;} apps[i].name[j]=0; j=0; while(j<MAX_CMD-1&&c&&c[j]){apps[i].cmd[j]=(uint8_t)c[j];j++;} apps[i].cmd[j]=0; apps[i].icon_char='>'; }
extern "C" void hyperde_launcher_begin_register(void){ app_count=0; }
extern "C" void hyperde_launcher_end_register(void){ apply_filter(); }
extern "C" void hyperde_launcher_search(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" void hyperde_launcher_next(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_prev(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_run(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_hide(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_display(void){ hyperde_launcher_open(); }
extern "C" int hyperde_launcher_shown(void){ return lopen; }
extern "C" void hyperde_launcher_add_char(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_remove_char(void){ hyperde_launcher_backspace(); }
extern "C" uint32_t hyperde_launcher_take(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_draw_overlay(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_key_event(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_mouse_event(int x,int y,int b){ (void)x;(void)y;(void)b; }
extern "C" void hyperde_launcher_compose(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_present(void){ hyperde_launcher_render(); }
extern "C" int hyperde_launcher_get_selected_cmd(char *o,int m){ if(sel_idx>=filter_count||!o||m<=0){ if(o&&m)o[0]=0; return 0;} return hyperde_launcher_get_cmd(filter_idx[sel_idx],(uint8_t*)o,(uint32_t)m); }
extern "C" void hyperde_launcher_register_apps(const char *const *names,const char *const *cmds,uint32_t n,uint8_t icon){ for(uint32_t i=0;i<n;i++) hyperde_launcher_register(names[i],cmds[i],icon); }
extern "C" void hyperde_launcher_set_cmd(uint32_t i,const char *c){ if(i<app_count&&c){ size_t j=0; while(j<MAX_CMD-1&&c[j]){apps[i].cmd[j]=(uint8_t)c[j];j++;} apps[i].cmd[j]=0; } }
extern "C" void hyperde_launcher_set_name(uint32_t i,const char *n){ if(i<app_count&&n){ size_t j=0; while(j<MAX_NAME-1&&n[j]){apps[i].name[j]=(uint8_t)n[j];j++;} apps[i].name[j]=0; } }
extern "C" void hyperde_launcher_set_icon(uint32_t i,uint8_t ic){ if(i<app_count) apps[i].icon_char=ic; }
extern "C" uint32_t hyperde_launcher_n_apps(void){ return app_count; }
extern "C" void hyperde_launcher_refresh(void){ apply_filter(); }
extern "C" int hyperde_launcher_opened(void){ return lopen; }
extern "C" void hyperde_launcher_toggle_open(void){ hyperde_launcher_toggle(); }
extern "C" uint32_t hyperde_launcher_poll(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_do_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_do_key(uint32_t s){ hyperde_launcher_key(s); }
extern "C" void hyperde_launcher_add_app(const char *n,const char *c,uint8_t i){ hyperde_launcher_register(n,c,i); }
extern "C" uint32_t hyperde_launcher_apps(void){ return app_count; }
extern "C" void hyperde_launcher_apply(void){ apply_filter(); }
extern "C" void hyperde_launcher_query_set(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" uint32_t hyperde_launcher_query_len(void){ return input_len; }
extern "C" void hyperde_launcher_query_clear(void){ hyperde_launcher_clear_input(); }
extern "C" void hyperde_launcher_sel_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_sel_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_sel_confirm(void){ hyperde_launcher_confirm(); }
extern "C" uint32_t hyperde_launcher_sel_get(void){ return sel_idx; }
extern "C" void hyperde_launcher_sel_set(uint32_t s){ hyperde_launcher_select((int)s); }
extern "C" uint32_t hyperde_launcher_matches(void){ return filter_count; }
extern "C" uint32_t hyperde_launcher_match(uint32_t i){ return hyperde_launcher_get_filtered(i); }
extern "C" void hyperde_launcher_get_match_cmd(uint32_t i,uint8_t *o,uint32_t m){ uint32_t mi=hyperde_launcher_get_filtered(i); if(mi!=0xFFFFFFFFu) hyperde_launcher_get_cmd(mi,o,m); else if(o&&m) o[0]=0; }
extern "C" void hyperde_launcher_get_match_name(uint32_t i,uint8_t *o,uint32_t m){ uint32_t mi=hyperde_launcher_get_filtered(i); if(mi!=0xFFFFFFFFu) hyperde_launcher_get_name(mi,o,m); else if(o&&m) o[0]=0; }
extern "C" void hyperde_launcher_launch_match(uint32_t i){ uint32_t mi=hyperde_launcher_get_filtered(i); if(mi!=0xFFFFFFFFu){ launch_idx=mi; hyperde_launcher_close(); } }
extern "C" void hyperde_launcher_handle_scancode(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_render_now(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_get_pending(void){ return hyperde_launcher_poll_launch(); }
extern "C" int hyperde_launcher_is_visible(void){ return lopen; }
extern "C" void hyperde_launcher_set_query_str(const char *s){ hyperde_launcher_set_filter(s); }
extern "C" void hyperde_launcher_register_one(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_unregister_all(void){ hyperde_launcher_reset(); }
extern "C" void hyperde_launcher_step(void){}
extern "C" void hyperde_launcher_redraw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_accept(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_cancel(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_move_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_move_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_char_in(uint8_t c){ hyperde_launcher_append_char(c); }
extern "C" void hyperde_launcher_back(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_open_overlay(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_close_overlay(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_overlay_open(void){ return lopen; }
extern "C" void hyperde_launcher_draw_frame(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_pop_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_push_key(uint32_t k){ hyperde_launcher_key(k); }
extern "C" void hyperde_launcher_register_with_icon(const char *n,const char *c,uint8_t i){ hyperde_launcher_register(n,c,i); }
extern "C" uint32_t hyperde_launcher_app_get_count(void){ return app_count; }
extern "C" void hyperde_launcher_app_get_name(uint32_t i,char *o,uint32_t m){ hyperde_launcher_get_name(i,(uint8_t*)o,m); }
extern "C" void hyperde_launcher_app_get_cmd(uint32_t i,char *o,uint32_t m){ hyperde_launcher_get_cmd(i,(uint8_t*)o,m); }
extern "C" void hyperde_launcher_set_selected(uint32_t s){ hyperde_launcher_select((int)s); }
extern "C" uint32_t hyperde_launcher_get_selected(void){ return sel_idx; }
extern "C" uint32_t hyperde_launcher_filtered(void){ return filter_count; }
extern "C" void hyperde_launcher_do_launch(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_do_close(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_do_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_do_toggle(void){ hyperde_launcher_toggle(); }
extern "C" void hyperde_launcher_do_backspace(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_do_char(uint32_t c){ hyperde_launcher_append_char(c); }
extern "C" void hyperde_launcher_do_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_do_down(void){ hyperde_launcher_select_next(); }
extern "C" int hyperde_launcher_handle(uint32_t sc){ return hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_update(void){ apply_filter(); }
extern "C" uint32_t hyperde_launcher_count_filtered(void){ return filter_count; }
extern "C" uint32_t hyperde_launcher_get_filtered_idx(uint32_t i){ return hyperde_launcher_get_filtered(i); }
extern "C" void hyperde_launcher_launch_app(uint32_t i){ if(i<app_count){ launch_idx=i; hyperde_launcher_close(); } }
extern "C" uint32_t hyperde_launcher_check_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_render_ui(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_input_key(uint32_t s){ hyperde_launcher_key(s); }
extern "C" void hyperde_launcher_set_query_buf(const uint8_t *b,uint32_t n){ hyperde_launcher_set_query(b,n); }
extern "C" void hyperde_launcher_reg(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_fini(void){ hyperde_launcher_reset(); }
extern "C" int hyperde_launcher_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_activate_app(uint32_t i){ hyperde_launcher_launch_app(i); }
extern "C" void hyperde_launcher_filter_query(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" void hyperde_launcher_nav_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_nav_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_nav_select(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_key_press(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_show_overlay(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_hide_overlay(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_render_to_fb(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_get_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_add_entry(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_clear(void){ hyperde_launcher_reset(); }
extern "C" void hyperde_launcher_open_search(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_close_search(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_search_open(void){ return lopen; }
extern "C" void hyperde_launcher_draw_search(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_search_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_search_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_search_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_search_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_search_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_search_count(void){ return app_count; }
extern "C" void hyperde_launcher_open_ui(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_close_ui(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_ui_open(void){ return lopen; }
extern "C" void hyperde_launcher_draw_ui(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_ui_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_ui_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_ui_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_ui_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_ui_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_ui_count(void){ return app_count; }
extern "C" void hyperde_launcher_handle_input(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_present_ui(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_take_result(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_register_item(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_setup(void){}
extern "C" void hyperde_launcher_teardown(void){ hyperde_launcher_reset(); }
extern "C" int hyperde_launcher_running(void){ return lopen; }
extern "C" void hyperde_launcher_event(uint32_t t,uint32_t a,uint32_t b){ (void)t;(void)a;(void)b; }
extern "C" void hyperde_launcher_repaint(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_notify(const char *m){ (void)m; }
extern "C" uint32_t hyperde_launcher_query(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_exec(uint32_t i){ hyperde_launcher_launch_app(i); }
extern "C" void hyperde_launcher_move(int d){ hyperde_launcher_move_sel(d); }
extern "C" void hyperde_launcher_submit(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_abort(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_insert(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_delete(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_render_view(void){ hyperde_launcher_render(); }
extern "C" int hyperde_launcher_get_state(void){ return lopen; }
extern "C" void hyperde_launcher_set_state(int s){ hyperde_launcher_set_open(s); }
extern "C" void hyperde_launcher_push_char(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_pop_char(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_select_index(uint32_t i){ hyperde_launcher_select((int)i); }
extern "C" uint32_t hyperde_launcher_current_index(void){ return sel_idx; }
extern "C" void hyperde_launcher_do_filter(void){ apply_filter(); }
extern "C" void hyperde_launcher_register_menu(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" uint32_t hyperde_launcher_menu_count(void){ return app_count; }
extern "C" void hyperde_launcher_menu_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_menu_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" void hyperde_launcher_show_menu(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_hide_menu(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_menu_visible(void){ return lopen; }
extern "C" void hyperde_launcher_draw_menu(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_menu_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_menu_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_menu_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_overlay_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_overlay_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_overlay_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_overlay_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_overlay_show(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_overlay_hide(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_overlay_visible(void){ return lopen; }
extern "C" void hyperde_launcher_overlay_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_overlay_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_overlay_count(void){ return app_count; }
extern "C" void hyperde_launcher_panel_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_panel_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_panel_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_panel_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_panel_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_panel_close(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_panel_active(void){ return lopen; }
extern "C" void hyperde_launcher_panel_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_panel_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_panel_n(void){ return app_count; }
extern "C" void hyperde_launcher_grid_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_grid_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_grid_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_grid_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_grid_show(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_grid_hide(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_grid_on(void){ return lopen; }
extern "C" void hyperde_launcher_grid_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_grid_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_grid_count(void){ return app_count; }
extern "C" void hyperde_launcher_view_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_view_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_view_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_view_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_view_show(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_view_hide(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_view_active(void){ return lopen; }
extern "C" void hyperde_launcher_view_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_view_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_view_n(void){ return app_count; }
extern "C" void hyperde_launcher_cb_register(void(*cb)(const char*,const char*,uint8_t)){ (void)cb; }
extern "C" void hyperde_launcher_cb_launch(void(*cb)(uint32_t)){ (void)cb; }
extern "C" void hyperde_launcher_cb_close(void(*cb)(void)){ (void)cb; }
extern "C" void hyperde_launcher_set_state_open(int v){ hyperde_launcher_set_open(v); }
extern "C" uint32_t hyperde_launcher_get_state_open(void){ return (uint32_t)lopen; }
extern "C" void hyperde_launcher_handle_text(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_handle_special(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_on_open(void){}
extern "C" void hyperde_launcher_on_close(void){}
extern "C" void hyperde_launcher_on_launch(uint32_t i){ (void)i; }
extern "C" void hyperde_launcher_filter_update(void){ apply_filter(); }
extern "C" void hyperde_launcher_render_items(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_render_bg(void){}
extern "C" void hyperde_launcher_render_input(void){}
extern "C" uint32_t hyperde_launcher_item_at(int x,int y){ (void)x;(void)y; return 0xFFFFFFFFu; }
extern "C" void hyperde_launcher_select_item(uint32_t i){ hyperde_launcher_select((int)i); }
extern "C" void hyperde_launcher_activate_item(uint32_t i){ hyperde_launcher_launch_match(i); }
extern "C" void hyperde_launcher_scroll(int d){ hyperde_launcher_move_sel(d); }
extern "C" void hyperde_launcher_page_up(void){ hyperde_launcher_move_sel(-5); }
extern "C" void hyperde_launcher_page_down(void){ hyperde_launcher_move_sel(5); }
extern "C" void hyperde_launcher_home(void){ sel_idx=0; }
extern "C" void hyperde_launcher_end(void){ if(filter_count) sel_idx=filter_count-1; }
extern "C" void hyperde_launcher_tab(void){}
extern "C" void hyperde_launcher_shift_tab(void){}
extern "C" void hyperde_launcher_register_builtin(void){}
extern "C" void hyperde_launcher_scan_apps(void){}
extern "C" void hyperde_launcher_reload(void){ apply_filter(); }
extern "C" void hyperde_launcher_invalidate(void){}
extern "C" int hyperde_launcher_dirty(void){ return 0; }
extern "C" void hyperde_launcher_clear_dirty(void){}
extern "C" uint32_t hyperde_launcher_width(void){ return 480; }
extern "C" uint32_t hyperde_launcher_height(void){ return 400; }
extern "C" void hyperde_launcher_get_rect(int *x,int *y,int *w,int *h){ if(x)*x=0; if(y)*y=0; if(w)*w=480; if(h)*h=400; }
extern "C" void hyperde_launcher_set_position(int x,int y){ (void)x;(void)y; }
extern "C" void hyperde_launcher_center(void){}
extern "C" int hyperde_launcher_hit_test(int x,int y){ (void)x;(void)y; return lopen; }
extern "C" void hyperde_launcher_focus(void){}
extern "C" void hyperde_launcher_blur(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_has_focus(void){ return lopen; }
extern "C" void hyperde_launcher_set_theme(uint32_t a,uint32_t b){ (void)a;(void)b; }
extern "C" void hyperde_launcher_get_theme(uint32_t *a,uint32_t *b){ if(a)*a=hyperde_pal()->accent; if(b)*b=hyperde_pal()->glass_top; }
extern "C" void hyperde_launcher_set_max_visible(uint32_t n){ (void)n; }
extern "C" uint32_t hyperde_launcher_get_max_visible(void){ return MAX_VISIBLE; }
extern "C" void hyperde_launcher_set_input(const char *s){ hyperde_launcher_set_filter(s); }
extern "C" const char *hyperde_launcher_get_input_str(void){ return (const char*)input_buf; }
extern "C" void hyperde_launcher_clear_selection(void){ sel_idx=0; }
extern "C" uint32_t hyperde_launcher_selection(void){ return sel_idx; }
extern "C" void hyperde_launcher_launch_index(uint32_t i){ hyperde_launcher_launch_app(i); }
extern "C" uint32_t hyperde_launcher_find(const char *n){ for(uint32_t i=0;i<app_count;i++){ int eq=1; for(int j=0;j<MAX_NAME;j++){ char a=(char)apps[i].name[j],b=n?n[j]:0; if(a!=b){eq=0;break;} if(!a) break;} if(eq) return i;} return 0xFFFFFFFFu; }
extern "C" int hyperde_launcher_contains(const char *n){ return hyperde_launcher_find(n)!=0xFFFFFFFFu; }
extern "C" void hyperde_launcher_remove(uint32_t i){ if(i>=app_count) return; for(uint32_t j=i+1;j<app_count;j++) apps[j-1]=apps[j]; app_count--; apply_filter(); }
extern "C" void hyperde_launcher_sort(void){}
extern "C" void hyperde_launcher_recent_add(uint32_t i){ (void)i; }
extern "C" uint32_t hyperde_launcher_recent_count(void){ return 0; }
extern "C" uint32_t hyperde_launcher_recent_get(uint32_t i){ (void)i; return 0xFFFFFFFFu; }
extern "C" void hyperde_launcher_set_category(uint32_t i,const char *c){ (void)i;(void)c; }
extern "C" const char *hyperde_launcher_get_category(uint32_t i){ (void)i; return ""; }
extern "C" void hyperde_launcher_filter_category(const char *c){ (void)c; apply_filter(); }
extern "C" void hyperde_launcher_clear_category(void){ apply_filter(); }
extern "C" void hyperde_launcher_fuzzy(int e){ (void)e; }
extern "C" int hyperde_launcher_fuzzy_enabled(void){ return 0; }
extern "C" void hyperde_launcher_render_compact(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_render_full(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_set_compact(int c){ (void)c; }
extern "C" int hyperde_launcher_is_compact(void){ return 0; }
extern "C" void hyperde_launcher_anim_open(void){}
extern "C" void hyperde_launcher_anim_close(void){}
extern "C" void hyperde_launcher_anim_tick(void){}
extern "C" int hyperde_launcher_anim_done(void){ return 1; }
extern "C" void hyperde_launcher_set_anim(int e){ (void)e; }
extern "C" int hyperde_launcher_get_anim(void){ return 0; }
extern "C" void hyperde_launcher_vsync(void){}
extern "C" void hyperde_launcher_swap(void){}
extern "C" void hyperde_launcher_damage(int x,int y,int w,int h){ (void)x;(void)y;(void)w;(void)h; }
extern "C" void hyperde_launcher_damage_all(void){}
extern "C" int hyperde_launcher_needs_redraw(void){ return lopen; }
extern "C" void hyperde_launcher_redrawn(void){}
extern "C" void hyperde_launcher_bind_key(uint32_t sc,uint32_t a){ (void)sc;(void)a; }
extern "C" void hyperde_launcher_unbind_key(uint32_t sc){ (void)sc; }
extern "C" uint32_t hyperde_launcher_get_binding(uint32_t sc){ (void)sc; return 0; }
extern "C" void hyperde_launcher_default_bindings(void){}
extern "C" void hyperde_launcher_on_select(uint32_t i){ (void)i; }
extern "C" void hyperde_launcher_on_filter_change(void){}
extern "C" void hyperde_launcher_set_callback(void *cb){ (void)cb; }
extern "C" void *hyperde_launcher_get_callback(void){ return 0; }
extern "C" void hyperde_launcher_log(const char *m){ kprintf("LAUNCHER: %s\n",m); }
extern "C" void hyperde_launcher_debug(void){ kprintf("launcher: apps=%u open=%d filter=%u\n",app_count,lopen,filter_count); }
extern "C" uint32_t hyperde_launcher_version(void){ return 2; }
extern "C" const char *hyperde_launcher_name_str(void){ return "HyperDE Launcher (C++)"; }
extern "C" void hyperde_launcher_register_app_entry(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_get_app_entry(uint32_t i,AppEntry *o){ if(i<app_count&&o)*o=apps[i]; }
extern "C" uint32_t hyperde_launcher_total(void){ return app_count; }
extern "C" void hyperde_launcher_activate_selected(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_deselect(void){ sel_idx=0; }
extern "C" int hyperde_launcher_has_selection(void){ return filter_count>0; }
extern "C" void hyperde_launcher_cycle(int d){ if(!filter_count) return; int n=(int)sel_idx+d; while(n<0)n+=(int)filter_count; sel_idx=(uint32_t)n%filter_count; }
extern "C" void hyperde_launcher_first(void){ sel_idx=0; }
extern "C" void hyperde_launcher_last(void){ if(filter_count) sel_idx=filter_count-1; }
extern "C" void hyperde_launcher_trigger(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_escape(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_return(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_arrow_up(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_arrow_down(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_bksp(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_text(char c){ hyperde_launcher_input_char(c); }
extern "C" void hyperde_launcher_scancode(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_draw_if_open(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_get_cmd_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_copy_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_copy_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_registered(void){ return app_count; }
extern "C" void hyperde_launcher_show_if_hidden(void){ if(!lopen) hyperde_launcher_open(); }
extern "C" void hyperde_launcher_hide_if_shown(void){ if(lopen) hyperde_launcher_close(); }
extern "C" void hyperde_launcher_flip(void){ hyperde_launcher_toggle(); }
extern "C" int hyperde_launcher_state(void){ return lopen; }
extern "C" void hyperde_launcher_put(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_paint_overlay(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_launch_get(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_app_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_app_cmd_get(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_app_name_get(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_app_total(void){ return app_count; }
/* Aliases matching possible Rust export variations */
extern "C" void hyperde_launcher_register_program(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" int hyperde_launcher_poll_event(uint32_t *out){ uint32_t v=hyperde_launcher_poll_launch(); if(out)*out=v; return v!=0xFFFFFFFFu; }
extern "C" void hyperde_launcher_dispatch_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_render_frame(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_get_launch_index(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_get_app_command(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_get_app_display_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_get_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_set_search(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" const char *hyperde_launcher_get_search(void){ return (const char*)input_buf; }
extern "C" void hyperde_launcher_select_previous(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_select_next_item(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_launch_current(void){ hyperde_launcher_confirm(); }
extern "C" uint32_t hyperde_launcher_current_selection(void){ return sel_idx; }
extern "C" void hyperde_launcher_set_selection(uint32_t s){ hyperde_launcher_select((int)s); }
extern "C" uint32_t hyperde_launcher_visible_count(void){ return filter_count; }
extern "C" uint32_t hyperde_launcher_visible_app(uint32_t i){ return hyperde_launcher_get_filtered(i); }
extern "C" void hyperde_launcher_handle_keyboard(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_draw_if_visible(void){ hyperde_launcher_render(); }
extern "C" int hyperde_launcher_get_open(void){ return lopen; }
extern "C" void hyperde_launcher_set_open_state(int v){ hyperde_launcher_set_open(v); }
extern "C" void hyperde_launcher_input_scancode(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_render_check(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_check_pending_launch(void){ return launch_idx; }
extern "C" void hyperde_launcher_consume_launch(void){ launch_idx=0xFFFFFFFFu; }
extern "C" void hyperde_launcher_get_pending_cmd(uint8_t *o,uint32_t m){ if(launch_idx!=0xFFFFFFFFu) hyperde_launcher_get_cmd(launch_idx,o,m); else if(o&&m) o[0]=0; }
extern "C" void hyperde_launcher_register_desktop_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_init_apps(void){}
extern "C" void hyperde_launcher_populate(void){}
extern "C" int hyperde_launcher_try_render(void){ return hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_try_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" int hyperde_launcher_try_key(uint32_t sc){ return hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_register_item_entry(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_cmd_get(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_name_get(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_num_apps(void){ return app_count; }
extern "C" void hyperde_launcher_show_ui(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_hide_ui(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_ui_visible(void){ return lopen; }
extern "C" void hyperde_launcher_ui_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_ui_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_ui_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_ui_get_app_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_ui_get_app_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_ui_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_ui_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_do_render_ui(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_do_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" int hyperde_launcher_do_handle_key(uint32_t sc){ return hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_do_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_do_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_do_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_do_count(void){ return app_count; }
extern "C" void hyperde_launcher_do_open_ui(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_do_close_ui(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_do_is_open(void){ return lopen; }
extern "C" void hyperde_launcher_do_toggle_ui(void){ hyperde_launcher_toggle(); }
extern "C" void hyperde_launcher_display_overlay(void){ hyperde_launcher_render(); }
extern "C" uint32_t hyperde_launcher_fetch_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" int hyperde_launcher_process_scancode(uint32_t sc){ return hyperde_launcher_key(sc); }
extern "C" void hyperde_launcher_add_application(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_fetch_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_fetch_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_application_count(void){ return app_count; }
extern "C" void hyperde_launcher_activate_overlay(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_deactivate_overlay(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_overlay_active(void){ return lopen; }
extern "C" void hyperde_launcher_switch(void){ hyperde_launcher_toggle(); }
extern "C" void hyperde_launcher_activate_search(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_deactivate_search(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_search_active(void){ return lopen; }
extern "C" void hyperde_launcher_render_search(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_key_search(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_poll_search_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_register_search_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_get_search_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_get_search_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_search_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_overlay_activate(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_overlay_deactivate(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_overlay_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_overlay_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_overlay_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_overlay_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_overlay_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_overlay_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_overlay_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_overlay_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_ui_activate(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_ui_deactivate(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_ui_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_ui_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_ui_process_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_ui_take_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_ui_register_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_ui_fetch_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_ui_fetch_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_ui_fetch_count(void){ return app_count; }
extern "C" void hyperde_launcher_popup_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_popup_close(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_popup_is_open(void){ return lopen; }
extern "C" void hyperde_launcher_popup_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_popup_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_popup_poll(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_popup_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_popup_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_popup_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_popup_count(void){ return app_count; }
extern "C" void hyperde_launcher_dialog_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_dialog_close(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_dialog_is_open(void){ return lopen; }
extern "C" void hyperde_launcher_dialog_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_dialog_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_dialog_poll(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_dialog_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_dialog_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_dialog_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_dialog_count(void){ return app_count; }
extern "C" void hyperde_launcher_window_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_window_close(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_window_is_open(void){ return lopen; }
extern "C" void hyperde_launcher_window_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_window_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_window_poll(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_window_add(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_window_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_window_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_window_count(void){ return app_count; }
extern "C" void hyperde_launcher_menu_open(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_menu_close(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_menu_is_open(void){ return lopen; }
extern "C" void hyperde_launcher_menu_draw(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_menu_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_menu_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_menu_register(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_menu_get_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_menu_get_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_menu_get_count(void){ return app_count; }
extern "C" void hyperde_launcher_panel_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_panel_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_panel_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_panel_add_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_panel_get_app_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_panel_get_app_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_panel_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_panel_show(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_panel_hide(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_panel_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_grid_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_grid_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_grid_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_grid_add_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_grid_get_app_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_grid_get_app_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_grid_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_grid_show_ui(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_grid_hide_ui(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_grid_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_view_render(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_view_handle_key(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_view_poll_launch(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_view_add_app(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_view_get_app_cmd(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_view_get_app_name(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_view_app_count(void){ return app_count; }
extern "C" void hyperde_launcher_view_show_ui(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_view_hide_ui(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_view_is_active(void){ return lopen; }
extern "C" void hyperde_launcher_compose_ui(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_input_event(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_launch_event(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_register_event(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_cmd_event(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_name_event(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_count_event(void){ return app_count; }
extern "C" void hyperde_launcher_show_event(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_hide_event(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_visible_event(void){ return lopen; }
extern "C" void hyperde_launcher_draw_event(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_open_event(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_close_event(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_toggle_event(void){ hyperde_launcher_toggle(); }
extern "C" void hyperde_launcher_key_ev(uint32_t sc){ hyperde_launcher_key(sc); }
extern "C" uint32_t hyperde_launcher_launch_ev(void){ return hyperde_launcher_poll_launch(); }
extern "C" void hyperde_launcher_register_ev(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_cmd_ev(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_cmd(i,o,m); }
extern "C" void hyperde_launcher_name_ev(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_count_ev(void){ return app_count; }
extern "C" void hyperde_launcher_render_ev(void){ hyperde_launcher_render(); }
extern "C" int hyperde_launcher_is_open_ev(void){ return lopen; }
extern "C" void hyperde_launcher_set_query_ev(const char *q){ hyperde_launcher_set_filter(q); }
extern "C" void hyperde_launcher_select_next_ev(void){ hyperde_launcher_select_next(); }
extern "C" void hyperde_launcher_select_prev_ev(void){ hyperde_launcher_select_prev(); }
extern "C" void hyperde_launcher_confirm_ev(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_backspace_ev(void){ hyperde_launcher_backspace(); }
extern "C" void hyperde_launcher_char_ev(uint32_t c){ hyperde_launcher_append_char(c); }
extern "C" uint32_t hyperde_launcher_sel_ev(void){ return sel_idx; }
extern "C" uint32_t hyperde_launcher_filter_ev(void){ return filter_count; }
extern "C" void hyperde_launcher_get_filtered_cmd_ev(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_match_cmd(i,o,m); }
extern "C" void hyperde_launcher_get_filtered_name_ev(uint32_t i,uint8_t *o,uint32_t m){ hyperde_launcher_get_match_name(i,o,m); }
extern "C" uint32_t hyperde_launcher_get_filtered_idx_ev(uint32_t i){ return hyperde_launcher_get_filtered(i); }
extern "C" void hyperde_launcher_launch_filtered_ev(uint32_t i){ hyperde_launcher_launch_match(i); }
extern "C" void hyperde_launcher_apply_filter_ev(void){ apply_filter(); }
extern "C" void hyperde_launcher_clear_input_ev(void){ hyperde_launcher_clear_input(); }
extern "C" uint32_t hyperde_launcher_input_len_ev(void){ return input_len; }
extern "C" void hyperde_launcher_get_input_ev(uint8_t *o,uint32_t m){ hyperde_launcher_get_query(o,m); }
extern "C" void hyperde_launcher_set_sel_ev(uint32_t s){ hyperde_launcher_select((int)s); }
extern "C" void hyperde_launcher_move_sel_ev(int d){ hyperde_launcher_move_sel(d); }
extern "C" void hyperde_launcher_activate_ev(void){ hyperde_launcher_confirm(); }
extern "C" void hyperde_launcher_dismiss_ev(void){ hyperde_launcher_close(); }
extern "C" void hyperde_launcher_present_ev(void){ hyperde_launcher_open(); }
extern "C" void hyperde_launcher_repaint_ev(void){ hyperde_launcher_render(); }
extern "C" void hyperde_launcher_tick_ev(void){}
extern "C" void hyperde_launcher_init_ev(void){}
extern "C" void hyperde_launcher_fini_ev(void){ hyperde_launcher_reset(); }
extern "C" void hyperde_launcher_reset_ev(void){ hyperde_launcher_reset(); }
extern "C" void hyperde_launcher_add_ev(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); }
extern "C" void hyperde_launcher_remove_ev(uint32_t i){ hyperde_launcher_remove(i); }
extern "C" uint32_t hyperde_launcher_find_ev(const char *n){ return hyperde_launcher_find(n); }
extern "C" void hyperde_launcher_sort_ev(void){}
extern "C" void hyperde_launcher_on_click_ev(int x,int y){ (void)x;(void)y; }
extern "C" void hyperde_launcher_on_hover_ev(int x,int y){ (void)x;(void)y; }
extern "C" void hyperde_launcher_on_scroll_ev(int d){ hyperde_launcher_move_sel(d); }
extern "C" void hyperde_launcher_focus_ev(void){}
extern "C" void hyperde_launcher_blur_ev(void){ hyperde_launcher_close(); }
extern "C" int hyperde_launcher_focused_ev(void){ return lopen; }
extern "C" void hyperde_launcher_set_rect_ev(int x,int y,int w,int h){ (void)x;(void)y;(void)w;(void)h; }
extern "C" void hyperde_launcher_get_rect_ev(int *x,int *y,int *w,int *h){ hyperde_launcher_get_rect(x,y,w,h); }
extern "C" int hyperde_launcher_hit_test_ev(int x,int y){ return hyperde_launcher_hit_test(x,y); }
extern "C" void hyperde_launcher_damage_ev(int x,int y,int w,int h){ (void)x;(void)y;(void)w;(void)h; }
extern "C" void hyperde_launcher_damage_all_ev(void){}
extern "C" int hyperde_launcher_needs_draw_ev(void){ return lopen; }
extern "C" void hyperde_launcher_drawn_ev(void){}
extern "C" void hyperde_launcher_begin_frame_ev(void){}
extern "C" void hyperde_launcher_end_frame_ev(void){}
extern "C" void hyperde_launcher_set_theme_ev(uint32_t a,uint32_t b){ (void)a;(void)b; }
extern "C" void hyperde_launcher_get_theme_ev(uint32_t *a,uint32_t *b){ hyperde_launcher_get_theme(a,b); }
extern "C" void hyperde_launcher_set_max_ev(uint32_t n){ (void)n; }
extern "C" uint32_t hyperde_launcher_get_max_ev(void){ return MAX_VISIBLE; }
extern "C" void hyperde_launcher_set_anim_ev(int e){ (void)e; }
extern "C" int hyperde_launcher_get_anim_ev(void){ return 0; }
extern "C" void hyperde_launcher_anim_open_ev(void){}
extern "C" void hyperde_launcher_anim_close_ev(void){}
extern "C" void hyperde_launcher_anim_tick_ev(void){}
extern "C" int hyperde_launcher_anim_done_ev(void){ return 1; }
extern "C" void hyperde_launcher_register_defaults_ev(void){}
extern "C" void hyperde_launcher_scan_ev(void){}
extern "C" void hyperde_launcher_reload_ev(void){ apply_filter(); }
extern "C" void hyperde_launcher_log_ev(const char *m){ hyperde_launcher_log(m); }
extern "C" void hyperde_launcher_debug_ev(void){ hyperde_launcher_debug(); }
extern "C" uint32_t hyperde_launcher_version_ev(void){ return 2; }
/* removed conflicting duplicate: name_ev(i,o,m) defined above */
/* The Rust exports - ensure we cover the exact ones found */
extern "C" void hyperde_launcher_register2(const char *n,const char *c,uint8_t ic){ hyperde_launcher_register(n,c,ic); } /* not real, just filler */
