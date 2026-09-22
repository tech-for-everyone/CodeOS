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
   QtExitWidget
   ═══════════════════════════════════════════════════════════════════ */

QtExitWidget::QtExitWidget(QWidget *parent) : QtAppWindow("Exit", parent) { resize(360, 170); }

void QtExitWidget::paintContent(QPainter &p, const QRect &r) {
    QFont f = font(); f.setPointSize(14); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(QRect(r.x(), r.y()+20, r.width(), 30), Qt::AlignHCenter, "Shut Down CodeOS?");
    f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(QRect(r.x(), r.y()+55, r.width(), 24), Qt::AlignHCenter, "Any unsaved work will be lost.");

    int bw = 120, bh = 36, by = r.bottom()-50;
    m_stopRect = QRect(r.center().x()-bw-10, by, bw, bh);
    m_cancelRect = QRect(r.center().x()+10, by, bw, bh);

    QFont bf = f; bf.setBold(true); p.setFont(bf);
    drawGlassButton(p, m_stopRect, "Shut Down",
                    m_hoveredBtn == 0 ? BtnHover : BtnNormal, true);
    drawGlassButton(p, m_cancelRect, "Cancel",
                    m_hoveredBtn == 1 ? BtnHover : BtnNormal, false);
}

void QtExitWidget::mousePressEvent(QMouseEvent *e) {
    if (m_stopRect.contains(e->pos())) {
        outb(0x64, 0xFE);
        asm volatile("cli; hlt");
    }
    if (m_cancelRect.contains(e->pos())) emit closeRequested();
    QtAppWindow::mousePressEvent(e);
}

void QtExitWidget::mouseMoveEvent(QMouseEvent *e) {
    int old = m_hoveredBtn;
    m_hoveredBtn = -1;
    if (m_stopRect.contains(e->pos())) m_hoveredBtn = 0;
    else if (m_cancelRect.contains(e->pos())) m_hoveredBtn = 1;
    if (old != m_hoveredBtn) update();
    QtAppWindow::mouseMoveEvent(e);
}

void QtExitWidget::leaveEvent(QEvent *) {
    if (m_hoveredBtn != -1) { m_hoveredBtn = -1; update(); }
}

