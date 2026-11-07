/* HyperDE keybindings - C++ port of keybind.rs */
#include "hyperde_internal.hpp"
#include <stdint.h>
extern "C" void kprintf(const char *fmt, ...);

enum { MOD_CTRL=1, MOD_ALT=2, MOD_SHIFT=4, MOD_SUPER=8, MOD_PRIMARY=MOD_SUPER };
static uint32_t mod_held;

static uint32_t scancode_to_mod(uint32_t sc){
    switch(sc){ case 0x1D:return MOD_CTRL; case 0x2A:case 0x36:return MOD_SHIFT; case 0x38:return MOD_ALT; case 0x5B:case 0x5C:return MOD_SUPER; default:return 0; }
}
extern "C" uint32_t hyperde_scancode_to_key_pub(uint32_t sc){
    if(sc>=0x02&&sc<=0x0A) return '1'+(sc-0x02);
    if(sc==0x0B) return '0';
    switch(sc){
        case 0x10:return 'q'; case 0x11:return 'w'; case 0x12:return 'e'; case 0x13:return 'r';
        case 0x14:return 't'; case 0x15:return 'y'; case 0x16:return 'u'; case 0x17:return 'i';
        case 0x18:return 'o'; case 0x19:return 'p'; case 0x1E:return 'a'; case 0x1F:return 's';
        case 0x20:return 'd'; case 0x21:return 'f'; case 0x22:return 'g'; case 0x23:return 'h';
        case 0x24:return 'j'; case 0x25:return 'k'; case 0x26:return 'l'; case 0x2C:return 'z';
        case 0x2D:return 'x'; case 0x2E:return 'c'; case 0x2F:return 'v'; case 0x30:return 'b';
        case 0x31:return 'n'; case 0x32:return 'm'; case 0x39:return ' '; case 0x1C:return '\r';
        case 0x0E:return '\b'; default:return 0;
    }
}
struct Bind { uint32_t sc,key,mods; int action; };
static const Bind BINDINGS[] = {
    {0x1C,'\r',MOD_PRIMARY,3},{0x20,'d',MOD_PRIMARY,4},{0x10,'q',MOD_PRIMARY,5},
    {0x21,'f',MOD_PRIMARY,6},{0x24,'j',MOD_PRIMARY,7},{0x25,'k',MOD_PRIMARY,8},
    {0x23,'h',MOD_PRIMARY,27},{0x26,'l',MOD_PRIMARY,28},
    {0x02,'1',MOD_PRIMARY,9},{0x03,'2',MOD_PRIMARY,10},{0x04,'3',MOD_PRIMARY,11},
    {0x05,'4',MOD_PRIMARY,12},{0x06,'5',MOD_PRIMARY,13},{0x07,'6',MOD_PRIMARY,14},
    {0x08,'7',MOD_PRIMARY,15},{0x09,'8',MOD_PRIMARY,16},{0x0A,'9',MOD_PRIMARY,17},
    {0x02,'1',MOD_PRIMARY|MOD_SHIFT,18},{0x03,'2',MOD_PRIMARY|MOD_SHIFT,19},{0x04,'3',MOD_PRIMARY|MOD_SHIFT,20},
    {0x05,'4',MOD_PRIMARY|MOD_SHIFT,21},{0x06,'5',MOD_PRIMARY|MOD_SHIFT,22},{0x07,'6',MOD_PRIMARY|MOD_SHIFT,23},
    {0x08,'7',MOD_PRIMARY|MOD_SHIFT,24},{0x09,'8',MOD_PRIMARY|MOD_SHIFT,25},{0x0A,'9',MOD_PRIMARY|MOD_SHIFT,26},
    {0x57,0,MOD_PRIMARY,31},
};
extern "C" int hyperde_keypress(uint32_t scancode, uint32_t){
    uint32_t mb = scancode_to_mod(scancode);
    if(mb){ mod_held |= mb; return 0; }
    uint32_t key = hyperde_scancode_to_key_pub(scancode);
    for(const auto &b: BINDINGS){
        if((b.sc==scancode || (b.key && b.key==key)) && b.mods==mod_held){
            kprintf("HYPERDE KEYBIND: matched action %d (mods=0x%x key=0x%x)\n", b.action, mod_held, key);
            return b.action;
        }
    }
    return 0;
}
extern "C" void hyperde_keyrelease(uint32_t scancode){
    uint32_t mb=scancode_to_mod(scancode); if(mb) mod_held &= ~mb;
}
extern "C" int hyperde_keybind_mods_held(void){ return (int)mod_held; }
extern "C" void hyperde_keybind_reset_mods(void){ mod_held=0; }
extern "C" void hyperde_keybind_set_launcher_open(int){ }
extern "C" int hyperde_keybind_is_launcher_open(void){ return 0; }
extern "C" int hyperde_action_spawn_terminal(void){ return 3; }
extern "C" int hyperde_action_spawn_launcher(void){ return 4; }
extern "C" int hyperde_action_close_window(void){ return 5; }
extern "C" int hyperde_action_toggle_floating(void){ return 6; }
extern "C" int hyperde_action_focus_next(void){ return 7; }
extern "C" int hyperde_action_focus_prev(void){ return 8; }
extern "C" int hyperde_action_workspace_switch(uint32_t n){ return 9+(int)n; }
extern "C" int hyperde_action_workspace_move(uint32_t n){ return 18+(int)n; }
extern "C" int hyperde_action_resize_left(void){ return 27; }
extern "C" int hyperde_action_resize_right(void){ return 28; }
extern "C" int hyperde_action_toggle_fullscreen(void){ return 31; }
