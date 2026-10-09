#include "qt_panels.h"
#include "codeos_plugin.h"
#include "codeos_platform.h"
#include "codeos_window_manager.h"
#include "codeos_file_manager.h"
#include "codeos_terminal.h"

#include <private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformtheme.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QStyleFactory>
#include <QPalette>
#include <QProxyStyle>
#include <QSplitter>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QPlainTextEdit>
#include <QByteArray>

extern "C" {
#include "mouse.h"
#include "keyboard.h"
#include "input.h"
#include "timer.h"
#include "kprintf.h"
#include "fb.h"
#include "version.h"
#include "mm.h"
#include "pmm.h"
#include "process.h"
#include "security.h"
#include "pkg.h"
#include "fs.h"
#include "ai.h"
#include "net.h"
#include "openweb_core.h"
#include "ow_html.h"
#include "apphost.h"
#include "block.h"
#include "io.h"
#include "updater.h"
#include "lvgl_launcher.h"
#include "wifi.h"
#include <vector>
}

extern "C" {
int sched_thread_count(void);
void installer_run(void);
void installer_detect_disk(void);
void installer_set_user(const char *full_name, const char *username, const char *password);
void installer_set_wifi(const char *ssid, const char *password);
void installer_set_locale(const char *locale);
}

#include "installer_qt.h"
/* ═══════════════════════════════════════════════════════════════════
   QtOpenWebWidget
   ═══════════════════════════════════════════════════════════════════ */

QtOpenWebWidget::QtOpenWebWidget(QWidget *parent) : QtAppWindow("OpenWeb", parent) {
    resize(1024, 768);
    m_status = "Ready";
    m_initialized = false;
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_quickNames << "Demo Server" << "OpenWeb Docs" << "Renderer Tests"
                 << "First Website" << "Never SSL" << "Plain HTTP"
                 << "Example.com" << "Example.net";
    m_quickUrls << "http://10.0.2.2:8095/" << "http://10.0.2.2:8095/docs/"
                << "http://10.0.2.2:8095/test/" << "http://info.cern.ch/"
                << "http://neverssl.com/" << "http://httpforever.com/"
                << "http://example.com/" << "http://example.net/";

    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible()) return;
        bool live = false;
        if (m_initialized) {
            int cnt = ow_core_tab_count();
            int idx = ow_core_active_tab();
            if (idx >= 0 && idx < cnt) {
                openweb_tab_t *tabs = ow_core_tabs();
                if (tabs && tabs[idx].loading) live = true;
            }
        }
        if (live || m_initialized) update();
    });
    m_refreshTimer.start(150);
}

void QtOpenWebWidget::showEvent(QShowEvent *event) {
    QtAppWindow::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        setFocus(Qt::OtherFocusReason);
    });
}

static QChar ow_shifted_char(int key) {
    static const struct { int key; QChar ch; } kShifted[] = {
        { '1', '!' }, { '2', '@' }, { '3', '#' }, { '4', '$' },
        { '5', '%' }, { '6', '^' }, { '7', '&' }, { '8', '*' },
        { '9', '(' }, { '0', ')' }, { '-', '_' }, { '=', '+' },
        { '[', '{' }, { ']', '}' }, { '\\', '|' }, { ';', ':' },
        { '\'', '"' }, { ',', '<' }, { '.', '>' }, { '/', '?' },
        { '`', '~' }
    };
    for (const auto &m : kShifted) {
        if (m.key == key) return m.ch;
    }
    QChar c = QChar(int(key));
    return c.toUpper();
}

bool QtOpenWebWidget::isStartPage() const {
    if (!m_initialized) return true;
    openweb_tab_t *tabs = ow_core_tabs();
    int cnt = ow_core_tab_count();
    int idx = ow_core_active_tab();
    if (!tabs || idx < 0 || idx >= cnt) return true;
    openweb_tab_t *t = &tabs[idx];
    QString u = QString::fromUtf8(t->url);
    if (u.startsWith("about:")) return true;
    if (t->error && t->content_len == 0) return false; /* error screen */
    if (t->content_len == 0) return true;              /* nothing loaded yet */
    return false;
}

QString QtOpenWebWidget::currentTabUrl() const {
    if (!m_initialized) return QString();
    openweb_tab_t *tabs = ow_core_tabs();
    int idx = ow_core_active_tab();
    if (!tabs || idx < 0 || idx >= ow_core_tab_count()) return QString();
    return QString::fromUtf8(tabs[idx].url);
}

QString QtOpenWebWidget::activeTitle() const {
    int idx = ow_core_active_tab();
    if (!m_initialized || idx < 0 || idx >= m_tabTitles.size()) return "New Tab";
    QString t = m_tabTitles[idx];
    if (t.isEmpty()) {
        QString u = currentTabUrl();
        if (u.isEmpty() || u.startsWith("about:")) return "New Tab";
        QString d = u.section('/', 2).section('/', 0);
        if (d.isEmpty()) return u;
        return d;
    }
    return t;
}

QString QtOpenWebWidget::resolveLink(const QString &rel) const {
    if (rel.isEmpty()) return rel;
    if (rel.startsWith("http://") || rel.startsWith("https://")) return rel;
    if (rel.startsWith("//")) return "http:" + rel;
    QString base = currentTabUrl();
    int sep = base.indexOf("://");
    if (sep < 0) return rel;
    int authEnd = base.indexOf('/', sep + 3);
    if (authEnd < 0) authEnd = base.length();
    QString schemeAuth = base.left(authEnd);
    if (rel.startsWith('/')) return schemeAuth + rel;
    /* same-directory resolution */
    QString dir = base.left(authEnd + 1);
    int lastSlash = base.lastIndexOf('/', authEnd - 1);
    if (lastSlash > sep + 3) dir = base.left(lastSlash + 1);
    return dir + rel;
}

void QtOpenWebWidget::navigateAbsolute(const QString &url) {
    kprintf("NAV abs url='%s'\n", url.toUtf8().constData());
    if (!m_initialized) {
        ow_core_init();
        m_initialized = ow_core_is_initialized();
    }
    QByteArray ba = url.toUtf8();
    kprintf("NAV calling ow_core_navigate\n");
    ow_core_navigate(ba.constData());
    kprintf("NAV returned from ow_core_navigate\n");
    m_urlInput = url;
    m_cursorPos = m_urlInput.length();
    m_scrollY = 0;
    m_status = "Loading " + url + "...";
    update();
}

void QtOpenWebWidget::navigateTo(const QString &url) {
    kprintf("NAVto enter len=%d\n", (int)url.length());
    QString clean = url.trimmed();
    kprintf("NAVto trimmed len=%d\n", (int)clean.length());
    if (clean.isEmpty()) { kprintf("NAVto empty->return\n"); return; }
    if (clean.startsWith("about:")) {
        if (!m_initialized) {
            ow_core_init();
            m_initialized = ow_core_is_initialized();
        }
        ow_core_navigate(clean.toUtf8().constData());
        m_urlInput = clean;
        m_cursorPos = m_urlInput.length();
        m_scrollY = 0;
        m_status = "Start page";
        update();
        return;
    }

    int idx = ow_core_active_tab();
    kprintf("NAVto idx=%d init=%d\n", idx, (int)m_initialized);
    if (!m_initialized) {
        ow_core_init();
        m_initialized = ow_core_is_initialized();
        idx = ow_core_active_tab();
        kprintf("NAVto re-idx=%d\n", idx);
    }
    if (idx < 0) idx = 0;
    while (idx >= m_hist.size()) m_hist.append(QStringList());
    while (idx >= m_histPos.size())  m_histPos.append(-1);
    while (idx >= m_tabTitles.size()) m_tabTitles.append(QString());
    kprintf("NAVto arrays ok h=%d\n", (int)m_hist.size());
    QStringList &h = m_hist[idx];
    int &pos = m_histPos[idx];
    if (pos >= 0 && pos < h.size() && h[pos] == clean) {
        /* same page — just reload-ish */
    } else {
        while (pos >= 0 && pos + 1 < h.size()) h.removeAt(h.size() - 1);
        h.append(clean);
        pos = h.size() - 1;
        if (h.size() > 64) { h.removeFirst(); pos -= 1; }
    }
    m_tabTitles[idx].clear();
    navigateAbsolute(clean);
}

