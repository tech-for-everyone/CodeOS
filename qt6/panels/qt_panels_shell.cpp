#include "qt_panels.h"
#include "codeos_plugin.h"
#include "codeos_platform.h"
#include "codeos_window_manager.h"
#include "codeos_file_manager.h"
#include "codeos_terminal.h"
#include "hyperde.h"
extern "C" void kprintf(const char *fmt, ...);

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
#include <QScreen>
#include <QDate>

extern "C" {
#include "mouse.h"
#include "keyboard.h"
#include "input.h"
#include "timer.h"

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
#include "penrose_bridge.h"
#include "block.h"
#include "io.h"
#include "updater.h"
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
   QtAppWindow — glass title-bar window base
   ═══════════════════════════════════════════════════════════════════ */

QtAppWindow::QtAppWindow(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title) {
    setMouseTracking(true);
    setMinimumSize(200, 120);
    resize(720, 500);
    setWindowOpacity(0);
    m_animIn = true;
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        bool dirty = false;
        if (m_animIn) {
            m_opacity = qMin(1.0f, m_opacity + 0.10f);
            m_scale = qMin(1.0f, m_scale + 0.03f);
            setWindowOpacity(m_opacity);
            dirty = true;
            if (m_opacity >= 1.0f) { m_animIn = false; }
        } else if (m_minimizing) {
            m_minProgress = qMin(1.0f, m_minProgress + 0.08f);
            float ease = 1.0f - (1.0f - m_minProgress) * (1.0f - m_minProgress);
            m_scale = 1.0f - ease * 0.7f;
            m_opacity = 1.0f - ease;
            dirty = true;
            if (m_minProgress >= 1.0f) {
                m_minimizing = false; m_scale = 1.0f; m_opacity = 0.0f;
                m_animTimer.stop();
                hide();
                return;
            }
        } else if (m_closing) {
            m_opacity = qMax(0.0f, m_opacity - 0.15f);
            m_scale = qMax(0.85f, m_scale - 0.03f);
            setWindowOpacity(m_opacity);
            dirty = true;
            if (m_opacity <= 0.0f) {
                m_animTimer.stop();
                QWidget::close();
                return;
            }
        } else {
            m_animTimer.stop();
        }
        if (dirty) update();
    });
    m_animTimer.start(16);
}

void QtAppWindow::showEvent(QShowEvent *) {
    m_opacity = 0.0f; m_scale = 0.92f; m_animIn = true; m_closing = false;
    m_minimizing = false; m_minProgress = 0.0f;
    setWindowOpacity(0);
    if (!m_animTimer.isActive()) m_animTimer.start(16);
}

void QtAppWindow::hideEvent(QHideEvent *) {
    m_opacity = 1.0f; m_scale = 1.0f; m_closing = false;
    m_minimizing = false; m_minProgress = 0.0f;
}

void QtAppWindow::closeEvent(QCloseEvent *event) {
    if (m_closing) {
        /* Animation finished: let QWidget::close() complete and free the widget.
           The wm window/registry cleanup already ran once via closeRequested. */
        event->accept();
        deleteLater();
    } else {
        m_closing = true;
        if (!m_animTimer.isActive()) m_animTimer.start(16);
        emit closeRequested();
        event->ignore();
    }
}

void QtAppWindow::setAppTitle(const QString &t) { m_title = t; update(); }

void QtAppWindow::startMinimizeAnim(int targetX, int targetY) {
    m_minTargetX = targetX;
    m_minTargetY = targetY;
    m_minimizing = true;
    m_minProgress = 0.0f;
    setWindowOpacity(1.0f);
    if (!m_animTimer.isActive()) m_animTimer.start(16);
}

QtAppWindow::ResizeEdge QtAppWindow::edgeAt(const QPoint &pos) const {
    int m = RESIZE_MARGIN;
    bool onLeft = pos.x() < m;
    bool onRight = pos.x() > width() - m;
    bool onTop = pos.y() < m;
    bool onBottom = pos.y() > height() - m;
    if (onTop && onLeft) return TopLeft;
    if (onTop && onRight) return TopRight;
    if (onBottom && onLeft) return BottomLeft;
    if (onBottom && onRight) return BottomRight;
    if (onLeft) return Left;
    if (onRight) return Right;
    if (onTop) return Top;
    if (onBottom) return Bottom;
    return NoEdge;
}

void QtAppWindow::updateCursor(ResizeEdge edge) {
    switch (edge) {
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
    }
}

void QtAppWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* ── Apply scale + translate transform ── */
    float sc = m_scale;
    QPointF offset(0, 0);
    if (m_minimizing && m_minProgress > 0.01f) {
        float ease = 1.0f - (1.0f - m_minProgress) * (1.0f - m_minProgress);
        sc = 1.0f - ease * 0.7f;
        QPointF target(m_minTargetX - geometry().x() - width()/2,
                       m_minTargetY - geometry().y() - height()/2);
        offset = target * ease;
    }
    if (sc < 0.999f || sc > 1.001f || offset.manhattanLength() > 0.5) {
        QPointF center = rect().center();
        p.translate(center + offset);
        p.scale(sc, sc);
        p.translate(-center);
    }

    int tb = 30, cr = 12, sh = 6;

    /* ── Multi-layer drop shadow (macOS 27 tight, 3 layers) ── */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0, 0x08)); p.drawRoundedRect(rect().adjusted(sh+3, sh+8, -sh-3, -sh+8), cr+3, cr+3);
    p.setBrush(QColor(0,0,0, 0x12)); p.drawRoundedRect(rect().adjusted(sh+1, sh+4, -sh-1, -sh+4), cr+1, cr+1);
    p.setBrush(QColor(0,0,0, 0x1C)); p.drawRoundedRect(rect().adjusted(sh, sh+1, -sh, -sh+1), cr, cr);

    /* ── Window body fill — semi-transparent glass ── */
    QLinearGradient bodyGrad(rect().topLeft(), rect().bottomLeft());
    bodyGrad.setColorAt(0.0, QColor(0x28,0x28,0x2C,145));
    bodyGrad.setColorAt(0.15, QColor(0x24,0x24,0x28,135));
    bodyGrad.setColorAt(0.5, QColor(0x20,0x20,0x24,125));
    bodyGrad.setColorAt(0.85, QColor(0x24,0x24,0x28,135));
    bodyGrad.setColorAt(1.0, QColor(0x28,0x28,0x2C,145));
    p.setPen(QPen(QColor(255,255,255,22),1)); p.setBrush(bodyGrad);
    p.drawRoundedRect(rect().adjusted(sh, sh, -sh, -sh+2), cr, cr);

    /* Content inner glass highlight (top edge) */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,14));
    p.drawRect(sh+cr, sh+1, width()-cr*2-sh*2, 1);
    /* Bottom inner shadow */
    p.setBrush(QColor(0,0,0,12));
    p.drawRect(sh+cr, height()-sh-2, width()-cr*2-sh*2, 1);

    m_titleBarRect = QRect(cr+sh, sh, width()-cr*2-sh*2, tb);

    /* ── Title bar glass — semi-transparent ── */
    QLinearGradient tbg(0, sh, 0, sh+tb);
    tbg.setColorAt(0.0, QColor(0x30,0x30,0x34,0x88));
    tbg.setColorAt(0.3, QColor(0x28,0x28,0x2C,0x80));
    tbg.setColorAt(0.7, QColor(0x22,0x22,0x26,0x78));
    tbg.setColorAt(1.0, QColor(0x1E,0x1E,0x22,0x70));
    p.setPen(Qt::NoPen); p.setBrush(tbg);
    p.drawRoundedRect(m_titleBarRect, cr, cr);

    /* Title bar top specular (3-row: bright→medium→soft) */
    p.setBrush(QColor(255,255,255,22));
    p.drawRect(m_titleBarRect.x()+cr, sh, m_titleBarRect.width()-cr*2, 1);
    p.setBrush(QColor(255,255,255,12));
    p.drawRect(m_titleBarRect.x()+cr, sh+1, m_titleBarRect.width()-cr*2, 1);
    p.setBrush(QColor(255,255,255,6));
    p.drawRect(m_titleBarRect.x()+cr, sh+2, m_titleBarRect.width()-cr*2, 1);

    /* Title bar bottom separator — crisp dark line */
    p.setBrush(QColor(0,0,0,40));
    p.drawRect(m_titleBarRect.x()+cr, m_titleBarRect.bottom(), m_titleBarRect.width()-cr*2, 1);

    /* ── Window controls — right side of the band (COSMIC style) ── */
    int dotY = sh+(tb-12)/2, gap = 20;
    int closeX = width()-sh-18;
    m_closeRect = QRect(closeX-6, dotY, 12, 12);
    m_minRect   = QRect(closeX-6-gap, dotY, 12, 12);
    m_maxRect   = QRect(closeX-6-gap*2, dotY, 12, 12);
    p.setPen(Qt::NoPen);
    /* Traffic lights: dim when window not focused, vivid when active */
    bool focused = isActiveWindow();
    int closeAlpha = focused ? 255 : 60;
    int minAlpha   = focused ? 255 : 60;
    int maxAlpha   = focused ? 255 : 60;
    /* Check hover for each button */
    QPoint gp = mapFromGlobal(cursor().pos());
    bool closeHover = m_closeRect.contains(gp);
    bool minHover   = m_minRect.contains(gp);
    bool maxHover   = m_maxRect.contains(gp);
    /* Outer glow when hovered */
    if (focused && closeHover) { p.setBrush(QColor(0xFF,0x5F,0x57,40)); p.drawEllipse(m_closeRect.adjusted(-4,-4,4,4)); }
    if (focused && minHover)   { p.setBrush(QColor(0xFE,0xBC,0x2E,40)); p.drawEllipse(m_minRect.adjusted(-4,-4,4,4)); }
    if (focused && maxHover)   { p.setBrush(QColor(0x28,0xC8,0x40,40)); p.drawEllipse(m_maxRect.adjusted(-4,-4,4,4)); }
    /* Button bodies */
    p.setBrush(QColor(0xFF,0x5F,0x57, closeAlpha)); p.drawEllipse(m_closeRect);
    p.setBrush(QColor(0xFE,0xBC,0x2E, minAlpha));   p.drawEllipse(m_minRect);
    p.setBrush(QColor(0x28,0xC8,0x40, maxAlpha));   p.drawEllipse(m_maxRect);
    /* Inner specular highlight on each dot */
    p.setBrush(QColor(255,255,255, focused ? 50 : 15));
    p.drawEllipse(m_closeRect.adjusted(1,1,-2,-2));
    p.drawEllipse(m_minRect.adjusted(1,1,-2,-2));
    p.drawEllipse(m_maxRect.adjusted(1,1,-2,-2));
    /* Hover glyphs (shown when hovered) */
    if (focused) {
        QFont sym("monospace"); sym.setPixelSize(9); sym.setBold(true); p.setFont(sym);
        if (closeHover) {
            p.setPen(QColor(0x80,0x20,0x20));
            p.drawText(m_closeRect, Qt::AlignCenter, QString::fromUtf8("\xC3\x97")); /* × */
        }
        if (minHover) {
            p.setPen(QColor(0x80,0x60,0x10));
            p.drawText(m_minRect, Qt::AlignCenter, QString::fromUtf8("\xE2\x80\x93")); /* – */
        }
        if (maxHover) {
            p.setPen(QColor(0x10,0x70,0x20));
            p.drawText(m_maxRect, Qt::AlignCenter, QString::fromUtf8("+"));
        }
    }

    /* ── Title text (centered between left inset and right controls) ── */
    QFont f = font(); f.setPointSize(11); f.setBold(true); p.setFont(f);
    p.setPen(c_text);
    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(m_title);

    /* Small icon to left of title */
    int freeL = sh+20, freeR = m_minRect.left()-14;
    int titleCx = freeL + (freeR-freeL)/2;
    QRect titleIconR(titleCx-tw/2-18, sh+(tb-14)/2, 14, 14);
    drawAppIcon(p, titleIconR, m_title, 14);

    p.drawText(titleCx-tw/2+2, sh+(tb+fm.ascent())/2, m_title);

    /* ── Content area ── */
    QRect cr2(cr+sh, tb+sh+2, width()-cr*2-sh*2, height()-tb-cr-sh*2);
    QLinearGradient cbg(cr2.topLeft(), cr2.bottomLeft());
    cbg.setColorAt(0, QColor(0x24,0x24,0x28,130));
    cbg.setColorAt(0.5, QColor(0x20,0x20,0x24,120));
    cbg.setColorAt(1, QColor(0x1C,0x1C,0x20,110));
    p.setBrush(cbg);
    p.setPen(QPen(QColor(255,255,255,14),1));
    p.drawRoundedRect(cr2, 8, 8);
    /* Content top highlight */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,12));
    p.drawRect(cr2.x()+8, cr2.y()+1, cr2.width()-16, 1);

    p.save();
    p.setClipRect(cr2.adjusted(1,2,-1,-1));
    paintContent(p, cr2);
    p.restore();
}

void QtAppWindow::toggleMaximize() {
    if (m_maximized) {
        setGeometry(m_restoreGeom);
        m_maximized = false;
    } else {
        m_restoreGeom = geometry();
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr) {
            QWidget *desk = mgr->desktop();
            if (desk) setGeometry(desk->x(), desk->y()+MENUBAR_H,
                                  desk->width(), desk->height()-MENUBAR_H-DOCK_H);
        }
        m_maximized = true;
    }
    update();
}

