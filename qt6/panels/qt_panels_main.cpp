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

/* ═══════════════════════════════════════════════════════════════════
   C entry points (called from kernel)
   ═══════════════════════════════════════════════════════════════════ */

extern "C" int qt6_panels_init_cpp(void) {
    QtDesktopManager *mgr = new QtDesktopManager();
    Q_UNUSED(mgr);
    QtDesktopManager::instance()->init();
    return 1;
}

extern "C" void qt6_panels_run_cpp(void) {
    QtDesktopManager::instance()->run();
}

extern "C" void qt6_panels_stop_cpp(void) {
    if (QtDesktopManager::instance())
        QtDesktopManager::instance()->stop();
}

extern "C" int qt6_panels_active_cpp(void) {
    return QtDesktopManager::instance() && QtDesktopManager::instance()->active() ? 1 : 0;
}
