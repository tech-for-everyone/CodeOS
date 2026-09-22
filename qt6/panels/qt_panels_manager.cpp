#include "qt_panels.h"
#include "codeos_plugin.h"
#include "codeos_platform.h"
#include "codeos_window_manager.h"
#include "codeos_file_manager.h"
#include "codeos_terminal.h"

#include <private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformtheme.h>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QWindow>
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
extern "C" void kprintf(const char *fmt, ...);
#include <QDesktopServices>
#include <QPlainTextEdit>

extern "C" void penrose_init(void);
extern "C" void penrose_pump(void);

extern "C" {
#include "mouse.h"
#include "keyboard.h"
#include "input.h"
#include "timer.h"
#include "kprintf.h"
#include "fb.h"
#include "hyperde.h"
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
#include "waydroid.h"
#include "block.h"
#include "io.h"
#include "updater.h"
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
   QtTilingManager
   ═══════════════════════════════════════════════════════════════════ */

QtTilingManager::QtTilingManager(QObject *parent) : QObject(parent) {
    for (int i = 0; i < m_workspaceCount; i++) {
        QtWorkspace ws;
        ws.id = i;
        ws.name = QString("Workspace %1").arg(i+1);
        ws.geometry = QRect(0, 0, 1920, 1080);
        m_workspaces.append(ws);
    }
}

void QtTilingManager::addWindow(QWidget *w, int wid) {
    if (wid < 0 || wid >= m_workspaceCount) wid = m_currentWorkspace;
    w->setProperty("workspaceId", wid);
    m_workspaces[wid].windows.append(w);
    kprintf("HYPERDE: tile ADD ws=%d n=%d cur=%d\n", wid, m_workspaces[wid].windows.size(), m_currentWorkspace);
    tileWorkspace(wid);
}

void QtTilingManager::removeWindow(QWidget *w) {
    int wid = w->property("workspaceId").toInt();
    if (wid >= 0 && wid < m_workspaceCount) {
        m_workspaces[wid].windows.removeAll(w);
        tileWorkspace(wid);
    }
}

void QtTilingManager::setWorkspace(int wid) {
    if (wid < 0 || wid >= m_workspaceCount) return;
    for (QWidget *w : m_workspaces[m_currentWorkspace].windows) w->hide();
    m_currentWorkspace = wid;
    for (QWidget *w : m_workspaces[m_currentWorkspace].windows) w->show();
    emit workspaceChanged(m_currentWorkspace);
}

void QtTilingManager::focusNext() {
    QtWorkspace &ws = m_workspaces[m_currentWorkspace];
    if (ws.windows.isEmpty()) return;
    QWidget *cur = QApplication::activeWindow();
    int idx = ws.windows.indexOf(cur);
    idx = (idx < 0) ? 0 : (idx+1) % ws.windows.size();
    ws.windows[idx]->activateWindow(); ws.windows[idx]->raise();
}

void QtTilingManager::focusPrev() {
    QtWorkspace &ws = m_workspaces[m_currentWorkspace];
    if (ws.windows.isEmpty()) return;
    QWidget *cur = QApplication::activeWindow();
    int idx = ws.windows.indexOf(cur);
    idx = (idx < 0) ? ws.windows.size()-1 : (idx-1+ws.windows.size()) % ws.windows.size();
    ws.windows[idx]->activateWindow(); ws.windows[idx]->raise();
}

void QtTilingManager::moveWindowToWorkspace(QWidget *w, int wid) {
    if (wid < 0 || wid >= m_workspaceCount) return;
    int old = w->property("workspaceId").toInt();
    if (old >= 0 && old < m_workspaceCount) { m_workspaces[old].windows.removeAll(w); tileWorkspace(old); }
    w->setProperty("workspaceId", wid); m_workspaces[wid].windows.append(w);
    w->hide(); tileWorkspace(wid);
}

void QtTilingManager::tileWorkspace(int wid) {
    if (wid < 0 || wid >= m_workspaceCount) return;
    QtWorkspace &ws = m_workspaces[wid];
    if (ws.windows.isEmpty()) return;
    QRect sr = QGuiApplication::primaryScreen()->geometry();
    kprintf("HYPERDE: tile RUN ws=%d n=%d sr=(%d,%d %dx%d)\n", wid, ws.windows.size(), sr.x(), sr.y(), sr.width(), sr.height());
    int gap = 8, n = ws.windows.size();
    if (n == 1) {
        int w = qMax(0, sr.width()-gap*2);
        int h = qMax(0, sr.height()-MENUBAR_H-DOCK_H-gap*2);
        ws.windows[0]->setGeometry(sr.x()+gap, sr.y()+MENUBAR_H+gap, w, h);
    } else {
        /* Clamp the usable area first so small screens can't go negative. */
        int usableW = qMax(0, sr.width()-gap*3);
        int usableH = qMax(0, sr.height()-MENUBAR_H-DOCK_H-gap*3);
        int hw = qMax(1, usableW/2);
        int rows = (n+1)/2;
        int h2 = qMax(1, usableH/rows);
        for (int i = 0; i < n; i++) {
            int x = (i%2) * (hw+gap*2) + gap;
            int y = (i/2) * (h2+gap*2) + MENUBAR_H + gap;
            ws.windows[i]->setGeometry(sr.x()+x, sr.y()+y, hw, h2);
        }
    }
    /* keep the HyperDE compositor's chrome in lock-step with the Qt geometry */
    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (!mgr || !mgr->wm()) return;
    for (QWidget *w : qAsConst(ws.windows)) {
        QtAppWindow *aw = qobject_cast<QtAppWindow*>(w);
        if (aw && aw->wmId() > 0)
            lvgl_wm_sync_geometry(mgr->wm(), aw->wmId(),
                                  aw->x(), aw->y(), aw->width(), aw->height());
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtDesktopManager
   ═══════════════════════════════════════════════════════════════════ */

QtDesktopManager *QtDesktopManager::s_instance = nullptr;
QtDesktopManager::QtDesktopManager() { s_instance = this; }
QtDesktopManager *QtDesktopManager::instance() { return s_instance; }

void QtDesktopManager::setFocusedApp(int idx) {
    m_focusedApp = idx;
    if (m_menubar) m_menubar->setActiveApp(idx>=0 && idx<APP_COUNT ? m_appNames[idx] : "");
}

bool QtDesktopManager::init() {
    int sw = fb_getwidth(), sh = fb_getheight();
    if (sw == 0 || sh == 0) return false;
    m_appNames = QStringList{"Terminal","About","Calc","Settings","OpenWeb",
                             "Explorer","Exit","Sys Info","SysMon",
                             "Install CodeOS","LT","NetBeam","Ziggy","Notes","Clock",
                             "Convert","LaunchApp","Android"};
    mouse_set_bounds(sw, sh);
    lvgl_wm_init(&m_wm, sw, sh, 30);
    codeos_font_init();
    codeos_image_init();

    /* Theme animation timer */
    connect(&m_themeTimer, &QTimer::timeout, this, [this]() {
        if (!m_themeAnimating) return;
        float diff = m_themeTarget - m_themeBlend;
        if (qAbs(diff) < 0.02f) {
            m_themeBlend = m_themeTarget;
            m_themeAnimating = false;
            /* Apply final QPalette */
            QPalette pal;
            if (m_themeBlend < 0.5f) {
                pal.setColor(QPalette::Window, QColor(30, 30, 32));
                pal.setColor(QPalette::WindowText, QColor(245, 245, 247));
                pal.setColor(QPalette::Base, QColor(24, 24, 26));
                pal.setColor(QPalette::Text, QColor(245, 245, 247));
                pal.setColor(QPalette::Button, QColor(44, 44, 46));
                pal.setColor(QPalette::ButtonText, QColor(245, 245, 247));
                pal.setColor(QPalette::Highlight, QColor(10, 132, 255));
                pal.setColor(QPalette::HighlightedText, Qt::white);
                pal.setColor(QPalette::PlaceholderText, QColor(142, 142, 147));
            } else {
                pal.setColor(QPalette::Window, QColor(0xF5,0xF5,0xF7));
                pal.setColor(QPalette::WindowText, QColor(0x1C,0x1C,0x1E));
                pal.setColor(QPalette::Base, Qt::white);
                pal.setColor(QPalette::Text, QColor(0x1C,0x1C,0x1E));
                pal.setColor(QPalette::Button, QColor(0xE8,0xE8,0xEA));
                pal.setColor(QPalette::ButtonText, QColor(0x1C,0x1C,0x1E));
                pal.setColor(QPalette::Highlight, QColor(0xFF,0x5A,0x36));
                pal.setColor(QPalette::HighlightedText, Qt::white);
                pal.setColor(QPalette::PlaceholderText, QColor(0x8E,0x8E,0x93));
            }
            qApp->setPalette(pal);
        } else {
            m_themeBlend += diff * 0.12f;
        }
        /* Repaint shell chrome */
        if (m_desktop) m_desktop->update();
        if (m_menubar) m_menubar->update();
        if (m_dock) m_dock->update();
    });
    m_themeTimer.start(16);

    return true;
}

void QtDesktopManager::setThemeTarget(bool dark) {
    m_themeTarget = dark ? 0.0f : 1.0f;
    m_themeAnimating = true;
    if (!m_themeTimer.isActive()) m_themeTimer.start(16);
}

QColor QtDesktopManager::themedColor(const QColor &dark, const QColor &light) const {
    float t = m_themeBlend;
    return QColor(dark.red()*(1-t) + light.red()*t,
                  dark.green()*(1-t) + light.green()*t,
                  dark.blue()*(1-t) + light.blue()*t,
                  dark.alpha()*(1-t) + light.alpha()*t);
}

void QtDesktopManager::showToast(const QString &text, const QColor &accent,
                                  std::function<void()> onClick) {
    if (m_toast) m_toast->pushToast(text, accent, std::move(onClick));
}

void QtDesktopManager::setupApps() {
    for (int i = 0; i < APP_COUNT; i++) { m_appRunning[i] = 0; m_appWindows[i] = nullptr; }
}

void QtDesktopManager::launchApp(int index) {
    if (index < 0 || index >= APP_COUNT) return;
    /* Dock-only pickers never own a window: just open their overlay. */
    if (index == LAUNCHAPP_INDEX) { toggleLauncher(); return; }
    if (index == ANDROID_INDEX) { toggleLauncherAndroid(); return; }
    kprintf("NBDEBUG: launchApp idx=%d running=%d\n", index, m_appRunning[index] ? 1 : 0);
    if (m_appRunning[index]) {
        if (m_appWindows[index]) {
            m_appWindows[index]->show();
            m_appWindows[index]->raise();
        }
        return;
    }

    m_appRunning[index] = 1;
    setFocusedApp(index);
    if (m_dock) { m_dock->setRunning(index, true); m_dock->startBounce(index); }

    auto registerWin = [this, index](QtAppWindow *w, int ww, int wh) {
        registerAppWindow(w, index, m_appNames.value(index), ww, wh);
    };

    switch (index) {
    case 0: /* Terminal */
        if (m_terminal) {
            GenericAppWindow *w = new GenericAppWindow("Terminal", m_terminal, m_desktop);
            registerWin(w, 800, 500);
        }
        break;
    case 5: /* File Manager */
        if (m_fileManager) {
            GenericAppWindow *w = new GenericAppWindow("File Manager", m_fileManager, m_desktop);
            registerWin(w, 900, 600);
        }
        break;
    case 1:  registerWin(new QtAboutWidget(m_desktop), 420, 340); break;
    case 2:  registerWin(new QtCalcWidget(m_desktop), 360, 520); break;
    case 3:  registerWin(new QtSettingsWidget(m_desktop), 900, 640); break;
    case 4:  registerWin(new QtOpenWebWidget(m_desktop), 1024, 768); break;
    case 6:  registerWin(new QtExitWidget(m_desktop), 360, 170); break;
    case 7:  registerWin(new QtSysInfoWidget(m_desktop), 400, 360); break;
    case 8:  registerWin(new QtSysMonWidget(m_desktop), 640, 460); break;
    case 9:  registerWin(new QtInstallerWidget(m_desktop), 520, 440); break;
    case 10: registerWin(new QtLTWidget(m_desktop), 960, 640); break;
    case 11: registerWin(new QtNetBeamWidget(m_desktop), 700, 520); break;
    case 12: registerWin(new QtZiggyWidget(m_desktop), 560, 640); break;
    case 13: registerWin(new QtNotesWidget(m_desktop), 560, 640); break;
    case 14: registerWin(new QtClockWidget(m_desktop), 560, 640); break;
    case 15: registerWin(new QtConvertWidget(m_desktop), 560, 640); break;
    default:
        kprintf("Qt6: App %d '%s' launched (stub)\n", index,
                m_appNames.value(index).toUtf8().constData());
        break;
    }
}

void QtDesktopManager::registerAppWindow(QtAppWindow *w, int slot,
                                         const QString &title, int ww, int wh) {
    lvgl_wm_rect_t wr;
    int wmId = lvgl_wm_create_window(&m_wm, title.toUtf8().constData(), ww, wh, &wr);
    w->setWmId(wmId);
    w->setGeometry(wr.x, wr.y, ww, wh);
    w->show();
    w->activateWindow();
    w->raise();
    /* Child windows are not real top-levels here, so activateWindow() does not
     * move Qt's keyboard focus; do it explicitly so hosted apps receive keys. */
    w->setFocus();
    if (wmId > 0) lvgl_wm_set_focus(&m_wm, wmId);
    QObject::connect(w, &QtAppWindow::closeRequested, [this, w, slot]() {
        /* Closing the hosted window also asks the guest app to stop. */
        if (slot == ANDROID_INDEX) apphost_kill();
        if (w->wmId() > 0) lvgl_wm_destroy_window(&m_wm, w->wmId());
        if (m_tilingManager) m_tilingManager->removeWindow(w);
        for (int i = 0; i < APP_COUNT; i++) {
            if (m_appWindows[i] == w) {
                m_appRunning[i] = 0; m_appWindows[i] = nullptr;
                if (m_dock) m_dock->setRunning(i, false);
                break;
            }
        }
        /* Keyboard focus returns to the desktop/shell */
        if (m_desktop) m_desktop->activateWindow();
    });
    m_appWindows[slot] = w;
    if (m_autoTiling && m_tilingManager) m_tilingManager->addWindow(w);
}

QStringList QtDesktopManager::androidAppNames() const {
    QStringList out;
    int n = waydroid_app_count();
    for (int i = 0; i < n; i++) {
        const char *label = waydroid_app_label(i);
        out << QString::fromUtf8(label ? label : "Android");
    }
    return out;
}

void QtDesktopManager::launchAndroidApp(int androidIndex) {
    const char *name = waydroid_app_name(androidIndex);
    if (!name) return;
    const char *labelC = waydroid_app_label(androidIndex);
    QString label = QString::fromUtf8(labelC ? labelC : name);

    /* apphost hosts one app at a time; re-launching just raises the window. */
    if (apphost_active()) {
        if (m_appWindows[ANDROID_INDEX]) {
            m_appWindows[ANDROID_INDEX]->show();
            m_appWindows[ANDROID_INDEX]->raise();
            m_appWindows[ANDROID_INDEX]->activateWindow();
        } else {
            showToast("An Android app is already running", QColor(0xFF,0x9F,0x0A));
        }
        return;
    }
    if (m_appRunning[ANDROID_INDEX]) { m_appRunning[ANDROID_INDEX] = 0; m_appWindows[ANDROID_INDEX] = nullptr; }

    showToast("Starting " + label + "...", QColor(0x30,0xD1,0x58));
    if (waydroid_app_launch_async(name) < 0) {
        showToast("Could not start " + label, QColor(0xFF,0x45,0x3A));
        return;
    }
    m_appRunning[ANDROID_INDEX] = 1;
    setFocusedApp(ANDROID_INDEX);
    if (m_dock) { m_dock->setRunning(ANDROID_INDEX, true); m_dock->startBounce(ANDROID_INDEX); }
    QString title = "Android - " + label;
    QtAppHostWidget *w = new QtAppHostWidget(m_desktop);
    w->setAppTitle(title);
    registerAppWindow(w, ANDROID_INDEX, title, 720, 480);
}

extern "C" void *codeos_get_platform_theme(void) {
    return QGuiApplicationPrivate::platform_theme;
}
extern "C" void *codeos_track_ptr;
extern "C" void codeos_dump_theme_mem(void *);

void QtDesktopManager::run() {
    if (!init()) return;

    int scr_w = fb_getwidth();
    int scr_h = fb_getheight();

    QGuiApplicationPrivate::platform_integration = createCodeOSQpaIntegration();
    QGuiApplicationPrivate::platform_theme = new QPlatformTheme;
    codeos_track_ptr = QGuiApplicationPrivate::platform_theme;

    int argc = 1;
    static char appName[] = "CodeOS";
    static char *argv[] = {appName, nullptr};
    QApplication app(argc, argv);
    app.setApplicationName("CodeOS");
    app.setQuitOnLastWindowClosed(false);

    QFont defaultFont; defaultFont.setFamily("Liberation Sans"); defaultFont.setPointSize(11);
    app.setFont(defaultFont); QApplication::setFont(defaultFont);

    /* ── Dark Fusion palette (Catppuccin-Mocha) ── */
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(30, 30, 32));
    dark.setColor(QPalette::WindowText,      QColor(245, 245, 247));
    dark.setColor(QPalette::Base,            QColor(24, 24, 26));
    dark.setColor(QPalette::AlternateBase,   QColor(35, 35, 37));
    dark.setColor(QPalette::ToolTipBase,     QColor(44, 44, 46));
    dark.setColor(QPalette::ToolTipText,     QColor(245, 245, 247));
    dark.setColor(QPalette::Text,            QColor(245, 245, 247));
    dark.setColor(QPalette::Button,          QColor(44, 44, 46));
    dark.setColor(QPalette::ButtonText,      QColor(245, 245, 247));
    dark.setColor(QPalette::BrightText,      QColor(255, 59, 48));
    dark.setColor(QPalette::Link,            QColor(10, 132, 255));
    dark.setColor(QPalette::Highlight,       QColor(10, 132, 255));
    dark.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    dark.setColor(QPalette::PlaceholderText, QColor(142, 142, 147));

    dark.setColor(QPalette::Disabled, QPalette::Text,         QColor(99, 99, 102));
    dark.setColor(QPalette::Disabled, QPalette::ButtonText,   QColor(99, 99, 102));
    dark.setColor(QPalette::Disabled, QPalette::WindowText,   QColor(99, 99, 102));
    dark.setColor(QPalette::Disabled, QPalette::Base,         QColor(35, 35, 37));
    dark.setColor(QPalette::Disabled, QPalette::AlternateBase, QColor(30, 30, 32));

    app.setPalette(dark);

    /* ── Global stylesheet for all Qt widgets ── */
    app.setStyleSheet(QStringLiteral(
        "* { font-family: 'Liberation Sans', sans-serif; }"

        "QScrollBar:vertical {"
        "   background: rgba(20,20,22,180); width: 8px; margin: 0; border: none;"
        "}"
        "QScrollBar::handle:vertical {"
        "   background: rgba(99,99,102,160); min-height: 30px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "   background: rgba(142,142,147,200);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "   height: 0; background: none; border: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "   background: none;"
        "}"

        "QScrollBar:horizontal {"
        "   background: rgba(20,20,22,180); height: 8px; margin: 0; border: none;"
        "}"
        "QScrollBar::handle:horizontal {"
        "   background: rgba(99,99,102,160); min-width: 30px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "   background: rgba(142,142,147,200);"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "   width: 0; background: none; border: none;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "   background: none;"
        "}"

        "QMenu {"
        "   background: #1E1E20; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   border-radius: 8px; padding: 4px;"
        "}"
        "QMenu::item { padding: 6px 28px; border-radius: 4px; }"
        "QMenu::item:selected { background: rgba(10,132,255,60); }"
        "QMenu::separator { height: 1px; background: #3A3A3C; margin: 4px 8px; }"

        "QToolTip {"
        "   background: #2C2C2E; color: #F5F5F7; border: 1px solid #48484A;"
        "   border-radius: 6px; padding: 4px 8px;"
        "}"

        "QInputDialog, QMessageBox {"
        "   background: #1E1E20; color: #F5F5F7;"
        "}"
        "QMessageBox QLabel { color: #F5F5F7; }"
        "QInputDialog QLabel { color: #F5F5F7; }"
        "QInputDialog QLineEdit {"
        "   background: #141416; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   border-radius: 4px; padding: 4px;"
        "}"
        "QMessageBox QPushButton, QInputDialog QPushButton {"
        "   background: #3A3A3C; color: #F5F5F7; border: 1px solid #48484A;"
        "   border-radius: 4px; padding: 6px 16px; min-width: 60px;"
        "}"
        "QMessageBox QPushButton:hover, QInputDialog QPushButton:hover {"
        "   background: #48484A;"
        "}"
        "QMessageBox QPushButton:pressed, QInputDialog QPushButton:pressed {"
        "   background: #0A84FF;"
        "}"

        "QLineEdit {"
        "   background: #141416; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   border-radius: 4px; padding: 4px 8px; selection-background-color: #0A84FF;"
        "}"
        "QLineEdit:focus { border: 1px solid #0A84FF; }"

        "QComboBox {"
        "   background: #2C2C2E; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   border-radius: 4px; padding: 4px 8px;"
        "}"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView {"
        "   background: #1E1E20; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   selection-background-color: #0A84FF;"
        "}"

        "QProgressBar {"
        "   background: #1C1C1E; border: 1px solid #3A3A3C; border-radius: 4px;"
        "   text-align: center; color: #F5F5F7;"
        "}"
        "QProgressBar::chunk { background: #0A84FF; border-radius: 3px; }"

        "QTabWidget::pane { border: 1px solid #3A3A3C; background: #1C1C1E; }"
        "QTabBar::tab {"
        "   background: #2C2C2E; color: #AEAEB2; border: 1px solid #3A3A3C;"
        "   padding: 6px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px;"
        "}"
        "QTabBar::tab:selected { background: #1C1C1E; color: #F5F5F7; }"
        "QTabBar::tab:hover { background: #3A3A3C; color: #F5F5F7; }"

        "QGroupBox {"
        "   border: 1px solid #3A3A3C; border-radius: 6px; margin-top: 8px;"
        "   padding-top: 12px; color: #F5F5F7;"
        "}"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"

        "QCheckBox { color: #F5F5F7; spacing: 8px; }"
        "QCheckBox::indicator {"
        "   width: 16px; height: 16px; border: 1px solid #48484A;"
        "   border-radius: 3px; background: #1C1C1E;"
        "}"
        "QCheckBox::indicator:checked { background: #0A84FF; border-color: #0A84FF; }"

        "QRadioButton { color: #F5F5F7; spacing: 8px; }"
        "QRadioButton::indicator {"
        "   width: 14px; height: 14px; border: 1px solid #48484A;"
        "   border-radius: 8px; background: #1C1C1E;"
        "}"
        "QRadioButton::indicator:checked { background: #0A84FF; border-color: #0A84FF; }"

        "QSlider::groove:horizontal {"
        "   background: #3A3A3C; height: 4px; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "   background: #0A84FF; width: 14px; height: 14px;"
        "   margin: -5px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background: #64D2FF; }"

        "QSpinBox, QDoubleSpinBox {"
        "   background: #1C1C1E; color: #F5F5F7; border: 1px solid #3A3A3C;"
        "   border-radius: 4px; padding: 2px 4px;"
        "}"
        "QSpinBox::up-button, QDoubleSpinBox::up-button,"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "   background: #2C2C2E; border: none; width: 16px;"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "   image: none; border-left: 4px solid transparent;"
        "   border-right: 4px solid transparent; border-bottom: 5px solid #F5F5F7;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "   image: none; border-left: 4px solid transparent;"
        "   border-right: 4px solid transparent; border-top: 5px solid #F5F5F7;"
        "}"
    ));

    m_desktop = new QtDesktopWidget();
    m_desktop->setGeometry(0, 0, scr_w, scr_h);
    m_desktop->setAttribute(Qt::WA_OpaquePaintEvent);
    m_desktop->showFullScreen(); m_desktop->setFocus();

    m_windowManager = new CodeOSWindowManager(qApp, this);
    m_fileManager = new CodeOSFileManager(m_desktop);
    m_terminal = new CodeOSTerminal(m_desktop);
    m_terminal->startShell();

    m_menubar = new QtMenubar(m_desktop);
    m_menubar->setGeometry(0, 0, scr_w, MENUBAR_H); m_menubar->show();

    m_dock = new QtDock(m_desktop);
    m_dock->setGeometry(0, scr_h-DOCK_H, scr_w, DOCK_H);
    m_dock->setItems(m_appNames); m_dock->show();

    m_launcher = new QtLauncherOverlay(m_desktop);
    m_launcher->setGeometry(0, 0, scr_w, scr_h); m_launcher->hide();

    m_hyperdeStrip = new QtHyperdeStrip(m_desktop);
    m_hyperdeStrip->setGeometry(0, 0, scr_w, 28);

    m_notifCenter = new QtNotifCenter(m_desktop);
    m_notifCenter->setGeometry(scr_w-320, MENUBAR_H, 320, scr_h-MENUBAR_H-DOCK_H);
    m_notifCenter->hide();

    m_ctxMenu = new QtCtxMenu(m_desktop); m_ctxMenu->hide();

    m_toast = new QtToastNotification(m_desktop);
    m_toast->setGeometry(scr_w-360, MENUBAR_H+8, 360, 48); m_toast->hide();

    m_appSwitcher = new QtAppSwitcher(m_desktop);
    m_appSwitcher->setGeometry(0, 0, scr_w, scr_h); m_appSwitcher->hide();

    m_quickSettings = new QtQuickSettings(m_desktop);
    m_quickSettings->hide();

    m_widgets = new QtWidgetsPanel(m_desktop);
    m_widgets->show();

    m_missionControl = new QtMissionControl(m_desktop);

    /* Translucent snap preview shown while a window is dragged to an edge */
    m_snapPreview = new QWidget(m_desktop);
    m_snapPreview->setStyleSheet(
        "background-color: rgba(255,90,54,36);"
        "border: 2px solid rgba(255,90,54,140);"
        "border-radius: 14px;");
    m_snapPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_snapPreview->hide();

    m_tilingManager = new QtTilingManager(this);
    /* keep HyperDE's bar workspace indicator in sync with the tiling WM */
    connect(m_tilingManager, &QtTilingManager::workspaceChanged, [this](int id) {
        hyperde_shell_set_workspace(id, m_tilingManager->workspaceCount());
    });
    hyperde_shell_set_workspace(m_tilingManager->currentWorkspace(), m_tilingManager->workspaceCount());

    setupApps();

    m_menubar->onLauncherToggled = [this]() { toggleLauncher(); };
    m_menubar->onNotifToggled = [this]() { if (m_notifCenter) m_notifCenter->toggle(); };
    m_menubar->onAppMenuClicked = [this]() {
        QStringList items = {"Close Window","Minimize"};
        std::vector<std::function<void()>> actions;
        /* Resolve the target at click-time so we never hold a stale pointer. */
        actions.push_back([this]() {
            QWidget *aw = QApplication::activeWindow();
            if (aw && qobject_cast<QtAppWindow*>(aw)) aw->close();
        });
        actions.push_back([this]() {
            QWidget *aw = QApplication::activeWindow();
            if (aw && qobject_cast<QtAppWindow*>(aw)) aw->showMinimized();
        });
        QPoint gp = m_menubar ? m_menubar->mapToGlobal(QPoint(100, m_menubar->height())) : QPoint(100, MENUBAR_H+4);
        if (m_ctxMenu) m_ctxMenu->showMenu(gp.x(), gp.y(), items, actions);
    };
    m_dock->onItemClicked = [this](int i) {
        /* The LaunchApp dock icon opens the fullscreen app picker. */
        if (i == LAUNCHAPP_INDEX) {
            toggleLauncher();
            return;
        }
        /* The Android dock icon opens the Android app picker. */
        if (i == ANDROID_INDEX) {
            toggleLauncherAndroid();
            return;
        }
        launchApp(i);
        if (i >= 0 && i < m_appNames.size())
            showToast("Opening " + m_appNames[i] + "...", appIconColor(m_appNames[i]));
    };
    m_launcher->onAppSelected = [this](int i) { launchApp(i); };
    m_launcher->onAndroidSelected = [this](int i) { launchAndroidApp(i); };

    m_clockTimer = new QTimer(m_desktop);
    connect(m_clockTimer, &QTimer::timeout, [this]() {
        QDateTime now = QDateTime::currentDateTime();
        if (m_menubar) m_menubar->setClockText(now.toString("h:mm AP"));
        if (m_widgets)
            m_widgets->setTime(now.toString("h:mm"), now.toString("dddd, MMMM d"));
    });
    m_clockTimer->start(1000);

    m_running = true;
    kprintf("Qt6: Full desktop running (%dx%d)\n", scr_w, scr_h);
    hyperde_shell_init();
    penrose_init();   /* penrose tiling WM (X11 position manager) */
    /* HyperDE owns the top chrome when active: hide the Qt menubar so its
       repaints never cover the Rust-drawn liquid-glass bar, and route bar
       clicks through the transparent strip to hyperde_shell_bar_hit(). */
    syncHyperdeChrome(hyperde_shell_active());

    app.installEventFilter(this);

    static bool pending_checked = false;
    if (m_menubar && !pending_checked) {
        pending_checked = true;
        if (updater_has_pending()) {
            m_menubar->setNotifBadge(true);
            if (m_notifCenter) m_notifCenter->pushNotif("Update pending — restart to complete", QColor(0x30,0xD1,0x58));
        }
    }

    while (m_running) {
        app.processEvents(QEventLoop::AllEvents, 16);
        extern void xs_present(void);      /* composite X11 client windows */
        penrose_pump();                    /* run penrose WM against X11 events */
        xs_present();
        hyperde_shell_pump(&m_wm);
        updater_tick();
        updater_t *s = updater_get_state();
        bool hasBadge = s->update_pending || (s->state == UPDATER_UPDATE_AVAILABLE);
        if (m_menubar) m_menubar->setNotifBadge(hasBadge);
        extern void sys_sleep(int ms);
        sys_sleep(1);
    }

    app.quit();
    lvgl_wm_quit(&m_wm);
}

bool QtDesktopManager::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        kprintf("KEY: key=%d mods=%d focus=%s launcher=%d\n", ke->key(),
                (int)ke->modifiers(),
                qApp->focusWidget() ? qApp->focusWidget()->metaObject()->className()
                                    : "none",
                m_launcher && m_launcher->isVisible() ? 1 : 0);
        /* While the launcher is open it owns the keyboard: route every key
           press to it, except the shell's Meta-combos (which must keep
           working, e.g. Meta+Space closes the launcher again). */
        if (m_launcher && m_launcher->isVisible()) {
            if (ke->modifiers() & Qt::MetaModifier) {
                if (handleGlobalShortcut(ke)) return true;
            }
            m_launcher->forwardKey(ke);
            return true;
        }
        if (handleGlobalShortcut(ke))
            return true;
    }
    /* Alts-Tab handled globally (works from any window) */
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab && (ke->modifiers() & Qt::AltModifier)) {
            if (m_appSwitcher) {
                if (!m_appSwitcher->isActive()) m_appSwitcher->activate();
                else if (ke->modifiers() & Qt::ShiftModifier) m_appSwitcher->prev();
                else m_appSwitcher->next();
            }
            return true;
        }
    }
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Alt && m_appSwitcher && m_appSwitcher->isActive()) {
            m_appSwitcher->commit();
            return true;
        }
        if (m_launcher && m_launcher->isVisible()) return true;
    }
    return QObject::eventFilter(obj, event);
}