void QtOpenWebWidget::resetTabTo(const QString &url, const QString &title) {
    int idx = ow_core_active_tab();
    while (idx >= m_hist.size()) m_hist.append(QStringList());
    while (idx >= m_histPos.size()) m_histPos.append(-1);
    while (idx >= m_tabTitles.size()) m_tabTitles.append(QString());
    m_urlInput = url;
    m_cursorPos = m_urlInput.length();
    m_scrollY = 0;
    if (!title.isEmpty()) m_tabTitles[idx] = title;
    update();
}

void QtOpenWebWidget::back() {
    int idx = ow_core_active_tab();
    if (idx < 0 || idx >= m_histPos.size()) return;
    int &pos = m_histPos[idx];
    if (pos <= 0) return;
    const QStringList &h = m_hist[idx];
    if (pos - 1 >= h.size()) return;
    pos -= 1;
    m_tabTitles[idx].clear();
    navigateAbsolute(h[pos]);
}

void QtOpenWebWidget::forward() {
    int idx = ow_core_active_tab();
    if (idx < 0 || idx >= m_histPos.size()) return;
    int &pos = m_histPos[idx];
    const QStringList &h = m_hist[idx];
    if (pos + 1 >= h.size()) return;
    pos += 1;
    m_tabTitles[idx].clear();
    navigateAbsolute(h[pos]);
}

void QtOpenWebWidget::reloadActive() {
    int idx = ow_core_active_tab();
    openweb_tab_t *tabs = ow_core_tabs();
    if (m_initialized && tabs && idx >= 0 && idx < ow_core_tab_count() &&
        tabs[idx].url[0]) {
        QString u = QString::fromUtf8(tabs[idx].url);
        if (u.startsWith("about:")) { update(); return; }
        navigateAbsolute(u);
    }
}

void QtOpenWebWidget::clearActiveField() {
    m_activeField = -1;
    m_fieldCursor = 0;
}

void QtOpenWebWidget::activateFieldAt(const QPoint &pos) {
    m_activeField = -1;
    m_fieldCursor = 0;
    for (int i = 0; i < m_fieldRects.size(); i++) {
        if (!m_fieldRects[i].contains(pos)) continue;
        int fi = m_fieldIds[i];
        if (fi < 0 || fi >= ow_field_cnt) return;
        ow_form_field_t *f = &ow_form_fields[fi];
        if (f->type != OW_FT_TEXT && f->type != OW_FT_PASSWORD && f->type != OW_FT_TEXTAREA)
            return;
        m_activeField = fi;
        QFont mf = font(); mf.setPointSize(10);
        QFontMetrics lfm(mf);
        int charW = qMax(6, lfm.averageCharWidth());
        int col = (pos.x() - m_fieldRects[i].x()) / charW - 1;
        const char *v = f->value;
        int vlen = 0; while (v[vlen]) vlen++;
        m_fieldCursor = qBound(0, col, qMin(f->width, vlen));
        return;
    }
}

void QtOpenWebWidget::nextField(int dir) {
    if (!m_initialized || ow_field_cnt <= 0) { clearActiveField(); return; }
    int pick = -1;
    int start = m_activeField;
    clearActiveField();
    for (int step = 1; step <= ow_field_cnt; step++) {
        int idx = (start + step * dir) % ow_field_cnt;
        if (idx < 0) idx += ow_field_cnt;
        ow_form_field_t *f = &ow_form_fields[idx];
        if (f->type == OW_FT_TEXT || f->type == OW_FT_PASSWORD || f->type == OW_FT_TEXTAREA) {
            pick = idx;
            break;
        }
    }
    if (pick >= 0) {
        m_activeField = pick;
        const char *v = ow_form_fields[pick].value;
        int vlen = 0; while (v[vlen]) vlen++;
        m_fieldCursor = vlen;
        scrollFieldIntoView(pick);
    }
}

void QtOpenWebWidget::scrollFieldIntoView(int fi) {
    if (fi < 0 || fi >= ow_field_cnt) return;
    int line = ow_form_fields[fi].line;
    int lineH = 18;
    int viewH = contentGeom().height();
    if (line * lineH < m_scrollY) {
        m_scrollY = qMax(0, line * lineH - 12);
    } else if (line * lineH + lineH - m_scrollY > viewH) {
        m_scrollY = qMax(0, line * lineH + lineH - viewH);
    }
}

void QtOpenWebWidget::submitForm(int form) {
    m_activeField = -1;
    m_fieldCursor = 0;
    if (form < 0 || form >= ow_form_cnt) return;
    if (!m_initialized) {
        ow_core_init();
        m_initialized = ow_core_is_initialized();
    }
    ow_form_t *f = &ow_forms[form];
    char q[2048];
    int ql = ow_form_build_query(f, q, sizeof(q));
    QString action = f->action[0] ? QString::fromUtf8(f->action) : QString();
    if (action.isEmpty()) action = currentTabUrl();
    action = resolveLink(action);
    if (action.isEmpty()) return;
    if (f->method == OW_FM_POST) {
        QByteArray a = action.toUtf8();
        kprintf("OW POST action='%s' body='%.400s' len=%d\n", a.constData(), q, ql);
        ow_core_navigate_post(a.constData(), q, ql);
        m_urlInput = action;
        m_cursorPos = m_urlInput.length();
        m_scrollY = 0;
        m_status = "Submitting " + action + "...";
        update();
        return;
    }
    QString url = action;
    if (ql > 0) {
        if (url.contains('?')) url += '&';
        else url += '?';
        url += QString::fromUtf8(q, ql);
    }
    navigateTo(url);
}

void QtOpenWebWidget::renderCurrentTab() {
    if (!m_initialized) return;
    int cnt = ow_core_tab_count();
    if (cnt <= 0) return;
    openweb_tab_t *tabs = ow_core_tabs();
    int idx = ow_core_active_tab();
    if (!tabs || idx < 0 || idx >= cnt) return;
    openweb_tab_t *tab = &tabs[idx];
    if (tab->content_len > 0 && tab->content_len < OW_CONTENT_MAX) {
        ow_core_render_active();
        /* capture <title> */
        if (ow_page_title[0]) {
            while (idx >= m_tabTitles.size()) m_tabTitles.append(QString());
            m_tabTitles[idx] = QString::fromUtf8(ow_page_title);
        }
        m_imageLoadTick = 0;
    }
    if (tab->status[0]) {
        if (!m_status.startsWith("Loading ") || !tab->error)
            m_status = QString::fromUtf8(tab->status);
    }
    if (tab->error) {
        int idx_t = ow_core_active_tab();
        while (idx_t >= m_tabTitles.size()) m_tabTitles.append(QString());
        m_tabTitles[idx_t].clear();
    }
}

