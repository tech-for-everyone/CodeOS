#include "ow_html.h"
#include "string.h"
#include "kprintf.h"

char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
int  ow_txt_lines;
ow_link_t ow_links[OW_MAX_LINKS];
int  ow_link_cnt;
int  ow_need_render;
ow_line_info_t ow_line_info[OW_TXT_LINES];

/* ─── helpers ─── */

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static void line_set_type(int ln, uint8_t t) {
    if (ln >= 0 && ln < OW_TXT_LINES) ow_line_info[ln].type = t;
}

static void flush_buf(char *buf, int *col) {
    if (*col == 0) return;
    buf[*col] = 0;
    int ln = ow_txt_lines;
    if (ln >= OW_TXT_LINES) return;
    int j;
    for (j = 0; buf[j] && j < OW_TXT_COLS - 1; j++)
        ow_txt[ln][j] = buf[j];
    ow_txt[ln][j] = 0;
    if (ow_line_info[ln].type == OW_LT_NORMAL)
        line_set_type(ln, OW_LT_NORMAL);
    ow_txt_lines = ln + 1;
    *col = 0;
}

static void add_blank_line(void) {
    if (ow_txt_lines > 0 && ow_txt[ow_txt_lines - 1][0] != 0) {
        if (ow_txt_lines < OW_TXT_LINES) {
            ow_txt[ow_txt_lines][0] = 0;
            line_set_type(ow_txt_lines, OW_LT_EMPTY);
            ow_txt_lines++;
        }
    }
}

static int tag_match(const char *html, int i, int len, const char *tag) {
    /* check if at position i we have <tag or </tag */
    if (!html || i < 0 || i >= len || html[i] != '<') return 0;
    int is_close = (i + 1 < len && html[i+1] == '/');
    const char *t = tag;
    int pos = is_close ? i + 2 : i + 1;
    while (*t && pos < len && lower_ascii(html[pos]) == lower_ascii(*t)) { t++; pos++; }
    if (*t) return 0;
    /* tag must be followed by >, space, /, or end */
    if (pos >= len) return 0;
    char n = html[pos];
    return (n == '>' || n == ' ' || n == '\t' || n == '/') ? (is_close ? 2 : 1) : 0;
}

static int tag_match_exact(const char *html, int i, int len, const char *tag) {
    if (!html || i < 0 || i >= len || html[i] != '<') return 0;
    int is_close = (i + 1 < len && html[i+1] == '/');
    const char *t = tag;
    int pos = is_close ? i + 2 : i + 1;
    while (*t && pos < len && lower_ascii(html[pos]) == lower_ascii(*t)) { t++; pos++; }
    if (*t) return 0;
    if (pos < len && html[pos] == '>') return is_close ? 2 : 1;
    return 0;
}

/* ─── main renderer ─── */