void QtAppWindow::mousePressEvent(QMouseEvent *e) {
    /* Clicking a window gives it keyboard focus (child windows do not get
     * this for free in the CodeOS compositor). */
    setFocus(Qt::MouseFocusReason);
    if (m_closeRect.contains(e->pos())) {
        /* Animated close: fade + shrink */
        m_closing = true;
        if (!m_animTimer.isActive()) m_animTimer.start(16);
        emit closeRequested();
        return;
    }
    if (m_minRect.contains(e->pos())) {
        /* Animated minimize: fly to dock */
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && mgr->dock()) {
            QtDock *dock = mgr->dock();
            int dockIdx = -1;
            for (int i = 0; i < APP_COUNT; i++)
                if (mgr->appWindows()[i] == this) { dockIdx = i; break; }
            if (dockIdx >= 0 && dockIdx < dock->itemCount()) {
                QRect iconRect = dock->itemRect(dockIdx);
                QPoint dockCenter = dock->mapToGlobal(iconRect.center());
                startMinimizeAnim(dockCenter.x(), dockCenter.y());
                return;
            }
        }
        startMinimizeAnim(x() + width()/2, y() + height());
        return;
    }
    if (m_maxRect.contains(e->pos())) {
        /* Toggle maximize */
        toggleMaximize();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        ResizeEdge edge = edgeAt(e->pos());
        if (edge != NoEdge) {
            m_resizing = true;
            m_resizeEdge = edge;
            m_resizeStart = e->globalPosition().toPoint();
            m_resizeStartGeom = geometry();
            raise();
            return;
        }
        if (m_titleBarRect.contains(e->pos())) {
            m_dragging = true; m_dragStart = e->pos(); raise();
        }
    }
}

void QtAppWindow::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && m_titleBarRect.contains(e->pos()))
        toggleMaximize();
}

void QtAppWindow::mouseMoveEvent(QMouseEvent *e) {
    if (m_resizing) {
        QPoint diff = e->globalPosition().toPoint() - m_resizeStart;
        QRect g = m_resizeStartGeom;
        int minW = minimumWidth(), minH = minimumHeight();
        if (m_resizeEdge & Left) { g.setLeft(qMin(g.left() + diff.x(), g.right() - minW)); }
        if (m_resizeEdge & Right) { g.setRight(qMax(g.right() + diff.x(), g.left() + minW)); }
        if (m_resizeEdge & Top) { g.setTop(qMin(g.top() + diff.y(), g.bottom() - minH)); }
        if (m_resizeEdge & Bottom) { g.setBottom(qMax(g.bottom() + diff.y(), g.top() + minH)); }
        setGeometry(g);
        return;
    }
    if (m_dragging) {
        move(pos() + e->pos() - m_dragStart);
        /* ── Edge snapping: detect zone, show translucent preview ── */
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && !m_maximized) {
            QPoint gp = e->globalPosition().toPoint();
            QWidget *top = window();
            QRect scr(0, 0, -1, -1);
            if (QScreen *s = top->screen())
                scr = s->geometry();
            if (scr.width() <= 0) scr = QRect(0, 0, (int)fb_getwidth(), (int)fb_getheight());
            int zone = QtDesktopManager::SnapNone;
            QRect target;
            if (gp.y() <= MENUBAR_H + 4) {
                zone = QtDesktopManager::SnapMax;
                target = QRect(0, MENUBAR_H, scr.width(), scr.height() - MENUBAR_H);
            } else if (gp.x() <= 8) {
                zone = QtDesktopManager::SnapLeft;
                target = QRect(0, MENUBAR_H, scr.width()/2, scr.height() - MENUBAR_H);
            } else if (gp.x() >= scr.width() - 8) {
                zone = QtDesktopManager::SnapRight;
                target = QRect(scr.width()/2, MENUBAR_H, scr.width() - scr.width()/2,
                               scr.height() - MENUBAR_H);
            }
            if (zone != m_snapZone) {
                m_snapZone = zone;
                mgr->updateSnapPreview(zone, target);
            } else if (zone != QtDesktopManager::SnapNone) {
                mgr->updateSnapPreview(zone, target);   /* keep geometry fresh */
            }
        }
    } else {
        updateCursor(edgeAt(e->pos()));
    }
    /* Repaint traffic-light hover glyphs */
    if (m_closeRect.contains(e->pos()) || m_minRect.contains(e->pos()) || m_maxRect.contains(e->pos()))
        update();
}

void QtAppWindow::mouseReleaseEvent(QMouseEvent *) {
    if (m_resizing) {
        if (m_wmId > 0)
            lvgl_wm_sync_geometry(QtDesktopManager::instance()->wm(), m_wmId, x(), y(), width(), height());
        m_resizing = false; m_resizeEdge = NoEdge;
    }
    if (m_dragging) {
        /* ── Apply snap zone chosen during the drag ── */
        if (m_snapZone != QtDesktopManager::SnapNone && !m_maximized) {
            m_restoreGeom = geometry();
            QtDesktopManager *mgr = QtDesktopManager::instance();
            QWidget *top = window();
            QRect scr(0, 0, (int)fb_getwidth(), (int)fb_getheight());
            if (QScreen *s = top->screen()) scr = s->geometry();
            QRect target;
            switch (m_snapZone) {
                case QtDesktopManager::SnapLeft:
                    target = QRect(0, MENUBAR_H, scr.width()/2, scr.height()-MENUBAR_H); break;
                case QtDesktopManager::SnapRight:
                    target = QRect(scr.width()/2, MENUBAR_H, scr.width()-scr.width()/2,
                                   scr.height()-MENUBAR_H); break;
                default:
                    target = QRect(0, MENUBAR_H, scr.width(), scr.height()-MENUBAR_H); break;
            }
            setGeometry(target);
            if (m_wmId > 0)
                lvgl_wm_sync_geometry(mgr ? mgr->wm() : nullptr, m_wmId, x(), y(), width(), height());
            m_snapZone = QtDesktopManager::SnapNone;
        }
        if (QtDesktopManager *mgr = QtDesktopManager::instance())
            mgr->hideSnapPreview();
        if (m_wmId > 0)
            lvgl_wm_sync_geometry(QtDesktopManager::instance()->wm(), m_wmId, x(), y(), width(), height());
    }
    m_dragging = false;
}

/* ═══════════════════════════════════════════════════════════════════
   QtDesktopWidget
   ═══════════════════════════════════════════════════════════════════ */

QtDesktopWidget::QtDesktopWidget(QWidget *parent) : QWidget(parent) {
    /* Paint a dark surface even during the first frame, before the first
       wallpaper paint event arrives.  This prevents the platform's default
       gray QWidget color from flashing through on startup. */
    QPalette desktopPalette = palette();
    desktopPalette.setColor(QPalette::Window, QColor(0x0B, 0x0B, 0x12));
    setPalette(desktopPalette);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(&m_crossfadeTimer, &QTimer::timeout, this, [this]() {
        if (!m_crossfading) return;
        m_crossfadeProgress = qMin(1.0f, m_crossfadeProgress + 0.04f);
        update();
        if (m_crossfadeProgress >= 1.0f) {
            m_crossfading = false;
            m_crossfadeTimer.stop();
        }
    });
}

void QtDesktopWidget::setWallpaper(const QImage &img) {
    startCrossfade(img, {}, {}, false);
}

void QtDesktopWidget::setGradientWallpaper(const QColor &t, const QColor &b) {
    startCrossfade(QImage(), t, b, true);
}

void QtDesktopWidget::startCrossfade(const QImage &img, const QColor &top, const QColor &bot, bool useGrad) {
    m_oldWallpaper = m_wallpaper;
    m_oldGradTop = m_gradTop;
    m_oldGradBottom = m_gradBottom;
    m_oldUseGrad = m_useGrad;
    m_wallpaper = img;
    m_gradTop = top;
    m_gradBottom = bot;
    m_useGrad = useGrad;
    m_crossfadeProgress = 0.0f;
    m_crossfading = true;
    if (!m_crossfadeTimer.isActive()) m_crossfadeTimer.start(16);
}

void QtDesktopWidget::paintWallpaperLayer(QPainter &p, const QRect &r,
                                           const QImage &img, bool useGrad,
                                           const QColor &gradTop, const QColor &gradBot) {
    int w = r.width(), h = r.height();
    if (!img.isNull()) {
        p.drawImage(r, img);
    } else if (useGrad) {
        QLinearGradient g(r.x(), r.y(), r.x() + w * 0.3, r.y() + h);
        g.setColorAt(0.0, gradTop);
        g.setColorAt(1.0, gradBot);
        p.fillRect(r, g);
    } else {
/* Golden Gate-style procedural wallpaper */
/* Sky gradient: deep navy → warm amber → golden horizon → magenta */
QLinearGradient sky(0, r.y(), 0, r.y() + h);
sky.setColorAt(0.00, QColor(0x0B, 0x0B, 0x2E));   /* deep navy */
sky.setColorAt(0.20, QColor(0x1A, 0x0A, 0x3E));   /* dark indigo */
sky.setColorAt(0.40, QColor(0x4A, 0x12, 0x42));   /* warm purple */
sky.setColorAt(0.55, QColor(0x8B, 0x2A, 0x2F));   /* deep rose */
sky.setColorAt(0.70, QColor(0xCC, 0x5A, 0x1A));   /* burnt orange */
sky.setColorAt(0.80, QColor(0xE8, 0x8A, 0x1A));   /* golden */
sky.setColorAt(0.88, QColor(0xF0, 0xA8, 0x30));   /* warm gold */
sky.setColorAt(1.00, QColor(0x2A, 0x1A, 0x10));   /* dark shore */
p.fillRect(r, sky);

/* Sun glow — large warm radial near horizon */
int sunCx = r.x() + w * 52 / 100;
int sunCy = r.y() + h * 72 / 100;
int sunR  = w * 30 / 100;
QRadialGradient sunGlow(sunCx, sunCy, sunR);
sunGlow.setColorAt(0.0, QColor(0xFF, 0xCC, 0x44, 90));
sunGlow.setColorAt(0.2, QColor(0xFF, 0x99, 0x33, 60));
sunGlow.setColorAt(0.5, QColor(0xFF, 0x66, 0x22, 25));
sunGlow.setColorAt(0.8, QColor(0xCC, 0x33, 0x11, 8));
sunGlow.setColorAt(1.0, QColor(0x00, 0x00, 0x00, 0));
p.fillRect(r, QBrush(sunGlow));

    /* Sun disc — small bright core */
    QRadialGradient sunDisc(sunCx, sunCy, sunR / 5);
    sunDisc.setColorAt(0.0, QColor(0xFF, 0xEE, 0xBB, 200));
    sunDisc.setColorAt(0.5, QColor(0xFF, 0xCC, 0x66, 100));
    sunDisc.setColorAt(1.0, QColor(0xFF, 0x88, 0x33, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(sunDisc));
    p.drawEllipse(QPoint(sunCx, sunCy), sunR / 4, sunR / 4);

    /* Light rays — radiating from sun */
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 8; i++) {
        double angle = (i * 45.0 + 15.0) * M_PI / 180.0;
        int rayLen = w * 40 / 100;
        int rx = sunCx + (int)(qCos(angle) * rayLen);
        int ry = sunCy + (int)(qSin(angle) * rayLen);
        QLinearGradient rayG(QPointF(sunCx, sunCy), QPointF(rx, ry));
        rayG.setColorAt(0.0, QColor(0xFF, 0xBB, 0x44, 12));
        rayG.setColorAt(0.3, QColor(0xFF, 0x99, 0x33, 6));
        rayG.setColorAt(1.0, QColor(0xFF, 0x66, 0x22, 0));
        p.setBrush(QBrush(rayG));
        p.save();
        p.translate(sunCx, sunCy);
        p.rotate(i * 45.0 + 15.0);
        p.drawRect(0, -3, rayLen, 6);
        p.restore();
    }

/* Water reflection — horizontal streaks below horizon */
int waterTop = r.y() + h * 78 / 100;
QLinearGradient waterG(0, waterTop, 0, r.y() + h);
waterG.setColorAt(0.0, QColor(0xCC, 0x6A, 0x2A, 40));
waterG.setColorAt(0.3, QColor(0x88, 0x44, 0x22, 30));
waterG.setColorAt(1.0, QColor(0x0A, 0x0A, 0x18, 60));
p.fillRect(QRect(r.x(), waterTop, w, r.y() + h - waterTop), waterG);

/* Water shimmer streaks */
p.setPen(Qt::NoPen);
for (int i = 0; i < 12; i++) {
    int sx = r.x() + w * (10 + i * 7) / 100;
    int sy = waterTop + (r.y() + h - waterTop) * (10 + (i * 17) % 70) / 100;
    int sw = w * (3 + (i * 5) % 8) / 100;
    int sh = 1 + (i % 2);
    int a = 15 + (i * 7) % 25;
    p.setBrush(QColor(0xFF, 0xBB, 0x66, a));
    p.drawEllipse(QPoint(sx, sy), sw, sh);
}

/* Bridge silhouette — simplified Golden Gate towers and cables */
int bridgeY = r.y() + h * 62 / 100;
int towerH = h * 22 / 100;
int deckY = bridgeY + towerH * 3 / 5;
int deckH = h * 2 / 100;
QColor bridgeColor(0x0A, 0x06, 0x04, 200);
QColor bridgeColorLight(0x2A, 0x10, 0x08, 140);

/* Left tower */
int ltX = r.x() + w * 25 / 100;
int towerW = qMax(4, w / 200);
p.setBrush(bridgeColor); p.setPen(Qt::NoPen);
p.drawRect(QRect(ltX - towerW/2, bridgeY, towerW, towerH));
p.drawRect(QRect(ltX - towerW*2, bridgeY, towerW*4, h*1/100));

/* Right tower */
int rtX = r.x() + w * 75 / 100;
p.drawRect(QRect(rtX - towerW/2, bridgeY, towerW, towerH));
p.drawRect(QRect(rtX - towerW*2, bridgeY, towerW*4, h*1/100));

/* Deck (horizontal road) */
p.setBrush(bridgeColor);
p.drawRect(QRect(r.x(), deckY, w, deckH));

/* Suspension cables — catenary curves from tower tops to deck */
p.setPen(QPen(bridgeColorLight, qMax(1, towerW/3)));
for (int side = 0; side < 2; side++) {
    int tx = (side == 0) ? ltX : rtX;
    int nextTx = (side == 0) ? rtX : ltX;
    QPainterPath cable;
    cable.moveTo(tx, bridgeY);
    int steps = 30;
    for (int s = 1; s <= steps; s++) {
        double t = (double)s / steps;
        int cx = tx + (int)((nextTx - tx) * t);
        /* Catenary: parabola with lowest point at midpoint */
        double sag = 4.0 * t * (1.0 - t);
        int cy = bridgeY + (int)(sag * (deckY - bridgeY) * 0.3);
        cable.lineTo(cx, cy);
    }
    p.drawPath(cable);
}

/* Main cables — tall curves between tower tops */
p.setPen(QPen(bridgeColorLight, qMax(1, towerW/2)));
for (int side = 0; side < 2; side++) {
    int tx1 = (side == 0) ? ltX : rtX;
    int tx2 = (side == 0) ? rtX : ltX;
    /* Tower top to next tower top with sag */
    int midX = (tx1 + tx2) / 2;
    int sagY = bridgeY - towerH * 2 / 10;
    QPainterPath mainCable;
    mainCable.moveTo(tx1, bridgeY - towerH * 1 / 5);
    mainCable.quadTo(midX, sagY, tx2, bridgeY - towerH * 1 / 5);
    p.drawPath(mainCable);
}

/* Fog layer at bottom — atmospheric haze */
QLinearGradient fog(0, r.y() + h * 82 / 100, 0, r.y() + h);
fog.setColorAt(0.0, QColor(0xCC, 0xAA, 0x88, 15));
fog.setColorAt(0.5, QColor(0x88, 0x66, 0x44, 30));
fog.setColorAt(1.0, QColor(0x22, 0x11, 0x08, 50));
p.fillRect(QRect(r.x(), r.y() + h * 82 / 100, w, h * 18 / 100), fog);

/* Top light sheen */
QLinearGradient tg(0, r.y(), 0, r.y() + h / 10);
tg.setColorAt(0.0, QColor(255, 255, 255, 10));
tg.setColorAt(1.0, QColor(255, 255, 255, 0));
p.fillRect(QRect(r.x(), r.y(), w, h / 10), tg);

/* Stars in upper sky */
p.setPen(Qt::NoPen);
for (int i = 0; i < 20; i++) {
    int sx = r.x() + (i * 137 + 23) % w;
    int sy = r.y() + (i * 89 + 11) % (h / 3);
    int sa = 30 + (i * 13) % 50;
    p.setBrush(QColor(255, 255, 255, sa));
    p.drawEllipse(QPoint(sx, sy), 1, 1);
}
    }
}

void QtDesktopWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    QRect r = rect();
    p.setRenderHint(QPainter::Antialiasing);

    if (m_crossfading && m_crossfadeProgress < 1.0f) {
        /* Crossfade: old layer at (1-t), new layer at t */
        p.setOpacity(1.0f - m_crossfadeProgress);
        paintWallpaperLayer(p, r, m_oldWallpaper, m_oldUseGrad, m_oldGradTop, m_oldGradBottom);
        p.setOpacity(m_crossfadeProgress);
        paintWallpaperLayer(p, r, m_wallpaper, m_useGrad, m_gradTop, m_gradBottom);
        p.setOpacity(1.0f);
    } else {
        paintWallpaperLayer(p, r, m_wallpaper, m_useGrad, m_gradTop, m_gradBottom);
    }

    /* Center-weighted vignette */
    QRadialGradient vig(width()/2.0, height()/2.0,
                        qSqrt(width()*width()+height()*height())/2.0);
    vig.setColorAt(0.0, QColor(0,0,0,0));
    vig.setColorAt(0.5, QColor(0,0,0,0));
    vig.setColorAt(0.75, QColor(0,0,0,15));
    vig.setColorAt(1.0, QColor(0,0,0,110));
    p.fillRect(rect(), QBrush(vig));
}

void QtDesktopWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        /* X11/GNUstep chrome clicks (title-band controls on penrose
         * windows) route here: close / minimize / focus. */
        int ctl = 0;
        uint32_t xid = prs_window_at((int)e->position().x(), (int)e->position().y(), &ctl);
        if (xid) {
            if (ctl == 1) {
                prs_kill_client(xid);
            } else if (ctl == 2) {
                prs_minimize_client(xid);
            } else {
                prs_focus_client(xid);
            }
            return;
        }
    }
    if (e->button() == Qt::RightButton) {
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && mgr->ctxMenu()) {
            QStringList items = {"New Terminal", "System Monitor", "Settings",
                                 "File Manager", "",
                                 "Change Wallpaper", "Toggle Dark Mode", "",
                                 "About CodeOS", "Install CodeOS"};
            std::vector<std::function<void()>> actions;
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(0); });
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(9); });
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(4); });
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(6); });
            actions.push_back([](){});
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(4); });
            actions.push_back([mgr](){
                mgr->setThemeTarget(!mgr->isDarkTheme());
                mgr->showToast(mgr->isDarkTheme() ? "Dark Mode" : "Light Mode",
                               QColor(0xFF,0x5A,0x36));
            });
            actions.push_back([](){});
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(1); });
            actions.push_back([](){ QtDesktopManager::instance()->launchApp(11); });
            QPoint gp = e->globalPosition().toPoint();
            mgr->ctxMenu()->showMenu(gp.x(), gp.y(), items, actions);
        }
    }
}

void QtDesktopWidget::keyPressEvent(QKeyEvent *e) {
    int key = e->key();
    if (key == Qt::Key_Tab && (e->modifiers() & Qt::AltModifier)) {
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && mgr->appSwitcher()) {
            if (!mgr->appSwitcher()->isActive()) mgr->appSwitcher()->activate();
            else if (e->modifiers() & Qt::ShiftModifier) mgr->appSwitcher()->prev();
            else mgr->appSwitcher()->next();
        }
    } else if (key == Qt::Key_F4 && (e->modifiers() & Qt::AltModifier)) {
        /* close the focused X11/GNUstep window */
        uint32_t xid = prs_desktop_focused();
        if (xid) prs_kill_client(xid);
    } else if (key == Qt::Key_F1) {
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && mgr->menubar() && mgr->menubar()->onLauncherToggled)
            mgr->menubar()->onLauncherToggled();
    }
}

void QtDesktopWidget::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Alt) {
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr && mgr->appSwitcher() && mgr->appSwitcher()->isActive())
            mgr->appSwitcher()->commit();
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtMenubar
   ═══════════════════════════════════════════════════════════════════ */

QtMenubar::QtMenubar(QWidget *parent) : QWidget(parent) {
    setFixedHeight(MENUBAR_H); setMouseTracking(true); m_clockText = "12:00 PM";
    connect(&m_badgeTimer, &QTimer::timeout, [this]() {
        m_badgePulse = !m_badgePulse;
        if (m_hasNotifBadge) update();
    });
    m_badgeTimer.start(500);
}

void QtMenubar::setActiveApp(const QString &t) { m_activeApp = t; update(); }
void QtMenubar::setClockText(const QString &t) { if (t == m_clockText) return; m_clockText = t; if (!m_clockRect.isNull()) update(m_clockRect); else update(); }
void QtMenubar::setNotifBadge(bool b) { if (b == m_hasNotifBadge) return; m_hasNotifBadge = b; m_badgePulse = true; update(); }
QStringList QtMenubar::menus() const { return {"File","Edit","View","Go","Window","Help"}; }

/* Default per-menu items: 0=File, 1=Edit, 2=View, 3=Go, 4=Window, 5=Help */
QStringList QtMenubar::defaultMenuItems(int menuIdx) const {
    switch (menuIdx) {
        case 0:  return {"New Window", "Open...", "", "Save", "Save As...", "", "Close Window"};
        case 1:  return {"Undo", "Redo", "", "Cut", "Copy", "Paste", "Select All"};
        case 2:  return {"Enter Full Screen", "", "Zoom In", "Zoom Out", "Reset Zoom"};
        case 3:  return {"Home", "Desktop", "Downloads", "", "Computer"};
        case 4:  return {"Minimize", "Zoom", "", "Bring All to Front"};
        default: return {"About CodeOS", "", "CodeOS Help"};
    }
}

void QtMenubar::closeMenu() { m_menuOpen = false; m_openMenuIdx = -1; update(); }

void QtMenubar::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    int w = width(), h = height();

    /* ── Menubar glass body — macOS 27 unified edge-to-edge ── */
    QLinearGradient barGrad(0, 0, 0, h);
    barGrad.setColorAt(0.0, QColor(0x28,0x28,0x2C,0x78));
    barGrad.setColorAt(0.3, QColor(0x24,0x24,0x28,0x72));
    barGrad.setColorAt(0.7, QColor(0x20,0x20,0x24,0x6C));
    barGrad.setColorAt(1.0, QColor(0x1C,0x1C,0x20,0x66));
    p.fillRect(rect(), barGrad);

    /* Top glass shine — 3-row specular */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xFF,0xFF,0xFF, 10)); p.drawRect(0, 0, w, h/3);
    p.setBrush(QColor(0xFF,0xFF,0xFF,16)); p.drawRect(0, 0, w, 1);
    p.setBrush(QColor(0xFF,0xFF,0xFF, 8)); p.drawRect(0, 1, w, 1);
    p.setBrush(QColor(0xFF,0xFF,0xFF, 4)); p.drawRect(0, 2, w, 1);

    /* Top edge — glass light */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,0x3C)); p.drawRect(0, 0, w, 1);

    /* Bottom edge — dark separator + subtle highlight above */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,0x30)); p.drawRect(0, h-1, w, 1);
    p.setBrush(QColor(255,255,255,3)); p.drawRect(0, h-2, w, 1);

    /* ── Apple logo glyph ── */
    QFont f = font(); f.setPointSize(13); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xFF,0x5A,0x36));  /* GG Orange accent */
    QFontMetrics fm(f);
    QString apple = QString::fromUtf8("\xE2\x9D\xB5");
    m_appleRect = QRect(12, 0, fm.horizontalAdvance(apple)+4, height());
    p.drawText(m_appleRect, Qt::AlignCenter, apple);

    /* Vertical separator after apple */
    p.setPen(QPen(QColor(0x63,0x63,0x66,0x44),1));
    p.drawLine(m_appleRect.right()+4, 4, m_appleRect.right()+4, h-4);

    /* ── Active app name (bold blue) ── */
    int xOff = m_appleRect.right()+14;
    f.setBold(false); f.setPointSize(11); p.setFont(f);
    QFontMetrics fm2(f);
    QString appText = m_activeApp.isEmpty() ? "Finder" : m_activeApp;

    /* Small app icon */
    if (!m_activeApp.isEmpty()) {
        QRect iconR(xOff, (h-16)/2, 16, 16);
        drawAppIcon(p, iconR, m_activeApp, 16);
        xOff += 20;
    }

    m_appNameRect = QRect(xOff, 0, fm2.horizontalAdvance(appText)+10, height());
    p.setPen(QColor(0xFF,0x5A,0x36));  /* GG Orange */
    p.drawText(m_appNameRect.adjusted(4,0,0,0), Qt::AlignVCenter|Qt::AlignLeft, appText);

    /* ── Menu items ── */
    xOff = m_appNameRect.right()+16;
    m_menuRects.clear();
    for (const QString &menu : menus()) {
        QRect mr(xOff, 0, fm2.horizontalAdvance(menu)+12, height());
        m_menuRects.append(mr);
        if (m_hoveredMenu == m_menuRects.size()-1) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255,255,255,35));
            p.drawRoundedRect(mr.adjusted(0,2,0,-2), 6, 6);
        }
        p.setPen(m_hoveredMenu==m_menuRects.size()-1 ? QColor(0xFF,0x5A,0x36) : c_subtext);
        p.drawText(mr, Qt::AlignCenter, menu);
        p.setPen(Qt::NoPen); xOff = mr.right();
    }

    /* ── Right-side status: workspace dots + clock ── */
    QDateTime now = QDateTime::currentDateTime();
    QString dtStr = now.toString("ddd MMM d  h:mm AP");
    f.setPointSize(10); f.setBold(false); p.setFont(f);
    QFontMetrics fm3(f);
    int tw = fm3.horizontalAdvance(dtStr)+20;

    /* Notification badge */
    if (m_hasNotifBadge && m_badgePulse) {
        int bx = width()-tw-20;
        m_notifRect = QRect(bx, (height()-16)/2, 16, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xFF,0x5A,0x36, 0x18)); p.drawEllipse(m_notifRect.adjusted(-2,-2,2,2));
        p.setBrush(QColor(0xFF,0x5A,0x36)); p.drawEllipse(m_notifRect);
    } else { m_notifRect = QRect(); }

    /* Clock */
    m_clockRect = QRect(width()-tw-8, 0, tw, height());
    p.setPen(c_subtext);
    p.drawText(m_clockRect.adjusted(4,0,-4,0), Qt::AlignVCenter|Qt::AlignRight, dtStr);

    /* ── Dropdown menu ── */
    if (m_menuOpen && m_openMenuIdx >= 0 && m_openMenuIdx < m_menuRects.size()) {
        QRect anchor = m_menuRects[m_openMenuIdx];
        QStringList items = m_menuItems;
        if (items.isEmpty())
            items = defaultMenuItems(m_openMenuIdx);
        int maxW = 0;
        QFont df = font(); df.setPointSize(10); df.setBold(false); p.setFont(df);
        QFontMetrics dfm(df);
        for (const QString &it : items) { int w = dfm.horizontalAdvance(it)+40; if (w>maxW) maxW = w; }
        maxW = qMax(maxW, 160);
        int itemH = 26, padY = 6;
        int totalH = items.size() * itemH + padY * 2;
        QRect menuRect(anchor.x(), anchor.bottom()+1, maxW, totalH);

        /* Menu shadow + glass body — macOS 27 */
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,50));
        p.drawRoundedRect(menuRect.adjusted(-4,-6,4,6), 12, 12);
        QLinearGradient mg(menuRect.topLeft(), menuRect.bottomLeft());
        mg.setColorAt(0, QColor(0x30,0x30,0x34,150));
        mg.setColorAt(0.5, QColor(0x28,0x28,0x2C,145));
        mg.setColorAt(1, QColor(0x22,0x22,0x26,140));
        p.setBrush(mg);
        p.setPen(QPen(QColor(255,255,255,20),1));
        p.drawRoundedRect(menuRect, 12, 12);
        /* Top inner highlight */
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255,255,255,22));
        p.drawRect(menuRect.x()+12, menuRect.y()+1, menuRect.width()-24, 1);

        /* Items */
        for (int i = 0; i < items.size(); i++) {
            QRect ir(menuRect.x(), menuRect.y()+padY+i*itemH, menuRect.width(), itemH);
            if (items[i].isEmpty()) {
                /* Separator — refined */
                p.setPen(QPen(QColor(99,99,102,60),1));
                p.drawLine(ir.x()+12, ir.center().y(), ir.right()-12, ir.center().y());
                continue;
            }
            /* Hover highlight — GG Orange */
            if (ir.contains(mapFromGlobal(cursor().pos()))) {
                p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36,55));
                p.drawRoundedRect(ir.adjusted(4,0,-4,0), 6, 6);
            }
            p.setPen(ir.contains(mapFromGlobal(cursor().pos())) ? QColor(0xFF,0x5A,0x36) : c_text); p.setFont(df);
            p.drawText(ir.adjusted(16,0,-8,0), Qt::AlignVCenter|Qt::AlignLeft, items[i]);
        }
    }
}

