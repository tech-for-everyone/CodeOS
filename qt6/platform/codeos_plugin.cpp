#include <QtGui/qpa/qplatformintegration.h>
#include <QtGui/qpa/qplatformscreen.h>
#include <QtGui/qpa/qplatformwindow.h>
#include <QtGui/qpa/qplatformbackingstore.h>
#include <QtGui/qpa/qplatformclipboard.h>
#include <QtGui/qpa/qplatformfontdatabase.h>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <QtGui/qpa/qwindowsysteminterface.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <QImage>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QList>
#include <QTimer>
#include <QClipboard>
#include <QMimeData>

#include <QtCore/qabstracteventdispatcher.h>
#include <QtCore/qsocketnotifier.h>
#include <QtCore/qcoreevent.h>
#include <QtCore/qlist.h>

#include <stdint.h>
#include <string.h>

#include "codeos_platform.h"

#include "jengine/jengine.h"

extern "C" void kprintf(const char *fmt, ...);

extern "C" {
void fb_cursor_move(int x, int y);
void fb_cursor_show(void);
void fb_cursor_hide(void);
void font_embed_load(void);
QPlatformFontDatabase *codeos_create_font_database(void);
}
static int g_cursor_x = 0;
static int g_cursor_y = 0;
static bool g_cursor_visible = false;

/* Last position the arrow was actually drawn at (init to sentinel so the
 * first mouse event always triggers a draw). */
static int g_drawn_x = -1;
static int g_drawn_y = -1;

static void showCursorNow() {
    fb_cursor_hide();
    fb_cursor_move(g_cursor_x, g_cursor_y);
    fb_cursor_show();
    g_drawn_x = g_cursor_x;
    g_drawn_y = g_cursor_y;
}

static void updateCursorOnMove() {
    if (!g_cursor_visible) return;
    if (g_cursor_x == g_drawn_x && g_cursor_y == g_drawn_y) return;
    showCursorNow();
}

/* Global integration instance */
static class CodeOSIntegration *g_integration = nullptr;

void codeos_platform_notify_focus_window(QWindow *w);

class CodeOSScreen : public QPlatformScreen {
public:
    CodeOSScreen() {
        codeos_fb_info_t info;
        if (codeos_platform_get_fb_info(&info) == 0 && info.width > 0 && info.height > 0) {
            m_geometry = QRect(0, 0, (int)info.width, (int)info.height);
            m_depth = info.bpp;
            m_physicalSize = QSizeF((int)info.width, (int)info.height);
        } else {
            m_geometry = QRect(0, 0, 1920, 1080);
            m_depth = 32;
            m_physicalSize = QSizeF(1920, 1080);
        }
    }

    QRect geometry() const override { return m_geometry; }
    QRect availableGeometry() const override { return m_geometry; }
    int depth() const override { return m_depth; }
    QImage::Format format() const override { return QImage::Format_ARGB32_Premultiplied; }
    QSizeF physicalSize() const override { return m_physicalSize; }
    QDpi logicalDpi() const override { return QDpi(96, 96); }
    qreal devicePixelRatio() const override { return 1.0; }
    SubpixelAntialiasingType subpixelAntialiasingTypeHint() const override {
        return Subpixel_RGB;
    }
    QString name() const override { return QStringLiteral("CodeOS Display"); }

private:
    QRect m_geometry;
    int m_depth = 32;
    QSizeF m_physicalSize;
};

class CodeOSWindow : public QPlatformWindow {
public:
    CodeOSWindow(QWindow *window) : QPlatformWindow(window) {
        m_handle = codeos_window_create(
            window->x(), window->y(),
            window->width(), window->height(),
            window->title().toUtf8().constData());
    }

    ~CodeOSWindow() override {
        codeos_window_destroy(m_handle);
    }

    void setGeometry(const QRect &rect) override {
        QPlatformWindow::setGeometry(rect);
        codeos_window_set_geometry(m_handle, rect.x(), rect.y(), rect.width(), rect.height());
        /* Geometry changes can clear the exposed flag; re-assert so Qt
           keeps flushing damaged regions to the framebuffer. */
        if (window()->isVisible())
            QWindowSystemInterface::handleExposeEvent(window(), rect);
    }

    void setVisible(bool visible) override {
        QPlatformWindow::setVisible(visible);
        if (visible) {
            codeos_window_show(m_handle);
            QWindowSystemInterface::handleExposeEvent(window(),
                QRect(QPoint(0, 0), window()->geometry().size()));
        } else {
            codeos_window_hide(m_handle);
        }
    }

