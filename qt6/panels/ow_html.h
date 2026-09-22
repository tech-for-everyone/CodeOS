#ifndef OW_HTML_H
#define OW_HTML_H

#include <stdint.h>
#include "openweb.h"

#define OW_TXT_LINES 512
#define OW_TXT_COLS  120
#define OW_MAX_LINKS 128
#define OW_MAX_IMAGES 32

#define OW_MAX_FORMS  8
#define OW_MAX_FIELDS 40
#define OW_MAX_SELECT_OPTS 16
#define OW_SELECT_OPT_SZ   56

enum { OW_FT_TEXT = 0, OW_FT_PASSWORD, OW_FT_SUBMIT, OW_FT_BUTTON,
       OW_FT_CHECKBOX, OW_FT_RADIO, OW_FT_TEXTAREA, OW_FT_SELECT };
enum { OW_FM_GET = 0, OW_FM_POST };

typedef struct { int line, sc, ec; char url[OW_URL_MAX]; } ow_link_t;
typedef struct { char url[OW_URL_MAX]; int x, y, w, h; int loaded; void *pixmap; } ow_image_t;

typedef struct {
    char action[OW_URL_MAX];
    uint8_t method;          /* OW_FM_GET / OW_FM_POST */
    uint8_t field_start;     /* index into ow_form_fields */
    uint8_t field_count;
} ow_form_t;

typedef struct {
    uint8_t type;            /* OW_FT_* */
    int form;                /* enclosing form index, or -1 */
    char name[OW_URL_MAX];
    char value[OW_URL_MAX];  /* current value (text) or fixed value (buttons) */
    int line, col, width;    /* rendered box position/size in the text grid */
    uint8_t checked;
    uint8_t opt_cnt, opt_sel;
    char opts[OW_MAX_SELECT_OPTS][OW_SELECT_OPT_SZ];
} ow_form_field_t;

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
#define OW_LT_IMAGE   16

typedef struct {
    uint8_t type;
} ow_line_info_t;

extern char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
extern int  ow_txt_lines;
extern ow_link_t ow_links[OW_MAX_LINKS];
extern int  ow_link_cnt;
extern ow_image_t ow_images[OW_MAX_IMAGES];
extern int  ow_image_cnt;
extern int  ow_need_render;
extern ow_line_info_t ow_line_info[OW_TXT_LINES];
/* Image index attached to OW_LT_IMAGE lines (or -1 otherwise). */
extern int  ow_line_img[OW_TXT_LINES];
/* <title> text extracted while rendering ("" if none). */
extern char ow_page_title[OW_URL_MAX];

extern ow_form_t ow_forms[OW_MAX_FORMS];
extern int  ow_form_cnt;
extern ow_form_field_t ow_form_fields[OW_MAX_FIELDS];
extern int  ow_field_cnt;

/* Edit a rendered form control. Values survive re-renders (per-page cache). */
void ow_field_set_value(int i, const char *v);
void ow_field_toggle(int i);
/* Find a rendered control covering grid cell (line,col), or -1. */
int  ow_field_at(int line, int col);
/* Build "name=value&..." (URL-encoded, skips unchecked + buttons). */
int  ow_form_build_query(const ow_form_t *f, char *out, int out_max);

/* HTML rendering is implemented in Rust (kernel/kernel/rust_ow/src/ow_render.rs)
 * and fills all of the globals above. */
void ow_render_rs(const char *html, int len);

/* Edit-cache bridge used by the Rust renderer around each laid-out control
 * (identity-checked restore/store of user edits; C-owned cache). */
void ow_fv_restore(ow_form_field_t *f, int fi);
void ow_fv_store(const ow_form_field_t *f, int fi);

/* Fetch an image over plain HTTP into buf (headers stripped). Returns bytes. */
int ow_image_download(const char *url, void *buf, int max_len);

/* ── Parallel image fetch API ── */
void ow_image_start_workers(void);
void ow_image_enqueue(int idx, const char *abs_url);
int  ow_image_state(int idx);              /* IMG_ST_* */
const unsigned char *ow_image_raw(int idx);
int  ow_image_raw_len(int idx);
void ow_image_reset_all(void);

#endif