void render_html(const char *html, int len) {
    int ti;
    for (ti = 0; ti < OW_TXT_LINES; ti++) {
        ow_txt[ti][0] = 0;
        ow_line_info[ti].type = OW_LT_NORMAL;
    }
    ow_txt_lines = 0;
    ow_link_cnt = 0;

    char buf[OW_TXT_COLS];
    int col = 0;
    int in_a = 0, cur_link = -1;
    int h_level = 0;
    int list_type = 0, ol_count = 0;
    int in_pre = 0;

    int i = 0;
    while (i < len && ow_txt_lines < OW_TXT_LINES) {
        if (html[i] == '<') {
            /* ── Heading tags ── */
            if (tag_match(html, i, len, "h1") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 1; goto skip_tag;
            }
            if (tag_match(html, i, len, "h2") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 2; goto skip_tag;
            }
            if (tag_match(html, i, len, "h3") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 3; goto skip_tag;
            }
            if (tag_match(html, i, len, "h4") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 4; goto skip_tag;
            }
            if (tag_match(html, i, len, "h5") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 5; goto skip_tag;
            }
            if (tag_match(html, i, len, "h6") == 1) {
                flush_buf(buf, &col); add_blank_line();
                h_level = 6; goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/h1") || tag_match_exact(html, i, len, "/h2") ||
                tag_match_exact(html, i, len, "/h3") || tag_match_exact(html, i, len, "/h4") ||
                tag_match_exact(html, i, len, "/h5") || tag_match_exact(html, i, len, "/h6")) {
                flush_buf(buf, &col);
                if (ow_txt_lines > 0) line_set_type(ow_txt_lines - 1, OW_LT_H1 - 1 + h_level);
                add_blank_line();
                h_level = 0; i += 5; continue;
            }

            /* ── Paragraph ── */
            if (tag_match(html, i, len, "p") == 1) {
                flush_buf(buf, &col); add_blank_line();
                goto skip_tag;
            }
            if (tag_match(html, i, len, "/p") == 2) {
                flush_buf(buf, &col); add_blank_line();
                i += 4; continue;
            }

            /* ── Line break ── */
            if (tag_match(html, i, len, "br")) {
                flush_buf(buf, &col);
                while (i < len && html[i] != '>') i++;
                if (i < len) i++;
                continue;
            }

            /* ── Lists ── */
            if (tag_match(html, i, len, "ul") == 1) {
                list_type = 1; goto skip_tag;
            }
            if (tag_match(html, i, len, "ol") == 1) {
                list_type = 2; ol_count = 0; goto skip_tag;
            }
            if (tag_match(html, i, len, "/ul") == 2 || tag_match(html, i, len, "/ol") == 2) {
                flush_buf(buf, &col); add_blank_line();
                list_type = 0; i += 5; continue;
            }
            if (tag_match(html, i, len, "li") == 1) {
                flush_buf(buf, &col);
                if (list_type == 2) ol_count++;
                if (list_type == 1) {
                    if (col < OW_TXT_COLS - 2) { buf[col++] = 0xE2; buf[col++] = 0x80; buf[col++] = 0xA2; }
                    if (col < OW_TXT_COLS - 1) buf[col++] = ' ';
                } else if (list_type == 2) {
                    char num[8]; int ni = 0, n = ol_count;
                    while (n) { num[ni++] = '0' + n % 10; n /= 10; }
                    for (int k = ni - 1; k >= 0; k--) if (col < OW_TXT_COLS - 1) buf[col++] = num[k];
                    if (col < OW_TXT_COLS - 2) { buf[col++] = '.'; buf[col++] = ' '; }
                } else {
                    if (col < OW_TXT_COLS - 2) { buf[col++] = '*'; buf[col++] = ' '; }
                }
                goto skip_tag;
            }
            if (tag_match(html, i, len, "/li") == 2) {
                flush_buf(buf, &col);
                if (ow_txt_lines > 0) line_set_type(ow_txt_lines - 1, OW_LT_LI);
                i += 5; continue;
            }

            /* ── Horizontal rule ── */
            if (tag_match(html, i, len, "hr")) {
                flush_buf(buf, &col);
                int ln = ow_txt_lines;
                if (ln < OW_TXT_LINES) {
                    int k;
                    for (k = 0; k < OW_TXT_COLS - 1; k++) ow_txt[ln][k] = '=';
                    ow_txt[ln][k] = 0;
                    line_set_type(ln, OW_LT_HR);
                    ow_txt_lines = ln + 1;
                }
                while (i < len && html[i] != '>') i++;
                if (i < len) i++;
                continue;
            }

                /* ── Blockquote ── */
            if (tag_match_exact(html, i, len, "blockquote") == 1) {
                flush_buf(buf, &col); add_blank_line();
                int ln = ow_txt_lines;
                if (ln < OW_TXT_LINES) {
                    ow_txt[ln][0] = '>';
                    ow_txt[ln][1] = ' ';
                    ow_txt[ln][2] = 0;
                    for (int li = 0; li < OW_TXT_COLS - 1; li++) ow_txt[ln][li] = ' ';
                    ow_txt[ln][OW_TXT_COLS-1] = 0;
                    line_set_type(ln, OW_LT_BQ);
                    ow_txt_lines = ln + 1;
                }
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/blockquote") == 2) {
                flush_buf(buf, &col); add_blank_line();
                i += 13; continue;
            }

            /* ── Code block (<pre> with <code>) ── */
            if (tag_match(html, i, len, "code") == 1 && !in_pre) {
                int col_before = col;
                (void)col_before;
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "code") == 2 && !in_pre) {
                goto skip_tag;
            }

            /* ── Text formatting ── */
            if (tag_match(html, i, len, "b") == 1 || tag_match(html, i, len, "strong") == 1) {
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "b") == 2 || tag_match_exact(html, i, len, "strong") == 2) {
                if (col < OW_TXT_COLS - 2) { buf[col++] = 0xC2; buf[col++] = 0xB7; }
                goto skip_tag;
            }
            if (tag_match(html, i, len, "i") == 1 || tag_match(html, i, len, "em") == 1) {
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "i") == 2 || tag_match_exact(html, i, len, "em") == 2) {
                if (col < OW_TXT_COLS - 3) { buf[col++] = 0xE2; buf[col++] = 0x81; buf[col++] = 0x84; }
                goto skip_tag;
            }

            /* ── Table tags ── */
            if (tag_match(html, i, len, "table") == 1) {
                flush_buf(buf, &col); add_blank_line();
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/table") == 2) {
                flush_buf(buf, &col); add_blank_line();
                i += 7; continue;
            }
            if (tag_match(html, i, len, "tr") == 1) {
                flush_buf(buf, &col);
                add_blank_line();
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/tr") == 2) {
                flush_buf(buf, &col);
                i += 5; continue;
            }
            if (tag_match(html, i, len, "th") == 1) {
                flush_buf(buf, &col);
                int ln = ow_txt_lines;
                if (ln < OW_TXT_LINES) {
                    ow_txt[ln][0] = 0;
                    line_set_type(ln, OW_LT_TH);
                }
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/th") == 2) {
                flush_buf(buf, &col);
                if (ow_txt_lines > 0) line_set_type(ow_txt_lines - 1, OW_LT_TH);
                i += 5; continue;
            }
            if (tag_match(html, i, len, "td") == 1) {
                flush_buf(buf, &col);
                goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/td") == 2) {
                flush_buf(buf, &col);
                i += 5; continue;
            }

        /* ── Anchor tags ── */
            if (!in_a && tag_match(html, i, len, "a") == 1) {
                if (ow_link_cnt < OW_MAX_LINKS) {
                    cur_link = ow_link_cnt;
                    const char *h = html + i + 2;
                    while (*h && *h != '>') {
                        if ((*h == 'h'||*h=='H') && (*(h+1)=='r'||*(h+1)=='R') &&
                            (*(h+2)=='e'||*(h+2)=='E') && (*(h+3)=='f'||*(h+3)=='F')) {
                            h += 5;
                            if (*h == '"' || *h == '\'') h++;
                            int ui = 0;
                            while (*h && *h != '"' && *h != '\'' && *h != '>' && *h != ' ' && ui < OW_URL_MAX-1)
                                ow_links[cur_link].url[ui++] = *h++;
                            ow_links[cur_link].url[ui] = 0;
                            ow_links[cur_link].line = -1;
                            ow_link_cnt++;
                            in_a = 1;
                            break;
                        }
                        h++;
                    }
                }
                while (i < len && html[i] != '>') i++;
                if (i < len) i++;
                continue;
            }
            if (in_a && tag_match(html, i, len, "a") == 2) {
                in_a = 0; cur_link = -1;
                while (i < len && html[i] != '>') i++;
                if (i < len) i++;
                continue;
            }

            /* ── Preformatted text ── */
            if (tag_match(html, i, len, "pre") == 1) {
                in_pre = 1; goto skip_tag;
            }
            if (tag_match_exact(html, i, len, "/pre")) {
                flush_buf(buf, &col); add_blank_line();
                in_pre = 0; i += 6; continue;
            }

            /* ── Skip <script>..</script> and <style>..</style> entirely ── */
            {
                int skiptag = 0;
                const char *skip_tags[] = {"script", "style", "noscript"};
                for (int st = 0; st < 3; st++) {
                    if (tag_match(html, i, len, skip_tags[st]) == 1) {
                        skiptag = 1;
                        int clen = strlen(skip_tags[st]);
                        const char *close_tag = skip_tags[st];
                        while (i < len && html[i] != '>') i++;
                        if (i < len) i++;
                        while (i < len) {
                            if (html[i] == '<' && html[i+1] == '/' &&
                                strncmp(html + i + 2, close_tag, clen) == 0) {
                                while (i < len && html[i] != '>') i++;
                                if (i < len) i++;
                                break;
                            }
                            i++;
                        }
                        break;
                    }
                }
                if (skiptag) continue;
            }

            /* skip all other tags */
