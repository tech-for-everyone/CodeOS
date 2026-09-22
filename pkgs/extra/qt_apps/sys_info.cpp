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

/* ═══════════════════════════════════════════════════════════════════
   QtSysInfoWidget
   ═══════════════════════════════════════════════════════════════════ */

QtSysInfoWidget::QtSysInfoWidget(QWidget *parent) : QtAppWindow("System Info", parent) {
    resize(540, 400);
}

void QtSysInfoWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Title ── */
    f.setPointSize(16); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(r.left()+20, r.top()+28, "System Info");

    /* ── Helper: draw a glass card with accent ── */
    auto drawCard = [&](const QRect &cr, const QColor &accent) {
        QLinearGradient bg(cr.topLeft(), cr.bottomLeft());
        bg.setColorAt(0.0, QColor(0x23,0x23,0x25,120));
        bg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
        p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(bg);
        p.drawRoundedRect(cr, 10, 10);
        /* Accent bar at top */
        p.setPen(Qt::NoPen); p.setBrush(accent);
        p.drawRoundedRect(QRect(cr.x()+1, cr.y(), cr.width()-2, 3), 1, 1);
    };

    /* ── Three cards side by side ── */
    int cardW = (r.width()-60)/3, cardH = 160;
    int cardY = r.top()+52;
    QRect sysCard(r.left()+16, cardY, cardW, cardH);
    QRect hwCard(r.left()+16+cardW+14, cardY, cardW, cardH);
    QRect swCard(r.left()+16+2*(cardW+14), cardY, cardW, cardH);

    drawCard(sysCard, QColor(0xFF,0x5A,0x36,180));
    drawCard(hwCard, QColor(0x30,0xD1,0x58,180));
    drawCard(swCard, QColor(0xBF,0x5A,0xF2,180));

    /* ── System card ── */
    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(sysCard.left()+16, sysCard.y()+22, "System");
    f.setPointSize(10); f.setBold(false); p.setFont(f);
    int sy = sysCard.y()+44;
    auto sLine = [&](const QString &k, const QString &v) {
        p.setPen(c_subtext); p.drawText(sysCard.left()+16, sy, k);
        p.setPen(c_text); p.drawText(sysCard.left()+sysCard.width()/2+8, sy, v);
        sy += 22;
    };
    sLine("OS:", "CodeOS 1.0");
    sLine("Kernel:", "codeos-1-kernel");
    sLine("Desktop:", "Qt6 + Eclipse");

    /* ── Hardware card ── */
    f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(hwCard.left()+16, hwCard.y()+22, "Hardware");
    f.setBold(false); p.setFont(f);
    sy = hwCard.y()+44;
    {
        auto hwLine = [&](const QString &k, const QString &v) {
            p.setPen(c_subtext); p.drawText(hwCard.left()+16, sy, k);
            p.setPen(c_text); p.drawText(hwCard.left()+hwCard.width()/2+8, sy, v);
            sy += 22;
        };
        hwLine("CPU:", QString("%1 cores").arg(sched_thread_count()));
        hwLine("Display:", QString("%1x%2").arg(fb_getwidth()).arg(fb_getheight()));
        hwLine("Memory:", "512 MB");
    }

    /* ── Software card ── */
    f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(swCard.left()+16, swCard.y()+22, "Software");
    f.setBold(false); p.setFont(f);
    sy = swCard.y()+44;
    {
        auto swLine = [&](const QString &k, const QString &v) {
            p.setPen(c_subtext); p.drawText(swCard.left()+16, sy, k);
            p.setPen(c_text); p.drawText(swCard.left()+swCard.width()/2+8, sy, v);
            sy += 22;
        };
        swLine("Jengine:", "Active");
        swLine("WiFi:", "Available");
        swLine("Containers:", "Enabled");
    }

    /* ── Memory usage bar ── */
    int barY = cardY + cardH + 24;
    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(r.left()+20, barY, "Memory Usage");
    barY += 16;

    QRect barBg(r.left()+20, barY, r.width()-40, 18);
    p.setPen(QPen(QColor(0x48,0x48,0x4A,120),1)); p.setBrush(QColor(0x1C,0x1C,0x1E));
    p.drawRoundedRect(barBg, 9, 9);
    int pct = 62; /* demo value */
    QRect barFill(barBg.x()+2, barBg.y()+2, (barBg.width()-4)*pct/100, barBg.height()-4);
    QLinearGradient barGrad(barFill.topLeft(), barFill.topRight());
    barGrad.setColorAt(0.0, QColor(0xFF,0x5A,0x36));
    barGrad.setColorAt(1.0, QColor(0x30,0xD1,0x58));
    p.setPen(Qt::NoPen); p.setBrush(barGrad);
    p.drawRoundedRect(barFill, 7, 7);
    f.setPointSize(8); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(barBg, Qt::AlignCenter, QString("%1% used — 317 MB / 512 MB").arg(pct));
}

