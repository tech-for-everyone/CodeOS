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
#include <vector>
}

#define LT_VM_CMD_CONSOLE_WRITE  10
#define LT_VM_CMD_CONSOLE_READ   11
#define LT_VM_CMD_STDIN_WRITE    12
#define LT_VM_CMD_STDIN_READ     13
#define LT_VM_CMD_CONSOLE_ENABLE 14
#define LT_VM_CMD_CONTAINER_START 15
#define LT_VM_CMD_CONTAINER_STOP  16
#define LT_SYSCALL_VM            52
#define LT_SYSCALL_CONTAINER_CREATE 23

extern "C" {
static inline int lt_sys_vm4(int cmd, unsigned long a2, unsigned long a3, unsigned long a4) {
    int ret;
    register unsigned long _a4 asm("r10") = a4;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(LT_SYSCALL_VM), "D"(cmd), "S"(a2), "d"(a3), "r"(_a4) : "memory");
    return ret;
}
static inline int lt_sys_vm3(int cmd, unsigned long a2, unsigned long a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(LT_SYSCALL_VM), "D"(cmd), "S"(a2), "d"(a3) : "memory");
    return ret;
}
static inline int lt_container_create(const char *name, const char *image) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(LT_SYSCALL_CONTAINER_CREATE), "D"(name), "S"(image) : "memory");
    return ret;
}
}

/* ═══════════════════════════════════════════════════════════════════
   QtLTWidget — Debian container console terminal
   ═══════════════════════════════════════════════════════════════════ */

QtLTWidget::QtLTWidget(QWidget *parent) : QtAppWindow("LT", parent) {
    setAppTitle("Linux Terminal — Debian Container");
    setFocusPolicy(Qt::StrongFocus);
    setFocus();

    m_lines.append("CodeOS Linux Terminal (LT)");
    m_lines.append("Launching Debian container...");

    /* Create the container */
    m_containerId = lt_container_create("lt-session", "debian-minimal");
    if (m_containerId < 0) {
        m_lines.append("");
        m_lines.append("[error] Failed to create container.");
        m_lines.append("Debian rootfs may not be installed.");
        m_lines.append("Use DevStore to download debian-minimal.");
        m_containerReady = false;
    } else {
        /* Launch with console bridge (container_launch_console enables console I/O internally) */
        int child_pid = lt_sys_vm3(LT_VM_CMD_CONTAINER_START, (unsigned long)m_containerId, 0);
        if (child_pid < 0) {
            m_lines.append("");
            m_lines.append("[error] Failed to start container.");
            m_lines.append("No entrypoint found (missing /bin/sh?)");
            m_lines.append("Debian rootfs may need extraction.");
            m_containerReady = false;
        } else {
            m_containerReady = true;
            m_lines.append(QString("Container started (pid=%1)").arg(child_pid));
            m_lines.append("Type commands below.");
            m_lines.append("");
        }
    }

    m_lines.append("");
    m_cursorCol = 0;
    scrollToBottom();

    /* Poll timer for console output */
    connect(&m_pollTimer, &QTimer::timeout, this, &QtLTWidget::pollConsole);
    m_pollTimer.start(60);
}

QtLTWidget::~QtLTWidget() {
    m_pollTimer.stop();
    if (m_containerId >= 0) {
        lt_sys_vm3(LT_VM_CMD_CONTAINER_STOP, (unsigned long)m_containerId, 0);
    }
}

void QtLTWidget::pollConsole() {
    if (!m_containerReady || m_containerId < 0) return;

    /* Read up to 4KB from container stdout */
    char buf[4096];
    int n = lt_sys_vm4(LT_VM_CMD_CONSOLE_READ, (unsigned long)m_containerId,
                       (unsigned long)(uintptr_t)buf, 4096);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n') {
                m_lines.append(m_currentLine);
                m_currentLine.clear();
                m_cursorCol = 0;
            } else if (ch == '\r') {
                m_cursorCol = 0;
            } else if (ch == '\b') {
                if (!m_currentLine.isEmpty()) {
                    m_currentLine.chop(1);
                    if (m_cursorCol > 0) m_cursorCol--;
                }
            } else if (ch >= 32) {
                m_currentLine.append(ch);
                m_cursorCol++;
            }
        }
        scrollToBottom();
        update();
    }
}

