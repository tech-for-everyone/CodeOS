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
#include "ow_http.h"
#include "ow_html.h"
#include "apphost.h"
#include "block.h"
#include "io.h"
#include "updater.h"
#include "lvgl_launcher.h"
#include "wifi.h"
#include "usb_audio.h"
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

extern "C" {
uint32_t net_get_ip(void);
uint32_t net_get_gateway(void);
uint32_t net_get_dns(void);
int      net_ready(void);
}

/* ═══════════════════════════════════════════════════════════════════
   QtSettingsWidget
   ═══════════════════════════════════════════════════════════════════ */

QtSettingsWidget::QtSettingsWidget(QWidget *parent) : QtAppWindow("Settings", parent) {
    setMinimumSize(640, 440); resize(900, 640);
    m_wallpaperPresets = {
        {"Big Sur", QColor(0x08,0x08,0x18), QColor(0x06,0x06,0x10)},
        {"Deep Space", QColor(0x1A,0x1A,0x2E), QColor(0x0A,0x0A,0x14)},
        {"Ocean Night", QColor(0x0F,0x17,0x2A), QColor(0x05,0x0A,0x14)},
        {"Purple Haze", QColor(0x2A,0x1A,0x2E), QColor(0x14,0x0A,0x1A)},
        {"Forest", QColor(0x0A,0x1A,0x0F), QColor(0x05,0x0F,0x0A)},
        {"Sunset", QColor(0x2E,0x1A,0x0A), QColor(0x1A,0x0A,0x05)},
        {"Arctic", QColor(0x1A,0x2A,0x2E), QColor(0x0A,0x14,0x1A)},
    };
    m_selectedWallpaper = 0;
}

void QtSettingsWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0, QColor(20,20,22,120)); bg.setColorAt(1, QColor(15,15,17,130));
    p.setBrush(bg); p.setPen(QPen(glass_border(80),1)); p.drawRoundedRect(r.adjusted(1,1,-1,-1), 6, 6);

    QStringList secs = {"General","Appearance","Desktop","Dock & Menu Bar","Keyboard","Display","Sound","Privacy","Network"};
    int sbW = 200;
    QRect sb(r.x()+2, r.y()+2, sbW, r.height()-4);
    QLinearGradient sbg(sb.topLeft(), sb.bottomLeft());
    sbg.setColorAt(0, QColor(25,25,27,220)); sbg.setColorAt(1, QColor(18,18,20,200));
    p.setBrush(sbg); p.setPen(Qt::NoPen); p.drawRoundedRect(sb, 8, 8);
    p.setBrush(glass_highlight(12)); p.drawRect(sb.x(), sb.y(), 1, sb.height());

    QFont f = font(); f.setPointSize(13); p.setFont(f); QFontMetrics fm(f);
    int yOff = sb.top()+16; m_sectionRects.clear();
    for (int i = 0; i < secs.size(); i++) {
        QRect sr(sb.x()+8, yOff, sbW-16, 36); m_sectionRects.append(sr);
        if (i == m_selectedSection) {
            p.setBrush(QColor(0xFF,0x5A,0x36,80)); p.setPen(Qt::NoPen); p.drawRoundedRect(sr, 8, 8);
            p.setBrush(glass_highlight(20)); p.drawRect(sr.x(), sr.y(), sr.width(), 1);
            p.setPen(c_text);
        } else p.setPen(c_subtext);
        p.drawText(sr.adjusted(12,0,0,0), Qt::AlignVCenter|Qt::AlignLeft, secs[i]);
        yOff += 42;
    }

    QRect ct(sb.right()+4, r.y()+4, r.width()-sbW-6, r.height()-8);
    QLinearGradient ctbg(ct.topLeft(), ct.bottomLeft());
    ctbg.setColorAt(0, QColor(25,25,27,200)); ctbg.setColorAt(1, QColor(18,18,20,180));
    p.setBrush(ctbg); p.setPen(QPen(glass_border(60),1)); p.drawRoundedRect(ct, 8, 8);
    p.setBrush(glass_highlight(10)); p.setPen(Qt::NoPen);
    p.drawRect(ct.x()+1, ct.y()+1, ct.width()-2, 1);

    f.setPointSize(16); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(ct.adjusted(20,16,-20,0), Qt::AlignTop|Qt::AlignLeft, secs[m_selectedSection]);
    f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);

    if (m_selectedSection == 1) {
        /* Appearance section — wallpaper picker + dark/light mode */
        p.drawText(ct.adjusted(20,48,-20,0), Qt::AlignTop|Qt::AlignLeft, "Wallpaper & Theme");

        /* Wallpaper presets grid */
        int pw = 100, ph = 70, gap = 12;
        int cols = qMin(5, m_wallpaperPresets.size());
        int gx = ct.x()+20, gy = ct.y()+80;
        m_wallpaperRects.clear();
        for (int i = 0; i < m_wallpaperPresets.size(); i++) {
            int row = i/cols, col = i%cols;
            QRect wr(gx+col*(pw+gap), gy+row*(ph+gap+16), pw, ph);
            m_wallpaperRects.append(wr);

            /* Mini preview gradient */
            QLinearGradient pg(wr.topLeft(), wr.bottomLeft());
            pg.setColorAt(0, m_wallpaperPresets[i].top);
            pg.setColorAt(1, m_wallpaperPresets[i].bottom);
            p.setBrush(pg);
            p.setPen(i==m_selectedWallpaper ? QPen(c_accent,2) : QPen(glass_border(80),1));
            p.drawRoundedRect(wr, 6, 6);

            /* Label */
            f.setPointSize(8); p.setFont(f);
            p.setPen(c_text);
            p.drawText(QRect(wr.x(), wr.bottom()+2, pw, 14), Qt::AlignHCenter|Qt::AlignTop,
                       m_wallpaperPresets[i].name);
        }

        /* Dark/Light mode toggle */
        int ty = gy + ((m_wallpaperPresets.size()+cols-1)/cols)*(ph+gap+16) + 20;
        f.setPointSize(12); f.setBold(true); p.setFont(f); p.setPen(c_text);
        p.drawText(ct.adjusted(20,ty,-20,0), Qt::AlignTop|Qt::AlignLeft, "Appearance Mode");

        m_darkModeRect = QRect(ct.x()+20, ty+28, 120, 32);
        m_lightModeRect = QRect(ct.x()+148, ty+28, 120, 32);

        /* Dark button — primary when dark mode is selected */
        drawGlassButton(p, m_darkModeRect, "Dark",
                        m_hoveredBtn == 0 ? BtnHover : BtnNormal, m_darkMode);

        /* Light button — primary when light mode is selected */
        drawGlassButton(p, m_lightModeRect, "Light",
                        m_hoveredBtn == 1 ? BtnHover : BtnNormal, !m_darkMode);
    } else if (m_selectedSection == 0) {
        /* ── General: system overview ── */
        f.setPointSize(12); f.setBold(true); p.setFont(f); p.setPen(c_text);
        p.drawText(ct.adjusted(20,52,-20,0), Qt::AlignTop|Qt::AlignLeft, "System");

        auto row = [&](int y, const QString &k, const QString &v) {
            f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
            p.drawText(ct.adjusted(24,y,-160,0), Qt::AlignTop|Qt::AlignLeft, k);
            p.setPen(c_text);
            p.drawText(ct.adjusted(180,y,-20,0), Qt::AlignTop|Qt::AlignRight, v);
        };
        uint64_t upS = timer_get_milliseconds() / 1000;
        row(84,  "Version",  QString("CodeOS %1").arg(KERNEL_VERSION));
        row(112, "Kernel",   KERNEL_UNAME);
        row(140, "Desktop",  "Qt6 Liquid Glass");
        row(168, "Display",  QString("%1 x %2").arg(fb_getwidth()).arg(fb_getheight()));
        row(196, "Uptime",
            QString("%1h %2m").arg(upS/3600).arg((upS%3600)/60));
    } else if (m_selectedSection == 4) {
        /* ── Network ── */
        bool up = net_ready() != 0;
        uint32_t ip = net_get_ip(), gw = net_get_gateway(), dns = net_get_dns();
        f.setPointSize(12); f.setBold(true); p.setFont(f); p.setPen(c_text);
        p.drawText(ct.adjusted(20,52,-20,0), Qt::AlignTop|Qt::AlignLeft,
                   up ? "Network — Connected" : "Network — Offline");
        if (up && ip) {
            auto ipStr = [](uint32_t a) {
                return QString("%1.%2.%3.%4")
                    .arg(a & 0xFF).arg((a >> 8) & 0xFF)
                    .arg((a >> 16) & 0xFF).arg((a >> 24) & 0xFF);
            };
            auto row = [&](int y, const QString &k, const QString &v) {
                f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
                p.drawText(ct.adjusted(24,y,-160,0), Qt::AlignTop|Qt::AlignLeft, k);
                p.setPen(c_text);
                p.drawText(ct.adjusted(180,y,-20,0), Qt::AlignTop|Qt::AlignRight, v);
            };
            row(88,  "IP address",   ipStr(ip));
            row(116, "Router",       gw ? ipStr(gw) : "—");
            row(144, "DNS",          dns ? ipStr(dns) : "—");
            row(172, "Interface",    "e1000 / rtl8139");
        } else {
            f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
            p.drawText(ct.adjusted(24,90,-20,0), Qt::AlignTop|Qt::AlignLeft,
                       "No network link detected.");
        }
    } else {
        /* Sections without dedicated UI yet get an honest placeholder */
        f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
        p.drawText(ct.adjusted(24,90,-20,0), Qt::AlignTop|Qt::AlignLeft,
                   "Nothing to configure in this build.");

        /* Version info kept on the fallback page too */
        p.setPen(c_text);
        p.drawText(ct.adjusted(20, 130, -20, 0), Qt::AlignTop|Qt::AlignLeft,
                   QString("CodeOS %1 — Qt6 Liquid Glass").arg(KERNEL_VERSION));
    }
}

