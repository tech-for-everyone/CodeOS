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
   QtExplorerWidget
   ═══════════════════════════════════════════════════════════════════ */

QtExplorerWidget::QtExplorerWidget(QWidget *parent) : QtAppWindow("Explorer", parent) {
    resize(850, 600);
    navigateTo("/");
}

void QtExplorerWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Toolbar ── */
    QRect toolbar(r.x()+8, r.y()+8, r.width()-16, 36);
    QLinearGradient tbBg(toolbar.topLeft(), toolbar.bottomLeft());
    tbBg.setColorAt(0.0, QColor(0x28,0x28,0x2A,120));
    tbBg.setColorAt(1.0, QColor(0x23,0x23,0x25,180));
    p.setPen(QPen(QColor(0x48,0x48,0x4A,80),1)); p.setBrush(tbBg);
    p.drawRoundedRect(toolbar, 8, 8);

    /* Up button */
    m_upRect = QRect(toolbar.x()+8, toolbar.y()+4, 28, 28);
    p.setPen(Qt::NoPen); p.setBrush(m_upRect.contains(mapFromGlobal(cursor().pos())) ? QColor(0x3A,0x3A,0x3C) : QColor(0x33,0x33,0x35));
    p.drawRoundedRect(m_upRect, 6, 6);
    p.setPen(c_text); f.setPointSize(12); p.setFont(f);
    p.drawText(m_upRect, Qt::AlignCenter, QString::fromUtf8("\xe2\x86\x90"));

    /* Home button */
    QRect homeBtn(m_upRect.right()+6, toolbar.y()+4, 28, 28);
    p.setPen(Qt::NoPen); p.setBrush(homeBtn.contains(mapFromGlobal(cursor().pos())) ? QColor(0x3A,0x3A,0x3C) : QColor(0x33,0x33,0x35));
    p.drawRoundedRect(homeBtn, 6, 6);
    p.setPen(c_text);
    p.drawText(homeBtn, Qt::AlignCenter, QString::fromUtf8("\xe2\x8c\x82"));

    /* Path breadcrumb */
    QRect pathArea(homeBtn.right()+8, toolbar.y()+2, toolbar.width()-(homeBtn.right()-toolbar.x())-16, toolbar.height()-4);
    p.setPen(c_text); f.setPointSize(10); p.setFont(f);
    p.drawText(pathArea.adjusted(8,0,-8,0), Qt::AlignVCenter|Qt::AlignLeft, m_path);

    /* ── Column headers ── */
    int hdrY = toolbar.bottom()+8;
    f.setPointSize(9); f.setBold(true); p.setFont(f); p.setPen(c_subtext);
    p.drawText(r.left()+52, hdrY, "NAME");
    p.drawText(r.left()+r.width()*55/100, hdrY, "TYPE");
    p.drawText(r.left()+r.width()*75/100, hdrY, "SIZE");
    p.setPen(QPen(QColor(0x48,0x48,0x4A,80),1));
    p.drawLine(r.left()+16, hdrY+14, r.right()-16, hdrY+14);

    /* ── File list ── */
    int listTop = hdrY + 20;
    int y = listTop - m_scroll;
    for (int i = 0; i < m_entries.size(); i++) {
        QRect row(r.left()+8, y, r.width()-16, 30);
        if (y + 30 > listTop && y < r.bottom()-30) {
            /* Alternating row background */
            if (i % 2 == 0) {
                p.setPen(Qt::NoPen); p.setBrush(QColor(0x23,0x23,0x25,60));
                p.drawRoundedRect(row, 4, 4);
            }
            /* Hover highlight */
            if (i == m_selected) {
                p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36,50));
                p.drawRoundedRect(row, 4, 4);
            }

            /* File icon */
            p.setPen(m_isDir[i] ? QColor(0xFF,0x5A,0x36) : QColor(0x8E,0x8E,0x93));
            f.setPointSize(11); p.setFont(f);
            p.drawText(QRect(row.left()+12, row.y(), 24, 30), Qt::AlignVCenter,
                       m_isDir[i] ? QString::fromUtf8("\xf0\x9f\x93\x81") : QString::fromUtf8("\xf0\x9f\x93\x84"));

            /* Name */
            p.setPen(m_isDir[i] ? c_accent : c_text);
            f.setPointSize(11); f.setBold(m_isDir[i]); p.setFont(f);
            p.drawText(QRect(row.left()+42, row.y(), r.width()*40/100-42, 30), Qt::AlignVCenter|Qt::AlignLeft, m_entries[i]);

            /* Type */
            f.setBold(false); p.setFont(f); p.setPen(c_subtext);
            QString type = m_isDir[i] ? "Folder" : "File";
            p.drawText(QRect(r.left()+r.width()*55/100, row.y(), r.width()*20/100, 30), Qt::AlignVCenter|Qt::AlignLeft, type);

            /* Size (random-looking but deterministic based on name) */
            QString size = m_isDir[i] ? "--" : QString("%1 KB").arg((m_entries[i].length()*137)%9999);
            p.drawText(QRect(r.left()+r.width()*75/100, row.y(), r.width()*20/100, 30), Qt::AlignVCenter|Qt::AlignLeft, size);
        }
        y += 30;
    }

    /* ── Status bar ── */
    QRect statusBar(r.x()+8, r.bottom()-28, r.width()-16, 24);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0x23,0x23,0x25,180));
    p.drawRoundedRect(statusBar, 6, 6);
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
    p.drawText(statusBar.adjusted(12,0,-12,0), Qt::AlignVCenter|Qt::AlignLeft,
               QString("%1 items").arg(m_entries.size()));
}