    void requestUpdate() override {
        /* The base implementation relies on platform plumbing this plugin
           doesn't have, so UpdateRequest events were never delivered and
           QWidgetRepaintManager stopped flushing after the first frames.
           Post the update request event explicitly (async, like other
           minimal platforms do). */
        if (window()->isVisible())
            QCoreApplication::postEvent(window(), new QEvent(QEvent::UpdateRequest));
    }

    void raise() override { codeos_window_raise(m_handle); }
    void lower() override { codeos_window_lower(m_handle); }

    void requestActivateWindow() override {
        codeos_platform_notify_focus_window(window());
        QWindowSystemInterface::handleFocusWindowChanged(
            window(), Qt::OtherFocusReason);
    }

    uint64_t handle() const { return m_handle; }

private:
    uint64_t m_handle;
};

class CodeOSBackingStore : public QPlatformBackingStore {
public:
    CodeOSBackingStore(QWindow *window)
        : QPlatformBackingStore(window) {}

    QPaintDevice *paintDevice() override {
        return &m_image;
    }

    void flush(QWindow *window, const QRegion &region,
               const QPoint &offset) override {
        if (m_image.isNull()) return;

        QRect geom = window->geometry();


        fb_cursor_hide();

        for (const QRect &r : region) {
            QRect src = r.translated(-offset);
            src = src.intersected(m_image.rect());
            if (src.isEmpty()) continue;

            const uchar *bits = m_image.constBits();
            int bpl = m_image.bytesPerLine();

            int dst_x = geom.x() + src.x();
            int dst_y = geom.y() + src.y();
            int cw = src.width();
            int ch = src.height();

            const uint32_t *pixels = (const uint32_t *)bits;
            int src_stride = bpl / 4;

            codeos_platform_blit(dst_x, dst_y, cw, ch,
                                &pixels[src.y() * src_stride + src.x()],
                                src_stride);
        }

        if (g_cursor_visible) {
            showCursorNow();
        }
    }

    void resize(const QSize &size, const QRegion &staticContents) override {
        Q_UNUSED(staticContents);
        if (m_image.size() != size) {
            m_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
            m_image.fill(Qt::transparent);
        }
    }

    bool scroll(const QRegion &area, int dx, int dy) override {
        Q_UNUSED(area);
        Q_UNUSED(dx);
        Q_UNUSED(dy);
        return false;
    }

private:
    QImage m_image;
};

class CodeOSClipboard : public QPlatformClipboard {
public:
    QMimeData *mimeData(QClipboard::Mode mode = QClipboard::Clipboard) override {
        Q_UNUSED(mode);
        return m_data.data();
    }

    void setMimeData(QMimeData *data, QClipboard::Mode mode) override {
        Q_UNUSED(mode);
        Q_UNUSED(data);
    }

    bool supportsMode(QClipboard::Mode mode) const override {
        Q_UNUSED(mode);
        return false;
    }

    bool ownsMode(QClipboard::Mode mode) const override {
        Q_UNUSED(mode);
        return false;
    }

private:
    QScopedPointer<QMimeData> m_data;
};

extern "C" void *codeos_get_platform_theme(void);
extern "C" void codeos_guard_theme(void *);

extern "C" void font_embed_load(void);

class CodeOSIntegration : public QPlatformIntegration {
public:
    CodeOSIntegration() {
        g_integration = this;
        codeos_platform_init();
    }

    void initialize() override {
        font_embed_load();
        /* jengine_init(); -- stub, not compiled */

        QPlatformScreen *scr = new CodeOSScreen();
        QWindowSystemInterface::handleScreenAdded(scr, true);
    }

    ~CodeOSIntegration() override {
        codeos_platform_cleanup();
        g_integration = nullptr;
    }

    bool hasCapability(QPlatformIntegration::Capability cap) const override {
        switch (cap) {
        case QPlatformIntegration::NonFullScreenWindows:
        case QPlatformIntegration::WindowManagement:
        case QPlatformIntegration::MultipleWindows:
        case QPlatformIntegration::ForeignWindows:
            return true;
        default:
            return false;
        }
    }

    QPlatformWindow *createPlatformWindow(QWindow *window) const override {
        return new CodeOSWindow(window);
    }

    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override {
        return new CodeOSBackingStore(window);
    }