void QtLTWidget::scrollToBottom() {
    QFontMetrics fm(m_font);
    int lineH = fm.height() + 2;
    int contentH = m_lines.size() * lineH + 40;
    int widgetH = height() - 80;
    if (contentH > widgetH)
        m_scrollOffset = contentH - widgetH;
    else
        m_scrollOffset = 0;
}

void QtLTWidget::paintContent(QPainter &p, const QRect &r) {
    /* Terminal background */
    p.fillRect(r, QColor(0x1A, 0x1A, 0x2E));

    p.setClipRect(r);
    QFontMetrics fm(m_font);
    int lineH = fm.height() + 2;
    int y = r.y() + 10 - m_scrollOffset;

    /* Draw lines */
    for (int i = 0; i < m_lines.size(); i++) {
        if (y + lineH < r.y() - lineH) { y += lineH; continue; }
        if (y > r.bottom() + lineH) break;

        const QString &line = m_lines[i];
        if (line.startsWith("[error]"))
            p.setPen(QColor(0xFF, 0x45, 0x3A));
        else if (line.startsWith("CodeOS"))
            p.setPen(QColor(0x64, 0xD2, 0xFF));
        else if (line.startsWith("Container"))
            p.setPen(QColor(0x30, 0xD1, 0x58));
        else
            p.setPen(QColor(0xF5, 0xF5, 0xF7));

        p.setFont(m_font);
        p.drawText(r.x() + 10, y + fm.ascent(), line);
        y += lineH;
    }

    /* Draw current input line with cursor */
    if (y >= r.y() - lineH && y <= r.bottom() + lineH) {
        p.setPen(QColor(0xF5, 0xF5, 0xF7));
        p.setFont(m_font);

        /* Draw prompt */
        QString prompt = "root@lt:~# ";
        p.drawText(r.x() + 10, y + fm.ascent(), prompt);

        /* Draw input text */
        int promptW = fm.horizontalAdvance(prompt);
        p.drawText(r.x() + 10 + promptW, y + fm.ascent(), m_currentLine);

        /* Draw cursor block */
        int cursorX = r.x() + 10 + promptW + fm.horizontalAdvance(m_currentLine.left(m_cursorCol));
        p.fillRect(cursorX, y, fm.averageCharWidth(), lineH, QColor(0xF5, 0xF5, 0xF7, 180));
    }

    p.setClipping(false);
}

void QtLTWidget::keyPressEvent(QKeyEvent *e) {
    QtAppWindow::keyPressEvent(e);
    if (e->text().isEmpty()) return;

    if (!m_containerReady) return;

    QString text = e->text();
    QChar ch = text.at(0);

    if (ch == '\n' || ch == '\r') {
        /* Send the line to the container */
        QString cmd = m_currentLine;
        m_lines.append("root@lt:~# " + cmd);
        m_currentLine.clear();
        m_cursorCol = 0;

        /* Send command + newline to stdin */
        QByteArray data = cmd.toUtf8() + '\n';
        lt_sys_vm4(LT_VM_CMD_STDIN_WRITE, (unsigned long)m_containerId,
                   (unsigned long)(uintptr_t)data.constData(), data.size());
        scrollToBottom();
    } else if (ch == '\b') {
        if (!m_currentLine.isEmpty()) {
            m_currentLine.chop(1);
            if (m_cursorCol > 0) m_cursorCol--;
        }
    } else if (ch.unicode() >= 32) {
        m_currentLine.append(ch);
        m_cursorCol++;
    }

    update();
}

void QtLTWidget::focusInEvent(QFocusEvent *e) { QWidget::focusInEvent(e); update(); }
void QtLTWidget::focusOutEvent(QFocusEvent *e) { QWidget::focusOutEvent(e); update(); }

