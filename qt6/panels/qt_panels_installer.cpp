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
   QtInstallerWidget — ChromeOS-style setup wizard
   ═══════════════════════════════════════════════════════════════════ */

QtInstallerWidget::QtInstallerWidget(QWidget *parent) : QtAppWindow("Install CodeOS", parent) {
    resize(680, 560);
    installer_detect_disk();
}

void QtInstallerWidget::drawButton(QPainter &p, const QRect &br, const QString &label, bool primary) {
    /* Wizard flow — no hover tracking needed, always render normal state */
    drawGlassButton(p, br, label, BtnNormal, primary);
}

void QtInstallerWidget::drawInputField(QPainter &p, const QRect &r, const QString &label, const QString &value, int cursorPos, bool active, bool passwordMode) {
    /* Label */
    QFont lf = font(); lf.setPointSize(10); lf.setBold(false); p.setFont(lf);
    p.setPen(c_subtext);
    p.drawText(QRect(r.x(), r.y()-18, r.width(), 16), Qt::AlignLeft, label);

    /* Field */
    p.setPen(active ? QPen(QColor(0xFF,0x5A,0x36), 2) : QPen(QColor(0x48,0x48,0x4A), 1));
    p.setBrush(active ? QColor(0x1A,0x1A,0x1C) : QColor(0x1C,0x1C,0x1E));
    p.drawRoundedRect(r, 8, 8);

    /* Text (masked if password) */
    QFont vf = font(); vf.setPointSize(11); p.setFont(vf);
    p.setPen(QColor(245,245,247));
    QString display = passwordMode ? QString(value.length(), QChar(0x2022)) : value;
    QFontMetrics vfm(vf);
    QString elided = vfm.elidedText(display, Qt::ElideRight, r.width()-20);
    p.drawText(r.adjusted(10,0,-10,0), Qt::AlignVCenter, elided);

    /* Cursor */
    if (active && !value.isEmpty()) {
        int curX = r.x() + 10 + vfm.horizontalAdvance(elided.left(qMin(cursorPos, elided.length())));
        if (curX > r.right()-10) curX = r.right()-10;
        m_blink = (m_blink + 1) % 50;
        if (m_blink < 25)
            p.drawLine(curX, r.y()+6, curX, r.bottom()-6);
    } else if (active && value.isEmpty()) {
        m_blink = (m_blink + 1) % 50;
        if (m_blink < 25)
            p.drawLine(r.x()+10, r.y()+6, r.x()+10, r.bottom()-6);
    }
}

void QtInstallerWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Welcome Screen ── */
    if (m_screen == SCREEN_WELCOME) {
        /* Big logo */
        p.setPen(QColor(0xFF,0x5A,0x36));
        QFont logo("monospace"); logo.setPointSize(42); logo.setBold(true); p.setFont(logo);
        p.drawText(QRect(r.x(), r.y()+60, r.width(), 60), Qt::AlignCenter, "CodeOS");

        /* Tagline */
        QFont tf = f; tf.setPointSize(14); tf.setBold(false); p.setFont(tf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+130, r.width()-120, 30), Qt::AlignHCenter,
                   "Set up your new computer in just a few steps.");

        /* Setup steps preview */
        QStringList steps = {"Connect to a network", "Create your account", "Install CodeOS"};
        for (int i = 0; i < steps.size(); i++) {
            int sy = r.y() + 200 + i * 40;
            QRect sr(r.center().x()-140, sy, 280, 32);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x2C,0x2C,0x2E,80));
            p.drawRoundedRect(sr, 16, 16);

            /* Step number circle */
            p.setBrush(QColor(0xFF,0x5A,0x36)); p.setPen(Qt::NoPen);
            p.drawEllipse(sr.x()+8, sr.y()+4, 24, 24);
            QFont nf = f; nf.setPointSize(10); nf.setBold(true); p.setFont(nf);
            p.setPen(Qt::white);
            p.drawText(QRect(sr.x()+8, sr.y()+4, 24, 24), Qt::AlignCenter, QString::number(i+1));

            QFont sf = f; sf.setPointSize(10); p.setFont(sf); p.setPen(c_text);
            p.drawText(QRect(sr.x()+42, sr.y(), sr.width()-50, 32), Qt::AlignVCenter, steps[i]);
        }

        /* Get Started button */
        m_nextBtn = QRect(r.center().x()-80, r.bottom()-70, 160, 44);
        drawButton(p, m_nextBtn, "Get Started", true);

    /* ── WiFi Screen ── */
    } else if (m_screen == SCREEN_WIFI) {
        QFont tf = f; tf.setPointSize(18); tf.setBold(true); p.setFont(tf); p.setPen(c_text);
        p.drawText(QRect(r.x(), r.y()+30, r.width(), 30), Qt::AlignHCenter, "Connect to WiFi");

        QFont sf = f; sf.setPointSize(10); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+68, r.width()-120, 20), Qt::AlignHCenter,
                   "Choose a network or continue without WiFi.");

        /* Network list */
        QStringList networks = {"CodeOS-Setup", "HomeNetwork", "CoffeeShop_WiFi"};
        QStringList sigBars = {QString::fromUtf8("\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85"),
                               QString::fromUtf8("\xe2\x98\x85\xe2\x98\x85\xe2\x98\x85\xe2\x98\x86\xe2\x98\x86"),
                               QString::fromUtf8("\xe2\x98\x85\xe2\x98\x85\xe2\x98\x86\xe2\x98\x86\xe2\x98\x86")};
        QStringList locks = {"", QString::fromUtf8(" \xf0\x9f\x94\x92"), QString::fromUtf8(" \xf0\x9f\x94\x92")};
        QStringList secTypes = {"Open", "WPA2", "WPA2"};

        for (int i = 0; i < networks.size(); i++) {
            int ny = r.y() + 100 + i * 52;
            QRect nr(r.center().x()-180, ny, 360, 44);
            bool sel = (i == m_selectedWifi);
            p.setPen(sel ? QPen(QColor(0xFF,0x5A,0x36), 2) : QPen(QColor(0x48,0x48,0x4A), 1));
            p.setBrush(sel ? QColor(0xFF,0x5A,0x36,20) : QColor(0x1C,0x1C,0x1E,180));
            p.drawRoundedRect(nr, 10, 10);

            /* WiFi icon */
            p.setPen(sel ? QColor(0xFF,0x5A,0x36) : c_subtext);
            QFont wf = f; wf.setPointSize(14); p.setFont(wf);
            p.drawText(QRect(nr.x()+12, nr.y(), 24, 44), Qt::AlignVCenter, QString::fromUtf8("\xf0\x9f\x93\x86"));

            /* Name + security */
            QFont nf = f; nf.setPointSize(11); nf.setBold(sel); p.setFont(nf);
            p.setPen(sel ? c_text : QColor(245,245,247));
            p.drawText(QRect(nr.x()+42, nr.y()+4, nr.width()-100, 20), Qt::AlignVCenter, networks[i] + locks[i]);

            QFont ef = f; ef.setPointSize(9); p.setFont(ef); p.setPen(c_subtext);
            p.drawText(QRect(nr.x()+42, nr.y()+24, nr.width()-100, 16), Qt::AlignVCenter,
                       secTypes[i] + "  " + sigBars[i]);
        }

        /* Skip link */
        QFont lnkf = f; lnkf.setPointSize(10); p.setFont(lnkf);
        p.setPen(QColor(0xFF,0x5A,0x36));
        p.drawText(QRect(r.x()+60, r.bottom()-90, r.width()-120, 20), Qt::AlignHCenter,
                   QString::fromUtf8("Skip for now \xe2\x86\x92"));

        /* WiFi password field (if an encrypted network is selected) */
        if (m_selectedWifi > 0) {
            QRect pf(r.center().x()-140, r.bottom()-130, 280, 36);
            drawInputField(p, pf, "Password", m_wifiPass, m_wifiCursorPos, true, true);
        }

    /* ── Account Screen ── */
    } else if (m_screen == SCREEN_ACCOUNT) {
        QFont tf = f; tf.setPointSize(18); tf.setBold(true); p.setFont(tf); p.setPen(c_text);
        p.drawText(QRect(r.x(), r.y()+30, r.width(), 30), Qt::AlignHCenter, "Create your account");

        QFont sf = f; sf.setPointSize(10); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+68, r.width()-120, 20), Qt::AlignHCenter,
                   "Choose a name and password for your account.");

        int fieldW = 320, fieldX = r.center().x() - fieldW/2;

        /* Full name */
        QRect nameField(fieldX, r.y()+130, fieldW, 38);
        drawInputField(p, nameField, "Full Name", m_fullName, m_nameCursorPos, m_activeField==0);

        /* Username */
        QRect userField(fieldX, r.y()+210, fieldW, 38);
        drawInputField(p, userField, "Username", m_username, m_userCursorPos, m_activeField==1);

        /* Password */
        QRect passField(fieldX, r.y()+290, fieldW, 38);
        drawInputField(p, passField, "Password", m_accountPass, m_passCursorPos, m_activeField==2, true);

        /* User avatar circle */
        p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36,40));
        p.drawEllipse(r.center().x()-30, r.y()+340, 60, 60);
        QFont avf = f; avf.setPointSize(22); avf.setBold(true); p.setFont(avf);
        p.setPen(QColor(0xFF,0x5A,0x36));
        QString initial = m_fullName.isEmpty() ? "?" : QString(m_fullName[0]).toUpper();
        p.drawText(QRect(r.center().x()-30, r.y()+340, 60, 60), Qt::AlignCenter, initial);

        /* Username under avatar */
        QFont uf = f; uf.setPointSize(10); p.setFont(uf); p.setPen(c_subtext);
        QString displayUser = m_username.isEmpty() ? "username" : m_username;
        p.drawText(QRect(r.center().x()-80, r.y()+404, 160, 20), Qt::AlignHCenter, displayUser);

    /* ── Summary Screen ── */
    } else if (m_screen == SCREEN_SUMMARY) {
        QFont tf = f; tf.setPointSize(18); tf.setBold(true); p.setFont(tf); p.setPen(c_text);
        p.drawText(QRect(r.x(), r.y()+30, r.width(), 30), Qt::AlignHCenter, "Ready to install");

        QFont sf = f; sf.setPointSize(10); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+68, r.width()-120, 20), Qt::AlignHCenter,
                   "CodeOS will be installed with the following settings:");

        /* Summary card */
        QRect scard(r.center().x()-180, r.y()+100, 360, 220);
        p.setPen(QPen(QColor(0x48,0x48,0x4A), 1));
        p.setBrush(QColor(0x1C,0x1C,0x1E,100));
        p.drawRoundedRect(scard, 12, 12);

        QStringList labels = {"Account:", "Username:", "WiFi:", "Locale:", "Filesystem:", "Disk:"};
        QString diskSize = QString::fromUtf8(installer_get_progress()->disk_size_str);
        QStringList networkNames = {"CodeOS-Setup","HomeNetwork","CoffeeShop_WiFi"};
        QString wifiStr = m_selectedWifi >= 0 ? networkNames[m_selectedWifi] : "Offline";
        QString fsStr = "ext2";
        QStringList vals = {
            m_fullName.isEmpty() ? "User" : m_fullName,
            m_username.isEmpty() ? "user" : m_username,
            wifiStr,
            m_locales[m_selectedLocale],
            fsStr,
            diskSize.isEmpty() ? "Detecting..." : diskSize
        };

        QFont lf = f; lf.setPointSize(10); p.setFont(lf);
        for (int i = 0; i < 6; i++) {
            int ly = scard.y() + 16 + i * 32;
            p.setPen(c_subtext);
            p.drawText(QRect(scard.x()+20, ly, 100, 24), Qt::AlignVCenter|Qt::AlignLeft, labels[i]);
            p.setPen(c_text);
            p.drawText(QRect(scard.x()+120, ly, scard.width()-140, 24), Qt::AlignVCenter|Qt::AlignLeft, vals[i]);
            if (i < 5) {
                p.setPen(QPen(QColor(0x48,0x48,0x4A), 1));
                p.drawLine(scard.x()+20, ly+28, scard.right()-20, ly+28);
            }
        }

        /* Warning */
        QFont wf = f; wf.setPointSize(9); wf.setBold(true); p.setFont(wf);
        p.setPen(QColor(0xFF,0x9F,0x0A));
        p.drawText(QRect(r.x()+60, scard.bottom()+20, r.width()-120, 20), Qt::AlignHCenter,
                   QString::fromUtf8("\xe2\x9a\xa0  All data on the disk will be erased"));

    /* ── Installing Screen ── */
    } else if (m_screen == SCREEN_INSTALLING) {
        installer_progress_t *prog = installer_get_progress();

        QFont tf = f; tf.setPointSize(18); tf.setBold(true); p.setFont(tf); p.setPen(c_text);
        p.drawText(QRect(r.x(), r.y()+50, r.width(), 30), Qt::AlignHCenter, "Installing CodeOS");

        /* Step name */
        QFont sf = f; sf.setPointSize(11); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+90, r.width()-120, 20), Qt::AlignHCenter,
                   QString::fromUtf8(prog->step_name));

        /* Big progress bar */
        QRect pbr(r.x()+80, r.y()+130, r.width()-160, 28);
        p.setBrush(QColor(0x1C,0x1C,0x1E)); p.setPen(QPen(QColor(0x48,0x48,0x4A),1));
        p.drawRoundedRect(pbr, 14, 14);
        if (prog->percent > 0) {
            QRect fill(pbr.x()+3, pbr.y()+3, (pbr.width()-6)*prog->percent/100, pbr.height()-6);
            QLinearGradient pg(fill.topLeft(), fill.topRight());
            pg.setColorAt(0, QColor(0xFF,0x5A,0x36));
            pg.setColorAt(1, QColor(0x30,0xD1,0x58));
            p.setBrush(pg); p.setPen(Qt::NoPen);
            p.drawRoundedRect(fill, 11, 11);
        }
        QFont pf = f; pf.setPointSize(10); pf.setBold(true); p.setFont(pf);
        p.setPen(QColor(245,245,247));
        p.drawText(pbr, Qt::AlignCenter, QString::number(prog->percent) + "%");

        /* Step dots */
        int dotY = r.y() + 176;
        int totalDots = prog->total_steps > 0 ? prog->total_steps : 7;
        int dotSpacing = 14;
        int dotsWidth = totalDots * dotSpacing;
        int dotsX = r.x() + (r.width() - dotsWidth) / 2;
        for (int i = 0; i < totalDots; i++) {
            int dx = dotsX + i * dotSpacing;
            if (i < prog->step)
                p.setBrush(QColor(0x30,0xD1,0x58));
            else if (i == prog->step - 1)
                p.setBrush(QColor(0xFF,0x5A,0x36));
            else
                p.setBrush(QColor(0x48,0x48,0x4A));
            p.setPen(Qt::NoPen);
            p.drawEllipse(dx, dotY, 7, 7);
        }

        /* Log */
        QRect logBox(r.x()+40, r.y()+200, r.width()-80, r.height()-240);
        p.setPen(QPen(QColor(0x48,0x48,0x4A), 1));
        p.setBrush(QColor(0x14,0x14,0x16,100));
        p.drawRoundedRect(logBox, 8, 8);

        QFont lf("monospace"); lf.setPointSize(8); p.setFont(lf);
        int lineH = 14;
        int maxLines = (logBox.height()-12) / lineH;
        int startLine = qMax(0, prog->log_lines - maxLines);
        for (int i = startLine; i < prog->log_lines; i++) {
            int ly = logBox.y() + 8 + (i - startLine) * lineH;
            if (ly + lineH > logBox.bottom()) break;
            bool err = QString::fromUtf8(prog->log[i]).startsWith("Failed") ||
                       QString::fromUtf8(prog->log[i]).startsWith("Error");
            p.setPen(err ? QColor(0xFF,0x45,0x3A) : QColor(0x30,0xD1,0x58));
            p.drawText(QRect(logBox.x()+8, ly, logBox.width()-16, lineH),
                       Qt::AlignVCenter, QString::fromUtf8(prog->log[i]));
        }

    /* ── Done Screen ── */
    } else if (m_screen == SCREEN_DONE) {
        /* Big check */
        p.setPen(Qt::NoPen); p.setBrush(QColor(0x30,0xD1,0x58,30));
        p.drawEllipse(r.center().x()-40, r.y()+60, 80, 80);
        p.setPen(QColor(0x30,0xD1,0x58));
        QFont checkf; checkf.setPointSize(40); checkf.setBold(true); p.setFont(checkf);
        p.drawText(QRect(r.center().x()-40, r.y()+60, 80, 80), Qt::AlignCenter, QString::fromUtf8("\xe2\x9c\x94"));

        QFont tf = f; tf.setPointSize(20); tf.setBold(true); p.setFont(tf); p.setPen(c_text);
        p.drawText(QRect(r.x(), r.y()+160, r.width(), 30), Qt::AlignHCenter, "All set!");

        QFont sf = f; sf.setPointSize(11); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+200, r.width()-120, 40), Qt::AlignHCenter|Qt::TextWordWrap,
                   "CodeOS has been installed.\nRemove the installation media and restart to begin.");

        m_nextBtn = QRect(r.center().x()-80, r.bottom()-70, 160, 44);
        drawButton(p, m_nextBtn, "Restart now", true);

    /* ── Error Screen ── */
    } else if (m_screen == SCREEN_ERROR) {
        p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x45,0x3A,30));
        p.drawEllipse(r.center().x()-40, r.y()+60, 80, 80);
        p.setPen(QColor(0xFF,0x45,0x3A));
        QFont checkf; checkf.setPointSize(40); checkf.setBold(true); p.setFont(checkf);
        p.drawText(QRect(r.center().x()-40, r.y()+60, 80, 80), Qt::AlignCenter, QString::fromUtf8("\xe2\x9c\x97"));

        QFont tf = f; tf.setPointSize(20); tf.setBold(true); p.setFont(tf); p.setPen(QColor(0xFF,0x45,0x3A));
        p.drawText(QRect(r.x(), r.y()+160, r.width(), 30), Qt::AlignHCenter, "Something went wrong");

        QFont sf = f; sf.setPointSize(11); p.setFont(sf); p.setPen(c_subtext);
        p.drawText(QRect(r.x()+60, r.y()+200, r.width()-120, 20), Qt::AlignHCenter,
                   "The installation could not be completed.");

        m_nextBtn = QRect(r.center().x()-80, r.bottom()-70, 160, 44);
        drawButton(p, m_nextBtn, "Try again", false);
    }

    /* ── Bottom navigation (wizard screens only) ── */
    if (m_screen >= SCREEN_WELCOME && m_screen <= SCREEN_SUMMARY) {
        QRect nav(r.x(), r.bottom()-56, r.width(), 56);
        p.setPen(QPen(QColor(0x38,0x38,0x3A),1));
        p.drawLine(nav.x()+40, nav.y(), nav.right()-40, nav.y());

        if (m_screen > SCREEN_WELCOME) {
            m_backBtn = QRect(nav.x()+40, nav.y()+8, 80, 36);
            drawButton(p, m_backBtn, "Back", false);
        } else {
            m_backBtn = QRect();
        }

        if (m_screen < SCREEN_SUMMARY) {
            m_nextBtn = QRect(nav.right()-120, nav.y()+8, 80, 36);
            drawButton(p, m_nextBtn, "Next", true);
        } else {
            m_nextBtn = QRect();
            m_actionBtn = QRect(nav.right()-140, nav.y()+8, 100, 36);
            drawButton(p, m_actionBtn, "Install", true);
        }
    }
}

