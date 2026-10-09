extern "C" {
#include "firewall.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
}

#define FW_RULES_MAX 32

static struct {
    char src_ip[64];
    char dst_ip[64];
    int action; /* 0=allow, 1=deny */
} firewall_rules[FW_RULES_MAX];
static int fw_rule_cnt = 0;

void firewall_init(void) {
    fw_rule_cnt = 0;
    memset(firewall_rules, 0, sizeof(firewall_rules));
}

void firewall_add_rule(const char *src, const char *dst, int action) {
    if (fw_rule_cnt < FW_RULES_MAX) {
        strncpy(firewall_rules[fw_rule_cnt].src_ip, src, 63);
        strncpy(firewall_rules[fw_rule_cnt].dst_ip, dst, 63);
        firewall_rules[fw_rule_cnt].action = action;
        fw_rule_cnt++;
    }
}

int firewall_check(const char *src, const char *dst) {
    for (int i = 0; i < fw_rule_cnt; i++) {
        if (strcmp(firewall_rules[i].src_ip, src) == 0 &&
            strcmp(firewall_rules[i].dst_ip, dst) == 0) {
            return firewall_rules[i].action;
        }
    }
    return 0; /* Default allow */
}

void firewall_draw(uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;

    /* Simple status display */
    fb_fillrect_gradient_v(0, 0, 200, 120,
                           0x30252525, 0x20151515);

    fb_drawstr_px(8, 20, "Firewall Status", C_BLUE, 0);
    fb_drawstr_px(8, 50, "Rules active: %d", fw_rule_cnt, C_TEXT, 0);
    fb_drawstr_px(8, 75, "Default: Allow", C_DIM, 0);

    int y = 100;
    /* Add rule button */
    fb_fill_rounded_rect(8, y, 96, 24, 6, 0x400066FF);
    fb_drawstr_px(10, y + 3, "Add Rule", C_WHITE, 0);

    /* Remove rule button */
    fb_fill_rounded_rect(112, y, 96, 24, 6, 0x300000AA);
    fb_drawstr_px(114, y + 3, "Remove", C_DIM, 0);
}

int firewall_click(int mx, int my) {
    (void)mx; (void)my;

    int y = 100;

    /* Add Rule button */
    if (mx >= 8 && mx < 104 && my >= y && my < y + 24) {
        return 1;
    }

    /* Remove Rule button */
    if (mx >= 112 && mx < 208 && my >= y && my < y + 24) {
        return 2;
    }

    return 0;
}

void firewall_key(int key) {
    (void)key;
}