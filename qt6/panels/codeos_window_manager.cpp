#include "codeos_window_manager.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QPainter>
#include <QStyleOption>
#include <QDebug>
#include <QTimer>

extern "C" {
#include "fb.h"
}

static QRect screenGeom() {
    return QRect(0, 0, (int)fb_getwidth(), (int)fb_getheight());
}

CodeOSWindowManager::CodeOSWindowManager(QApplication *app, QObject *parent)
    : QObject(parent), m_app(app) {
    QRect geom = screenGeom();

    /* No overlay desktop widget: this manager is legacy; the visible desktop
     * is QtDesktopWidget (qt_panels_shell.cpp) and window layout is handled
     * by lvgl_wm.  A fullscreen QWidget here would paint over the wallpaper
     * and freeze everything below the menubar. */
    Q_UNUSED(geom);
}

CodeOSWindowManager::~CodeOSWindowManager() {
    qDeleteAll(m_windows.keys());
    delete m_desktop;
}

CodeOSWindowManager::WindowData CodeOSWindowManager::createWindowDecorations(QWidget *window, const QString &title) {
    WindowData data;

    data.widget = window;
    window->setParent(nullptr);
    window->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setMouseTracking(true);
    window->installEventFilter(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(window);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(0);

    data.titleBar = new QWidget(window);
    data.titleBar->setFixedHeight(32);
    data.titleBar->setStyleSheet(
        "background-color: #2d2d3d;"
        "border-top-left-radius: 4px;"
        "border-top-right-radius: 4px;"
    );
    data.titleBar->setMouseTracking(true);
    data.titleBar->installEventFilter(this);

    QHBoxLayout *titleLayout = new QHBoxLayout(data.titleBar);
    titleLayout->setContentsMargins(12, 0, 8, 0);
    titleLayout->setSpacing(8);

    data.titleLabel = new QLabel(title, data.titleBar);
    data.titleLabel->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: 500;");
    data.titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLayout->addWidget(data.titleLabel);

    data.minimizeBtn = new QPushButton("─", data.titleBar);
    data.minimizeBtn->setFixedSize(28, 28);
    data.minimizeBtn->setStyleSheet(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background: #3d3d4d; }"
        "QPushButton:pressed { background: #4d4d5d; }"
    );
    data.minimizeBtn->setToolTip("Minimize");
    connect(data.minimizeBtn, &QPushButton::clicked, [this, window]() { minimizeWindow(window); });
    titleLayout->addWidget(data.minimizeBtn);

    data.maximizeBtn = new QPushButton("□", data.titleBar);
    data.maximizeBtn->setFixedSize(28, 28);
    data.maximizeBtn->setStyleSheet(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background: #3d3d4d; }"
        "QPushButton:pressed { background: #4d4d5d; }"
    );
    data.maximizeBtn->setToolTip("Maximize");
    connect(data.maximizeBtn, &QPushButton::clicked, [this, window]() {
        auto it = m_windows.find(window);
        if (it != m_windows.end()) {
            if (it->maximized) restoreWindow(window);
            else maximizeWindow(window);
        }
    });
    titleLayout->addWidget(data.maximizeBtn);

    data.closeBtn = new QPushButton("✕", data.titleBar);
    data.closeBtn->setFixedSize(28, 28);
    data.closeBtn->setStyleSheet(
        "QPushButton { color: #ffffff; background: transparent; border: none; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background: #ef4444; }"
        "QPushButton:pressed { background: #dc2626; }"
    );
    data.closeBtn->setToolTip("Close");
    connect(data.closeBtn, &QPushButton::clicked, [this, window]() { closeWindow(window); });
    titleLayout->addWidget(data.closeBtn);

    mainLayout->addWidget(data.titleBar);

    data.content = new QWidget(window);
    data.content->setStyleSheet(
        "background-color: #1e1e2e;"
        "border-bottom-left-radius: 4px;"
        "border-bottom-right-radius: 4px;"
    );
    mainLayout->addWidget(data.content, 1);

    return data;
}

QWidget *CodeOSWindowManager::createWindow(const QString &title, const QRect &geometry, Qt::WindowFlags flags) {
    QWidget *window = new QWidget(nullptr, flags | Qt::FramelessWindowHint);
    WindowData data = createWindowDecorations(window, title);
    m_windows[window] = data;

    if (geometry.isValid()) {
        window->setGeometry(geometry);
    } else {
        QRect scr = screenGeom();
        int w = scr.width() * 3 / 4;
        int h = scr.height() * 3 / 4;
        window->setGeometry((scr.width() - w) / 2, (scr.height() - h) / 2, w, h);
    }

    if (!m_activeWindow) {
        setActiveWindow(window);
    }

    window->show();
    raiseWindow(window);
    return window;
}

void CodeOSWindowManager::closeWindow(QWidget *window) {
    auto it = m_windows.find(window);
    if (it != m_windows.end()) {
        if (m_activeWindow == window) {
            m_activeWindow = nullptr;
        }
        m_windows.erase(it);
        window->deleteLater();

        if (!m_activeWindow && !m_windows.isEmpty()) {
            for (auto wit = m_windows.begin(); wit != m_windows.end(); ++wit) {
                if (wit.key()->isVisible()) {
                    raiseWindow(wit.key());
                    break;
                }
            }
        }
    }
}

void CodeOSWindowManager::minimizeWindow(QWidget *window) {
    auto it = m_windows.find(window);
    if (it != m_windows.end()) {
        it->savedGeometry = window->geometry();
        window->hide();
        if (m_activeWindow == window) {
            m_activeWindow = nullptr;
            if (!m_windows.isEmpty()) {
                for (auto wit = m_windows.begin(); wit != m_windows.end(); ++wit) {
                    if (wit.key()->isVisible()) {
                        setActiveWindow(wit.key());
                        break;
                    }
                }
            }
        }
    }
}

void CodeOSWindowManager::maximizeWindow(QWidget *window) {
    auto it = m_windows.find(window);
    if (it != m_windows.end() && !it->maximized) {
        it->savedGeometry = window->geometry();
        window->setGeometry(screenGeom());
        it->maximized = true;
        it->maximizeBtn->setText("❐");
        raiseWindow(window);
    }
}

void CodeOSWindowManager::restoreWindow(QWidget *window) {
    auto it = m_windows.find(window);
    if (it != m_windows.end() && it->maximized) {
        window->setGeometry(it->savedGeometry);
        it->maximized = false;
        it->maximizeBtn->setText("□");
        raiseWindow(window);
    }
}

void CodeOSWindowManager::raiseWindow(QWidget *window) {
    window->raise();
    if (m_activeWindow != window) {
        setActiveWindow(window);
    }
}

void CodeOSWindowManager::setActiveWindow(QWidget *window) {
    if (!window || !m_windows.contains(window) || !window->isVisible()) {
        return;
    }
    if (m_activeWindow && m_activeWindow != window) {
        auto it = m_windows.find(m_activeWindow);
        if (it != m_windows.end()) {
            it->titleBar->setStyleSheet(
                "background-color: #2d2d3d;"
                "border-top-left-radius: 4px;"
                "border-top-right-radius: 4px;"
            );
        }
    }
    m_activeWindow = window;
    auto it = m_windows.find(window);
    if (it != m_windows.end()) {
        it->titleBar->setStyleSheet(
            "background-color: #3b82f6;"
            "border-top-left-radius: 4px;"
            "border-top-right-radius: 4px;"
        );
    }
}

QList<QWidget*> CodeOSWindowManager::windows() const {
    return m_windows.keys();
}

QWidget *CodeOSWindowManager::activeWindow() const {
    return m_activeWindow;
}

void CodeOSWindowManager::setSnapEnabled(bool enabled) {
    m_snapEnabled = enabled;
}

bool CodeOSWindowManager::isSnapEnabled() const {
    return m_snapEnabled;
}

int CodeOSWindowManager::hitTestResizeEdge(const QRect &frame, const QPoint &pos) {
    int edge = 0;
    int margin = 8;
    if (pos.x() >= frame.left() && pos.x() < frame.left() + margin) edge |= 1;
    if (pos.x() > frame.right() - margin && pos.x() <= frame.right()) edge |= 2;
    if (pos.y() >= frame.top() && pos.y() < frame.top() + margin) edge |= 4;
    if (pos.y() > frame.bottom() - margin && pos.y() <= frame.bottom()) edge |= 8;
    return edge;
}

void CodeOSWindowManager::applySnap(QWidget *window, const QPoint &pos) {
    if (!m_snapEnabled) return;

    QRect scr = screenGeom();
    int margin = 20;

    if (pos.x() <= scr.left() + margin) {
        window->setGeometry(scr.left(), scr.top(),
                            scr.width() / 2, scr.height());
    } else if (pos.x() >= scr.right() - margin) {
        window->setGeometry(scr.left() + scr.width() / 2, scr.top(),
                            scr.width() / 2, scr.height());
    } else if (pos.y() <= scr.top() + margin) {
        window->setGeometry(scr.left(), scr.top(),
                            scr.width(), scr.height());
    }
}

bool CodeOSWindowManager::eventFilter(QObject *obj, QEvent *event) {
    QWidget *window = nullptr;
    for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
        if (it.key() == obj || it->titleBar == obj || it->content == obj) {
            window = it.key();
            break;
        }
    }

    if (!window) return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            handleMousePress(window, me);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        handleMouseMove(window, me);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            handleMouseRelease(window, me);
            return true;
        }
        break;
    }
    case QEvent::Enter: {
        raiseWindow(window);
        break;
    }
    case QEvent::WindowActivate: {
        setActiveWindow(window);
        break;
    }
    default:
        break;
    }

    return false;
}