void QtInstallerWidget::mousePressEvent(QMouseEvent *e) {
    QPoint pos = e->pos();

    if (m_backBtn.contains(pos) && m_screen > SCREEN_WELCOME && m_screen <= SCREEN_SUMMARY) {
        navigateBack(); return;
    }
    if (m_nextBtn.contains(pos) && m_screen < SCREEN_SUMMARY) {
        navigateNext(); return;
    }
    if (m_actionBtn.contains(pos) && m_screen == SCREEN_SUMMARY) {
        startInstall(); return;
    }
    if (m_nextBtn.contains(pos) && m_screen == SCREEN_DONE) {
        outb(0x64, 0xFE); asm volatile("cli; hlt"); return;
    }
    if (m_nextBtn.contains(pos) && m_screen == SCREEN_ERROR) {
        installer_detect_disk();
        m_screen = SCREEN_WELCOME;
        m_selectedWifi = -1; m_wifiPass.clear();
        m_fullName.clear(); m_username.clear(); m_accountPass.clear();
        update(); return;
    }

    /* WiFi network selection */
    if (m_screen == SCREEN_WIFI) {
        for (int i = 0; i < 3; i++) {
            QRect nr(rect().center().x()-180, rect().y()+100+i*52, 360, 44);
            if (nr.contains(pos)) {
                m_selectedWifi = (m_selectedWifi == i) ? -1 : i;
                update(); return;
            }
        }
    }

    /* Account field focus */
    if (m_screen == SCREEN_ACCOUNT) {
        int fieldW = 320, fieldX = rect().center().x() - fieldW/2;
        QRect nameF(fieldX, rect().y()+130, fieldW, 38);
        QRect userF(fieldX, rect().y()+210, fieldW, 38);
        QRect passF(fieldX, rect().y()+290, fieldW, 38);
        if (nameF.contains(pos)) { m_activeField = 0; update(); return; }
        if (userF.contains(pos)) { m_activeField = 1; update(); return; }
        if (passF.contains(pos)) { m_activeField = 2; update(); return; }
    }

    QtAppWindow::mousePressEvent(e);
}