    QPlatformClipboard *clipboard() const override {
        static CodeOSClipboard *instance = nullptr;
        if (!instance)
            instance = new CodeOSClipboard();
        return instance;
    }

    QAbstractEventDispatcher *createEventDispatcher() const override;

    void pollInput();

    void setFocusWindow(QWindow *w) { m_focusWindow = w; }
    QWindow *focusWindow() const { return m_focusWindow; }

    QPlatformFontDatabase *fontDatabase() const override {
        static QPlatformFontDatabase *fdb = nullptr;
        if (!fdb)
            fdb = codeos_create_font_database();
        return fdb;
    }

    QPlatformNativeInterface *nativeInterface() const override {
        return nullptr;
    }

private:
    QWindow *m_focusWindow = nullptr;
};

void codeos_platform_notify_focus_window(QWindow *w) {
    if (g_integration) g_integration->setFocusWindow(w);
}

class CodeOSEventDispatcher : public QAbstractEventDispatcher {
public:
    explicit CodeOSEventDispatcher(QObject *parent = nullptr)
        : QAbstractEventDispatcher(parent) {}

    bool processEvents(QEventLoop::ProcessEventsFlags flags) override {
        Q_UNUSED(flags);
        g_integration->pollInput();

        QCoreApplication::sendPostedEvents();

        bool didWork = QWindowSystemInterface::sendWindowSystemEvents(flags);

        uint64_t now = codeos_platform_tick_ms();
        for (int i = m_timers.size() - 1; i >= 0; --i) {
            TimerEntry &t = m_timers[i];
            if (now >= t.nextFire) {
                if (t.object)
                    QCoreApplication::postEvent(t.object, new QTimerEvent(t.timerId));
                t.nextFire = now + t.interval;
                didWork = true;
            }
        }

        return didWork;
    }

    void registerSocketNotifier(QSocketNotifier *notifier) override {
        Q_UNUSED(notifier);
    }

    void unregisterSocketNotifier(QSocketNotifier *notifier) override {
        Q_UNUSED(notifier);
    }

    void registerTimer(int timerId, qint64 interval, Qt::TimerType timerType,
                       QObject *object) override {
        Q_UNUSED(timerType);
        uint64_t now = codeos_platform_tick_ms();
        m_timers.append({timerId, interval, now + interval, object});
    }

    bool unregisterTimer(int timerId) override {
        for (int i = 0; i < m_timers.size(); ++i) {
            if (m_timers[i].timerId == timerId) {
                m_timers.removeAt(i);
                return true;
            }
        }
        return false;
    }

    bool unregisterTimers(QObject *object) override {
        for (int i = m_timers.size() - 1; i >= 0; --i) {
            if (m_timers[i].object == object)
                m_timers.removeAt(i);
        }
        return true;
    }

    QList<TimerInfo> registeredTimers(QObject *object) const override {
        QList<TimerInfo> result;
        for (const auto &t : m_timers) {
            if (t.object == object)
                result.append({t.timerId, (int)t.interval, Qt::PreciseTimer});
        }
        return result;
    }

    int remainingTime(int timerId) override {
        uint64_t now = codeos_platform_tick_ms();
        for (const auto &t : m_timers) {
            if (t.timerId == timerId) {
                if (now >= t.nextFire) return 0;
                return (int)(t.nextFire - now);
            }
        }
        return -1;
    }

    void wakeUp() override {}
    void interrupt() override {}

private:
    struct TimerEntry {
        int timerId;
        qint64 interval;
        uint64_t nextFire;
        QObject *object;
    };
    QList<TimerEntry> m_timers;
};

QAbstractEventDispatcher *CodeOSIntegration::createEventDispatcher() const {
    return new CodeOSEventDispatcher();
}