void QtSettingsWidget::mousePressEvent(QMouseEvent *e) {
    for (int i = 0; i < m_sectionRects.size(); i++)
        if (m_sectionRects[i].contains(e->pos())) { m_selectedSection = i; update(); return; }

    if (m_selectedSection == 1) {
        /* Wallpaper picker clicks */
        for (int i = 0; i < m_wallpaperRects.size(); i++) {
            if (m_wallpaperRects[i].contains(e->pos())) {
                m_selectedWallpaper = i;
                QtDesktopManager *mgr = QtDesktopManager::instance();
                if (mgr && mgr->desktop()) {
                    if (i == 0) {
                        /* "Big Sur" — restore default orb wallpaper */
                        mgr->desktop()->setWallpaper(QImage());
                    } else {
                        mgr->desktop()->setGradientWallpaper(
                            m_wallpaperPresets[i].top, m_wallpaperPresets[i].bottom);
                    }
                }
                update(); return;
            }
        }
        /* Dark/Light mode toggle */
        if (m_darkModeRect.contains(e->pos())) {
            m_darkMode = true;
            QtDesktopManager::instance()->setThemeTarget(false);
            QtDesktopManager::instance()->showToast("Dark Mode", QColor(0xFF,0x5A,0x36));
            update(); return;
        }
        if (m_lightModeRect.contains(e->pos())) {
            m_darkMode = false;
            QtDesktopManager::instance()->setThemeTarget(true);
            QtDesktopManager::instance()->showToast("Light Mode", QColor(0xFF,0x5A,0x36));
            update(); return;
        }
    }
    QtAppWindow::mousePressEvent(e);
}

void QtSettingsWidget::mouseMoveEvent(QMouseEvent *e) {
    int hov = -1;
    if (m_selectedSection == 1) {
        if (m_darkModeRect.contains(e->pos())) hov = 0;
        else if (m_lightModeRect.contains(e->pos())) hov = 1;
    }
    if (hov != m_hoveredBtn) { m_hoveredBtn = hov; update(); }
    QtAppWindow::mouseMoveEvent(e);
}

void QtSettingsWidget::leaveEvent(QEvent *event) {
    if (m_hoveredBtn != -1) { m_hoveredBtn = -1; update(); }
    QWidget::leaveEvent(event);
}

