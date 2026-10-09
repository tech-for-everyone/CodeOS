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
   QtAboutWidget
   ═══════════════════════════════════════════════════════════════════ */

QtAboutWidget::QtAboutWidget(QWidget *parent) : QtAppWindow("About CodeOS", parent) {
    resize(460, 420);
}

void QtAboutWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    int cx = r.center().x();

    /* ── Large app icon ── */
    QRect iconRect(cx-48, r.y()+24, 96, 96);
    drawAppIcon(p, iconRect, "About", 96);

    /* ── Title ── */
    QFont f = font(); f.setPointSize(28); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(QRect(r.x(), r.y()+136, r.width(), 40), Qt::AlignHCenter, "CodeOS");

    /* ── Subtitle ── */
    f.setPointSize(12); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(QRect(r.x(), r.y()+178, r.width(), 24), Qt::AlignHCenter, "A hobby operating system");

    /* ── Glass info card ── */
    QRect card(r.x()+40, r.y()+216, r.width()-80, 130);
    QLinearGradient cardBg(card.topLeft(), card.bottomLeft());
    cardBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120));
    cardBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(cardBg);
    p.drawRoundedRect(card, 12, 12);

    /* Accent line at top of card */
    QLinearGradient accentLine(card.topLeft(), card.topRight());
    accentLine.setColorAt(0.0, QColor(0xFF,0x5A,0x36,180));
    accentLine.setColorAt(1.0, QColor(0x30,0xD1,0x58,180));
    p.setPen(Qt::NoPen); p.setBrush(accentLine);
    p.drawRoundedRect(QRect(card.x()+1, card.y(), card.width()-2, 3), 1, 1);

    /* Card content */
    f.setPointSize(11); f.setBold(false); p.setFont(f);
    int y = card.y() + 20;
    auto cardLine = [&](const QString &k, const QString &v) {
        p.setPen(c_subtext); p.drawText(card.left()+20, y, k);
        p.setPen(c_text); p.drawText(card.left()+card.width()/2, y, v);
        y += 22;
    };
    cardLine("Version:", "1.0");
    cardLine("Kernel:", "codeos-1-kernel");
    cardLine("Desktop:", "Qt6 Liquid Glass");
    cardLine("CPU:", QString("%1 cores").arg(sched_thread_count()));
    cardLine("Display:", QString("%1x%2").arg(fb_getwidth()).arg(fb_getheight()));

    /* ── Footer link ── */
    f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_accent);
    p.drawText(QRect(r.x(), r.y()+370, r.width(), 24), Qt::AlignHCenter, "codeos.dev");
}

