/* HyperDE config - C++ port. Provides C ABI exports from config.rs.
 * Full mlua/Lua execution is not ported; we do simple parsing of key values
 * from the embedded conf.lua and keep sensible defaults. */
#include "hyperde_internal.hpp"
#include <stddef.h>
#include <stdint.h>

extern "C" void kprintf(const char *fmt, ...);

static uint32_t g_backend = 0; /* 0=x11,1=wayland */
static uint32_t g_workspace_count = 9;
static uint8_t g_workspace_names[9][16];
static uint32_t g_accent = 0x0040D9F0;
static uint32_t g_focused_border = 0x0040D9F0;
static uint32_t g_normal_border = 0x003c3836;
static uint32_t g_border_width = 2;
static uint32_t g_panel_height = 28;
static int g_blur = 1, g_shadow = 1;
static uint32_t g_anim_dur = 200;
static int g_inited;

static void init_defaults(void){
    if(g_inited) return; g_inited=1;
    for(int i=0;i<9;i++){ g_workspace_names[i][0]='1'+i; g_workspace_names[i][1]=0; }
}
static int memeq(const uint8_t *a,const char *b,size_t n){ for(size_t i=0;i<n;i++) if(a[i]!=(uint8_t)b[i]) return 0; return 1; }
static uint32_t parse_hex(const uint8_t *s, size_t len){
    if(len<7||s[0]!='#') return 0;
    uint32_t v=0; for(size_t i=1;i<len && i<9;i++){ uint8_t c=s[i],h;
        if(c>='0'&&c<='9') h=c-'0'; else if(c>='a'&&c<='f') h=c-'a'+10; else if(c>='A'&&c<='F') h=c-'A'+10; else break;
        v=(v<<4)|h;
    }
    return v;
}

extern "C" int hyperde_config_backend(void){ init_defaults(); return (int)g_backend; }

extern "C" void hyperde_config_load(const uint8_t *data, size_t len){
    init_defaults();
    if(!data||!len) return;
    /* simple scan: backend = "x11"/"wayland", active_color = "#...", panel_height = N */
    for(size_t i=0;i+12<len;i++){
        if(memeq(data+i,"backend",7)){
            /* look for quoted value */
            for(size_t j=i;j<i+30 && j<len;j++) if(data[j]=='"'||data[j]=='\''){
                if(j+8<len && memeq(data+j+1,"wayland",7)) g_backend=1;
                else g_backend=0;
                break;
            }
        }
        if(memeq(data+i,"active_color",12)){
            for(size_t j=i;j<i+30 && j<len;j++) if(data[j]=='#'){ uint32_t c=parse_hex(data+j,7); if(c){ g_accent=c; g_focused_border=c; } break; }
        }
        if(memeq(data+i,"inactive_color",14)){
            for(size_t j=i;j<i+30 && j<len;j++) if(data[j]=='#'){ uint32_t c=parse_hex(data+j,7); if(c) g_normal_border=c; break; }
        }
        if(memeq(data+i,"panel_height",12)){
            for(size_t j=i;j<i+20 && j<len;j++) if(data[j]>='0'&&data[j]<='9'){ uint32_t v=0; while(j<len&&data[j]>='0'&&data[j]<='9'){v=v*10+(data[j]-'0');j++;} if(v>=16&&v<=64) g_panel_height=v; break; }
        }
        if(memeq(data+i,"border_width",12)){
            for(size_t j=i;j<i+20 && j<len;j++) if(data[j]>='0'&&data[j]<='9'){ uint32_t v=0; while(j<len&&data[j]>='0'&&data[j]<='9'){v=v*10+(data[j]-'0');j++;} g_border_width=v; break; }
        }
    }
    kprintf("HYPERDE CONFIG: loaded (C++), backend=%s accent=0x%x panel=%u\n", g_backend?"wayland":"x11", g_accent, g_panel_height);
}

extern "C" uint32_t hyperde_config_workspace_count(void){ init_defaults(); return g_workspace_count; }
extern "C" void hyperde_config_workspace_name(uint32_t idx, uint8_t *out, uint32_t max_len){
    init_defaults(); if(!out||!max_len||idx>=g_workspace_count) return;
    uint32_t j=0; while(j<15 && j+1<max_len && g_workspace_names[idx][j]){ out[j]=g_workspace_names[idx][j]; j++; } out[j]=0;
}
extern "C" uint32_t hyperde_config_accent(void){ init_defaults(); return g_accent; }
extern "C" uint32_t hyperde_config_border_width(void){ init_defaults(); return g_border_width; }
extern "C" uint32_t hyperde_config_panel_height(void){ init_defaults(); return g_panel_height; }
extern "C" int hyperde_config_blur_enabled(void){ init_defaults(); return g_blur; }
extern "C" int hyperde_config_shadow_enabled(void){ init_defaults(); return g_shadow; }
extern "C" uint32_t hyperde_config_animation_duration(void){ init_defaults(); return g_anim_dur; }
extern "C" uint32_t hyperde_config_focused_border(void){ init_defaults(); return g_focused_border; }
extern "C" uint32_t hyperde_config_normal_border(void){ init_defaults(); return g_normal_border; }
extern "C" void hyperde_config_parse_file(const char *path){ (void)path; init_defaults(); }
extern "C" const uint8_t *hyperde_config_terminal_cmd(void){ static const uint8_t c[]="xterm"; return c; }
extern "C" const uint8_t *hyperde_config_launcher_cmd(void){ static const uint8_t c[]=""; return c; }
extern "C" int hyperde_config_is_floating_class(const char *cls){ (void)cls; return 0; }
extern "C" int hyperde_config_focus_follow_mouse(void){ return 1; }
extern "C" uint32_t hyperde_config_blur_radius(void){ return 8; }
extern "C" uint32_t hyperde_config_shadow_blur(void){ return 4; }
extern "C" uint32_t hyperde_config_glass_alpha(void){ return 0xC8; }
extern "C" int hyperde_config_loaded(void){ return g_inited; }