void QtOpenWebWidget::ensureImages() {
    if (!m_initialized || ow_image_cnt == 0) return;
    ow_image_start_workers();

    for (int i = 0; i < ow_image_cnt && i < OW_MAX_IMAGES; i++) {
        while (m_imageCache.size() <= i) { m_imageCache.append(QImage()); m_imageLoaded.append(false); }
        if (m_imageLoaded[i]) continue;
        if (!ow_images[i].url[0]) { m_imageLoaded[i] = true; continue; }
        QString abs = resolveLink(QString::fromUtf8(ow_images[i].url));

        int st = ow_image_state(i);
        if (st == 3 /* IMG_ST_READY */) {
            int n = ow_image_raw_len(i);
            const unsigned char *raw = ow_image_raw(i);
            if (raw && n > 8) {
                QImage img = QImage::fromData(raw, n);
                if (!img.isNull()) {
                    m_imageCache[i] = img;
                    m_imageLoaded[i] = true;
                } else {
                    m_imageLoaded[i] = true;
                }
            } else {
                m_imageLoaded[i] = true;
            }
        } else if (st == 0 /* IMG_ST_EMPTY */ || st == 4 /* IMG_ST_FAIL */) {
            if (abs.startsWith("http://")) {
                ow_image_enqueue(i, abs.toUtf8().constData());
            } else {
                m_imageLoaded[i] = true;
            }
        }
        /* st == 1 (PENDING) or 2 (FETCHING): leave for next tick */
    }
}

QRect QtOpenWebWidget::startTileRect(int idx, const QRect &r) const {
    int cols = 4;
    int tw = qMin(220, (r.width() - 96) / cols);
    int th = 90;
    int gap = 16;
    int x0 = r.center().x() - (cols * tw + (cols - 1) * gap) / 2;
    int row = idx / cols, col = idx % cols;
    int top = r.center().y() - 40;
    return QRect(x0 + col * (tw + gap), top + row * (th + gap), tw, th);
}

QRect QtOpenWebWidget::contentGeom() const {
    int toolbarH = 36, tabH = 28;
    return QRect(m_lastRect.x(), m_lastRect.y() + toolbarH + tabH + 1,
                 m_lastRect.width(), m_lastRect.height() - toolbarH - tabH - 24);
}

QString QtOpenWebWidget::existingTabUrl(int i) const {
    if (!m_initialized) return QString();
    openweb_tab_t *tabs = ow_core_tabs();
    if (tabs && i >= 0 && i < ow_core_tab_count())
        return QString::fromUtf8(tabs[i].url);
    return QString();
}

