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
   QtAppHostWidget
   ═══════════════════════════════════════════════════════════════════ */

QtAppHostWidget::QtAppHostWidget(QWidget *parent) : QtAppWindow("Hosted App", parent) {
    setMinimumSize(400, 240); resize(720, 480); setFocusPolicy(Qt::StrongFocus);
    m_timer.setInterval(80);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!apphost_active()) { if (apphost_exited()) { m_timer.stop(); close(); } return; }
        uint8_t buf[4096]; int n = apphost_drain(buf, sizeof(buf));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (buf[i]=='\n'||buf[i]=='\r') {
                    if (!m_lineBuf.isEmpty()) kprintf("[host] %s\n", m_lineBuf.toUtf8().constData());
                    m_lines.append(m_lineBuf); m_lineBuf.clear(); if (m_lines.size()>500) m_lines.removeFirst();
                }
                else if (buf[i]==0xFF) { kprintf("[host] <<APP EXIT>>\n"); m_lines.append("<<APP EXIT>>"); m_lineBuf.clear(); }
                else m_lineBuf += QChar((ushort)buf[i]);
            }
            update();
        }
        if (apphost_exited()) { m_timer.stop(); close(); }
    });
    m_timer.start();
}

void QtAppHostWidget::keyPressEvent(QKeyEvent *e) {
    int key = e->key();
    if (key==Qt::Key_Backspace) { uint8_t b='\b'; apphost_write_in(&b,1); }
    else if (key==Qt::Key_Return||key==Qt::Key_Enter) { uint8_t b='\n'; apphost_write_in(&b,1); }
    else if (key>=Qt::Key_Space && key<=Qt::Key_AsciiTilde) {
        /* The CodeOS platform reports printable keys by ASCII key code and may
         * leave text() empty; fall back to the key code so typing still works. */
        QChar ch = e->text().isEmpty() ? QChar((ushort)key) : e->text()[0];
        uint8_t b = (uint8_t)ch.toLatin1();
        if (b) apphost_write_in(&b,1);
    }
    else if (key==Qt::Key_Escape) apphost_kill();
}

void QtAppHostWidget::paintContent(QPainter &p, const QRect &r) {
    p.fillRect(r, QColor(0x0A,0x0A,0x0C));
    QFont f = m_font; f.setPointSize(10); p.setFont(f);
    QFontMetrics fm(f); int lineH = fm.height()+1, maxLines = r.height()/lineH;
    int yOff = r.top()+lineH, n = m_lines.size(), start = n > maxLines-1 ? n-maxLines+1 : 0;
    for (int i = start; i < n; i++) {
        p.setPen(QColor(0xCD,0xD6,0xF4));
        p.drawText(r.left()+6, yOff, m_lines[i]); yOff += lineH;
    }
    if (!m_lineBuf.isEmpty()) { p.setPen(c_text); p.drawText(r.left()+6, yOff, m_lineBuf+"_"); }
}