bool QtDesktopManager::handleGlobalShortcut(QKeyEvent *event) {
    if (!m_tilingManager) return false;
    int key = event->key();
    bool ctrl = event->modifiers() & Qt::ControlModifier;
    bool super = event->modifiers() & Qt::MetaModifier;

    auto closeFocused = [this]() {
        /* Prefer the window the WM thinks is focused (HyperDE drives focus). */
        int fid = lvgl_wm_focused_id(&m_wm);
        for (int i = 0; i < APP_COUNT; i++) {
            QtAppWindow *w = m_appWindows[i];
            if (w && w->wmId() == fid) { w->close(); return true; }
        }
        QWidget *aw = QApplication::activeWindow();
        if (aw && qobject_cast<QtAppWindow*>(aw)) { aw->close(); return true; }
        return false;
    };

    /* Ctrl+W / Escape = close focused window */
    if (ctrl && key == Qt::Key_W) {
        if (closeFocused()) return true;
    }
    if (key == Qt::Key_Escape) {
        if (closeFocused()) return true;
    }
    /* Ctrl+M = minimize with animation */
    if (ctrl && key == Qt::Key_M) {
        QWidget *aw = QApplication::activeWindow();
        if (aw && qobject_cast<QtAppWindow*>(aw)) {
            QtAppWindow *qaw = qobject_cast<QtAppWindow*>(aw);
            int dockIdx = -1;
            int targetX = qaw->x() + qaw->width()/2;
            int targetY = qaw->y() + qaw->height();
            if (m_dock) {
                for (int i = 0; i < APP_COUNT; i++)
                    if (m_appWindows[i] == qaw) { dockIdx = i; break; }
                if (dockIdx >= 0 && dockIdx < m_dock->itemCount()) {
                    QRect iconRect = m_dock->itemRect(dockIdx);
                    QPoint dockCenter = m_dock->mapToGlobal(iconRect.center());
                    targetX = dockCenter.x();
                    targetY = dockCenter.y();
                }
            }
            qaw->startMinimizeAnim(targetX, targetY);
            return true;
        }
    }

    if (super) {
        if (key == Qt::Key_Space) { toggleLauncher(); return true; }
        if (key == Qt::Key_Up) { toggleMissionControl(); return true; }
        if (key >= Qt::Key_1 && key <= Qt::Key_9) { m_tilingManager->setWorkspace(key - Qt::Key_1); return true; }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { launchApp(0); return true; }
        if (key == Qt::Key_Q) { closeFocused(); return true; }
        if (key == Qt::Key_F) {
            QWidget *aw = QApplication::activeWindow();
            if (aw) { if (aw->isFullScreen()) aw->showNormal(); else aw->showFullScreen(); }
            return true;
        }
        if (key == Qt::Key_T) { toggleAutoTiling(); return true; }
        if (key == Qt::Key_Comma) { toggleQuickSettings(); return true; }
        if (key == Qt::Key_Left) { m_tilingManager->focusPrev(); return true; }
        if (key == Qt::Key_Right) { m_tilingManager->focusNext(); return true; }
    }

    /* F12 = HyperDE compositor switch (swap top-bar chrome ownership) */
    if (key == Qt::Key_F12) {
        int active = hyperde_shell_active() ? 0 : 1;
        hyperde_shell_set_active(active);
        syncHyperdeChrome(active);
        return true;
    }
    return false;
}