void QtInstallerWidget::navigateNext() {
    switch (m_screen) {
        case SCREEN_WELCOME:  m_screen = SCREEN_WIFI; break;
        case SCREEN_WIFI:     m_screen = SCREEN_ACCOUNT; m_activeField = 0; break;
        case SCREEN_ACCOUNT:  m_screen = SCREEN_SUMMARY; break;
        default: break;
    }
    update();
}

void QtInstallerWidget::navigateBack() {
    switch (m_screen) {
        case SCREEN_WIFI:     m_screen = SCREEN_WELCOME; break;
        case SCREEN_ACCOUNT:  m_screen = SCREEN_WIFI; break;
        case SCREEN_SUMMARY:  m_screen = SCREEN_ACCOUNT; break;
        default: break;
    }
    update();
}

void QtInstallerWidget::startInstall() {
    m_screen = SCREEN_INSTALLING;
    update();

    /* Pass data to kernel */
    installer_set_user(m_fullName.toUtf8().constData(),
                       m_username.toUtf8().constData(),
                       m_accountPass.toUtf8().constData());
    installer_set_locale(m_localeCodes[m_selectedLocale].toUtf8().constData());
    if (m_selectedWifi >= 0) {
        QStringList ssids = {"CodeOS-Setup", "HomeNetwork", "CoffeeShop_WiFi"};
        installer_set_wifi(ssids[m_selectedWifi].toUtf8().constData(),
                           m_wifiPass.toUtf8().constData());
    } else {
        installer_set_wifi("", "");
    }

    /* Start installer in background, poll progress via timer */
    QTimer *installTimer = new QTimer(this);
    connect(installTimer, &QTimer::timeout, this, [this, installTimer]() {
        installer_progress_t *prog = installer_get_progress();
        if (prog->done || prog->error) {
            installTimer->stop();
            installTimer->deleteLater();
            m_screen = prog->error ? SCREEN_ERROR : SCREEN_DONE;
            update();
        } else {
            update(); /* repaint to show updated progress bar + log */
        }
    });
    installTimer->start(200);
    /* Run installer in a separate thread-like fashion using singleShot chain */
    QTimer::singleShot(50, this, [this]() { installer_run(); });
}