void QtExplorerWidget::navigateTo(const QString &path) {
    m_path = path; m_entries.clear(); m_isDir.clear(); m_selected = -1; m_scroll = 0;
    QDir dir(path);
    if (path != "/") {
        m_entries << ".."; m_isDir << true;
    }
    QFileInfoList list = dir.entryInfoList(QDir::AllEntries | QDir::NoDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &fi : list) {
        m_entries << fi.fileName();
        m_isDir << fi.isDir();
    }
}

void QtExplorerWidget::mousePressEvent(QMouseEvent *e) {
    if (m_upRect.contains(e->pos())) {
        if (m_path == "/") { update(); return; }
        QString parent = m_path;
        if (parent.endsWith('/') && parent.length() > 1) parent.chop(1);
        int slash = parent.lastIndexOf('/');
        if (slash <= 0) navigateTo("/");
        else navigateTo(parent.left(slash));
        update(); return;
    }
    int contentTop = 92;
    int yClick = e->pos().y() - contentTop + m_scroll;
    int yEntry = 0;
    for (int i = 0; i < m_entries.size(); i++) {
        if (yClick >= yEntry && yClick < yEntry + 30) {
            m_selected = i;
            update(); return;
        }
        yEntry += 30;
    }
    QtAppWindow::mousePressEvent(e);
}

void QtExplorerWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    int contentTop = 92;
    int yClick = e->pos().y() - contentTop + m_scroll;
    int yEntry = 0;
    for (int i = 0; i < m_entries.size(); i++) {
        if (yClick >= yEntry && yClick < yEntry + 30) {
            if (m_isDir[i]) {
                QString newName = m_entries[i];
                if (newName == "..") {
                    if (m_path != "/") {
                        QString parent = m_path;
                        if (parent.endsWith('/') && parent.length() > 1) parent.chop(1);
                        int slash = parent.lastIndexOf('/');
                        navigateTo(slash <= 0 ? "/" : parent.left(slash));
                    }
                } else {
                    QString newPath = m_path;
                    if (!newPath.endsWith('/')) newPath += '/';
                    navigateTo(newPath + newName);
                }
                update();
            }
            return;
        }
        yEntry += 30;
    }
}

void QtExplorerWidget::wheelEvent(QWheelEvent *e) {
    int delta = e->angleDelta().y();
    int maxScroll = qMax(0, m_entries.size() * 32 - (height() - 100));
    if (delta > 0) m_scroll = qMax(0, m_scroll - 60);
    else m_scroll = qMin(maxScroll, m_scroll + 60);
    update();
}