QWidget *QtDesktopManager::snapPreview() { return m_snapPreview; }

void QtDesktopManager::updateSnapPreview(int zone, const QRect &target) {
    if (zone == SnapNone || target.isEmpty()) { hideSnapPreview(); return; }
    m_snapZoneActive = zone;
    if (!m_snapPreview) return;
    m_snapPreview->setGeometry(target);
    m_snapPreview->show();
    m_snapPreview->raise();
}

void QtDesktopManager::hideSnapPreview() {
    m_snapZoneActive = 0;
    if (m_snapPreview) m_snapPreview->hide();
}

void QtDesktopManager::toggleMissionControl() {
    if (!m_missionControl) return;
    if (m_missionControl->isOpen()) m_missionControl->hideOverview();
    else m_missionControl->showOverview();
}

void QtDesktopManager::toggleLauncher() {
    if (!m_launcher) return;
    if (m_launcher->isVisible()) m_launcher->hideLauncher();
    else m_launcher->showLauncher();
}

void QtDesktopManager::toggleLauncherAndroid() {
    if (!m_launcher) return;
    if (m_launcher->isVisible() && m_launcher->androidMode()) m_launcher->hideLauncher();
    else m_launcher->showLauncher(true);
}

void QtDesktopManager::focusHyperdeWindow(int wmIdx) {
    lvgl_wm_window_t *ww = lvgl_wm_window_at(&m_wm, wmIdx);
    if (!ww) return;
    for (int i = 0; i < APP_COUNT; i++) {
        if (m_appWindows[i] && m_appWindows[i]->wmId() == ww->id) {
            m_appWindows[i]->show();
            m_appWindows[i]->raise();
            lvgl_wm_set_focus(&m_wm, ww->id);
            setFocusedApp(i);
            m_appWindows[i]->activateWindow();
            return;
        }
    }
}

void QtDesktopManager::syncHyperdeChrome(bool active) {
    if (m_hyperdeStrip) {
        if (active) { m_hyperdeStrip->setGeometry(0, 0, (int)fb_getwidth(), 28); m_hyperdeStrip->showStrip(); }
        else m_hyperdeStrip->hideStrip();
    }
    if (m_menubar) { if (active) m_menubar->hide(); else m_menubar->show(); }
}

void QtDesktopManager::stop() {
    m_running = false;
    for (int i = 0; i < APP_COUNT; i++) {
        if (m_appWindows[i]) {
            if (m_appWindows[i]->wmId() > 0)
                lvgl_wm_destroy_window(&m_wm, m_appWindows[i]->wmId());
            m_appWindows[i]->deleteLater();
            m_appWindows[i] = nullptr;
        }
        m_appRunning[i] = 0;
    }
    if (m_desktop) { m_desktop->hide(); m_desktop->deleteLater(); m_desktop = nullptr; }
}