void QtMenubar::mousePressEvent(QMouseEvent *e) {
    QPoint pos = e->pos();

    /* If menu is open, check if clicking a menu item or closing */
    if (m_menuOpen) {
        QStringList items = m_menuItems;
        if (items.isEmpty())
            items = defaultMenuItems(m_openMenuIdx);
        QRect anchor = m_menuRects[m_openMenuIdx];
        int maxW = 160;
        QFont df = font(); df.setPointSize(10);
        QFontMetrics dfm(df);
        for (const QString &it : items) { int w = dfm.horizontalAdvance(it)+40; if (w>maxW) maxW = w; }
        int itemH = 26, padY = 6;
        int totalH = items.size() * itemH + padY * 2;
        QRect menuRect(anchor.x(), anchor.bottom()+1, maxW, totalH);
        if (menuRect.contains(pos)) {
            int idx = (pos.y() - menuRect.y() - padY) / itemH;
            if (idx >= 0 && idx < items.size() && !items[idx].isEmpty()) {
                /* Execute menu action */
                QString action = items[idx];
                if (action == "Close Window") { QWidget *aw = QApplication::activeWindow(); if (aw) aw->close(); }
                else if (action == "Minimize") { QWidget *aw = QApplication::activeWindow(); if (aw) aw->showMinimized(); }
                else if (action == "Enter Full Screen") { QWidget *aw = QApplication::activeWindow(); if (aw) aw->showFullScreen(); }
                else if (action == "About CodeOS") { QtDesktopManager::instance()->launchApp(1); }
                else if (action == "New Window") { QtDesktopManager::instance()->launchApp(0); }
                else if (action == "Zoom In" || action == "Zoom Out" || action == "Reset Zoom") { /* visual only */ }
                else if (onAppMenuClicked) onAppMenuClicked();
            }
            m_menuOpen = false; m_openMenuIdx = -1; update();
            return;
        } else {
            /* Click outside closes menu */
            m_menuOpen = false; m_openMenuIdx = -1;
        }
    }

    if (m_appleRect.contains(pos) && onLauncherToggled) onLauncherToggled();
    if (m_notifRect.contains(pos) && onNotifToggled) onNotifToggled();
    for (int i = 0; i < m_menuRects.size(); i++) {
        if (m_menuRects[i].contains(pos)) {
            if (m_menuOpen && m_openMenuIdx == i) {
                /* Toggle off */
                m_menuOpen = false; m_openMenuIdx = -1;
            } else {
                m_menuOpen = true; m_openMenuIdx = i;
                m_menuItems.clear();
            }
            update(); return;
        }
    }
    /* Click on menubar outside menus closes */
    if (m_menuOpen) { m_menuOpen = false; m_openMenuIdx = -1; update(); }
}

void QtMenubar::mouseMoveEvent(QMouseEvent *e) {
    QPoint pos = e->pos();
    /* Hover over menu items when dropdown is open */
    if (m_menuOpen) {
        update(); /* redraw hover state */
        return;
    }
    /* Hover over menu bar items */
    int old = m_hoveredMenu; m_hoveredMenu = -1;
    for (int i = 0; i < m_menuRects.size(); i++) {
        if (m_menuRects[i].contains(pos)) { m_hoveredMenu = i; break; }
    }
    if (m_menuOpen && m_hoveredMenu >= 0 && m_hoveredMenu != m_openMenuIdx) {
        /* Switch to different menu while holding */
        m_openMenuIdx = m_hoveredMenu; m_menuItems.clear();
    }
    if (old != m_hoveredMenu) update();
}

void QtMenubar::mouseReleaseEvent(QMouseEvent *) {}

/* ═══════════════════════════════════════════════════════════════════
   QtDock
   ═══════════════════════════════════════════════════════════════════ */

QtDock::QtDock(QWidget *parent) : QWidget(parent) {
    setFixedHeight(DOCK_H); setMouseTracking(true);
    connect(&m_animTimer, &QTimer::timeout, this, &QtDock::updateAnimations);
    /* Don't start timer unconditionally — start on hover */
}

void QtDock::setItems(const QStringList &names) {
    m_items.clear();
    for (int i = 0; i < names.size() && i < APP_COUNT; i++) {
        DockItem item; item.name = names[i]; m_items.append(item);
    }
    update();
}

void QtDock::setRunning(int i, bool r) { if (i>=0 && i<m_items.size()) { m_items[i].running = r; update(); } }
void QtDock::setBadge(int i, int c) { if (i>=0 && i<m_items.size()) { m_items[i].badge = c; update(); } }
void QtDock::setHover(int i, bool h) { m_hoveredIndex = h ? i : -1; update(); }

void QtDock::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    int count = m_items.size(); if (!count) return;

    /* Fit the dock into the window: with many items the default 56px icons
     * and 20px slots overflow, which pushes the trailing icons (the app
     * pickers) off-screen where they cannot be clicked. Tighten the slot
     * spacing, and only shrink the icons if spacing alone is not enough. */
    int iconSz = m_iconSize;
    int itemW = m_iconSize + 20;
    int avail = qMax(1, width() - 16);
    if (count * itemW > avail) {
        itemW = qMax(1, avail / count);
        iconSz = qBound(24, itemW - 10, m_iconSize);
    }
    int totalW = count * itemW + 16;
    int startX = qMax(8, (width()-totalW)/2);
    int iconY = qMax(2, (height()-iconSz-10)/2);

    QRect dockBg(startX-8, iconY-8, totalW+16, iconSz+30);

    /* ── Dock multi-layer shadow (3 layers, like devos2) ── */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0, 8));  p.drawRoundedRect(dockBg.adjusted(1,5,-1,5), 22,22);
    p.setBrush(QColor(0,0,0,16));  p.drawRoundedRect(dockBg.adjusted(0,3,0,3), 20,20);
    p.setBrush(QColor(0,0,0,20));  p.drawRoundedRect(dockBg.adjusted(-2,1,2,2), 20,20);

    /* ── Dock glass pill body — macOS 27 refined ── */
    QLinearGradient dockGrad(dockBg.topLeft(), dockBg.bottomLeft());
    dockGrad.setColorAt(0.0, QColor(0x30,0x30,0x34,135));
    dockGrad.setColorAt(0.1, QColor(0x34,0x34,0x38,130));
    dockGrad.setColorAt(0.3, QColor(0x2C,0x2C,0x30,125));
    dockGrad.setColorAt(0.5, QColor(0x24,0x24,0x28,120));
    dockGrad.setColorAt(0.7, QColor(0x2C,0x2C,0x30,125));
    dockGrad.setColorAt(0.9, QColor(0x34,0x34,0x38,130));
    dockGrad.setColorAt(1.0, QColor(0x30,0x30,0x34,135));
    p.setPen(QPen(QColor(255,255,255,22),1)); p.setBrush(dockGrad);
    p.drawRoundedRect(dockBg, 20, 20);

    /* Inner glass gradient (subtle top-to-bottom lightening) */
    QRect innerGrad(dockBg.x()+20, dockBg.y()+1, dockBg.width()-40, dockBg.height()-2);
    QLinearGradient ig(innerGrad.topLeft(), innerGrad.bottomLeft());
    ig.setColorAt(0, QColor(255,255,255,10));
    ig.setColorAt(0.5, QColor(255,255,255,4));
    ig.setColorAt(1, QColor(0,0,0,8));
    p.setPen(Qt::NoPen); p.setBrush(ig); p.drawRoundedRect(innerGrad, 18, 18);

    /* Glass highlight band (top — crisp 2px) */
    QRect hlBand(dockBg.x()+28, dockBg.y()+1, dockBg.width()-56, 2);
    QLinearGradient hl(hlBand.topLeft(), hlBand.bottomLeft());
    hl.setColorAt(0, QColor(255,255,255,24)); hl.setColorAt(1, QColor(255,255,255,0));
    p.setPen(Qt::NoPen); p.setBrush(hl); p.drawRect(hlBand);

    /* Hairline specular */
    p.setPen(QPen(QColor(255,255,255,4),1)); p.drawLine(dockBg.x()+36, dockBg.y()+3, dockBg.right()-36, dockBg.y()+3);

    /* Bottom reflection bar — subtle warm glow */
    QRect reflBar(dockBg.x()+30, dockBg.bottom()-4, dockBg.width()-60, 2);
    QLinearGradient refl(reflBar.topLeft(), reflBar.bottomLeft());
    refl.setColorAt(0, QColor(255,0x9F,0x0A,8));
    refl.setColorAt(1, QColor(255,0x5A,0x36,4));
    p.setPen(Qt::NoPen); p.setBrush(refl); p.drawRect(reflBar);

    /* Glass border + outer focus glow ring (double ring) */
    p.setPen(QPen(QColor(255,255,255,18),1)); p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(dockBg, 20, 20);
    p.setPen(QPen(QColor(0xFF,0x5A,0x36,0x22),1));
    p.drawRoundedRect(dockBg.adjusted(-1,-1,1,1), 21, 21);

    for (int i = 0; i < count; i++) {
        int ix = startX+8+i*itemW;
        m_items[i].rect = QRect(ix, iconY, iconSz, iconSz);

        float t = m_items[i].hoverProgress;
        int maxIcon = iconSz + (m_maxIconSize - m_iconSize);
        int sz = iconSz + (int)((maxIcon-iconSz)*t);
        int bdy = m_items[i].bouncing ? m_items[i].bounceOffset : 0;
        QRect dr(ix+(iconSz-sz)/2, iconY+(iconSz-sz)/2+bdy, sz, sz);
        m_items[i].magnifiedRect = dr;

        /* ── Icon glow ring (when magnified) — COSMIC cyan ── */
        if (t > 0.15f) {
            float ga = (t - 0.15f) / 0.85f;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x40,0xD9,0xF0, (int)(42*ga)));
            p.drawRoundedRect(dr.adjusted(-5,-5,5,5), 14, 14);
            p.setBrush(QColor(0x40,0xD9,0xF0, (int)(26*ga)));
            p.drawRoundedRect(dr.adjusted(-3,-3,3,3), 11, 11);
            p.setBrush(QColor(0x63,0x63,0x66, (int)(58*ga)));
            p.drawRoundedRect(dr.adjusted(-1,-1,1,1), 9, 9);
        }

        /* ── Procedural icon ── */
        drawAppIcon(p, dr, m_items[i].name, sz);

        /* ── Running indicator dot (cyane glow, COSMIC-style) ── */
        if (m_items[i].running) {
            int dotCx = ix + iconSz/2;
            int dotCy = iconY + iconSz + 8;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x40,0xD9,0xF0, 0x18)); p.drawEllipse(QPoint(dotCx,dotCy), 5, 5);
            p.setBrush(QColor(0x40,0xD9,0xF0, 0x40)); p.drawEllipse(QPoint(dotCx,dotCy), 4, 4);
            p.setBrush(QColor(0x40,0xD9,0xF0));       p.drawEllipse(QPoint(dotCx,dotCy), 3, 3);
            p.setPen(QPen(QColor(255,255,255,60),1));  p.drawLine(dotCx-1,dotCy-2,dotCx+1,dotCy-2);
        }

        /* Badge */
        if (m_items[i].badge > 0) {
            QString bs = QString::number(m_items[i].badge);
            QFont bf = font(); bf.setPointSize(8); p.setFont(bf);
            QFontMetrics bfm(bf);
            int bw = bfm.horizontalAdvance(bs)+8;
            QRect br(ix+iconSz-bw/2, iconY-4, bw, 14);
            p.setBrush(c_red); p.drawRoundedRect(br, 7, 7);
            p.setPen(Qt::white); p.drawText(br, Qt::AlignCenter, bs);
        }

        /* Label (fades in when hovered) */
        if (t > 0.5f) {
            QFont lf = font(); lf.setPointSize(9); lf.setBold(false); p.setFont(lf);
            QFontMetrics fm2(lf);
            QString label = m_items[i].name;
            int lw = fm2.horizontalAdvance(label);
            QRect labelRect(ix+(iconSz-lw)/2, iconY-20, lw+8, 16);
            int labelAlpha = (int)((t-0.5f)*2.0f*220);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20,20,22,labelAlpha));
            p.drawRoundedRect(labelRect.adjusted(-4,-2,4,2), 6, 6);
            p.setPen(QColor(245,245,247,labelAlpha));
            p.drawText(labelRect, Qt::AlignCenter, label);
        }

        /* ── Icon reflection below dock (mirrors magnified icons) ── */
        if (t > 0.2f) {
            int refH = qMin(sz/3, 24);
            int refX = dr.x();
            int refY = dockBg.bottom() + 2;
            QLinearGradient rg(refX, refY, refX, refY + refH);
            QColor rc = appIconColor(m_items[i].name);
            rc.setAlpha((int)(32 * t));
            rg.setColorAt(0.0, rc);
            rc.setAlpha(0);
            rg.setColorAt(1.0, rc);
            p.setPen(Qt::NoPen); p.setBrush(rg);
            p.drawRoundedRect(QRect(refX, refY, sz, refH), 4, 4);
        }
    }
}