skip_tag:
            while (i < len && html[i] != '>') i++;
            if (i < len) i++;
            continue;
        }

        /* ── HTML entities ── */
        if (html[i] == '&') {
            if (strncmp(html+i,"&amp;",5)==0)  { if(col<OW_TXT_COLS-1) buf[col++]='&'; i+=5; continue; }
            if (strncmp(html+i,"&lt;",4)==0)   { if(col<OW_TXT_COLS-1) buf[col++]='<'; i+=4; continue; }
            if (strncmp(html+i,"&gt;",4)==0)   { if(col<OW_TXT_COLS-1) buf[col++]='>'; i+=4; continue; }
            if (strncmp(html+i,"&nbsp;",6)==0) { if(col<OW_TXT_COLS-1) buf[col++]=' '; i+=6; continue; }
            if (strncmp(html+i,"&quot;",6)==0) { if(col<OW_TXT_COLS-1) buf[col++]='"'; i+=6; continue; }
            if (strncmp(html+i,"&#x27;",6)==0) { if(col<OW_TXT_COLS-1) buf[col++]='\''; i+=6; continue; }
            if (strncmp(html+i,"&#39;",5)==0)  { if(col<OW_TXT_COLS-1) buf[col++]='\''; i+=5; continue; }
            if (strncmp(html+i,"&#x2F;",6)==0) { if(col<OW_TXT_COLS-1) buf[col++]='/'; i+=6; continue; }
            /* unrecognized entity — just pass through */
        }

        /* ── Whitespace handling in pre mode ── */
        if (in_pre) {
            if (html[i] == '\n') {
                flush_buf(buf, &col);
                i++;
                continue;
            }
            if (html[i] >= ' ') {
                if (col < OW_TXT_COLS - 1) buf[col++] = html[i];
                i++;
                continue;
            }
            i++;
            continue;
        }

        if (html[i] == '\r') { i++; continue; }
        if (html[i] == '\n' || html[i] == '\t') {
            if (html[i] == '\t') { if(col<OW_TXT_COLS-1) buf[col++]=' '; if(col<OW_TXT_COLS-1) buf[col++]=' '; i++; continue; }
            int para = (i > 0 && html[i-1] == '\n');
            if (col > 0 || para) {
                flush_buf(buf, &col);
            }
            i++; continue;
        }

        if (html[i] >= ' ') {
            if (col >= OW_TXT_COLS - 1) {
                int brk = -1;
                for (int k = col-1; k >= 0; k--) { if (buf[k] == ' ') { brk = k; break; } }
                if (brk > 0) {
                    flush_buf(buf, &brk);
                    int ni = 0;
                    for (int k = brk + 1; k < col; k++) buf[ni++] = buf[k];
                    col = ni;
                } else {
                    flush_buf(buf, &col);
                    col = 0;
                }
            }
            if (in_a && cur_link >= 0 && cur_link < OW_MAX_LINKS) {
                if (ow_links[cur_link].line < 0) { ow_links[cur_link].line = ow_txt_lines; ow_links[cur_link].sc = col; }
            }
            buf[col++] = html[i];
            if (in_a && cur_link >= 0 && cur_link < OW_MAX_LINKS) {
                ow_links[cur_link].ec = col;
            }
        }
        i++;
    }
    if (col > 0) {
        flush_buf(buf, &col);
    }
    ow_need_render = 0;
}