void QtInstallerWidget::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && m_screen != SCREEN_INSTALLING) {
        emit closeRequested(); return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_screen == SCREEN_DONE) {
            outb(0x64, 0xFE); asm volatile("cli; hlt");
        } else if (m_screen == SCREEN_ERROR) {
            installer_detect_disk();
            m_screen = SCREEN_WELCOME; m_selectedWifi = -1;
            m_wifiPass.clear(); m_fullName.clear(); m_username.clear(); m_accountPass.clear();
            update();
        } else if (m_screen < SCREEN_SUMMARY) {
            navigateNext();
        } else if (m_screen == SCREEN_SUMMARY) {
            startInstall();
        }
        return;
    }
    if (e->key() == Qt::Key_Backspace) {
        if (m_screen == SCREEN_WIFI && !m_wifiPass.isEmpty()) {
            m_wifiPass.chop(1); m_wifiCursorPos = m_wifiPass.length(); update(); return;
        }
        if (m_screen == SCREEN_ACCOUNT) {
            if (m_activeField == 0 && !m_fullName.isEmpty()) { m_fullName.chop(1); m_nameCursorPos = m_fullName.length(); update(); }
            else if (m_activeField == 1 && !m_username.isEmpty()) { m_username.chop(1); m_userCursorPos = m_username.length(); update(); }
            else if (m_activeField == 2 && !m_accountPass.isEmpty()) { m_accountPass.chop(1); m_passCursorPos = m_accountPass.length(); update(); }
            return;
        }
        if (m_screen > SCREEN_WELCOME && m_screen <= SCREEN_SUMMARY) {
            navigateBack(); return;
        }
    }
    /* Tab to switch fields */
    if (e->key() == Qt::Key_Tab && m_screen == SCREEN_ACCOUNT) {
        m_activeField = (m_activeField + 1) % 3;
        update(); return;
    }
    /* Text input */
    if (e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde && !(e->modifiers() & Qt::ControlModifier)) {
        if (m_screen == SCREEN_WIFI) {
            m_wifiPass += e->text(); m_wifiCursorPos = m_wifiPass.length(); update();
        } else if (m_screen == SCREEN_ACCOUNT) {
            if (m_activeField == 0) { m_fullName += e->text(); m_nameCursorPos = m_fullName.length(); }
            else if (m_activeField == 1) { m_username += e->text(); m_userCursorPos = m_username.length(); }
            else if (m_activeField == 2) { m_accountPass += e->text(); m_passCursorPos = m_accountPass.length(); }
            update();
        }
        return;
    }
    QtAppWindow::keyPressEvent(e);
}