void QtDock::mousePressEvent(QMouseEvent *e) {
    if (e->button()!=Qt::LeftButton) return;
    if (!m_items.isEmpty()) {
        QRect r0 = m_items[0].rect, rn = m_items[m_items.size()-1].rect;
        kprintf("DOCKPRESS btn=%d pos=(%d,%d) item0=(%d,%d,%d,%d) itemN=(%d,%d,%d,%d)\n",
                (int)e->button(), e->x(), e->y(),
                r0.x(), r0.y(), r0.width(), r0.height(),
                rn.x(), rn.y(), rn.width(), rn.height());
    }
    for (int i = 0; i < m_items.size(); i++) {
        QRect hr = m_items[i].magnifiedRect.isValid() ? m_items[i].magnifiedRect : m_items[i].rect;
        if (hr.contains(e->pos())) { m_pressedIndex = i; kprintf("DOCKPRESS hit i=%d\n", i); return; }
    }
}

void QtDock::mouseMoveEvent(QMouseEvent *e) {
    int old = m_hoveredIndex; m_hoveredIndex = -1;
    for (int i = 0; i < m_items.size(); i++) {
        QRect hr = m_items[i].magnifiedRect.isValid() ? m_items[i].magnifiedRect : m_items[i].rect;
        if (hr.contains(e->pos())) { m_hoveredIndex = i; break; }
    }
    if (m_hoveredIndex >= 0 && !m_animTimer.isActive()) m_animTimer.start(16);
    if (old != m_hoveredIndex) update();
}

void QtDock::leaveEvent(QEvent *) {
    m_hoveredIndex = -1;
    m_animTimer.stop();
    for (int i = 0; i < m_items.size(); i++) {
        m_items[i].hoverProgress = 0.0f;
        m_items[i].hoverVelocity = 0.0f;
    }
    update();
}

void QtDock::mouseReleaseEvent(QMouseEvent *e) {
    kprintf("DOCKREL btn=%d pos=(%d,%d) pressed=%d\n",
            (int)e->button(), e->x(), e->y(), m_pressedIndex);
    if (e->button()==Qt::LeftButton && m_pressedIndex >= 0) {
        QRect hr = m_items[m_pressedIndex].magnifiedRect.isValid() ? m_items[m_pressedIndex].magnifiedRect : m_items[m_pressedIndex].rect;
        if (hr.contains(e->pos()) && onItemClicked) { kprintf("DOCKREL click i=%d\n", m_pressedIndex); onItemClicked(m_pressedIndex); }
    }
    m_pressedIndex = -1; update();
}

void QtDock::startBounce(int index) {
    if (index < 0 || index >= m_items.size()) return;
    m_items[index].bouncing = true;
    m_items[index].bounceT = 0.0f;
    if (!m_animTimer.isActive()) m_animTimer.start(16);
}

void QtDock::updateAnimations() {
    bool dirty = false;
    bool anyBounce = false;
    for (int i = 0; i < m_items.size(); i++) {
        float target = 0.0f;
        if (m_hoveredIndex == i) target = 1.0f;
        else if (m_hoveredIndex >= 0) {
            int dist = qAbs(i - m_hoveredIndex);
            if (dist == 1) target = 0.55f;
            else if (dist == 2) target = 0.25f;
        }
        if (qAbs(m_items[i].hoverProgress - target) > 0.01f) {
            m_items[i].hoverVelocity += (target - m_items[i].hoverProgress) * 0.35f;
            m_items[i].hoverVelocity *= 0.75f;
            m_items[i].hoverProgress += m_items[i].hoverVelocity;
            dirty = true;
        }
        if (m_items[i].bouncing) {
            /* two decaying arcs over ~1s */
            m_items[i].bounceT += 1.0f / 60.0f;
            float bt = m_items[i].bounceT;
            if (bt >= 1.0f) {
                m_items[i].bouncing = false;
                m_items[i].bounceOffset = 0;
            } else {
                float decay = 1.0f - bt;
                m_items[i].bounceOffset =
                    (int)(-qFabs(qSin(bt * 6.2831853f)) * 22.0f * decay);
            }
            dirty = true;
        }
        anyBounce |= m_items[i].bouncing;
    }
    if (dirty) update();
    if (!dirty && !anyBounce && m_hoveredIndex < 0) m_animTimer.stop();
}

void QtDock::contextMenuEvent(QContextMenuEvent *e) {
    for (int i = 0; i < m_items.size(); i++) {
        if (m_items[i].rect.contains(e->pos()) || m_items[i].magnifiedRect.contains(e->pos())) {
            int appIdx = i;
            QMenu menu(this);
            menu.setStyleSheet("QMenu{background:#1E1E20;color:#F5F5F7;border:1px solid #3A3A3C;border-radius:8px;padding:4px;}"
                               "QMenu::item{padding:8px 24px;border-radius:4px;}"
                               "QMenu::item:selected{background:#3A3A3C;}");
            if (m_items[i].running) {
                QAction *quitAct = menu.addAction("Quit");
                connect(quitAct, &QAction::triggered, [appIdx]() {
                    QtDesktopManager *mgr = QtDesktopManager::instance();
                    if (mgr && mgr->appWindows()[appIdx]) mgr->appWindows()[appIdx]->close();
                });
            } else {
                QAction *openAct = menu.addAction("Open");
                connect(openAct, &QAction::triggered, [appIdx]() {
                    QtDesktopManager *instance = QtDesktopManager::instance();
                    if (instance) instance->launchApp(appIdx);
                });
            }
            menu.exec(e->globalPos()); break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtHyperdeStrip
   ═══════════════════════════════════════════════════════════════════ */

void QtHyperdeStrip::mousePressEvent(QMouseEvent *e) {
    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (!mgr) return;
    int code = hyperde_shell_bar_hit((int)e->position().x(), (int)e->position().y());
    if (code == 1 || code == 903) {
        mgr->toggleLauncher();          /* logo / clock → app grid */
    } else if (code >= 920 && code < 930) {
        if (mgr->tilingManager()) mgr->tilingManager()->setWorkspace(code - 920);
    } else if (code >= 900 && code <= 905) {
        mgr->toggleQuickSettings();     /* NET/CPU/MEM/battery/power → quick settings */
    } else if (code >= 2000) {
        /* X11/GNUstep task pill → focus that desktop window */
        prs_desktop_win_t wins[PRS_WIN_MAX];
        int n = prs_desktop_windows(wins, PRS_WIN_MAX);
        int idx = code - 2000;
        if (idx >= 0 && idx < n && wins[idx].mapped) {
            prs_focus_client(wins[idx].xid);
        }
    } else if (code >= 1000) {
        mgr->focusHyperdeWindow(code - 1000);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtLauncherOverlay
   ═══════════════════════════════════════════════════════════════════ */

QtLauncherOverlay::QtLauncherOverlay(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
}

void QtLauncherOverlay::showLauncher(bool androidMode) {
    m_open = true; m_androidMode = androidMode;
    m_searchText.clear(); m_currentPage = 0; m_hoveredIndex = -1; m_selIndex = 0;
    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (mgr) {
        m_appNames = mgr->appNames();
        m_androidLabels = mgr->androidAppNames();
    }
    if (m_appNames.isEmpty()) m_appNames = QStringList{"Terminal","About","Calc","Settings","OpenWeb",
                                                       "Explorer","Exit","Sys Info","SysMon",
                                                       "Install CodeOS","LT","NetBeam","Ziggy","Notes","Clock",
                                                       "Convert","LaunchApp","Android"};
    updateGrid(); rebuildItemRects(); show(); raise(); setFocus(); update();
}

void QtLauncherOverlay::hideLauncher() { m_open = false; m_searchText.clear(); m_hoveredIndex = -1; m_selIndex = 0; hide(); }

void QtLauncherOverlay::updateGrid() {
    m_filteredNames.clear();
    /* COSMIC-style fuzzy search: prefix ranks first, then substring matches.
     * LaunchApp is a dock-only picker — keep it out of the grid so selecting
     * it can't toggle/close the overlay. Android mode lists the guest apps. */
    const QStringList &src = m_androidMode ? m_androidLabels : m_appNames;
    QList<QPair<QString,int>> tagged;
    if (m_searchText.isEmpty()) {
        for (int i = 0; i < src.size(); i++) {
            if (!m_androidMode && i == LAUNCHAPP_INDEX) continue;
            tagged.append({src[i], i});
        }
    } else {
        QString q = m_searchText.toLower();
        for (int i = 0; i < src.size(); i++) {
            if (!m_androidMode && i == LAUNCHAPP_INDEX) continue;
            QString n = src[i];
            QString l = n.toLower();
            if (l.contains(q)) tagged.append({n, i});
        }
        /* stable sort: prefix matches float to the front */
        std::stable_sort(tagged.begin(), tagged.end(), [&](const QPair<QString,int>&a, const QPair<QString,int>&b){
            bool pa = a.first.toLower().startsWith(m_searchText.toLower());
            bool pb = b.first.toLower().startsWith(m_searchText.toLower());
            if (pa != pb) return pa;
            return a.second < b.second;
        });
    }
    for (const auto &t : tagged) m_filteredNames.append(t.first);
    m_currentPage = 0; m_hoveredIndex = -1; m_selIndex = 0;
    rebuildItemRects();
}

void QtLauncherOverlay::rebuildItemRects() {
    m_itemRects.clear();
    if (m_filteredNames.isEmpty()) return;
    /* Must mirror paintEvent's grid geometry so clicks map to the current page. */
    int panelW = qMin(900, width()-80);
    int panelH = qMin(height()-120, 680);
    QRect ctr((width()-panelW)/2, (height()-panelH)/2, panelW, panelH);
    QRect sr = ctr.adjusted(24,24,-24,0); sr.setHeight(48);
    int cols = 5, cellW = 140, cellH = 120;
    int gridW = cols*cellW;
    int gx = ctr.x()+(ctr.width()-gridW)/2;
    int gy = sr.bottom()+24;
    int start = m_currentPage*m_itemsPerPage;
    int end = qMin(start+m_itemsPerPage, m_filteredNames.size());
    for (int i = start; i < end; i++) {
        int idx = i - start;
        m_itemRects.append(QRect(gx+(idx%cols)*cellW+10, gy+(idx/cols)*cellH+10, cellW-20, cellH-20));
    }
}

void QtLauncherOverlay::paintEvent(QPaintEvent *) {
    if (!m_open) return;
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);

    /* ── Multi-layer dark backdrop with radial vignette ── */
    p.fillRect(rect(), QColor(0,0,0,140));
    QRadialGradient vig(width()/2.0, height()/2.0, qMin(width(),height())/1.8);
    vig.setColorAt(0.0, QColor(0,0,0,0));
    vig.setColorAt(0.5, QColor(0,0,0,10));
    vig.setColorAt(0.75, QColor(0,0,0,35));
    vig.setColorAt(1.0, QColor(0,0,0,90));
    p.fillRect(rect(), QBrush(vig));

    int panelW = qMin(900, width()-80);
    int panelH = qMin(height()-120, 680);
    QRect ctr((width()-panelW)/2, (height()-panelH)/2, panelW, panelH);

    /* ── Panel shadow (2-layer: ambient + key) ── */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,80));
    p.drawRoundedRect(ctr.adjusted(-20,-26,20,26), 30, 30);
    p.setBrush(QColor(0,0,0,110));
    p.drawRoundedRect(ctr.adjusted(-8,-12,8,12), 26, 26);

    /* ── Panel glass body (5-stop vertical gradient) ── */
    QLinearGradient bg(0, ctr.top(), 0, ctr.bottom());
    bg.setColorAt(0.0, QColor(28,28,32,155));
    bg.setColorAt(0.1, QColor(32,32,36,145));
    bg.setColorAt(0.5, QColor(38,38,42,135));
    bg.setColorAt(0.9, QColor(32,32,36,145));
    bg.setColorAt(1.0, QColor(28,28,32,155));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,22),1));
    p.drawRoundedRect(ctr, 24, 24);

    /* Top inner highlight (1px bright line) */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,38));
    p.drawRect(ctr.x()+24, ctr.y()+1, ctr.width()-48, 1);
    /* Bottom subtle separator */
    p.setBrush(QColor(255,255,255,8));
    p.drawRect(ctr.x()+24, ctr.bottom(), ctr.width()-48, 1);

    /* ── Search bar (COSMIC: rounded, accent focus underline) ── */
    QRect sr = ctr.adjusted(24,24,-24,0); sr.setHeight(48);
    QLinearGradient sbBg(sr.topLeft(), sr.bottomLeft());
    sbBg.setColorAt(0.0, QColor(16,16,18,200));
    sbBg.setColorAt(1.0, QColor(12,12,14,190));
    p.setBrush(sbBg);
    p.setPen(QPen(QColor(255,255,255,20),1));
    p.drawRoundedRect(sr, 12, 12);

    /* search bar is focused whenever the launcher is open → accent underline */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x40,0xD9,0xF0, 220));
    p.drawRoundedRect(QRect(sr.x()+14, sr.bottom()-3, sr.width()-28, 3), 2, 2);

    /* Vector search icon (lens + handle) — accent-tinted */
    int iconCx = sr.x()+24, iconCy = sr.y()+24;
    p.setPen(QPen(QColor(0x40,0xD9,0xF0,220), 1.8, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(iconCx, iconCy), 7, 7);
    p.drawLine(QPoint(iconCx+5, iconCy+5), QPoint(iconCx+9, iconCy+9));

    /* Search text */
    QFont f = font(); f.setPointSize(15); p.setFont(f);
    p.setPen(m_searchText.isEmpty() ? c_subtext : c_text);
    p.drawText(QRect(sr.x()+42, sr.y(), sr.width()-52, 48), Qt::AlignVCenter|Qt::AlignLeft,
               m_searchText.isEmpty() ? (m_androidMode ? "Search Android apps..." : "Search apps...")
                                      : m_searchText);

    /* ── App grid ── */
    int cols = 5, cellW = 140, cellH = 120;
    int gridW = cols*cellW;
    int gx = ctr.x()+(ctr.width()-gridW)/2;
    int gy = sr.bottom()+24;
    int start = m_currentPage*m_itemsPerPage;
    int end = qMin(start+m_itemsPerPage, m_filteredNames.size());

    m_itemRects.clear();
    int highlight = (m_hoveredIndex >= 0) ? m_hoveredIndex : m_selIndex;
    for (int i = start; i < end; i++) {
        int idx = i - start;
        QRect ir(gx+(idx%cols)*cellW+10, gy+(idx/cols)*cellH+10, cellW-20, cellH-20);
        m_itemRects.append(ir);

        if (idx == highlight) {
            /* Accent-tinted selection glow + border (hover & keyboard share it) */
            QRadialGradient hglow(ir.center(), ir.width()*0.7);
            hglow.setColorAt(0, QColor(0x40,0xD9,0xF0,60));
            hglow.setColorAt(0.6, QColor(0x40,0xD9,0xF0,22));
            hglow.setColorAt(1, QColor(0x40,0xD9,0xF0,0));
            p.setBrush(hglow); p.setPen(Qt::NoPen); p.drawRoundedRect(ir, 16, 16);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0x40,0xD9,0xF0,200), 1.5));
            p.drawRoundedRect(ir, 16, 16);
        }

        QRect iconRect(ir.x()+20, ir.y()+12, 70, 70);
        QString name = m_filteredNames[i];

        /* Procedural icon */
        drawAppIcon(p, iconRect, name, 70);

        f.setPointSize(11); f.setBold(false); p.setFont(f); p.setPen(c_text);
        p.drawText(QRect(ir.x(), iconRect.bottom()+8, ir.width(), 20), Qt::AlignHCenter|Qt::AlignTop, name);

        QtDesktopManager *mgr = QtDesktopManager::instance();
        int appIdx = mgr ? mgr->appNames().indexOf(name) : -1;
        if (appIdx >= 0 && mgr && mgr->appRunning(appIdx)) {
            p.setBrush(QColor(0x40,0xD9,0xF0)); p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(ir.center().x(), ir.bottom()-6), 3, 3);
        }

        /* keyboard-selected index badge (COSMIC uses a count) */
        if (idx == m_selIndex && m_hoveredIndex < 0) {
            QFont sf = font(); sf.setPointSize(10); sf.setBold(true);
            QFontMetrics sm(sf);
            QString numStr = QString::number(visibleIndex()+1);
            int bw = sm.horizontalAdvance(numStr)+8, bh = 16;
            QRect br(ir.right()-bw-6, ir.y()+6, bw, bh);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0x40,0xD9,0xF0,235));
            p.drawRoundedRect(br, 8, 8);
            p.setPen(QColor(0x14,0x2A,0x33)); p.setFont(sf);
            p.drawText(br, Qt::AlignCenter, numStr);
            p.setFont(f);
        }
    }

    /* Page dots */
    int totalPages = (m_filteredNames.size()+m_itemsPerPage-1)/m_itemsPerPage;
    if (totalPages > 1) {
        int ds = 8, dsp = 16;
        int dw = totalPages*dsp-(dsp-ds);
        int dx = (width()-dw)/2, dy = ctr.bottom()-40;
        for (int i = 0; i < totalPages; i++) {
            p.setBrush(i==m_currentPage ? QColor(0x40,0xD9,0xF0) : QColor(99,99,102,100));
            p.setPen(Qt::NoPen); p.drawEllipse(dx+i*dsp, dy, ds, ds);
        }
    }
}