void CodeOSWindowManager::handleMousePress(QWidget *window, QMouseEvent *event) {
    WindowData &data = m_windows[window];
    QPoint globalPos = event->globalPosition().toPoint();
    QPoint localPos = window->mapFromGlobal(globalPos);
    QWidget *child = window->childAt(localPos);

    if (child == data.closeBtn || child == data.minimizeBtn || child == data.maximizeBtn) {
        return;
    }

    if (data.titleBar->geometry().contains(localPos)) {
        data.dragging = true;
        data.dragEdge = 0;
        data.dragStartPos = globalPos - window->frameGeometry().topLeft();
        raiseWindow(window);
    } else {
        int edge = hitTestResizeEdge(window->rect(), localPos);
        if (edge) {
            data.dragging = true;
            data.dragEdge = edge;
            data.dragStartPos = globalPos;
            raiseWindow(window);
        }
    }
}

void CodeOSWindowManager::handleMouseMove(QWidget *window, QMouseEvent *event) {
    WindowData &data = m_windows[window];
    QPoint globalPos = event->globalPosition().toPoint();

    if (data.dragging) {
        if (data.dragEdge == 0) {
            QPoint newPos = globalPos - data.dragStartPos;
            window->move(newPos);
        } else {
            QRect geom = window->geometry();
            QPoint delta = globalPos - data.dragStartPos;

            if (data.dragEdge & 1) { geom.setLeft(geom.left() + delta.x()); }
            if (data.dragEdge & 2) { geom.setRight(geom.right() + delta.x()); }
            if (data.dragEdge & 4) { geom.setTop(geom.top() + delta.y()); }
            if (data.dragEdge & 8) { geom.setBottom(geom.bottom() + delta.y()); }

            if (geom.width() >= window->minimumWidth() &&
                geom.height() >= window->minimumHeight()) {
                window->setGeometry(geom);
            }
        }
    } else {
        int edge = hitTestResizeEdge(window->rect(), window->mapFromGlobal(globalPos));
        if (edge) {
            Qt::CursorShape cursor = Qt::ArrowCursor;
            if ((edge & 1) && (edge & 4)) cursor = Qt::SizeFDiagCursor;
            else if ((edge & 2) && (edge & 8)) cursor = Qt::SizeFDiagCursor;
            else if ((edge & 1) && (edge & 8)) cursor = Qt::SizeBDiagCursor;
            else if ((edge & 2) && (edge & 4)) cursor = Qt::SizeBDiagCursor;
            else if (edge & (1 | 2)) cursor = Qt::SizeHorCursor;
            else if (edge & (4 | 8)) cursor = Qt::SizeVerCursor;
            window->setCursor(cursor);
        } else if (data.titleBar->geometry().contains(window->mapFromGlobal(globalPos))) {
            window->setCursor(Qt::ArrowCursor);
        } else {
            window->unsetCursor();
        }
    }
}

void CodeOSWindowManager::handleMouseRelease(QWidget *window, QMouseEvent *event) {
    WindowData &data = m_windows[window];
    if (data.dragging) {
        bool wasMoving = data.dragEdge == 0;
        data.dragging = false;
        data.dragEdge = 0;
        window->unsetCursor();

        if (wasMoving) {
            applySnap(window, event->globalPosition().toPoint());
        }
    }
}