void QtOpenWebWidget::paintPageContent(QPainter &p, const QRect &contentRect) {
    QFont f = font();
    QPoint mouse = mapFromGlobal(cursor().pos());

    if (isStartPage()) {
        /* ── Builtin start page ── */
        p.fillRect(contentRect, QColor(0x12,0x12,0x14));
        QLinearGradient g(0, contentRect.y(), 0, contentRect.y()+260);
        g.setColorAt(0, QColor(0x1B,0x22,0x33));
        g.setColorAt(1, QColor(0x12,0x12,0x14));
        p.fillRect(contentRect.adjusted(0,0,0,-contentRect.height()+260), g);

        /* logo */
        QRect logoRect(contentRect.center().x()-46, contentRect.y()+46, 92, 92);
        QLinearGradient lg(logoRect.topLeft(), logoRect.bottomRight());
        lg.setColorAt(0, QColor(0x64,0xD9,0xF0));
        lg.setColorAt(1, QColor(0x29,0x6B,0xFF));
        p.setPen(Qt::NoPen); p.setBrush(lg);
        p.drawEllipse(logoRect);
        QFont lf = f; lf.setPointSize(30); lf.setBold(true); p.setFont(lf);
        p.setPen(QColor(255,255,255,240));
        p.drawText(logoRect, Qt::AlignCenter, "W");

        QFont tf2 = f; tf2.setPointSize(20); tf2.setBold(true); p.setFont(tf2);
        p.setPen(QColor(245,245,247));
        p.drawText(contentRect.adjusted(0,0,0,-100), Qt::AlignCenter, "OpenWeb");
        QFont sf2 = f; sf2.setPointSize(9); p.setFont(sf2);
        p.setPen(QColor(142,142,147));
        p.drawText(contentRect.adjusted(0,36,0,-100), Qt::AlignCenter,
                   "plain-HTTP browser for CodeOS");

        /* big search field */
        QRect sbar(contentRect.center().x()-180, contentRect.y()+150, 360, 34);
        p.setBrush(QColor(0x24,0x24,0x28)); p.setPen(QPen(QColor(0x64,0xD9,0xF0,140),1));
        p.drawRoundedRect(sbar, 8, 8);
        if (m_urlInput.isEmpty()) {
            p.setPen(QColor(142,142,147,170));
            p.drawText(sbar.adjusted(12,0,-8,0), Qt::AlignVCenter, "Search or enter address...");
        } else {
            p.setPen(QColor(245,245,247,220));
            QFontMetrics sfm(f);
            QString sv = sfm.elidedText(m_urlInput, Qt::ElideRight, sbar.width()-24);
            p.drawText(sbar.adjusted(12,0,-8,0), Qt::AlignVCenter, sv);
            m_blinkCounter = (m_blinkCounter + 1) % 60;
            if ((m_blinkCounter / 30) % 2 == 0) {
                int cx = sbar.x()+12+sfm.horizontalAdvance(sv.left(m_cursorPos));
                p.drawLine(cx, sbar.y()+7, cx, sbar.bottom()-7);
            }
        }

        /* quick links */
        for (int i = 0; i < m_quickNames.size() && i < 8; i++) {
            QRect tr_ = startTileRect(i, contentRect);
            bool hovered = tr_.contains(mouse);
            QLinearGradient tg(tr_.topLeft(), tr_.bottomLeft());
            tg.setColorAt(0, QColor(0x2A,0x2C,0x33));
            tg.setColorAt(1, QColor(0x20,0x22,0x28));
            p.setBrush(tg);
            p.setPen(hovered ? QPen(QColor(0x64,0xD9,0xF0,200),1)
                             : QPen(QColor(0x3C,0x3E,0x46),1));
            p.drawRoundedRect(tr_, 8, 8);
            QRect dot(tr_.x()+14, tr_.y()+16, 26, 26);
            p.setBrush(QColor(0x64,0xD9,0xF0).darker(160)); p.setPen(Qt::NoPen);
            p.drawEllipse(dot);
            QFont nf = f; nf.setPointSize(9); nf.setBold(true); p.setFont(nf);
            p.setPen(QColor(255,255,255));
            p.drawText(dot, Qt::AlignCenter, m_quickNames[i].left(1));
            QFont nn = f; nn.setPointSize(9); p.setFont(nn);
            p.setPen(QColor(235,235,237));
            p.drawText(tr_.adjusted(50,10,-10,0), Qt::AlignLeft|Qt::AlignVCenter|Qt::AlignTop,
                       m_quickNames[i]);
            QFont fu = f; fu.setPointSize(7); p.setFont(fu);
            p.setPen(QColor(142,142,147));
            QFontMetrics fum(fu);
            QString fuTxt = fum.elidedText(m_quickUrls[i], Qt::ElideRight, tr_.width()-60);
            p.drawText(tr_.adjusted(50,24,-10,-8), Qt::AlignLeft|Qt::AlignVCenter|Qt::AlignTop, fuTxt);
        }
        p.setFont(f);
        p.setPen(QColor(110,110,115));
        p.drawText(contentRect.adjusted(0,0,0,-18), Qt::AlignHCenter|Qt::AlignBottom,
                   "Ctrl+L address   Ctrl+T new tab   Ctrl+W close tab   Ctrl+R reload   Esc clear");
        return;
    }

    /* Error page when a load failed with no content */
    openweb_tab_t *tabs = ow_core_tabs();
    int errIdx = ow_core_active_tab();
    bool err = tabs && errIdx >= 0 && errIdx < ow_core_tab_count() &&
               tabs[errIdx].error && tabs[errIdx].content_len == 0;
    if (err) {
        p.fillRect(contentRect, QColor(0xF5,0xF5,0xF7));
        p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36,40));
        p.drawRoundedRect(QRect(contentRect.center().x()-28, contentRect.y()+40, 56, 56), 14, 14);
        QFont wf = f; wf.setPointSize(20); wf.setBold(true); p.setFont(wf);
        p.setPen(QColor(0xFF,0x5A,0x36));
        p.drawText(QRect(contentRect.center().x()-28, contentRect.y()+46, 56, 56),
                   Qt::AlignCenter, "!");
        QFont hf = f; hf.setPointSize(13); hf.setBold(true); p.setFont(hf);
        p.setPen(QColor(0x1C,0x1C,0x1E));
        p.drawText(QRect(contentRect.x()+24, contentRect.y()+116, contentRect.width()-48, 28),
                   Qt::AlignHCenter, "Page could not load");
        QFont bf2 = f; bf2.setPointSize(10); p.setFont(bf2);
        p.setPen(QColor(0x55,0x55,0x58));
        p.drawText(QRect(contentRect.x()+24, contentRect.y()+148, contentRect.width()-48, 22),
                   Qt::AlignHCenter, m_status);
        p.setPen(QColor(0x88,0x88,0x8C));
        p.drawText(QRect(contentRect.x()+24, contentRect.y()+178, contentRect.width()-48, 44),
                   Qt::AlignHCenter,
                   "Full URLs over plain HTTP work, e.g. http://10.0.2.2:8095/\n"
                   "(search engines and HTTPS sites need TLS — not in the kernel).");
        return;
    }

    /* ── Render HTML text ── */
    renderCurrentTab();
    int lineH = 18;
    int startY = contentRect.y() + 12 - m_scrollY;
    m_linkRects.clear();
    m_linkUrls.clear();
    m_fieldRects.clear();
    m_fieldIds.clear();

    f.setPointSize(10); f.setBold(false); p.setFont(f);
    QFont codeFont("monospace"); codeFont.setPointSize(10);
    QFontMetrics lfm(f);
    int charW = qMax(6, lfm.averageCharWidth());

    for (int i = 0; i < ow_txt_lines && i < OW_TXT_LINES; i++) {
        int ly = startY + i * lineH;
        if (ly + lineH < contentRect.y() || ly > contentRect.bottom()) continue;

        /* Images */
        if (ow_line_info[i].type == OW_LT_IMAGE) {
            int im = (i < OW_TXT_LINES) ? ow_line_img[i] : -1;
            ensureImages();
            if (im >= 0 && im < m_imageCache.size() && !m_imageCache[im].isNull()) {
                QImage img = m_imageCache[im];
                int w = ow_images[im].w > 0 ? ow_images[im].w : img.width();
                int h = ow_images[im].h > 0 ? ow_images[im].h : img.height();
                int maxW = contentRect.width() - 32;
                if (w <= 0) w = maxW;
                if (w > maxW) { h = (h * maxW) / w; w = maxW; }
                p.fillRect(QRect(contentRect.x()+16, ly, w, h), QColor(0xEE,0xEE,0xF0));
                p.drawImage(QRect(contentRect.x()+16, ly, w, h), img, img.rect());
                p.setPen(QPen(QColor(0xCC,0xCC,0xCC),1));
                p.drawRect(QRect(contentRect.x()+16, ly, w, h));
            } else {
                int imi = im >= 0 ? im : 0;
                int w = (im >= 0 && ow_images[im].w > 0) ? qMin(ow_images[im].w, contentRect.width()-32) : 240;
                int h = 60;
                p.fillRect(QRect(contentRect.x()+16, ly, w, h), QColor(0xE8,0xE8,0xEC));
                p.setPen(QPen(QColor(0xAA,0xAA,0xAE),1));
                p.drawRect(QRect(contentRect.x()+16, ly, w, h));
                p.setPen(QColor(0x88,0x88,0x8C));
                p.drawText(QRect(contentRect.x()+16, ly, w, h), Qt::AlignCenter,
                           ow_images[imi].url[0] ? QString("image: ") + QString::fromUtf8(ow_images[imi].url)
                                                 : QString("unavailable image"));
            }
            continue;
        }

        int lt = (i < OW_TXT_LINES) ? ow_line_info[i].type : OW_LT_NORMAL;
        QFont lf = f;
        QColor pen(0x1C, 0x1C, 0x1E);
        bool fillRow = false;
        switch (lt) {
            case OW_LT_H1: lf.setPointSize(18); lf.setBold(true); lf.setItalic(false); break;
            case OW_LT_H2: lf.setPointSize(15); lf.setBold(true); lf.setItalic(false); break;
            case OW_LT_H3: lf.setPointSize(13); lf.setBold(true); lf.setItalic(false); pen = QColor(0x33,0x33,0x35); break;
            case OW_LT_H4: case OW_LT_H5: case OW_LT_H6:
                lf.setPointSize(11); lf.setBold(true); lf.setItalic(false); pen = QColor(0x33,0x33,0x35); break;
            case OW_LT_HR: {
                p.setPen(QPen(QColor(0xCC,0xCC,0xCC),1));
                p.drawLine(contentRect.x()+16, ly+lineH/2, contentRect.right()-16, ly+lineH/2);
                continue;
            }
            case OW_LT_CODE:
                p.setFont(codeFont); p.setPen(QColor(0x1C,0x1C,0x1E)); break;
            case OW_LT_BQ:
                lf.setPointSize(10); lf.setBold(false); lf.setItalic(false);
                pen = QColor(0x66,0x66,0x66); break;
            case OW_LT_LI:
                lf.setPointSize(10); lf.setBold(false); lf.setItalic(false);
                pen = QColor(0x33,0x33,0x35); break;
            case OW_LT_BOLD:
                lf.setPointSize(10); lf.setBold(true); lf.setItalic(false); break;
            case OW_LT_ITALIC:
                lf.setPointSize(10); lf.setBold(false); lf.setItalic(true); break;
            case OW_LT_TH:
                lf.setPointSize(10); lf.setBold(true); lf.setItalic(false);
                pen = QColor(0x1F,0x3A,0x61); fillRow = true;
                p.fillRect(QRect(contentRect.x()+8, ly, contentRect.width()-16, lineH), QColor(0xE6,0xEE,0xF9));
                break;
            case OW_LT_TD:
                lf.setPointSize(10); lf.setBold(false); lf.setItalic(false);
                pen = QColor(0x33,0x33,0x35); fillRow = true;
                p.fillRect(QRect(contentRect.x()+8, ly, contentRect.width()-16, lineH), QColor(0xF5,0xF6,0xF7,120));
                break;
            case OW_LT_EMPTY:
                continue;
            default:
                lf.setPointSize(10); lf.setBold(false); lf.setItalic(false); break;
        }
        p.setFont(lf);
        p.setPen(pen);
        (void)fillRow;

        char lineBuf[OW_TXT_COLS + 1];
        memcpy(lineBuf, ow_txt[i], OW_TXT_COLS);
        lineBuf[OW_TXT_COLS] = '\0';
        int len = OW_TXT_COLS;
        while (len > 0 && lineBuf[len-1] == ' ') len--;
        lineBuf[len] = '\0';
        if (len == 0) continue;
        QString lineText = QString::fromUtf8(lineBuf);

        bool isLinkLine = false;
        int baseX = contentRect.x() + 16;
        /* Form fields on this line (boxes are grid cells f->col..f->col+width). */
        for (int fi = 0; fi < ow_field_cnt && fi < OW_MAX_FIELDS; fi++) {
            ow_form_field_t *f = &ow_form_fields[fi];
            if (f->line != i) continue;
            int fx = baseX + f->col * charW;
            int fw = (f->width + 2) * charW;
            int fh = (f->type == OW_FT_TEXTAREA) ? lineH * 2 : lineH;
            QRect fr(fx, ly, fw, fh);
            m_fieldRects.append(fr);
            m_fieldIds.append(fi);
            if (fi == m_activeField &&
                (f->type == OW_FT_TEXT || f->type == OW_FT_PASSWORD || f->type == OW_FT_TEXTAREA)) {
                p.fillRect(fr.adjusted(0,0,-1,-1), QColor(0xFF,0xF2,0xCC,110));
                p.setPen(QPen(QColor(0xFF,0x8A,0x2E),1));
                p.drawRect(fr.adjusted(0,0,-1,-1));
            }
            (void)fh;
        }
        for (int li = 0; li < ow_link_cnt && li < OW_MAX_LINKS; li++) {
            if (ow_links[li].line != i) continue;
            isLinkLine = true;
            int sc = ow_links[li].sc;
            int ec = ow_links[li].ec;
            if (sc < 0) sc = 0;
            if (ec <= sc) ec = len;
            int lx = baseX + sc * charW;
            int lw = qMax(10, (ec - sc) * charW);
            m_linkRects.append(QRect(lx, ly, lw, lineH));
            m_linkUrls.append(QString::fromUtf8(ow_links[li].url));
            p.setPen(QPen(QColor(0xFF,0x5A,0x36,110),1));
            p.drawLine(lx, ly+lineH-4+1, lx + lw, ly+lineH-4+1);
        }
        if (isLinkLine) p.setPen(QColor(0xFF,0x5A,0x36));
        p.drawText(baseX, ly+lineH-4, lineText);
    }

    /* Active text/password field caret (drawn last so it sits above glyphs). */
    if (m_activeField >= 0 && m_activeField < ow_field_cnt) {
        ow_form_field_t *f = &ow_form_fields[m_activeField];
        if (f->type == OW_FT_TEXT || f->type == OW_FT_PASSWORD) {
            int ly = startY + f->line * lineH;
            if (ly + lineH >= contentRect.y() && ly <= contentRect.bottom()) {
                int inner = f->width < 0 ? 0 : f->width;
                int cur = m_fieldCursor < 0 ? 0 : (m_fieldCursor > inner ? inner : m_fieldCursor);
                int cx = contentRect.x() + 16 + (f->col + 1 + cur) * charW;
                m_blinkCounter = (m_blinkCounter + 1) % 60;
                if ((m_blinkCounter / 30) % 2 == 0) {
                    p.setPen(QColor(0x1C,0x1C,0x1E));
                    p.drawLine(cx, ly + 3, cx, ly + lineH - 3);
                }
            }
        }
    }

    /* ── Scroll bar ── */
    m_maxScroll = qMax(0, ow_txt_lines * lineH - contentRect.height() + 24);
    if (m_maxScroll > 0) {
        int sbH = contentRect.height();
        int thumbH = qMax(20, sbH * contentRect.height() / (m_maxScroll + contentRect.height()));
        int thumbY = contentRect.y() + (m_scrollY * (sbH - thumbH)) / m_maxScroll;
        m_scrollThumb = QRect(contentRect.right()-6, thumbY, 4, thumbH);
        p.setPen(Qt::NoPen);
        p.setBrush(m_scrollDrag ? QColor(0xFF,0x5A,0x36,160) : QColor(0x99,0x99,0x99,60));
        p.drawRoundedRect(m_scrollThumb, 2, 2);
    } else {
        m_scrollThumb = QRect();
        m_scrollDrag = false;
    }
}

void QtOpenWebWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    int toolbarH = 36, tabH = 28;
    m_lastRect = r;

    /* ── Navigation toolbar ── */
    QRect toolbar(r.x(), r.y(), r.width(), toolbarH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2C,0x2C,0x2E,100)); p.drawRect(toolbar);

    int idx = m_initialized ? ow_core_active_tab() : -1;
    bool canBack = idx >= 0 && idx < m_histPos.size() && m_histPos[idx] > 0 &&
                   idx < m_hist.size() && m_hist[idx].size() > 0;
    bool canFwd = idx >= 0 && idx < m_histPos.size() && idx < m_hist.size() &&
                  m_histPos[idx] + 1 < m_hist[idx].size();

    /* Back / Forward / Reload / Stop buttons */
    int bx = r.x() + 8;
    int bw = 28, bh = 24;
    m_backRect = QRect(bx, r.y()+(toolbarH-bh)/2, bw, bh);
    m_fwdRect  = QRect(bx+bw+4, r.y()+(toolbarH-bh)/2, bw, bh);
    m_reloadRect = QRect(bx+bw*2+8, r.y()+(toolbarH-bh)/2, bw, bh);
    m_stopRect   = QRect(bx+bw*3+12, r.y()+(toolbarH-bh)/2, bw, bh);

    auto drawBtn = [&](const QRect &br, const QString &label, bool enabled) {
        bool hovered = enabled && br.contains(mapFromGlobal(cursor().pos()));
        p.setBrush(hovered ? QColor(0x63,0x63,0x66,100)
                           : QColor(0x3A,0x3A,0x3C,80));
        p.setPen(Qt::NoPen); p.drawRoundedRect(br, 4, 4);
        QFont bf = f; bf.setPointSize(10); bf.setBold(true); p.setFont(bf);
        p.setPen(enabled ? QColor(245,245,247,220) : QColor(142,142,147,90));
        p.drawText(br, Qt::AlignCenter, label);
    };
    drawBtn(m_backRect, QString::fromUtf8("\xe2\x97\x80"), canBack);
    drawBtn(m_fwdRect,  QString::fromUtf8("\xe2\x96\xb6"), canFwd);
    drawBtn(m_reloadRect, QString::fromUtf8("\xe2\x86\xbb"), true);

    /* Stop button, WebKit-style: only meaningful while the active tab loads. */
    bool loadingNow = m_initialized;
    if (loadingNow) {
        openweb_tab_t *tabs = ow_core_tabs();
        int ai = ow_core_active_tab();
        loadingNow = tabs && ai >= 0 && ai < ow_core_tab_count() && tabs[ai].loading;
    }
    drawBtn(m_stopRect, QString::fromUtf8("\xe2\x9c\x95"), loadingNow);

    /* URL bar */
    int urlX = m_stopRect.right() + 10;
    int urlW = r.width() - (urlX - r.x()) - 8;
    QRect urlBar(urlX, r.y()+(toolbarH-28)/2, urlW, 28);
    p.setBrush(QColor(0x1C,0x1C,0x1E)); p.setPen(QPen(QColor(0x63,0x63,0x66,120),1));
    p.drawRoundedRect(urlBar, 6, 6);

    f.setPointSize(10); f.setBold(false); p.setFont(f);
    p.setPen(QColor(245,245,247,200));
    QString displayUrl = m_urlInput;
    bool isEdit = !m_urlInput.isEmpty();
    if (!isEdit) {
        displayUrl = "Enter URL or search...";
        p.setPen(QColor(142,142,147,160));
    }
    QFontMetrics ufm(f);
    QString visibleText = ufm.elidedText(displayUrl, Qt::ElideRight, urlW-24);
    p.drawText(urlBar.adjusted(10,0,-10,0), Qt::AlignVCenter, visibleText);
    if (isEdit) {
        int curX = urlBar.x() + 10 + ufm.horizontalAdvance(visibleText.left(m_cursorPos));
        if (curX > urlBar.right()-10) curX = urlBar.right()-10;
        m_blinkCounter = (m_blinkCounter + 1) % 60;
        if ((m_blinkCounter / 30) % 2 == 0)
            p.drawLine(curX, urlBar.y()+6, curX, urlBar.bottom()-6);
    }

    /* Load-progress fill inside the URL bar (the demo's
     * web_view.estimated-load-progress → set_progress_fraction). */
    if (loadingNow) {
        int lp = m_initialized ? ow_core_load_progress() : 0;
        QRect pf(urlBar.x()+6, urlBar.bottom()-4, qMax(2, urlBar.width()-12), 2);
        p.fillRect(pf, QColor(0x2A,0x2A,0x2C));
        int fw = (pf.width() * qBound(0, lp, 100)) / 100;
        if (fw > 0)
            p.fillRect(QRect(pf.x(), pf.y(), fw, pf.height()), QColor(0x64,0xD9,0xF0));
    }

    /* ── Tab bar ── */
    QRect tabBar(r.x(), toolbar.bottom(), r.width(), tabH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x25,0x25,0x27,100)); p.drawRect(tabBar);

    m_tabRects.clear();
    m_tabCloseRects.clear();
    int tabCount = 0;
    int usedCount = 0;
    int activeTab = -1;
    int tx = r.x() + 4;

    m_newTabRect = QRect(tx, tabBar.y()+(tabH-20)/2, 24, 20);
    p.setBrush(QColor(0x3A,0x3A,0x3C,100)); p.setPen(Qt::NoPen); p.drawRoundedRect(m_newTabRect, 4, 4);
    QFont tf = f; tf.setPointSize(11); tf.setBold(true); p.setFont(tf);
    p.setPen(QColor(245,245,247,160));
    p.drawText(m_newTabRect, Qt::AlignCenter, "+");
    tx += 30;

    if (m_initialized) {
    openweb_tab_t *tabs = ow_core_tabs();
    tabCount = ow_core_tab_count();
    usedCount = ow_core_used_tab_count();
    activeTab = ow_core_active_tab();
    if (tabCount < 0 || tabCount > 32) tabCount = 0;
    if (usedCount < 0 || usedCount > 32) usedCount = 0;
    if (activeTab < 0 || activeTab > 32) activeTab = -1;
    for (int i = 0; i < tabCount && tabs; i++) {
        if (tabs[i].url[0] == '\0' && i >= usedCount) continue;
        QString tabUrl = QString::fromUtf8(tabs[i].url);
        QString title;
        if (i < m_tabTitles.size()) title = m_tabTitles[i];
        if (tabUrl.isEmpty() || tabUrl.startsWith("about:")) {
            if (title.isEmpty()) title = "New Tab";
        } else if (!title.isEmpty()) {
            title = title.simplified();
        } else {
            title = tabUrl.section('/', 2).section('/', 0); /* domain only */
        }
        if (title.length() > 20) title = title.left(18) + "...";

        int tw = ufm.horizontalAdvance(title) + 34;
        QRect tr(tx, tabBar.y()+(tabH-22)/2, tw, 22);
        m_tabRects.append(tr);

        bool active = (i == activeTab);
        bool hovered = (i == m_hoveredTab) && !active;
        p.setBrush(active ? QColor(0x3A,0x3A,0x3C,160)
                          : (hovered ? QColor(0x45,0x45,0x47,130) : QColor(0x2C,0x2C,0x2E,80)));
        p.setPen(active ? QPen(QColor(0xFF,0x5A,0x36,80),1) : Qt::NoPen);
        p.drawRoundedRect(tr, 4, 4);

        /* Loading spinner placeholder */
        if (tabs[i].loading) {
            p.setPen(QPen(QColor(0xFF,0x5A,0x36),2));
            p.drawArc(tr.right()-24, tr.y()+3, 14, 14, 0, 270*16);
        }

        QFont ttf = f; ttf.setPointSize(9); p.setFont(ttf);
        p.setPen(active ? QColor(245,245,247) : QColor(142,142,147));
        p.drawText(tr.adjusted(6,0,-20,0), Qt::AlignVCenter|Qt::AlignLeft, title);

        /* Close button */
        QRect cr(tr.right()-16, tr.y()+(22-12)/2+1, 12, 12);
        m_tabCloseRects.append(cr);
        bool overClose = cr.contains(mapFromGlobal(cursor().pos()));
        p.setBrush(overClose ? QColor(0xFF,0x69,0x5A,200) : QColor(0x5A,0x5A,0x5E,60));
        p.setPen(Qt::NoPen); p.drawRoundedRect(cr, 3, 3);
        p.setPen(QColor(230,230,232,180));
        p.drawLine(cr.x()+3, cr.y()+3, cr.right()-3, cr.bottom()-3);
        p.drawLine(cr.right()-3, cr.y()+3, cr.x()+3, cr.bottom()-3);

        tx += tw + 4;
    }
    } /* m_initialized */

    p.setPen(QPen(QColor(0x3A,0x3A,0x3C),1));
    p.drawLine(r.x(), tabBar.bottom(), r.right(), tabBar.bottom());

    /* ── Content area ── */
    QRect contentRect(r.x(), tabBar.bottom()+1, r.width(), r.height()-toolbarH-tabH-24);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF5,0xF5,0xF7));
    p.drawRect(contentRect);

    if (m_initialized)
        paintPageContent(p, contentRect);

    /* ── Status bar ── */
    QRect statusBar(r.x(), r.bottom()-20, r.width(), 20);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2C,0x2C,0x2E,100)); p.drawRect(statusBar);

    int progress = m_initialized ? ow_core_load_progress() : 0;
    if (progress > 0 && progress < 100) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xFF,0x5A,0x36));
        p.drawRect(r.x(), r.bottom()-22, r.width() * progress / 100, 2);
    }

    QFont sf = f; sf.setPointSize(9); p.setFont(sf);
    p.setPen(QColor(142,142,147));
    QString statusText = m_hoverUrl.isEmpty()
        ? (m_status.isEmpty() ? "Ready" : m_status)
        : "↗ " + m_hoverUrl;
    p.drawText(statusBar.adjusted(8,0,-220,0), Qt::AlignVCenter, statusText);

    if (ow_link_cnt > 0) {
        p.drawText(statusBar.adjusted(8,0,-8,0), Qt::AlignVCenter|Qt::AlignRight,
                   QString("%1 links · %2").arg(ow_link_cnt).arg(activeTitle()));
    } else {
        p.drawText(statusBar.adjusted(8,0,-8,0), Qt::AlignVCenter|Qt::AlignRight,
                   activeTitle());
    }

    m_scrollY = qBound(0, m_scrollY, m_maxScroll);
}