void QtLauncherOverlay::activateName(const QString &name) {
    if (m_androidMode) {
        int ai = m_androidLabels.indexOf(name);
        if (ai >= 0) { hideLauncher(); if (onAndroidSelected) onAndroidSelected(ai); }
    } else {
        int realIdx = m_appNames.indexOf(name);
        if (realIdx >= 0) { hideLauncher(); if (onAppSelected) onAppSelected(realIdx); }
    }
}

void QtLauncherOverlay::mousePressEvent(QMouseEvent *e) {
    if (e->button()!=Qt::LeftButton) return;
    for (int i = 0; i < m_itemRects.size(); i++) {
        if (m_itemRects[i].contains(e->pos())) {
            int appIdx = m_currentPage*m_itemsPerPage+i;
            if (appIdx < m_filteredNames.size()) activateName(m_filteredNames[appIdx]);
            return;
        }
    }
    hideLauncher();
}

void QtLauncherOverlay::mouseMoveEvent(QMouseEvent *e) {
    int old = m_hoveredIndex; m_hoveredIndex = -1;
    for (int i = 0; i < m_itemRects.size(); i++)
        if (m_itemRects[i].contains(e->pos())) { m_hoveredIndex = i; break; }
    if (old != m_hoveredIndex) update();
}

void QtLauncherOverlay::keyPressEvent(QKeyEvent *e) {
    if (e->key()==Qt::Key_Escape) { hideLauncher(); return; }
    if (e->key()==Qt::Key_Backspace && !m_searchText.isEmpty()) {
        m_searchText.chop(1); updateGrid(); update(); return;
    }
    int cols = 5;
    int perPage = m_itemsPerPage;
    int visible = m_filteredNames.size();
    if (e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {
        int vi = visibleIndex();
        if (vi >= 0 && vi < visible) {
            QString selName = m_filteredNames[vi];
            activateName(selName);
        }
        return;
    }
    if (e->key()==Qt::Key_Tab || e->key()==Qt::Key_Right) { m_selIndex = (m_selIndex+1) % qMax(1, qMin(visible-startOfPage(m_currentPage, perPage, visible), perPage)); update(); return; }
    if (e->key()==Qt::Key_Backtab || e->key()==Qt::Key_Left) {
        int cur = m_selIndex==0 ? perPage-1 : m_selIndex-1;
        m_selIndex = qMax(0, cur); update(); return;
    }
    if (e->key()==Qt::Key_Down) {
        int curIdx = m_selIndex + cols;
        m_selIndex = qMin(curIdx, qMax(0, visible-startOfPage(m_currentPage, perPage, visible)-1)); update(); return;
    }
    if (e->key()==Qt::Key_Up) {
        m_selIndex = qMax(0, m_selIndex - cols); update(); return;
    }
    if (e->key()==Qt::Key_PageDown) { showPage(m_currentPage+1); update(); return; }
    if (e->key()==Qt::Key_PageUp)   { showPage(m_currentPage-1); update(); return; }
    QString txt = e->text();
    /* forwarded events (launcher open over a focused app) carry no QPA text;
       synthesize it from the ASCII keycode, honoring Shift */
    if (txt.isEmpty() && e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde) {
        QChar ch(int(e->key()));
        if (e->modifiers() & Qt::ShiftModifier) ch = ch.toUpper();
        txt = QString(ch);
    }
    if (!txt.isEmpty() && e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde) {
        m_searchText += txt; updateGrid(); update();
    }
}

void QtLauncherOverlay::showPage(int page) {
    int totalPages = qMax(1, (m_filteredNames.size()+m_itemsPerPage-1)/m_itemsPerPage);
    m_currentPage = (page % totalPages + totalPages) % totalPages;
    m_selIndex = 0; m_hoveredIndex = -1;
    rebuildItemRects();
}

int QtLauncherOverlay::startOfPage(int page, int perPage, int total) const {
    int start = page*perPage;
    return (start < total) ? start : qMax(0, total-1);
}

/* ═══════════════════════════════════════════════════════════════════
   QtNotifCenter
   ═══════════════════════════════════════════════════════════════════ */

QtNotifCenter::QtNotifCenter(QWidget *parent) : QWidget(parent) { setMouseTracking(true); }

void QtNotifCenter::toggle() {
    m_open = !m_open;
    if (m_open) {
        int pw = 340;
        setGeometry(parentWidget()->width()-pw, MENUBAR_H, pw,
                    parentWidget()->height()-MENUBAR_H-DOCK_H);
        show(); raise();
    } else hide();
    update();
}

void QtNotifCenter::pushNotif(const QString &text, const QColor &color) {
    if (m_notifs.size() >= 8) m_notifs.removeFirst();
    m_notifs.append({text, color}); update();
}

void QtNotifCenter::paintEvent(QPaintEvent *) {
    if (!m_open) return;
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0,0,0,180));

    int pw = 340;
    QRect pr(width()-pw, 0, pw, height());
    QLinearGradient bg(pr.topLeft(), pr.bottomLeft());
    bg.setColorAt(0, QColor(0x28,0x28,0x2C,145));
    bg.setColorAt(0.5, QColor(0x2E,0x2E,0x32,140));
    bg.setColorAt(1, QColor(0x32,0x32,0x36,135));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,18),1));
    p.drawRect(pr.adjusted(0,0,-1,0));
    /* Top inner highlight */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,18));
    p.drawRect(pr.x()+16, pr.y()+1, pw-32, 1);

    QFont f = font(); f.setPointSize(18); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(QRect(pr.x()+16, 16, pw-32, 40), Qt::AlignLeft|Qt::AlignTop, "Notifications");

    f.setPointSize(11); f.setBold(false); p.setFont(f);
    int yOff = 60;
    for (const Notif &n : m_notifs) {
        QRect nr(pr.x()+12, yOff, pw-24, 48);
        /* Notification card glass */
        QLinearGradient nb(nr.topLeft(), nr.bottomLeft());
        nb.setColorAt(0, QColor(0x38,0x38,0x3A,120));
        nb.setColorAt(1, QColor(0x2C,0x2C,0x2E,110));
        p.setBrush(nb);
        p.setPen(QPen(QColor(255,255,255,12),1));
        p.drawRoundedRect(nr, 10, 10);
        /* Color accent bar */
        p.setPen(Qt::NoPen);
        p.setBrush(n.color);
        p.drawRoundedRect(QRect(nr.x(), nr.y()+8, 3, nr.height()-16), 2, 2);
        p.setPen(c_text);
        p.drawText(nr.adjusted(12,0,-12,0), Qt::AlignVCenter|Qt::AlignLeft, n.text);
        yOff += 56;
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtQuickSettings — COSMIC-style quick-settings toast (top-right)
   ═══════════════════════════════════════════════════════════════════ */

QtQuickSettings::QtQuickSettings(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void QtQuickSettings::showPanel() {
    int pw = 360, ph = 208;
    QWidget *pw_ = parentWidget();
    QRect base = pw_ ? pw_->rect() : rect();
    setGeometry(base.right()-pw-10, 38, pw, ph);
    m_open = true;
    raise(); show(); setFocus(); update();
}

void QtQuickSettings::paintEvent(QPaintEvent *) {
    if (!m_open) return;
    m_cpu = hyperde_shell_cpu();
    m_mem = hyperde_shell_mem_mb();
    m_memTotal = hyperde_shell_mem_total_mb();

    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);

    /* drop shadow + glass card */
    QRect c = rect().adjusted(0,0,0,0);
    p.setPen(Qt::NoPen); p.setBrush(QColor(0,0,0,90));
    p.drawRoundedRect(c.translated(4,6), 18, 18);
    p.setBrush(QColor(0,0,0,110));
    p.drawRoundedRect(c.translated(1,2), 16, 16);

    QLinearGradient bg(0,0,0,height());
    bg.setColorAt(0, QColor(0x28,0x28,0x2C,235));
    bg.setColorAt(0.5, QColor(0x24,0x24,0x28,240));
    bg.setColorAt(1, QColor(0x20,0x20,0x24,245));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,20),1));
    p.drawRoundedRect(c, 16, 16);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,30));
    p.drawRoundedRect(c.adjusted(14,1,-14,-1), 14,14);  /* top glow line */

    QFont f = font(); f.setPointSize(15); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(QRect(16, 14, width()-32, 28), Qt::AlignLeft|Qt::AlignVCenter, "Quick Settings");

    /* Status row: network + battery toggles */
    int yy = 56;
    auto drawPill = [&](const QString &label, bool on) {
        QRect pr2(16, yy, 156, 40);
        QLinearGradient pb(pr2.topLeft(), pr2.bottomLeft());
        pb.setColorAt(0, on ? QColor(0x40,0xD9,0xF0,60)  : QColor(0x3A,0x3A,0x3E,180));
        pb.setColorAt(1, on ? QColor(0x2A,0x8A,0x9E,90) : QColor(0x30,0x30,0x34,170));
        p.setBrush(pb);
        p.setPen(QPen(on ? QColor(0x40,0xD9,0xF0,160) : QColor(255,255,255,14), 1));
        p.drawRoundedRect(pr2, 12, 12);
        p.setBrush(on ? QColor(0x40,0xD9,0xF0) : QColor(99,99,102));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(pr2.x()+16, pr2.y()+20), 5, 5);
        p.setPen(c_text); f.setPointSize(11); f.setBold(false); p.setFont(f);
        p.drawText(pr2.adjusted(28,0,-8,0), Qt::AlignVCenter|Qt::AlignLeft, label);
    };
    drawPill("Network", true);
    int oldX = 16; int oldY = yy;
    Q_UNUSED(oldX); Q_UNUSED(oldY);
    /* power pill on the right */
    QRect pr2(width()-172, yy, 156, 40);
    QLinearGradient pb(pr2.topLeft(), pr2.bottomLeft());
    pb.setColorAt(0, QColor(0x2A,0x2A,0x2E,180)); pb.setColorAt(1, QColor(0x26,0x26,0x2A,170));
    p.setBrush(pb); p.setPen(QPen(QColor(255,255,255,14),1));
    p.drawRoundedRect(pr2, 12, 12);
    p.setFont(f); p.setPen(c_text);
    p.drawText(pr2, Qt::AlignCenter, "Power");

    yy = 112;
    /* CPU meter */
    p.setPen(c_subtext); f.setPointSize(11); p.setFont(f);
    p.drawText(QRect(16, yy, 80, 20), Qt::AlignLeft|Qt::AlignVCenter, "CPU");
    p.setPen(c_text);
    p.drawText(QRect(width()-120, yy, 104, 20), Qt::AlignRight|Qt::AlignVCenter,
               QString("%1%").arg(m_cpu));
    QRect barR(16, yy+24, width()-32, 8);
    p.setPen(Qt::NoPen); p.setBrush(QColor(255,255,255,18));
    p.drawRoundedRect(barR, 4, 4);
    p.setBrush(QColor(0x40,0xD9,0xF0));
    p.drawRoundedRect(QRect(barR.x(), barR.y(), qMax(8, int(barR.width()*(qBound(0.0, double(m_cpu), 100.0))/100.0)), barR.height()), 4, 4);

    yy += 44;
    /* Memory meter */
    p.setPen(c_subtext);
    p.drawText(QRect(16, yy, 120, 20), Qt::AlignLeft|Qt::AlignVCenter, "MEMORY");
    p.setPen(c_text);
    int memPct = m_memTotal > 0 ? qBound(0, int(100.0*m_mem/m_memTotal), 100) : 0;
    QString memTxt = m_memTotal >= 1024
        ? QString("%1 / %2 GB").arg(m_mem/1024.0, 0, 'f', 1).arg(m_memTotal/1024.0, 0, 'f', 1)
        : QString("%1 / %2 MB").arg(m_mem).arg(m_memTotal);
    p.drawText(QRect(width()-164, yy, 148, 20), Qt::AlignRight|Qt::AlignVCenter, memTxt);
    QRect memR(16, yy+24, width()-32, 8);
    p.setPen(Qt::NoPen); p.setBrush(QColor(255,255,255,18));
    p.drawRoundedRect(memR, 4, 4);
    p.setBrush(QColor(0x30,0xD1,0x58));
    p.drawRoundedRect(QRect(memR.x(), memR.y(), qMax(8, int(memR.width()*memPct/100.0)), memR.height()), 4, 4);
}

