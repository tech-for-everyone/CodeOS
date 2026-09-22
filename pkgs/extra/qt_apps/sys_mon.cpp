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
uint64_t sched_busy_ticks(void);
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
int      net_ready(void);
}

/* ═══════════════════════════════════════════════════════════════════
   QtSysMonWidget
   ═══════════════════════════════════════════════════════════════════ */

QtSysMonWidget::QtSysMonWidget(QWidget *parent) : QtAppWindow("System Monitor", parent) {
    resize(640, 560);
    m_bootMs = timer_get_milliseconds();
    m_lastBusy = sched_busy_ticks();
    m_lastTotal = 0;
    connect(&m_timer, &QTimer::timeout, [this]() {
        uint64_t total = timer_get_milliseconds() / 10;   /* 10ms tick units */
        uint64_t busy  = sched_busy_ticks();
        int pct = (total > m_lastTotal)
                  ? (int)((busy - m_lastBusy) * 100 / (total - m_lastTotal)) : 0;
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        m_lastTotal = total; m_lastBusy = busy;
        m_cpuHist[m_cpuIdx] = pct;
        m_cpuPct = pct;
        m_cpuIdx = (m_cpuIdx+1) % 60;
        update();
    });
    m_timer.start(1000);
}

void QtSysMonWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Title ── */
    f.setPointSize(16); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(r.left()+20, r.top()+28, "System Monitor");

    /* ── CPU card ── */
    QRect cpuCard(r.left()+16, r.top()+50, r.width()-32, 150);
    QLinearGradient cpuBg(cpuCard.topLeft(), cpuCard.bottomLeft());
    cpuBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120));
    cpuBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(cpuBg);
    p.drawRoundedRect(cpuCard, 10, 10);

    /* CPU accent */
    p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36,180));
    p.drawRoundedRect(QRect(cpuCard.x()+1, cpuCard.y(), cpuCard.width()-2, 3), 1, 1);

    f.setPointSize(12); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(cpuCard.left()+16, cpuCard.y()+22,
               QString("CPU Usage — %1%").arg(m_cpuPct));

    /* Grid lines */
    QRect graph(cpuCard.left()+40, cpuCard.y()+36, cpuCard.width()-56, cpuCard.height()-52);
    p.setPen(QPen(QColor(0x48,0x48,0x4A,60),1,Qt::DotLine));
    for (int pct = 25; pct <= 75; pct += 25) {
        int gy = graph.bottom() - pct * graph.height() / 100;
        p.drawLine(graph.left(), gy, graph.right(), gy);
    }

    /* CPU bars with gradient */
    int barW = qMax(2, (graph.width()-4) / 60);
    for (int i = 0; i < 60; i++) {
        int idx = (m_cpuIdx - 60 + i + 60) % 60;
        int h = m_cpuHist[idx] * graph.height() / 100;
        if (h < 2) h = 2;
        QRect bar(graph.x()+2+i*barW, graph.bottom()-h, barW-1, h);
        /* Gradient from blue at bottom to green at top */
        QLinearGradient barG(bar.topLeft(), bar.bottomLeft());
        barG.setColorAt(0.0, QColor(0x30,0xD1,0x58));
        barG.setColorAt(1.0, QColor(0xFF,0x5A,0x36));
        p.setPen(Qt::NoPen); p.setBrush(barG);
        p.drawRoundedRect(bar, 1, 1);
    }

    /* Y-axis labels */
    f.setPointSize(8); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(QRect(graph.x()-36, graph.bottom()-graph.height()*75/100-6, 32, 12), Qt::AlignRight|Qt::AlignVCenter, "75%");
    p.drawText(QRect(graph.x()-36, graph.bottom()-graph.height()*50/100-6, 32, 12), Qt::AlignRight|Qt::AlignVCenter, "50%");
    p.drawText(QRect(graph.x()-36, graph.bottom()-graph.height()*25/100-6, 32, 12), Qt::AlignRight|Qt::AlignVCenter, "25%");

    /* ── Stats cards row ── */
    int statsY = cpuCard.bottom()+16;
    int cardW2 = (r.width()-48)/2;

    /* Memory card */
    QRect memCard(r.left()+16, statsY, cardW2, 90);
    QLinearGradient memBg(memCard.topLeft(), memCard.bottomLeft());
    memBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120)); memBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(memBg);
    p.drawRoundedRect(memCard, 10, 10);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x30,0xD1,0x58,180));
    p.drawRoundedRect(QRect(memCard.x()+1, memCard.y(), memCard.width()-2, 3), 1, 1);

    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(memCard.left()+16, memCard.y()+22, "Memory");
    /* Progress bar */
    uint64_t usedB  = pmm_count_used() * 4096ULL;
    uint64_t totalB = pmm_total_pages() * 4096ULL;
    int memPct = totalB ? (int)(usedB * 100 / totalB) : 0;
    if (memPct > 100) memPct = 100;
    QRect mbar(memCard.left()+16, memCard.y()+38, memCard.width()-32, 14);
    p.setPen(QPen(QColor(0x48,0x48,0x4A,100),1)); p.setBrush(QColor(0x14,0x14,0x16));
    p.drawRoundedRect(mbar, 7, 7);
    int fillW = (mbar.width()-4) * memPct / 100;
    if (fillW < 6) fillW = 6;
    QRect mfill(mbar.x()+2, mbar.y()+2, fillW, mbar.height()-4);
    QColor memCol = memPct > 85 ? QColor(0xFF,0x45,0x3A)
                  : memPct > 65 ? QColor(0xFF,0xD6,0x0A) : QColor(0x30,0xD1,0x58);
    p.setPen(Qt::NoPen); p.setBrush(memCol);
    p.drawRoundedRect(mfill, 5, 5);
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(memCard.left()+16, memCard.y()+68,
               QString("%1 MB / %2 MB  (%3%)")
                   .arg(usedB >> 20).arg(totalB >> 20).arg(memPct));

    /* Processes card */
    QRect procCard(r.left()+32+cardW2, statsY, cardW2, 90);
    QLinearGradient procBg(procCard.topLeft(), procCard.bottomLeft());
    procBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120)); procBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(procBg);
    p.drawRoundedRect(procCard, 10, 10);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0xBF,0x5A,0xF2,180));
    p.drawRoundedRect(QRect(procCard.x()+1, procCard.y(), procCard.width()-2, 3), 1, 1);

    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(procCard.left()+16, procCard.y()+22, "Processes");
    int procs = 0;
    for (int i = 0; i < PROC_MAX; i++)
        if (proc_table[i].pid > 0 && proc_table[i].state != PROC_DEAD) procs++;
    f.setPointSize(20); f.setBold(true); p.setFont(f); p.setPen(c_accent);
    p.drawText(QRect(procCard.left()+16, procCard.y()+36, procCard.width()-32, 36), Qt::AlignVCenter,
               QString("%1 · %2").arg(procs).arg(sched_thread_count()));
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(procCard.left()+16, procCard.y()+72, "processes · threads");

    /* ── Uptime card ── */
    int row2Y = statsY + 106;
    QRect upCard(r.left()+16, row2Y, cardW2, 90);
    QLinearGradient upBg(upCard.topLeft(), upCard.bottomLeft());
    upBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120)); upBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(upBg);
    p.drawRoundedRect(upCard, 10, 10);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x64,0xD2,0xFF,180));
    p.drawRoundedRect(QRect(upCard.x()+1, upCard.y(), upCard.width()-2, 3), 1, 1);

    uint64_t upS = (timer_get_milliseconds() - m_bootMs) / 1000;
    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(upCard.left()+16, upCard.y()+22, "Uptime");
    f.setPointSize(18); f.setBold(true); p.setFont(f); p.setPen(QColor(0x64,0xD2,0xFF));
    p.drawText(QRect(upCard.left()+16, upCard.y()+36, upCard.width()-32, 36), Qt::AlignVCenter,
               QString("%1:%2:%3")
                   .arg(upS / 3600, 2, 10, QChar('0'))
                   .arg((upS / 60) % 60, 2, 10, QChar('0'))
                   .arg(upS % 60, 2, 10, QChar('0')));
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(upCard.left()+16, upCard.y()+72, "hours · minutes · seconds");

    /* ── Network card ── */
    QRect netCard(r.left()+32+cardW2, row2Y, cardW2, 90);
    QLinearGradient netBg(netCard.topLeft(), netCard.bottomLeft());
    netBg.setColorAt(0.0, QColor(0x23,0x23,0x25,120)); netBg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E,108));
    p.setPen(QPen(QColor(0x63,0x63,0x66,80),1)); p.setBrush(netBg);
    p.drawRoundedRect(netCard, 10, 10);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x30,0xD1,0x58,180));
    p.drawRoundedRect(QRect(netCard.x()+1, netCard.y(), netCard.width()-2, 3), 1, 1);

    bool up = net_ready() != 0;
    uint32_t ip = net_get_ip(), gw = net_get_gateway();
    f.setPointSize(11); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(netCard.left()+16, netCard.y()+22, "Network");
    f.setPointSize(13); f.setBold(true); p.setFont(f);
    p.setPen(up ? QColor(0x30,0xD1,0x58) : QColor(0xFF,0x45,0x3A));
    p.drawText(netCard.left()+16, netCard.y()+48, up ? "Connected" : "Offline");
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    if (up && ip) {
        p.drawText(netCard.left()+16, netCard.y()+70,
                   QString("IP %1.%2.%3.%4   GW %5.%6.%7.%8")
                       .arg(ip & 0xFF).arg((ip >> 8) & 0xFF)
                       .arg((ip >> 16) & 0xFF).arg((ip >> 24) & 0xFF)
                       .arg(gw & 0xFF).arg((gw >> 8) & 0xFF)
                       .arg((gw >> 16) & 0xFF).arg((gw >> 24) & 0xFF));
    } else {
        p.drawText(netCard.left()+16, netCard.y()+70, "no link");
    }
}