void QtOpenWebWidget::mousePressEvent(QMouseEvent *e) {
    QPoint pos = e->pos();

    if (m_newTabRect.contains(pos)) {
        ow_core_new_tab();
        while (m_hist.size() < ow_core_tab_count()) m_hist.append(QStringList());
        while (m_histPos.size() < ow_core_tab_count()) m_histPos.append(-1);
        while (m_tabTitles.size() < ow_core_tab_count()) m_tabTitles.append(QString());
        int idx = ow_core_active_tab();
        if (idx >= 0 && idx < m_hist.size()) m_hist[idx].clear();
        if (idx >= 0 && idx < m_histPos.size()) m_histPos[idx] = -1;
        m_urlInput.clear();
        m_cursorPos = 0;
        m_scrollY = 0;
        m_status = "Ready";
        update(); return;
    }
    for (int i = 0; i < m_tabCloseRects.size(); i++) {
        if (m_tabCloseRects[i].contains(pos)) {
            if (ow_core_tab_count() > 1) {
                ow_core_close_active_tab();
                if (m_hist.size() > ow_core_tab_count()) m_hist.removeAt(qMin(i, m_hist.size()-1));
                if (m_histPos.size() > ow_core_tab_count()) m_histPos.removeAt(qMin(i, m_histPos.size()-1));
                if (m_tabTitles.size() > ow_core_tab_count()) m_tabTitles.removeAt(qMin(i, m_tabTitles.size()-1));
                int ai = ow_core_active_tab();
                if (ai >= 0 && ai < m_hist.size() && !m_hist[ai].isEmpty()) {
                    int p = m_histPos.value(ai, -1);
                    if (p >= 0 && p < m_hist[ai].size()) m_urlInput = m_hist[ai][p];
                } else m_urlInput.clear();
            }
            m_scrollY = 0;
            update(); return;
        }
    }
    for (int i = 0; i < m_tabRects.size(); i++) {
        if (m_tabRects[i].contains(pos)) {
            if (i != ow_core_active_tab()) {
                ow_core_set_active_tab(i);
                m_scrollY = 0;
                int ai = i;
                if (ai >= 0 && ai < m_hist.size() && !m_hist[ai].isEmpty() &&
                    m_histPos.value(ai, -1) >= 0 &&
                    m_histPos.value(ai, -1) < m_hist[ai].size())
                    m_urlInput = m_hist[ai][m_histPos.value(ai, -1)];
                else
                    m_urlInput = existingTabUrl(i);
            }
            update(); return;
        }
    }

    if (m_backRect.contains(pos)) {
        back(); update(); return;
    }
    if (m_fwdRect.contains(pos)) {
        forward(); update(); return;
    }
    if (m_reloadRect.contains(pos)) {
        reloadActive(); update(); return;
    }
    if (m_stopRect.contains(pos)) {
        ow_core_stop();
        m_status = "Stopped";
        update(); return;
    }

    /* Start page tiles */
    if (isStartPage()) {
        for (int i = 0; i < m_quickNames.size() && i < 8; i++) {
            if (startTileRect(i, contentGeom()).contains(pos)) {
                navigateTo(m_quickUrls[i]);
                update(); return;
            }
        }
    }

    /* Form field clicks (fields take precedence over coincident links) */
    for (int i = 0; i < m_fieldRects.size(); i++) {
        if (m_fieldRects[i].contains(pos)) {
            int fi = m_fieldIds[i];
            if (fi < 0 || fi >= ow_field_cnt) break;
            ow_form_field_t *f = &ow_form_fields[fi];
            activateFieldAt(pos);
            if (f->type == OW_FT_CHECKBOX || f->type == OW_FT_RADIO) {
                m_activeField = -1;
                m_fieldCursor = 0;
                ow_field_toggle(fi);
                if (m_initialized) ow_core_render_active();
                update(); return;
            }
            if (f->type == OW_FT_SUBMIT || f->type == OW_FT_BUTTON) {
                m_activeField = -1;
                m_fieldCursor = 0;
                submitForm(f->form);
                return;
            }
            if (f->type == OW_FT_SELECT) {
                if (f->opt_cnt > 0) {
                    f->opt_sel = (f->opt_sel + 1) % f->opt_cnt;
                    ow_field_set_value(fi, f->opts[f->opt_sel]);
                }
                m_activeField = -1;
                m_fieldCursor = 0;
                if (m_initialized) ow_core_render_active();
                update(); return;
            }
            update(); return;
        }
    }

    /* Link clicks (span-aware) */
    for (int i = 0; i < m_linkRects.size(); i++) {
        if (m_linkRects[i].contains(pos)) {
            navigateTo(resolveLink(m_linkUrls[i]));
            update(); return;
        }
    }

    /* Scrollbar drag */
    if (m_scrollThumb.isValid() && m_maxScroll > 0 &&
        (m_scrollThumb.adjusted(0,-6,0,6)).contains(pos)) {
        m_scrollDrag = true;
        m_scrollDragY = pos.y();
        m_scrollDragBase = m_scrollY;
        return;
    }

    QtAppWindow::mousePressEvent(e);
}