void QtQuickSettings::mousePressEvent(QMouseEvent *e) {
    /* Power pill → animated soft shutdown toast */
    QRect c = rect();
    if (e->x() >= c.width()-172 && e->x() <= c.width()-16 && e->y() >= 56 && e->y() <= 96) {
        QtDesktopManager *mgr = QtDesktopManager::instance();
        if (mgr) mgr->showToast("Shutting down…", QColor(0xFF,0x5A,0x36));
        return;
    }
    hidePanel();
}

void QtQuickSettings::keyPressEvent(QKeyEvent *e) {
    if ((e->key()==Qt::Key_Escape) || (e->key()==Qt::Key_S && (e->modifiers() & Qt::ControlModifier)))
        hidePanel();
}

void QtNotifCenter::mousePressEvent(QMouseEvent *e) {
    if (e->button()==Qt::LeftButton && (!rect().contains(e->pos()) || e->pos().x()<16)) toggle();
}

/* ═══════════════════════════════════════════════════════════════════
   QtToastNotification — slide-in banners
   ═══════════════════════════════════════════════════════════════════ */

QtToastNotification::QtToastNotification(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        bool dirty = false;
        for (int i = m_toasts.size()-1; i >= 0; i--) {
            Toast &t = m_toasts[i];
            if (t.fading) {
                t.opacity = qMax(0.0f, t.opacity - 0.06f);
                dirty = true;
                if (t.opacity <= 0.0f) { m_toasts.removeAt(i); dirty = true; }
            } else {
                if (t.slideX > 0.01f) {
                    t.slideX = qMax(0.0f, t.slideX - 0.08f);
                    dirty = true;
                } else {
                    t.slideX = 0.0f;
                }
            }
        }
        if (m_toasts.isEmpty()) { m_animTimer.stop(); hide(); }
        if (dirty) update();
    });
}

void QtToastNotification::pushToast(const QString &text, const QColor &accent,
                                     std::function<void()> onClick) {
    Toast t; t.text = text; t.accent = accent; t.onClick = std::move(onClick);
    m_toasts.prepend(t);
    if (m_toasts.size() > 5) m_toasts.removeLast();
    setFixedSize(340, MENUBAR_H + 12 + m_toasts.size() * 48);
    show();
    if (!m_animTimer.isActive()) m_animTimer.start(16);
    /* Auto-dismiss after 3 seconds — the toast was just prepended, so it lives at index 0. */
    QTimer::singleShot(3000, this, [this, idx = 0]() {
        if (idx < m_toasts.size() && !m_toasts[idx].fading) {
            m_toasts[idx].fading = true;
            if (!m_animTimer.isActive()) m_animTimer.start(16);
        }
    });
}

void QtToastNotification::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    int toastH = 44, gap = 4, padX = 12;
    int baseY = 4;

    for (int i = 0; i < m_toasts.size(); i++) {
        const Toast &t = m_toasts[i];
        int xOff = (int)(340.0f * t.slideX);
        int ty = baseY + i * (toastH + gap);
        QRect tr(xOff, ty, 340 - xOff, toastH);
        if (tr.width() <= 0) continue;

        p.setOpacity(t.opacity);

        /* Shadow — 2-layer */
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,50));
        p.drawRoundedRect(tr.adjusted(-4,-6,4,6), 12, 12);
        p.setBrush(QColor(0,0,0,25));
        p.drawRoundedRect(tr.adjusted(1,2,1,2), 10, 10);

        /* Glass body — macOS 27 refined */
        QLinearGradient bg(tr.topLeft(), tr.bottomLeft());
        bg.setColorAt(0, QColor(0x2C,0x2C,0x30,150));
        bg.setColorAt(0.5, QColor(0x24,0x24,0x28,145));
        bg.setColorAt(1, QColor(0x1E,0x1E,0x22,140));
        p.setBrush(bg);
        p.setPen(QPen(QColor(255,255,255,18),1));
        p.drawRoundedRect(tr, 10, 10);

        /* Accent left bar */
        p.setPen(Qt::NoPen);
        p.setBrush(t.accent);
        p.drawRoundedRect(QRect(tr.x(), tr.y()+6, 4, tr.height()-12), 2, 2);

        /* Text */
        QFont f = font(); f.setPointSize(10); f.setBold(false); p.setFont(f);
        p.setPen(QColor(245,245,247,(int)(255*t.opacity)));
        p.drawText(tr.adjusted(padX+8, 0, -padX, 0), Qt::AlignVCenter|Qt::AlignLeft, t.text);

        p.setOpacity(1.0f);
    }
}

void QtToastNotification::mousePressEvent(QMouseEvent *e) {
    int toastH = 44, gap = 4, baseY = 4;
    for (int i = 0; i < m_toasts.size(); i++) {
        int ty = baseY + i * (toastH + gap);
        QRect tr(0, ty, 340, toastH);
        if (tr.contains(e->pos())) {
            if (m_toasts[i].onClick) m_toasts[i].onClick();
            m_toasts[i].fading = true;
            if (!m_animTimer.isActive()) m_animTimer.start(16);
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtWidgetsPanel — desktop gadget stack (clock · calendar · weather)
   ═══════════════════════════════════════════════════════════════════ */

QtWidgetsPanel::QtWidgetsPanel(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);   /* clicks pass through */
    int W = 292;
    setGeometry(parent ? parent->width() - W - 28 : 960, MENUBAR_H + 26, W, 560);
    m_date = QDate::currentDate().toString("dddd, MMMM d");
}

void QtWidgetsPanel::setTime(const QString &timeText, const QString &dateText) {
    bool changed = false;
    if (timeText != m_time) { m_time = timeText; changed = true; }
    if (!dateText.isEmpty() && dateText != m_date) { m_date = dateText; changed = true; }
    if (changed) update();
}

void QtWidgetsPanel::paintGlassCard(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    /* soft drop shadow */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,50));
    p.drawRoundedRect(r.adjusted(1,4,-1,4), 22, 22);
    /* glass body */
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0.0, QColor(0x30,0x30,0x36,190));
    g.setColorAt(0.5, QColor(0x24,0x24,0x2A,170));
    g.setColorAt(1.0, QColor(0x2C,0x2C,0x32,185));
    p.setPen(QPen(QColor(255,255,255,28), 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 20, 20);
    /* top specular */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,18));
    p.drawRoundedRect(r.adjusted(14,1,-14,-r.height()+3), 8, 8);
}

void QtWidgetsPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const int cardW = width();
    const QRect clockCard(0, 0, cardW, 128);
    const QRect calCard(0, clockCard.bottom()+16, cardW, 236);
    const QRect wxCard(0, calCard.bottom()+16, cardW, 132);

    paintGlassCard(p, clockCard);

    /* ── Clock ── */
    QFont big = font(); big.setPointSize(44); big.setBold(true); big.setLetterSpacing(QFont::AbsoluteSpacing, -1.0);
    p.setFont(big); p.setPen(QColor(0xF5,0xF5,0xF7));
    p.drawText(clockCard.adjusted(0, 18, 0, 0), Qt::AlignHCenter, m_time);
    QFont med = font(); med.setPointSize(12);
    p.setFont(med); p.setPen(c_subtext);
    p.drawText(clockCard.adjusted(0, -10, 0, -14), Qt::AlignHCenter | Qt::AlignBottom, m_date);

    /* ── Calendar month grid ── */
    paintGlassCard(p, calCard);
    QDate today = QDate::currentDate();
    QFont hf = font(); hf.setPointSize(13); hf.setBold(true);
    p.setFont(hf); p.setPen(QColor(0xF5,0xF5,0xF7));
    p.drawText(calCard.adjusted(20, 12, -20, 0), Qt::AlignLeft,
               today.toString("MMMM yyyy").toUpper());
    static const QString dow[7] = {"S","M","T","W","T","F","S"};
    QFont df = font(); df.setPointSize(9);
    p.setFont(df); p.setPen(QColor(0x8E,0x8E,0x93,180));
    QDate first = QDate(today.year(), today.month(), 1);
    int colW = (calCard.width()-32)/7;
    for (int d = 0; d < 7; d++)
        p.drawText(QRect(calCard.x()+16+d*colW, calCard.y()+42, colW, 14),
                   Qt::AlignCenter, dow[d]);
    QFont cf = font(); cf.setPointSize(11);
    p.setFont(cf);
    int rowH = 24, rowY = calCard.y()+62;
    for (QDate d = first; d.month() == today.month(); d = d.addDays(1)) {
        int col = d.dayOfWeek() % 7;          /* Qt: Mon=1..Sun=7 → Sun=0 col */
        QRect cell(calCard.x()+16+col*colW, rowY + (d.day()-1+first.dayOfWeek()%7)/7*rowH, colW, rowH-2);
        if (d == today) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xFF,0x5A,0x36));
            p.drawEllipse(cell.adjusted(colW/2-11, 0, -colW/2+11, 0));
            p.setPen(Qt::white);
        } else {
            p.setPen(QColor(0xE8,0xE8,0xEC,220));
        }
        p.drawText(cell, Qt::AlignCenter, QString::number(d.day()));
    }

    /* ── Weather (Golden Gate themed) ── */
    paintGlassCard(p, wxCard);
    /* sun disc + glow */
    QPointF sunC(wxCard.x()+52, wxCard.y()+58);
    QRadialGradient glow(sunC, 34);
    glow.setColorAt(0, QColor(255,0xC9,0x66,120));
    glow.setColorAt(1, QColor(255,0xC9,0x66,0));
    p.setPen(Qt::NoPen); p.setBrush(glow);
    p.drawEllipse(sunC, 34, 34);
    p.setBrush(QColor(0xFF,0xD6,0x5A));
    p.drawEllipse(sunC, 17, 17);
    QFont wf = font(); wf.setPointSize(26); wf.setBold(true);
    p.setFont(wf); p.setPen(QColor(0xF5,0xF5,0xF7));
    p.drawText(QRect(wxCard.x()+92, wxCard.y()+22, wxCard.width()-100, 40), Qt::AlignLeft, "68°");
    QFont wsf = font(); wsf.setPointSize(11);
    p.setFont(wsf); p.setPen(c_subtext);
    p.drawText(QRect(wxCard.x()+94, wxCard.y()+60, wxCard.width()-104, 18), Qt::AlignLeft, "Sunny — H:72° L:55°");
    p.setFont(df);
    p.drawText(QRect(wxCard.x()+20, wxCard.y()+wxCard.height()-34, wxCard.width()-40, 18),
               Qt::AlignLeft, "Golden Gate");
}

/* ═══════════════════════════════════════════════════════════════════
   QtMissionControl — Super+Up window overview
   ═══════════════════════════════════════════════════════════════════ */

QtMissionControl::QtMissionControl(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    /* Keep repainting while open: the shared backing store's periodic
       full-screen recomposites otherwise drop this overlay after its
       first pass. */
    m_keepAlive = new QTimer(this);
    connect(m_keepAlive, &QTimer::timeout, this, [this]() { update(); });
    hide();
}

void QtMissionControl::showOverview() {
    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (!mgr || !parentWidget()) return;
    setGeometry(parentWidget()->rect());
    m_cells.clear();
    m_grabs.clear();
    QList<QtAppWindow*> wins;
    QtAppWindow **aws = mgr->appWindows();
    for (int i = 0; i < APP_COUNT; i++)
        if (aws[i] && aws[i]->isVisible() && !aws[i]->isClosing())
            wins.append(aws[i]);
    if (wins.isEmpty()) { mgr->showToast("No open windows", c_subtext); return; }

    const int cols = wins.size() <= 4 ? 2 : 3;
    const int rows = (wins.size() + cols - 1) / cols;
    QRect area = rect().adjusted(90, MENUBAR_H + 70, -90, -110);
    qreal cellW = area.width() / (qreal)cols;
    qreal cellH = area.height() / (qreal)rows;

    int n = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols && n < wins.size(); c++, n++) {
            QtAppWindow *w = wins[n];
            QSizeF scaled(w->width(), w->height());
            qreal k = qMin(cellW / scaled.width(), cellH / scaled.height()) * 0.86;
            int pw = (int)(scaled.width() * k), ph = (int)(scaled.height() * k);
            int px = area.x() + (int)(c * cellW + (cellW - pw)/2);
            int py = area.y() + (int)(r * cellH + (cellH - ph)/2);
            m_cells.append({ QRect(px, py, pw, ph), w });
            m_grabs.append(w->grab());
        }
    }
    m_open = true; m_fade = 0.0f;
    show(); raise();
    m_keepAlive->start(50);
    update();
}

void QtMissionControl::hideOverview() {
    m_open = false;
    m_keepAlive->stop();
    hide();
}

void QtMissionControl::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(8, 8, 14, 216));

    for (int ci = 0; ci < m_cells.size(); ++ci) {
        const McCell &c = m_cells[ci];
        QPixmap pm = ci < m_grabs.size() ? m_grabs.at(ci) : QPixmap();
        if (pm.isNull()) continue;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        /* card shadow + frame */
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,110));
        p.drawRoundedRect(c.rect.adjusted(2,6,-2,6), 12, 12);
        /* live preview scaled to fit, preserving aspect */
        QImage img = pm.toImage().scaled(c.rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect ir(c.rect.x() + (c.rect.width()-img.width())/2,
                 c.rect.y() + (c.rect.height()-img.height())/2, img.width(), img.height());
        p.drawImage(ir, img);
        p.setPen(QPen(QColor(255,255,255,36), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(ir, 6, 6);
        /* title chip */
        QFont f = font(); f.setPointSize(11); f.setBold(true);
        p.setFont(f);
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(c.win->appTitle()) + 20;
        QRect tr(c.rect.x() + (c.rect.width()-tw)/2, c.rect.y() - 14, tw, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20,20,26,235));
        p.drawRoundedRect(tr, 11, 11);
        p.setPen(QColor(245,245,247));
        p.drawText(tr, Qt::AlignCenter, c.win->appTitle());
    }

    QFont hint = font(); hint.setPointSize(10);
    p.setFont(hint); p.setPen(QColor(142,142,147,200));
    p.drawText(QRect(0, height()-46, width(), 24), Qt::AlignHCenter,
               "Click a window to focus it   ·   Esc to exit");
}

void QtMissionControl::mousePressEvent(QMouseEvent *e) {
    for (const McCell &c : m_cells) {
        if (c.rect.contains(e->pos())) {
            hideOverview();
            c.win->showNormal();
            c.win->raise();
            c.win->activateWindow();
            return;
        }
    }
    hideOverview();
}

void QtMissionControl::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { hideOverview(); return; }
    QWidget::keyPressEvent(e);
}

/* ═══════════════════════════════════════════════════════════════════
   QtAppSwitcher
   ═══════════════════════════════════════════════════════════════════ */

QtAppSwitcher::QtAppSwitcher(QWidget *parent) : QWidget(parent) { hide(); }

void QtAppSwitcher::activate() {
    m_active = true; m_running.clear();
    QtDesktopManager *mgr = QtDesktopManager::instance();
    for (int i = 0; mgr && i < APP_COUNT; i++)
        if (mgr->appRunning(i)) m_running.append(i);
    if (m_running.isEmpty()) m_running << 0;
    m_selected = 0; show(); raise(); update();
}

void QtAppSwitcher::deactivate() { m_active = false; hide(); m_running.clear(); m_selected = 0; }
void QtAppSwitcher::next() { if (m_running.size()>1) m_selected = (m_selected+1)%m_running.size(); update(); }
void QtAppSwitcher::prev() { if (m_running.size()>1) m_selected = (m_selected+m_running.size()-1)%m_running.size(); update(); }
void QtAppSwitcher::commit() {
    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (mgr && m_selected >= 0 && m_selected < m_running.size()) mgr->launchApp(m_running[m_selected]);
    m_active = false; hide();
}

void QtAppSwitcher::paintEvent(QPaintEvent *) {
    if (!m_active) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0,0,0,140));

    QtDesktopManager *mgr = QtDesktopManager::instance();
    if (!mgr || m_running.isEmpty()) return;

    int iconSz = 64, pad = 12, labelH = 20, gap = 8;
    int count = m_running.size();
    int stripW = count * (iconSz + gap) + pad * 2;
    int stripH = iconSz + labelH + pad * 2 + 8;
    int cx = (width() - stripW) / 2;
    int cy = (height() - stripH) / 2;

    /* Glass background panel */
    QLinearGradient bg(0, cy, 0, cy + stripH);
    bg.setColorAt(0, QColor(28,28,32,150));
    bg.setColorAt(0.5, QColor(34,34,38,140));
    bg.setColorAt(1, QColor(26,26,30,135));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,20),1));
    p.drawRoundedRect(cx, cy, stripW, stripH, 14, 14);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,28));
    p.drawRect(cx+1, cy+1, stripW-2, 1);

    /* Draw each running app icon in a row */
    for (int i = 0; i < count; i++) {
        int appIdx = m_running[i];
        QString name = mgr->appNames().value(appIdx, "?");
        bool selected = (i == m_selected);

        int ix = cx + pad + i * (iconSz + gap);
        int iy = cy + pad;

        /* Selected highlight background — GG Orange */
        if (selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xFF,0x5A,0x36,50));
            p.drawRoundedRect(ix - 4, iy - 4, iconSz + 8, iconSz + 8, 12, 12);
            p.setPen(QPen(QColor(0xFF,0x5A,0x36,140),2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(ix - 4, iy - 4, iconSz + 8, iconSz + 8, 12, 12);
        }

        /* Icon */
        QRect iconRect(ix, iy, iconSz, iconSz);
        drawAppIcon(p, iconRect, name, iconSz);

        /* App name label below */
        QFont lf = font(); lf.setPointSize(9); lf.setBold(selected); p.setFont(lf);
        p.setPen(selected ? QColor(245,245,247) : QColor(142,142,147));
        QFontMetrics lfm(lf);
        QString elided = lfm.elidedText(name, Qt::ElideRight, iconSz + 12);
        QRect labelRect(ix - 4, iy + iconSz + 4, iconSz + 8, labelH);
        p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   QtCtxMenu
   ═══════════════════════════════════════════════════════════════════ */

QtCtxMenu::QtCtxMenu(QWidget *parent) : QWidget(parent) { setMouseTracking(true); hide(); }

void QtCtxMenu::showMenu(int x, int y, const QStringList &items,
                          const std::vector<std::function<void()>> &actions) {
    m_items = items; m_actions = actions; m_hoveredIndex = -1;
    int mw = 200, mh = items.size()*28+8;
    int sw = parentWidget() ? parentWidget()->width() : 1920;
    int sh = parentWidget() ? parentWidget()->height() : 1080;
    if (x+mw>sw) x = sw-mw-8;
    if (y+mh>sh) y = sh-mh-8;
    m_menuRect = QRect(x,y,mw,mh); setGeometry(m_menuRect);
    m_open = true; show(); raise(); update();
}

void QtCtxMenu::hideMenu() { m_open = false; hide(); }

void QtCtxMenu::paintEvent(QPaintEvent *) {
    if (!m_open) return;
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);

    /* Shadow */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,60));
    p.drawRoundedRect(rect().adjusted(-4,-6,4,6), 14, 14);

    /* Glass body — macOS 27 refined */
    QLinearGradient bg(rect().topLeft(), rect().bottomLeft());
    bg.setColorAt(0, QColor(42,42,44,250));
    bg.setColorAt(0.5, QColor(36,36,38,140));
    bg.setColorAt(1, QColor(30,30,32,135));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,20),1));
    p.drawRoundedRect(rect().adjusted(0,0,-1,-1), 12, 12);
    /* Top inner highlight */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,28));
    p.drawRect(rect().x()+12, rect().y()+1, rect().width()-24, 1);

    QFont f = font(); f.setPointSize(12); p.setFont(f);
    int yOff = 6;
    for (int i = 0; i < m_items.size(); i++) {
        QRect ir(2, yOff, width()-4, 28);
        if (m_items[i].isEmpty()) {
            /* Separator — thin line with subtle gradient */
            p.setPen(QPen(QColor(99,99,102,80),1));
            p.drawLine(12, yOff+14, width()-12, yOff+14);
            yOff += 28; continue;
        }
        if (i == m_hoveredIndex) {
            p.setBrush(QColor(0xFF,0x5A,0x36,50)); p.setPen(Qt::NoPen);
            p.drawRoundedRect(ir, 6, 6);
        }
        p.setPen(i == m_hoveredIndex ? QColor(0xFF,0x5A,0x36) : c_text);
        p.drawText(ir.adjusted(12,0,-12,0), Qt::AlignVCenter|Qt::AlignLeft, m_items[i]);
        yOff += 28;
    }
}

void QtCtxMenu::mousePressEvent(QMouseEvent *e) {
    if (!rect().contains(e->pos())) { hideMenu(); return; }
    /* Rows start at yOff=6 and are 28px tall in paintEvent — match them. */
    int idx = (e->pos().y() > 6) ? (e->pos().y()-6)/28 : 0;
    if (idx >= 0 && idx < (int)m_items.size() && !m_items[idx].isEmpty()
        && idx < (int)m_actions.size() && m_actions[idx]) m_actions[idx]();
    hideMenu();
}

void QtCtxMenu::mouseMoveEvent(QMouseEvent *e) {
    int idx = (e->pos().y() > 6) ? (e->pos().y()-6)/28 : 0;
    if (idx >= (int)m_items.size()) idx = -1;
    if (idx != m_hoveredIndex) { m_hoveredIndex = idx; update(); }
}
