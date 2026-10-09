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
   QtTerminalWidget
   ═══════════════════════════════════════════════════════════════════ */

QtTerminalWidget::QtTerminalWidget(QWidget *parent) : QtAppWindow("Terminal", parent) {
    m_lines << "CodeOS Terminal v0.2" << "Type a command and press Enter" << "";
    setMinimumSize(480, 300); resize(800, 500); setFocusPolicy(Qt::StrongFocus);
}

void QtTerminalWidget::appendOutput(const QString &text) {
    m_lines.append(text); if (m_lines.size()>500) m_lines.removeFirst(); update();
}

void QtTerminalWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0, QColor(20,20,22,120)); bg.setColorAt(1, QColor(15,15,17,130));
    p.setBrush(bg); p.setPen(QPen(glass_border(80),1)); p.drawRoundedRect(r.adjusted(1,1,-1,-1), 6, 6);

    QFont f = m_font; f.setPointSize(12); f.setStyleHint(QFont::Monospace); p.setFont(f);
    QFontMetrics fm(f);
    int lineH = fm.height()+2, maxLines = (r.height()-16)/lineH;
    int n = m_lines.size();
    int start = n > maxLines ? n - maxLines - m_scrollOffset : 0;
    if (start < 0) { m_scrollOffset += start; start = 0; }
    int y = r.top()+10;
    for (int i = start; i < n; i++) {
        p.setPen(m_lines[i].startsWith("$ ") ? QColor(0x89,0xB4,0xFA) : QColor(0xF5,0xF5,0xF7));
        p.drawText(r.left()+12, y+fm.ascent(), m_lines[i]);
        y += lineH;
    }
    QString prompt = "$ " + m_inputLine;
    p.setPen(QColor(0x89,0xB4,0xFA));
    p.drawText(r.left()+12, y+fm.ascent(), prompt);

    m_blink = (m_blink + 1) % 60;
    if (m_blink < 30 && hasFocus()) {
        int pw = fm.horizontalAdvance(prompt.left(m_cursorPos+2));
        p.setPen(QColor(0xF5,0xF5,0xF7));
        p.drawText(r.left()+12+pw, y+fm.ascent(), "\xe2\x96\x88");
    }
}

void QtTerminalWidget::keyPressEvent(QKeyEvent *e) {
    int key = e->key();
    if (key==Qt::Key_Return || key==Qt::Key_Enter) {
        if (!m_inputLine.trimmed().isEmpty()) m_history.append(m_inputLine);
        m_historyPos = m_history.size();
        m_lines.append("$ " + m_inputLine); m_inputLine.clear(); m_cursorPos = 0;
    } else if (key==Qt::Key_Backspace && m_cursorPos > 0) {
        m_inputLine.remove(m_cursorPos-1, 1); m_cursorPos--;
    } else if (key==Qt::Key_Up && m_historyPos > 0) {
        m_historyPos--; m_inputLine = m_history[m_historyPos]; m_cursorPos = m_inputLine.length();
    } else if (key==Qt::Key_Down) {
        if (m_historyPos < m_history.size()-1) {
            m_historyPos++; m_inputLine = m_history[m_historyPos]; m_cursorPos = m_inputLine.length();
        } else { m_historyPos = m_history.size(); m_inputLine.clear(); m_cursorPos = 0; }
    } else if (key==Qt::Key_Left && m_cursorPos > 0) m_cursorPos--;
    else if (key==Qt::Key_Right && m_cursorPos < m_inputLine.length()) m_cursorPos++;
    else if (key==Qt::Key_C && e->modifiers() & Qt::ControlModifier) {
        appendOutput("^C"); m_inputLine.clear(); m_cursorPos = 0;
    } else if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde) {
        QString txt = e->text();
        if (!txt.isEmpty()) { m_inputLine.insert(m_cursorPos, txt[0]); m_cursorPos++; }
    }
    update();
}

void QtTerminalWidget::wheelEvent(QWheelEvent *e) {
    int delta = e->angleDelta().y();
    if (delta > 0) m_scrollOffset = qMax(0, m_scrollOffset - 3);
    else m_scrollOffset = qMin(qMax(0, m_lines.size() - 5), m_scrollOffset + 3);
    update();
}