void QtOpenWebWidget::mouseMoveEvent(QMouseEvent *e) {
    QPoint pos = e->pos();

    /* Link hover preview + pointer cursor over interactive areas */
    m_hoverUrl.clear();
    bool overInteractive = false;
    for (int i = 0; i < m_linkRects.size(); i++) {
        if (m_linkRects[i].contains(pos)) {
            m_hoverUrl = m_linkUrls[i];
            overInteractive = true;
            break;
        }
    }
    for (int i = 0; i < m_fieldRects.size(); i++) {
        if (m_fieldRects[i].contains(pos)) { overInteractive = true; break; }
    }
    int hoverTab = -1;
    if (m_newTabRect.contains(pos)) overInteractive = true;
    for (int i = 0; i < m_tabRects.size(); i++) {
        if (m_tabRects[i].contains(pos)) { hoverTab = i; overInteractive = true; break; }
    }
    if (m_backRect.contains(pos) || m_fwdRect.contains(pos) || m_reloadRect.contains(pos) || m_stopRect.contains(pos))
        overInteractive = true;
    if (m_hoveredTab != hoverTab) { m_hoveredTab = hoverTab; update(); }
    setCursor(overInteractive ? Qt::PointingHandCursor : Qt::ArrowCursor);

    if (m_scrollDrag && m_maxScroll > 0) {
        int sbH = contentGeom().height();
        int thumbH = qMax(20, sbH * sbH / (m_maxScroll + sbH));
        int dy = pos.y() - m_scrollDragY;
        int delta = (m_maxScroll * dy) / qMax(1, sbH - thumbH);
        m_scrollY = qBound(0, m_scrollDragBase + delta, m_maxScroll);
        update();
    }

    QtAppWindow::mouseMoveEvent(e);
    update();
}

void QtOpenWebWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (m_scrollDrag) {
        m_scrollDrag = false;
        update();
        return;
    }
    QtAppWindow::mouseReleaseEvent(e);
}

void QtOpenWebWidget::leaveEvent(QEvent *event) {
    m_hoverUrl.clear();
    if (m_hoveredTab != -1) {
        m_hoveredTab = -1;
        update();
    }
    setCursor(Qt::ArrowCursor);
    QtAppWindow::leaveEvent(event);
}