void CodeOSIntegration::pollInput() {
    codeos_input_event_t ev;
    while (codeos_platform_poll_input(&ev) > 0 && ev.type != 0) {
        if (ev.type == 1) {
            Qt::KeyboardModifiers mods;
            if (ev.modifiers & 1) mods |= Qt::ShiftModifier;
            if (ev.modifiers & 2) mods |= Qt::ControlModifier;
            if (ev.modifiers & 4) mods |= Qt::AltModifier;
            if (ev.modifiers & 8) mods |= Qt::MetaModifier;
            Qt::Key qt_key = (Qt::Key)ev.key;
            // Map kernel keycodes to Qt keycodes
            switch (ev.key) {
                case 0x8B: qt_key = Qt::Key_F1; break;   // KEY_F1
                case 0x8C: qt_key = Qt::Key_F2; break;   // KEY_F2
                case 0x8D: qt_key = Qt::Key_F3; break;   // KEY_F3
                case 0x8E: qt_key = Qt::Key_F4; break;   // KEY_F4
                case 0x8F: qt_key = Qt::Key_F5; break;   // KEY_F5
                case 0x90: qt_key = Qt::Key_F6; break;   // KEY_F6
                case 0x91: qt_key = Qt::Key_F7; break;   // KEY_F7
                case 0x92: qt_key = Qt::Key_F8; break;   // KEY_F8
                case 0x93: qt_key = Qt::Key_F9; break;   // KEY_F9
                case 0x94: qt_key = Qt::Key_F10; break;  // KEY_F10
                case 0x95: qt_key = Qt::Key_F11; break;  // KEY_F11
                case 0x96: qt_key = Qt::Key_F12; break;  // KEY_F12
                case 0x80: qt_key = Qt::Key_Up; break;       // KEY_UP
                case 0x81: qt_key = Qt::Key_Down; break;     // KEY_DOWN
                case 0x82: qt_key = Qt::Key_Left; break;     // KEY_LEFT
                case 0x83: qt_key = Qt::Key_Right; break;    // KEY_RIGHT
                case 0x84: qt_key = Qt::Key_Home; break;     // KEY_HOME
                case 0x85: qt_key = Qt::Key_End; break;      // KEY_END
                case 0x86: qt_key = Qt::Key_Delete; break;   // KEY_DEL
                case 0x87: qt_key = Qt::Key_PageUp; break;   // KEY_PGUP
                case 0x88: qt_key = Qt::Key_PageDown; break; // KEY_PGDN
                case 0x89: qt_key = Qt::Key_Insert; break;   // KEY_INS
                case 0x8A: qt_key = Qt::Key_Meta; break;     // KEY_SUPER
                default:
                    if (ev.key >= 32 && ev.key <= 126)
                        qt_key = (Qt::Key)ev.key; // ASCII printable
                    else if (ev.key == '\n')
                        qt_key = Qt::Key_Return;
                    else if (ev.key == '\t')
                        qt_key = Qt::Key_Tab;
                    else if (ev.key == 0x08)
                        qt_key = Qt::Key_Backspace;
                    else if (ev.key == 0x1B)
                        qt_key = Qt::Key_Escape;
                    else if (ev.key >= 1 && ev.key <= 26 && (mods & Qt::ControlModifier))
                        qt_key = (Qt::Key)('A' - 1 + ev.key); // ctrl-letter -> Qt::Key_A..Z
                    else if (ev.key >= 'a' && ev.key <= 'z' && (mods & Qt::ShiftModifier))
                        qt_key = (Qt::Key)(ev.key - ('a' - 'A')); // shift+letter -> Qt::Key_A..Z
                    else
                        qt_key = Qt::Key_unknown;
            }
            QWindow *ktarget = g_integration ? g_integration->focusWindow() : nullptr;
            if (!ktarget)
                ktarget = QGuiApplication::focusWindow();
            if (!ktarget) {
                ktarget = QGuiApplication::topLevelAt(QPoint(g_cursor_x, g_cursor_y));
                for (QWindow *w : QGuiApplication::topLevelWindows()) {
                    if (w && w->handle() && w->isVisible()) { ktarget = w; break; }
                }
            }
            /* Qt wants the produced text separately from the key code; the
             * kernel delivers printable ASCII directly, so pass it through
             * (except for shortcut chords, which must not produce text). */
            QString kkey_text;
            if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
                && qt_key >= 0x20 && qt_key <= 0x7e)
                kkey_text = QChar((ushort)qt_key);
            QWindowSystemInterface::handleKeyEvent(
                ktarget, 0, QEvent::KeyPress, qt_key, mods,
                kkey_text);
            kprintf("POST key=%d mods=%d tgt=%p\n", qt_key, (int)mods, (void*)ktarget);
        } else if (ev.type == 2) {
            static int last_buttons = 0;
            Qt::MouseButtons buttons;
            if (ev.mouse_buttons & 1) buttons |= Qt::LeftButton;
            if (ev.mouse_buttons & 2) buttons |= Qt::RightButton;
            if (ev.mouse_buttons & 4) buttons |= Qt::MiddleButton;

            QPoint pos(ev.mouse_x, ev.mouse_y);
            g_cursor_x = pos.x();
            g_cursor_y = pos.y();
            g_cursor_visible = true;
            updateCursorOnMove();
            QWindow *target = QGuiApplication::topLevelAt(pos);
            if (!target) {
                const auto windows = QGuiApplication::allWindows();
                for (QWindow *w : windows) {
                    if (w && w->handle() && w->isVisible()) {
                        target = w;
                        break;
                    }
                }
            }
            if (target && target->handle()) {
                QPlatformWindow *win = target->handle();
                QPoint local = pos - win->geometry().topLeft();
                kprintf("POSTM b=%d pos=(%d,%d) tgt=%p geo=(%d,%d %dx%d) tit=%s\n",
                        (int)buttons, pos.x(), pos.y(), (void*)target,
                        win->geometry().x(), win->geometry().y(),
                        win->geometry().width(), win->geometry().height(),
                        target->title().toUtf8().constData());
                QEvent::Type ev_type = QEvent::MouseMove;
                Qt::MouseButton ev_button = Qt::NoButton;
                if (buttons != last_buttons) {
                    Qt::MouseButtons newly_down = Qt::MouseButtons(int(buttons) & ~int(last_buttons));
                    Qt::MouseButtons newly_up = Qt::MouseButtons(int(last_buttons) & ~int(buttons));
                    if (newly_down) ev_type = QEvent::MouseButtonPress;
                    else ev_type = QEvent::MouseButtonRelease;
                    Qt::MouseButtons chg = newly_down | newly_up;
                    if (chg & Qt::LeftButton) ev_button = Qt::LeftButton;
                    else if (chg & Qt::RightButton) ev_button = Qt::RightButton;
                    else if (chg & Qt::MiddleButton) ev_button = Qt::MiddleButton;
                    else ev_button = (Qt::MouseButton)(int)chg;
                }
                QWindowSystemInterface::handleMouseEvent(
                    target, local, local, buttons, ev_button, ev_type);
                last_buttons = buttons;
            }
        }
    }

    codeos_input_event_t mev;
    static int last_buttons = 0;
    if (codeos_platform_poll_mouse_continuous(&mev) > 0) {
        Qt::MouseButtons buttons;
        if (mev.mouse_buttons & 1) buttons |= Qt::LeftButton;
        if (mev.mouse_buttons & 2) buttons |= Qt::RightButton;
        if (mev.mouse_buttons & 4) buttons |= Qt::MiddleButton;

        QPoint pos(mev.mouse_x, mev.mouse_y);
        g_cursor_x = pos.x();
        g_cursor_y = pos.y();
        g_cursor_visible = true;
        updateCursorOnMove();
        QWindow *target = QGuiApplication::topLevelAt(pos);
        if (!target) {
            const auto windows = QGuiApplication::allWindows();
            for (QWindow *w : windows) {
                if (w && w->handle() && w->isVisible()) {
                    target = w;
                    break;
                }
            }
        }
        if (target && target->handle()) {
            QPlatformWindow *win = target->handle();
            QPoint local = pos - win->geometry().topLeft();

            if (mev.mouse_buttons != last_buttons) {
                if (mev.mouse_buttons & 1) {
                    QWindowSystemInterface::handleMouseEvent(
                        target, local, local, buttons,
                        Qt::LeftButton, QEvent::MouseButtonPress);
                } else if (!(mev.mouse_buttons & 1) && (last_buttons & 1)) {
                    QWindowSystemInterface::handleMouseEvent(
                        target, local, local, buttons,
                        Qt::LeftButton, QEvent::MouseButtonRelease);
                } else if (mev.mouse_buttons & 2) {
                    QWindowSystemInterface::handleMouseEvent(
                        target, local, local, buttons,
                        Qt::RightButton, QEvent::MouseButtonPress);
                } else if (!(mev.mouse_buttons & 2) && (last_buttons & 2)) {
                    QWindowSystemInterface::handleMouseEvent(
                        target, local, local, buttons,
                        Qt::RightButton, QEvent::MouseButtonRelease);
                }
                last_buttons = mev.mouse_buttons;
            }

            QWindowSystemInterface::handleMouseEvent(
                target, local, local, buttons,
                buttons & Qt::LeftButton ? Qt::LeftButton
                : buttons & Qt::RightButton ? Qt::RightButton
                : Qt::MiddleButton,
                QEvent::MouseMove);
        }
    }
}

QPlatformIntegration *createCodeOSQpaIntegration() {
    return new CodeOSIntegration();
}
