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
   QtCalcWidget
   ═══════════════════════════════════════════════════════════════════ */

QtCalcWidget::QtCalcWidget(QWidget *parent) : QtAppWindow("Calculator", parent) {
    setMinimumSize(300, 440); resize(360, 520);
    setMouseTracking(true);
    m_btnLabels[0]="C";  m_btnLabels[1]="+/-"; m_btnLabels[2]="%";  m_btnLabels[3]="/";
    m_btnLabels[4]="7";  m_btnLabels[5]="8";   m_btnLabels[6]="9";  m_btnLabels[7]="*";
    m_btnLabels[8]="4";  m_btnLabels[9]="5";   m_btnLabels[10]="6"; m_btnLabels[11]="-";
    m_btnLabels[12]="1"; m_btnLabels[13]="2";  m_btnLabels[14]="3"; m_btnLabels[15]="+";
    m_btnLabels[16]="0"; m_btnLabels[17]=".";  m_btnLabels[18]="("; m_btnLabels[19]="=";
}

void QtCalcWidget::pressButton(int idx) {
    QString lbl = m_btnLabels[idx];
    if (lbl=="C") { m_display.clear(); m_current=0; m_op=0; m_newNumber=true; }
    else if (lbl=="+/-") {
        if (!m_display.isEmpty()) {
            if (m_display.startsWith('-')) m_display.remove(0,1);
            else m_display.prepend('-');
        }
    }
    else if (lbl=="%") {
        if (!m_display.isEmpty()) { m_display = QString::number(m_display.toDouble() / 100.0, 'f', 6);
            while (m_display.endsWith('0')) m_display.chop(1);
            if (m_display.endsWith('.')) m_display.chop(1);
        }
    }
    else if (lbl=="=") {
        if (m_op && !m_display.isEmpty()) {
            double v = m_display.toDouble();
            if (m_op=='+') m_current+=v; else if (m_op=='-') m_current-=v;
            else if (m_op=='*') m_current*=v; else if (m_op=='/' && v!=0.0) m_current/=v;
            m_op=0; m_display = QString::number(m_current,'f',6);
            while (m_display.endsWith('0')) m_display.chop(1);
            if (m_display.endsWith('.')) m_display.chop(1);
        }
        m_newNumber = true;
    } else if (lbl=="+"||lbl=="-"||lbl=="*"||lbl=="/") {
        if (!m_display.isEmpty()) { m_current=m_display.toDouble(); m_op=lbl[0].toLatin1(); m_newNumber=true; }
    } else if (lbl==".") {
        if (m_newNumber) { m_display="0."; m_newNumber=false; }
        else if (!m_display.contains('.')) m_display += ".";
    } else {
        m_display = m_newNumber ? lbl : m_display+lbl;
        m_newNumber = false;
    }
    update();
}

void QtCalcWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0, QColor(20,20,22,120)); bg.setColorAt(1, QColor(15,15,17,130));
    p.setBrush(bg); p.setPen(QPen(glass_border(80),1)); p.drawRoundedRect(r.adjusted(1,1,-1,-1), 6, 6);

    int bw = (r.width()-28)/4, bh = qMax(42, (r.height()-170)/5);

    /* Display */
    QRect dr(r.left()+8, r.top()+16, r.width()-16, 80);
    QLinearGradient dg(dr.topLeft(), dr.bottomLeft());
    dg.setColorAt(0, QColor(25,25,27,200)); dg.setColorAt(1, QColor(18,18,20,220));
    p.setBrush(dg); p.setPen(QPen(glass_border(60),1)); p.drawRoundedRect(dr, 8, 8);

    QFont f = font(); f.setPointSize(32); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(dr.adjusted(12,0,-12,0), Qt::AlignRight|Qt::AlignVCenter, m_display.isEmpty()?"0":m_display);

    int sy = r.top()+110;
    for (int i = 0; i < 20; i++) {
        int row = i/4, col = i%4;
        QRect br(r.left()+8+col*(bw+6), sy+row*(bh+6), bw, bh);
        m_btnRects[i] = br;
        QString lbl = m_btnLabels[i];
        bool primary = (lbl=="/"||lbl=="*"||lbl=="-"||lbl=="+"||lbl=="=");
        ButtonState st = BtnNormal;
        if (i == m_pressedBtn) st = BtnPressed;
        else if (i == m_hoveredBtn) st = BtnHover;
        else if (lbl=="C"||lbl=="+/-"||lbl=="%") st = BtnDisabled;
        drawGlassButton(p, br, lbl, st, primary, lbl.length()<=1 ? 20 : 16);
    }
}

void QtCalcWidget::mousePressEvent(QMouseEvent *e) {
    for (int i = 0; i < 20; i++)
        if (m_btnRects[i].contains(e->pos())) {
            m_pressedBtn = i;
            update();
            return;
        }
    QtAppWindow::mousePressEvent(e);
}

void QtCalcWidget::mouseReleaseEvent(QMouseEvent *e) {
    int idx = -1;
    for (int i = 0; i < 20; i++)
        if (m_btnRects[i].contains(e->pos())) { idx = i; break; }
    bool same = (idx >= 0 && idx == m_pressedBtn);
    m_pressedBtn = -1;
    if (same) pressButton(idx);
    update();
}

void QtCalcWidget::mouseMoveEvent(QMouseEvent *e) {
    int idx = -1;
    for (int i = 0; i < 20; i++)
        if (m_btnRects[i].contains(e->pos())) { idx = i; break; }
    if (idx != m_hoveredBtn) { m_hoveredBtn = idx; update(); }
}

void QtCalcWidget::leaveEvent(QEvent *) {
    m_hoveredBtn = -1;
    update();
}

void QtCalcWidget::keyPressEvent(QKeyEvent *e) {
    QString txt = e->text();
    /* Match button labels case-insensitively */
    for (int i = 0; i < 20; i++)
        if (m_btnLabels[i].compare(txt, Qt::CaseInsensitive) == 0) { pressButton(i); return; }
    if (e->key()==Qt::Key_Return) pressButton(19);
    else if (e->key()==Qt::Key_Backspace && !m_display.isEmpty()) { m_display.chop(1); update(); }
}