void QtOpenWebWidget::keyPressEvent(QKeyEvent *e) {
    kprintf("OW key=%d mods=%d\n", e->key(), (int)e->modifiers());

    /* Form field editing takes priority over URL-bar/shortcut handling. */
    if (m_activeField >= 0 && m_activeField < ow_field_cnt) {
        ow_form_field_t *f = &ow_form_fields[m_activeField];
        int vlen = 0;
        {
            const char *v = f->value;
            while (v[vlen]) vlen++;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (f->form >= 0) submitForm(f->form);
            else clearActiveField();
            update(); return;
        }
        if (e->key() == Qt::Key_Tab) {
            nextField((e->modifiers() & Qt::ShiftModifier) ? -1 : 1);
            update(); return;
        }
        if (e->key() == Qt::Key_Escape) {
            clearActiveField();
            update(); return;
        }
        QString val = QString::fromUtf8(f->value);
        bool changed = false;
        int cursor = m_fieldCursor;
        if (e->key() == Qt::Key_Backspace) {
            if (e->modifiers() & Qt::ControlModifier) {
                int p = cursor;
                while (p > 0 && val[p-1] == ' ') p--;
                while (p > 0 && val[p-1] != ' ') p--;
                val.remove(p, cursor - p);
                cursor = p;
            } else if (cursor > 0) {
                val.remove(cursor - 1, 1);
                cursor--;
            }
            changed = true;
        } else if (e->key() == Qt::Key_Delete) {
            if (cursor < vlen) val.remove(cursor, 1);
            changed = true;
        } else if (e->key() == Qt::Key_Left) {
            cursor = qMax(0, cursor - 1);
        } else if (e->key() == Qt::Key_Right) {
            cursor = qMin(vlen, cursor + 1);
        } else if (e->key() == Qt::Key_Home) {
            cursor = 0;
        } else if (e->key() == Qt::Key_End) {
            cursor = vlen;
        } else if (e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde &&
                   !(e->modifiers() & Qt::ControlModifier)) {
            QString txt = e->text();
            if (txt.isEmpty()) {
                QChar ch(int(e->key()));
                if (e->modifiers() & Qt::ShiftModifier)
                    ch = ow_shifted_char(int(e->key()));
                txt = QString(ch);
            }
            if (val.length() + txt.length() < OW_URL_MAX - 1) {
                val.insert(cursor, txt);
                cursor += txt.length();
                changed = true;
            }
        }
        if (cursor < 0) cursor = 0;
        m_fieldCursor = cursor;
        if (changed) {
            QByteArray ba = val.toUtf8();
            ow_field_set_value(m_activeField, ba.constData());
        }
        update(); return;
    }

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        kprintf("OW ret branch, urlInput len=%d\n", (int)m_urlInput.length());
        if (!m_urlInput.isEmpty()) {
            navigateTo(m_urlInput);
            update();
        }
        return;
    }
    if (e->key() == Qt::Key_Backspace) {
        if (!m_urlInput.isEmpty()) {
            if (e->modifiers() & Qt::ControlModifier) {
                int p = m_cursorPos;
                while (p > 0 && m_urlInput[p-1] == ' ') p--;
                while (p > 0 && m_urlInput[p-1] != ' ' &&
                       m_urlInput[p-1] != '/' && m_urlInput[p-1] != '.') p--;
                m_urlInput.remove(p, m_cursorPos - p);
                m_cursorPos = p;
            } else {
                m_urlInput.remove(m_cursorPos-1, 1);
                m_cursorPos--;
            }
            update();
        }
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        if (m_cursorPos < m_urlInput.length()) {
            m_urlInput.remove(m_cursorPos, 1);
            update();
        }
        return;
    }
    if (e->key() == Qt::Key_Left && !(e->modifiers() & Qt::ControlModifier)) {
        m_cursorPos = qMax(0, m_cursorPos - 1);
        update(); return;
    }
    if (e->key() == Qt::Key_Right && !(e->modifiers() & Qt::ControlModifier)) {
        m_cursorPos = qMin(m_urlInput.length(), m_cursorPos + 1);
        update(); return;
    }
    if (e->key() == Qt::Key_Left && (e->modifiers() & Qt::ControlModifier)) {
        /* jump to previous separator */
        int p = m_cursorPos;
        while (p > 0 && m_urlInput[p-1] == ' ') p--;
        while (p > 0 && m_urlInput[p-1] != ' ' && m_urlInput[p-1] != '/' &&
               m_urlInput[p-1] != '.' && m_urlInput[p-1] != ':') p--;
        m_cursorPos = p;
        update(); return;
    }
    if (e->key() == Qt::Key_Right && (e->modifiers() & Qt::ControlModifier)) {
        int L = m_urlInput.length();
        int p = m_cursorPos;
        while (p < L && m_urlInput[p] != ' ' && m_urlInput[p] != '/' &&
               m_urlInput[p] != '.' && m_urlInput[p] != ':') p++;
        while (p < L && (m_urlInput[p] == ' ' || m_urlInput[p] == '/' ||
                         m_urlInput[p] == '.' || m_urlInput[p] == ':')) p++;
        m_cursorPos = qMin(L, p);
        update(); return;
    }
    if (e->key() == Qt::Key_Home) {
        if (e->modifiers() & Qt::ControlModifier) { m_scrollY = 0; }
        else m_cursorPos = 0;
        update(); return;
    }
    if (e->key() == Qt::Key_End) {
        if (e->modifiers() & Qt::ControlModifier) { m_scrollY = m_maxScroll; }
        else m_cursorPos = m_urlInput.length();
        update(); return;
    }
    if (e->key() == Qt::Key_Up) {
        m_scrollY = qMax(0, m_scrollY - 60);
        update(); return;
    }
    if (e->key() == Qt::Key_Down) {
        m_scrollY = qMin(m_maxScroll, m_scrollY + 60);
        update(); return;
    }
    if (e->key() == Qt::Key_PageUp) {
        m_scrollY = qMax(0, m_scrollY - 300);
        update(); return;
    }
    if (e->key() == Qt::Key_PageDown) {
        m_scrollY = qMin(m_maxScroll, m_scrollY + 300);
        update(); return;
    }
    if (e->key() == Qt::Key_Tab) {
        int cnt = m_initialized ? ow_core_tab_count() : 0;
        if (cnt > 1) {
            int a = ow_core_active_tab();
            ow_core_set_active_tab((a + 1) % cnt);
            m_scrollY = 0;
            int ai = (a + 1) % cnt;
            if (ai >= 0 && ai < m_hist.size() && !m_hist[ai].isEmpty() &&
                m_histPos.value(ai, -1) >= 0 && m_histPos.value(ai, -1) < m_hist[ai].size())
                m_urlInput = m_hist[ai][m_histPos.value(ai, -1)];
            else
                m_urlInput = existingTabUrl(ai);
            update();
        }
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        m_urlInput.clear();
        m_cursorPos = 0;
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        switch (e->key()) {
            case Qt::Key_L:
                m_urlInput.clear();
                m_cursorPos = 0;
                update(); return;
            case Qt::Key_T:
                ow_core_new_tab();
                while (m_hist.size() < ow_core_tab_count()) m_hist.append(QStringList());
                while (m_histPos.size() < ow_core_tab_count()) m_histPos.append(-1);
                while (m_tabTitles.size() < ow_core_tab_count()) m_tabTitles.append(QString());
                m_urlInput.clear();
                m_cursorPos = 0;
                m_scrollY = 0;
                m_status = "Ready";
                update(); return;
            case Qt::Key_W:
                if (ow_core_tab_count() > 1) {
                    ow_core_close_active_tab();
                    if (m_hist.size() > ow_core_tab_count())
                        m_hist.removeAt(qMin(ow_core_active_tab(), m_hist.size()-1));
                    if (m_histPos.size() > ow_core_tab_count())
                        m_histPos.removeAt(qMin(ow_core_active_tab(), m_histPos.size()-1));
                    if (m_tabTitles.size() > ow_core_tab_count())
                        m_tabTitles.removeAt(qMin(ow_core_active_tab(), m_tabTitles.size()-1));
                    m_scrollY = 0;
                    m_status = "Ready";
                }
                update(); return;
            case Qt::Key_R:
            case Qt::Key_F5:
                reloadActive();
                update(); return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!m_urlInput.isEmpty()) {
                    /* URL + Enter completes the domain */
                    navigateTo(m_urlInput + ".com");
                    update();
                }
                return;
            default: break;
        }
    }
    if (e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde &&
        !(e->modifiers() & Qt::ControlModifier)) {
        QString txt = e->text();
        if (txt.isEmpty()) {
            QChar ch(int(e->key()));
            if (e->modifiers() & Qt::ShiftModifier)
                ch = ow_shifted_char(int(e->key()));
            txt = QString(ch);
        }
        m_urlInput.insert(m_cursorPos, txt);
        m_cursorPos += txt.length();
        update();
        return;
    }
    QtAppWindow::keyPressEvent(e);
}

void QtOpenWebWidget::wheelEvent(QWheelEvent *e) {
    int delta = e->angleDelta().y();
    if (delta > 0) m_scrollY = qMax(0, m_scrollY - 40);
    else m_scrollY = qMin(m_maxScroll, m_scrollY + 40);
    update();
}