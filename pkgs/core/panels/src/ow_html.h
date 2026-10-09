#ifndef OW_HTML_H
#define OW_HTML_H

#include <stdint.h>
#include "openweb.h"

#define OW_TXT_LINES 512
#define OW_TXT_COLS  120
#define OW_MAX_LINKS 128

typedef struct { int line, sc, ec; char url[OW_URL_MAX]; } ow_link_t;

/* Line type for visual rendering */
#define OW_LT_NORMAL  0
#define OW_LT_H1      1
#define OW_LT_H2      2
#define OW_LT_H3      3
#define OW_LT_H4      4
#define OW_LT_H5      5
#define OW_LT_H6      6
#define OW_LT_HR      7
#define OW_LT_LI      8
#define OW_LT_EMPTY   9
#define OW_LT_TH      10
#define OW_LT_TD      11
#define OW_LT_BQ      12
#define OW_LT_BOLD    13
#define OW_LT_ITALIC  14
#define OW_LT_CODE    15

typedef struct {
    uint8_t type;
} ow_line_info_t;

extern char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
extern int  ow_txt_lines;
extern ow_link_t ow_links[OW_MAX_LINKS];
extern int  ow_link_cnt;
extern int  ow_need_render;
extern ow_line_info_t ow_line_info[OW_TXT_LINES];

void render_html(const char *html, int len);

#endif
