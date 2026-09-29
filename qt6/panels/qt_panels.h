#ifndef QT_PANELS_H
#define QT_PANELS_H

#include <QWidget>
#include <QApplication>
#include <QPainter>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QList>
#include <QStringList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QImage>
#include <QProcess>
#include <QClipboard>
#include <QElapsedTimer>

#include "codeos_window_manager.h"
#include "codeos_file_manager.h"
#include "codeos_terminal.h"
#include "lvgl_wm.h"
#include "codeos_font.h"
#include "codeos_image.h"

/* Shared colors, glass helpers, icon drawing (defined in qt_panels_common.cpp) */
#include "qt_panels_common.h"

/* ── Forward declarations ── */
class QtDesktopWidget;
class QtMenubar;
class QtDock;
class QtLauncherOverlay;
class QtHyperdeStrip;
class QtNotifCenter;
class QtAppSwitcher;
class QtCtxMenu;
class QtAppWindow;
class QtDesktopManager;
class QtTilingManager;
class QtWidgetsPanel;
class QtMissionControl;
class GenericAppWindow;
class QtTerminalWidget;
class QtAppHostWidget;
class QtCalcWidget;
class QtSettingsWidget;
class QtAboutWidget;
class QtSysInfoWidget;
class QtExitWidget;
class QtSysMonWidget;
class QtExplorerWidget;
class QtOpenWebWidget;
class QtInstallerWidget;
class QtLTWidget;
class QtNetBeamWidget;
class QtZiggyWidget;
class QtNotesWidget;
class QtClockWidget;
class QtConvertWidget;
class QPlainTextEdit;
class QResizeEvent;
class QComboBox;
class QLineEdit;
class QLabel;
class QWidget;
class QTimer;

/* ═══════════════════════════════════════════════════════════════════
   QtAppWindow — base class for all app windows with glass title bar
   ═══════════════════════════════════════════════════════════════════ */
class QtAppWindow : public QWidget {
    Q_OBJECT
public:
    explicit QtAppWindow(const QString &title, QWidget *parent = nullptr);
    virtual ~QtAppWindow() = default;
    void setAppTitle(const QString &title);
    QString appTitle() const { return m_title; }
    void setWmId(int id) { m_wmId = id; }
    int wmId() const { return m_wmId; }
    bool isClosing() const { return m_closing; }
    void startMinimizeAnim(int targetX, int targetY);

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    virtual void paintContent(QPainter &p, const QRect &contentRect) = 0;
    void closeEvent(QCloseEvent *event) override;

    enum ResizeEdge { NoEdge=0, Left=1, Right=2, Top=4, Bottom=8,
                      TopLeft=5, TopRight=6, BottomLeft=9, BottomRight=10 };
    ResizeEdge edgeAt(const QPoint &pos) const;
    void updateCursor(ResizeEdge edge);

    QString m_title;
    QRect m_titleBarRect;
    QRect m_closeRect, m_minRect, m_maxRect;
    bool m_dragging = false;
    QPoint m_dragStart;
    bool m_resizing = false;
    ResizeEdge m_resizeEdge = NoEdge;
    QPoint m_resizeStart;
    QRect m_resizeStartGeom;
    static const int RESIZE_MARGIN = 6;
    int m_wmId = 0;
    /* Animation */
    float m_opacity = 0.0f;
    float m_scale = 0.92f;
    QTimer m_animTimer;
    bool m_animIn = false;
    bool m_closing = false;
    /* Maximize / Minimize */
    bool m_maximized = false;
    bool m_minimizing = false;
    float m_minProgress = 0.0f;
    QRect m_restoreGeom;
    int m_minTargetX = 0, m_minTargetY = 0;
    /* Edge snapping while dragging: 0=none 1=left half 2=right half 3=max */
    int m_snapZone = 0;

private:
    void toggleMaximize();
};

/* Generic wrapper for any QWidget content */
class GenericAppWindow : public QtAppWindow {
    Q_OBJECT
public:
    GenericAppWindow(const QString &title, QWidget *content, QWidget *parent = nullptr)
        : QtAppWindow(title, parent), m_content(content) {
        if (m_content) {
            m_content->setParent(this);
            int tb = 30, cr = 14, sh = 8;
            QVBoxLayout *layout = new QVBoxLayout(this);
            layout->setContentsMargins(cr+sh, tb+sh+2, cr+sh, cr+sh);
            layout->addWidget(m_content);
        }
    }
protected:
    void paintContent(QPainter &, const QRect &) override {}
private:
    QWidget *m_content = nullptr;
};

/* ═══════════════════════════════════════════════════════════════════
   QtDesktopWidget — fullscreen desktop background
   ═══════════════════════════════════════════════════════════════════ */
class QtDesktopWidget : public QWidget {
    Q_OBJECT
public:
    explicit QtDesktopWidget(QWidget *parent = nullptr);
    void setWallpaper(const QImage &image);
    void setGradientWallpaper(const QColor &top, const QColor &bottom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *) override {}
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QImage m_wallpaper;
    /* Keep the rich procedural wallpaper active until the user chooses a
       custom image or gradient. */
    bool m_useGrad = false;
    QColor m_gradTop{0x0B, 0x0B, 0x2E};
    QColor m_gradBottom{0x14, 0x08, 0x20};
    /* Crossfade */
    QImage m_oldWallpaper;
    QColor m_oldGradTop, m_oldGradBottom;
    bool m_oldUseGrad = false;
    float m_crossfadeProgress = 1.0f;
    bool m_crossfading = false;
    QTimer m_crossfadeTimer;
    void startCrossfade(const QImage &img, const QColor &top, const QColor &bot, bool useGrad);
    void paintWallpaperLayer(QPainter &p, const QRect &r,
                             const QImage &img, bool useGrad,
                             const QColor &gradTop, const QColor &gradBot);
};

/* ═══════════════════════════════════════════════════════════════════
   QtMenubar — top glass menubar
   ═══════════════════════════════════════════════════════════════════ */
class QtMenubar : public QWidget {
    Q_OBJECT
public:
    explicit QtMenubar(QWidget *parent = nullptr);
    void setActiveApp(const QString &title);
    void setClockText(const QString &text);
    void setNotifBadge(bool hasBadge);
    QStringList menus() const;
    void closeMenu();

    std::function<void()> onAppMenuClicked;
    std::function<void()> onLauncherToggled;
    std::function<void()> onNotifToggled;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QStringList defaultMenuItems(int menuIdx) const;
    QString m_activeApp;
    QString m_clockText;
    bool m_hasNotifBadge = false;
    bool m_badgePulse = true;
    QTimer m_badgeTimer;
    QRect m_appleRect, m_appNameRect, m_clockRect, m_notifRect;
    QList<QRect> m_menuRects;
    int m_hoveredMenu = -1;
    bool m_menuOpen = false;
    int m_openMenuIdx = -1;
    QStringList m_menuItems;
};

/* ═══════════════════════════════════════════════════════════════════
   QtDock — macOS-style glass dock with fish-eye magnification
   ═══════════════════════════════════════════════════════════════════ */
class QtDock : public QWidget {
    Q_OBJECT
public:
    explicit QtDock(QWidget *parent = nullptr);
    void setItems(const QStringList &names);
    void setRunning(int index, bool running);
    void setBadge(int index, int count);
    void setHover(int index, bool hovered);
    void startBounce(int index);
    int itemCount() const { return m_items.size(); }
    QRect itemRect(int index) const { return (index >= 0 && index < m_items.size()) ? m_items[index].rect : QRect(); }

    std::function<void(int)> onItemClicked;

public slots:
    void updateAnimations();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct DockItem {
        QString name;
        bool running = false;
        int badge = 0;
        QRect rect;
        QRect magnifiedRect;
        float hoverProgress = 0.0f;
        float hoverVelocity = 0.0f;
        bool bouncing = false;
        float bounceT = 0.0f;     /* 0..1 launch-bounce progress */
        int bounceOffset = 0;     /* px, negative = up */
    };
    QList<DockItem> m_items;
    int m_hoveredIndex = -1;
    int m_pressedIndex = -1;
    int m_iconSize = 56;
    int m_maxIconSize = 88;
    QTimer m_animTimer;
};

/* ═══════════════════════════════════════════════════════════════════
   QtLauncherOverlay — fullscreen app launcher with search
   ═══════════════════════════════════════════════════════════════════ */
class QtLauncherOverlay : public QWidget {
    Q_OBJECT
public:
    explicit QtLauncherOverlay(QWidget *parent = nullptr);
    void showLauncher(bool androidMode = false);
    void hideLauncher();
    bool isOpen() const { return m_open; }
    bool androidMode() const { return m_androidMode; }
    std::function<void(int)> onAppSelected;
    /* Android mode: index into waydroid_app_name()/waydroid_app_label(). */
    std::function<void(int)> onAndroidSelected;
    /* called by the desktop manager's global filter while the launcher has
       keyboard ownership (window apps keep focus, keys are forwarded here) */
    void forwardKey(QKeyEvent *e) { keyPressEvent(e); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateGrid();
    void showPage(int page);
    void rebuildItemRects();
    void activateName(const QString &name);
    int startOfPage(int page, int perPage, int total) const;
    int visibleIndex() const { return m_currentPage*m_itemsPerPage + (m_hoveredIndex >= 0 ? m_hoveredIndex : m_selIndex); }
    bool m_open = false;
    bool m_androidMode = false;
    QList<QRect> m_itemRects;
    QStringList m_appNames;
    QStringList m_androidLabels;
    QStringList m_filteredNames;
    QString m_searchText;
    int m_hoveredIndex = -1;
    int m_selIndex = 0;
    int m_currentPage = 0;
    int m_itemsPerPage = 20;
};

/* ═══════════════════════════════════════════════════════════════════
   QtNotifCenter — side panel notifications
   ═══════════════════════════════════════════════════════════════════ */
class QtNotifCenter : public QWidget {
    Q_OBJECT
public:
    explicit QtNotifCenter(QWidget *parent = nullptr);
    void toggle();
    bool isOpen() const { return m_open; }
    void pushNotif(const QString &text, const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Notif { QString text; QColor color; };
    bool m_open = false;
    QList<Notif> m_notifs;
};

/* ═══════════════════════════════════════════════════════════════════
   QtQuickSettings — COSMIC-style quick-settings toast (top-right).
   Rises below the bar on a keyboard shortcut or applet click; shows
   live CPU / memory readouts and a power affordance.
   ═══════════════════════════════════════════════════════════════════ */
class QtQuickSettings : public QWidget {
    Q_OBJECT
public:
    explicit QtQuickSettings(QWidget *parent = nullptr);
    void toggle() { m_open ? hidePanel() : showPanel(); }
    bool isOpen() const { return m_open; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showPanel();
    void hidePanel() { m_open = false; hide(); }
    bool m_open = false;
    int m_cpu = 0, m_mem = 0, m_memTotal = 0;
};

/* ═══════════════════════════════════════════════════════════════════
   QtAppSwitcher — alt-tab overlay
   ═══════════════════════════════════════════════════════════════════ */
class QtAppSwitcher : public QWidget {
    Q_OBJECT
public:
    explicit QtAppSwitcher(QWidget *parent = nullptr);
    void activate();
    void deactivate();
    bool isActive() const { return m_active; }
    void next();
    void prev();
    int selected() const { return m_selected; }
    void commit();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_active = false;
    int m_selected = 0;
    QList<int> m_running;
};

/* ═══════════════════════════════════════════════════════════════════
   QtCtxMenu — glass context menu
   ═══════════════════════════════════════════════════════════════════ */
class QtCtxMenu : public QWidget {
    Q_OBJECT
public:
    explicit QtCtxMenu(QWidget *parent = nullptr);
    void showMenu(int x, int y, const QStringList &items,
                  const std::vector<std::function<void()>> &actions);
    void hideMenu();
    bool isOpen() const { return m_open; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    bool m_open = false;
    QStringList m_items;
    std::vector<std::function<void()>> m_actions;
    int m_hoveredIndex = -1;
    QRect m_menuRect;
};

/* ═══════════════════════════════════════════════════════════════════
   App Widgets
   ═══════════════════════════════════════════════════════════════════ */

class QtTerminalWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtTerminalWidget(QWidget *parent = nullptr);
    void appendOutput(const QString &text);

protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QStringList m_lines;
    QString m_inputLine;
    int m_cursorPos = 0;
    QStringList m_history;
    int m_historyPos = 0;
    QFont m_font{"Monospace", 11};
    int m_blink = 0;
    int m_scrollOffset = 0;
};

class QtAppHostWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtAppHostWidget(QWidget *parent = nullptr);

protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QStringList m_lines;
    QString m_lineBuf;
    QFont m_font{"Monospace", 10};
    QTimer m_timer;
};

class QtCalcWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtCalcWidget(QWidget *parent = nullptr);

protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QString m_display;
    double m_current = 0;
    char m_op = 0;
    bool m_newNumber = true;
    QRect m_btnRects[20];
    QString m_btnLabels[20];
    int m_hoveredBtn = -1;
    int m_pressedBtn = -1;
    void pressButton(int idx);
};

class QtSettingsWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtSettingsWidget(QWidget *parent = nullptr);

protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_selectedSection = 0;
    QList<QRect> m_sectionRects;
    struct WallpaperPreset { QString name; QColor top; QColor bottom; };
    QList<WallpaperPreset> m_wallpaperPresets;
    QList<QRect> m_wallpaperRects;
    int m_selectedWallpaper = 0;
    bool m_darkMode = true;
    QRect m_darkModeRect, m_lightModeRect;
    int m_hoveredBtn = -1;
};

class QtAboutWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtAboutWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
};

class QtNotesWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtNotesWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void loadNotes();
    void saveNotes();
    QString m_path;
    QPlainTextEdit *m_editor = nullptr;
};

class QtClockWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtClockWidget(QWidget *parent = nullptr);
    ~QtClockWidget() override;
protected:
    void paintContent(QPainter &p, const QRect &r) override;
private:
    QTimer *m_timer = nullptr;
};

class QtConvertWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtConvertWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void fillUnits();
    void convert();
    QWidget *m_body = nullptr;
    QComboBox *m_cat = nullptr;
    QComboBox *m_from = nullptr;
    QComboBox *m_to = nullptr;
    QLineEdit *m_in = nullptr;
    QLabel *m_out = nullptr;
};

class QtSysInfoWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtSysInfoWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
};

class QtExitWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtExitWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
private:
    QRect m_stopRect, m_cancelRect;
    int m_hoveredBtn = -1;
};

class QtSysMonWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtSysMonWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
private:
    uint64_t m_bootMs = 0;
    uint64_t m_lastBusy = 0, m_lastTotal = 0;
    int      m_cpuPct = 0;
    int m_tab = 0;
    int m_cpuHist[60] = {};
    int m_cpuIdx = 0;
    QTimer m_timer;
};

class QtExplorerWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtExplorerWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    QString m_path;
    QStringList m_entries;
    QList<bool> m_isDir;
    int m_scroll = 0;
    int m_selected = -1;
    QRect m_upRect;
    void navigateTo(const QString &path);
};

class QtOpenWebWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtOpenWebWidget(QWidget *parent = nullptr);
protected:
    void showEvent(QShowEvent *event) override;
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void navigateTo(const QString &url);
    void navigateAbsolute(const QString &url);
    void reloadActive();
    void back();
    void forward();
    void resetTabTo(const QString &url, const QString &title);
    void renderCurrentTab();
    QString currentTabUrl() const;
    QString resolveLink(const QString &rel) const;
    QString activeTitle() const;
    QString existingTabUrl(int i) const;
    bool isStartPage() const;
    void ensureImages();
    void paintPageContent(QPainter &p, const QRect &contentRect);
    QRect contentGeom() const;
    QRect startTileRect(int idx, const QRect &r) const;
    QString m_urlInput;
    int m_cursorPos = 0;
    QString m_status;
    int m_scrollY = 0;
    int m_maxScroll = 0;
    QRect m_backRect, m_fwdRect, m_reloadRect, m_stopRect, m_newTabRect;
    QList<QRect> m_tabRects;
    QList<QRect> m_tabCloseRects;
    int m_hoveredTab = -1;
    QRect m_scrollThumb;
    QRect m_lastRect;
    QList<QRect> m_linkRects;
    QList<QString> m_linkUrls;
    QString m_hoverUrl;
    /* form field interaction */
    QList<QRect> m_fieldRects;
    QList<int>   m_fieldIds;
    int m_activeField = -1;
    int m_fieldCursor = 0;
    void submitForm(int form);
    void activateFieldAt(const QPoint &pos);
    void nextField(int dir);
    void clearActiveField();
    void scrollFieldIntoView(int fi);
    bool m_initialized = false;
    int m_blinkCounter = 0;
    /* per-tab in-widget history */
    QList<QStringList> m_hist;      /* per tab: visited URLs (oldest first) */
    QList<int>         m_histPos;   /* per tab: current position (-1 = none) */
    QList<QString>     m_tabTitles; /* per tab: page titles */
    /* images */
    QList<QImage> m_imageCache;
    QList<bool>   m_imageLoaded;
    long m_imageLoadTick = 0;
    /* scrollbar drag */
    bool m_scrollDrag = false;
    int  m_scrollDragY = 0;
    int  m_scrollDragBase = 0;
    /* start page quick links (name, url) */
    QList<QString> m_quickNames;
    QList<QString> m_quickUrls;
    QTimer m_refreshTimer;
};

class QtInstallerWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtInstallerWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    enum Screen {
        SCREEN_WELCOME, SCREEN_WIFI, SCREEN_ACCOUNT,
        SCREEN_SUMMARY, SCREEN_INSTALLING, SCREEN_DONE, SCREEN_ERROR
    };
    Screen m_screen = SCREEN_WELCOME;
    int m_hoveredBtn = -1;
    int m_selectedWifi = -1;
    QString m_wifiPass;
    int m_wifiCursorPos = 0;
    bool m_wifiPassVisible = false;
    QString m_fullName, m_username, m_accountPass;
    int m_nameCursorPos = 0, m_userCursorPos = 0, m_passCursorPos = 0;
    int m_activeField = 0;
    QRect m_backBtn, m_nextBtn, m_actionBtn;
    void drawButton(QPainter &p, const QRect &br, const QString &label, bool primary);
    void drawInputField(QPainter &p, const QRect &r, const QString &label, const QString &value, int cursorPos, bool active, bool passwordMode = false);
    void navigateNext();
    void navigateBack();
    void startInstall();
    int m_selectedLocale = 0;
    QStringList m_locales = {"English", "Espa\u00f1ol", "Fran\u00e7ais", "Deutsch", "Portugu\u00eas"};
    QStringList m_localeCodes = {"en_US", "es_ES", "fr_FR", "de_DE", "pt_BR"};
    int m_blink = 0;
};

class QtLTWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtLTWidget(QWidget *parent = nullptr);
    ~QtLTWidget();
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
private:
    void pollConsole();
    void scrollToBottom();
    int m_containerId = -1;
    bool m_containerReady = false;
    QStringList m_lines;
    QString m_currentLine;
    int m_cursorCol = 0;
    int m_scrollOffset = 0;
    QFont m_font{"Monospace", 11};
    QTimer m_pollTimer;
};

class QtNetBeamWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtNetBeamWidget(QWidget *parent = nullptr);
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
private:
    struct Device {
        QString name;
        QString type;
        QColor color;
        int signalStrength;
        bool transferring;
        int progress;
        uint32_t ip;
        uint32_t lastSeenMs;
    };
    QList<Device> m_devices;
    QList<QString> m_history;
    QStringList m_log;
    int m_selectedDevice = -1;
    int m_tab = 0;
    QRect m_btnRects[5];
    int m_hoveredBtn = -1;
    int m_hoveredDevice = -1;
    bool m_discovering = false;
    QTimer m_discoverTimer;
    int m_discoverPhase = 0;
    QStringList m_selectedFiles;
    uint32_t m_myIp = 0;
    QString m_myName;
    int m_xferReported = 0;
    void discoverDevices();
    void chooseFiles();
    void sendFilesToDevice(int deviceIndex);
    void pollNetwork();
};

class QtZiggyWidget : public QtAppWindow {
    Q_OBJECT
public:
    explicit QtZiggyWidget(QWidget *parent = nullptr);
    ~QtZiggyWidget() override;
protected:
    void paintContent(QPainter &p, const QRect &r) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void setListening(bool enabled);
    void pollMicrophone();
    bool handleVoiceCommand(const QString &text);
    void submitText();
    struct Message { bool fromAi; QString text; };
    QList<Message> m_messages;
    QString m_input;
    QTimer m_micTimer;
    QRect m_listenButton;
    QRect m_sendRect;
    QString m_status = "Microphone unavailable";
    QString m_transcript = "Connect a USB microphone to talk to Ziggy.";
    int m_micStream = -1;
    bool m_listening = false;
    int m_level = 0;
    int m_hoveredBtn = -1;
};

/* ═══════════════════════════════════════════════════════════════════
   Tiling Window Manager (Cosmic-style)
   ═══════════════════════════════════════════════════════════════════ */
struct QtWorkspace {
    int id;
    QString name;
    QList<QWidget*> windows;
    QRect geometry;
};

class QtTilingManager : public QObject {
    Q_OBJECT
public:
    explicit QtTilingManager(QObject *parent = nullptr);
    void addWindow(QWidget *window, int workspaceId = -1);
    void removeWindow(QWidget *window);
    void setWorkspace(int workspaceId);
    int currentWorkspace() const { return m_currentWorkspace; }
    int workspaceCount() const { return m_workspaceCount; }
    void focusNext();
    void focusPrev();
    void moveWindowToWorkspace(QWidget *window, int workspaceId);
    void tileWorkspace(int workspaceId);

signals:
    void workspaceChanged(int id);

private:
    int m_currentWorkspace = 0;
    int m_workspaceCount = 6;
    QList<QtWorkspace> m_workspaces;
};

/* ═══════════════════════════════════════════════════════════════════
   QtToastNotification — slide-in banner notifications
   ═══════════════════════════════════════════════════════════════════ */
class QtToastNotification : public QWidget {
    Q_OBJECT
public:
    explicit QtToastNotification(QWidget *parent = nullptr);
    void pushToast(const QString &text, const QColor &accent = QColor(0xFF,0x5A,0x36),
                   std::function<void()> onClick = nullptr);
    int count() const { return m_toasts.size(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    struct Toast {
        QString text;
        QColor accent;
        float opacity = 1.0f;
        float slideX = 1.0f;   /* 1.0 = off-screen, 0.0 = fully in */
        bool fading = false;
        std::function<void()> onClick;
    };
    QList<Toast> m_toasts;
    QTimer m_animTimer;
};

/* ═══════════════════════════════════════════════════════════════════
   QtWidgetsPanel — Sonoma-style desktop gadget stack (right side)
   Clock card · month calendar card · Golden Gate weather card
   ═══════════════════════════════════════════════════════════════════ */
class QtWidgetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit QtWidgetsPanel(QWidget *parent = nullptr);
    void setTime(const QString &timeText, const QString &dateText);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintGlassCard(QPainter &p, const QRect &r);
    QString m_time = "12:00";
    QString m_date;
};

/* ═══════════════════════════════════════════════════════════════════
   QtMissionControl — Super+Up overview of all open windows
   Live widget grabs arranged in a grid on a dark scrim
   ═══════════════════════════════════════════════════════════════════ */
class QtMissionControl : public QWidget {
    Q_OBJECT
public:
    explicit QtMissionControl(QWidget *parent = nullptr);
    void showOverview();
    void hideOverview();
    bool isOpen() const { return m_open; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct McCell { QRect rect; QtAppWindow *win; };
    QTimer *m_keepAlive = nullptr;
    bool m_open = false;
    float m_fade = 0.0f;
    QList<McCell> m_cells;
    QList<QPixmap> m_grabs;
};

/* ═══════════════════════════════════════════════════════════════════
   QtDesktopManager — singleton orchestrator
   ═══════════════════════════════════════════════════════════════════ */
/* Transparent top strip that receives clicks on the Rust-drawn HyperDE bar.
   Paints nothing (the Rust compositor owns those pixels); hit-testing is
   delegated to hyperde_shell_bar_hit() so layout never drifts. */
class QtHyperdeStrip : public QWidget {
public:
    explicit QtHyperdeStrip(QWidget *parent) : QWidget(parent) {
        setMouseTracking(true);
        setAutoFillBackground(false);
    }
    void showStrip() { show(); raise(); }
    void hideStrip() { hide(); }
protected:
    void paintEvent(QPaintEvent *) override {}
    void mousePressEvent(QMouseEvent *e) override;
};

class QtDesktopManager : public QObject {
    Q_OBJECT
public:
    static QtDesktopManager *instance();

    bool init();
    void run();
    void stop();
    bool active() const { return m_running; }

    QtDesktopWidget *desktop()           { return m_desktop; }
    QtMenubar *menubar()                 { return m_menubar; }
    QtDock *dock()                       { return m_dock; }
    QtLauncherOverlay *launcher()        { return m_launcher; }
    QtNotifCenter *notifCenter()         { return m_notifCenter; }
    QtWidgetsPanel *widgetsPanel()       { return m_widgets; }
    QtMissionControl *missionControl()   { return m_missionControl; }

    /* Window edge snapping: show/hide the translucent preview + apply */
    enum SnapZone { SnapNone = 0, SnapLeft = 1, SnapRight = 2, SnapMax = 3 };
    QWidget *snapPreview();
    void updateSnapPreview(int zone, const QRect &target);
    void hideSnapPreview();

    QtAppSwitcher *appSwitcher()         { return m_appSwitcher; }
    QtCtxMenu *ctxMenu()                 { return m_ctxMenu; }
    QtQuickSettings *quickSettings()     { return m_quickSettings; }
    void toggleQuickSettings() { if (m_quickSettings) m_quickSettings->toggle(); }
    QtToastNotification *toast()         { return m_toast; }
    CodeOSWindowManager *windowManager() { return m_windowManager; }
    CodeOSFileManager *fileManager()     { return m_fileManager; }
    CodeOSTerminal *terminal()           { return m_terminal; }
    lvgl_wm_t *wm()                      { return &m_wm; }
    QStringList appNames() const         { return m_appNames; }
    QStringList androidAppNames() const;  /* labels for the Android picker */
    bool appRunning(int i) const         { return i >= 0 && i < APP_COUNT && m_appRunning[i]; }
    QtAppWindow **appWindows()           { return m_appWindows; }

    QtTilingManager *tilingManager()     { return m_tilingManager; }
    void toggleAutoTiling()              { m_autoTiling = !m_autoTiling; showToast(m_autoTiling ? "Auto-tiling: on" : "Auto-tiling: off", QColor(0x40,0xD9,0xF0)); }
    bool handleGlobalShortcut(QKeyEvent *event);
    bool eventFilter(QObject *obj, QEvent *event) override;

    QtDesktopManager();
    void setFocusedApp(int idx);
    void launchApp(int index);
    void launchAndroidApp(int androidIndex);
    void setupApps();
    void toggleMissionControl();

    /* HyperDE chrome interaction */
    void toggleLauncher();
    void toggleLauncherAndroid();
    void focusHyperdeWindow(int wmIdx);
    void syncHyperdeChrome(bool active);
    QtHyperdeStrip *hyperdeStrip() { return m_hyperdeStrip; }

    /* Theme animation */
    bool isDarkTheme() const { return m_themeBlend < 0.5f; }
    void setThemeTarget(bool dark);
    QColor themedColor(const QColor &dark, const QColor &light) const;

    /* Toast convenience */
    void showToast(const QString &text, const QColor &accent = QColor(0xFF,0x5A,0x36),
                   std::function<void()> onClick = nullptr);

private:
    static QtDesktopManager *s_instance;
    void registerAppWindow(QtAppWindow *w, int slot, const QString &title, int ww, int wh);
    bool m_running = false;
    int m_focusedApp = -1;

    QtDesktopWidget *m_desktop = nullptr;
    QtMenubar *m_menubar = nullptr;
    QtDock *m_dock = nullptr;
    QtLauncherOverlay *m_launcher = nullptr;
    QtHyperdeStrip *m_hyperdeStrip = nullptr;
    QtNotifCenter *m_notifCenter = nullptr;
    QtQuickSettings *m_quickSettings = nullptr;
    QtAppSwitcher *m_appSwitcher = nullptr;
    QtCtxMenu *m_ctxMenu = nullptr;
    QtToastNotification *m_toast = nullptr;
    QtWidgetsPanel *m_widgets = nullptr;
    QtMissionControl *m_missionControl = nullptr;
    QWidget *m_snapPreview = nullptr;
    int m_snapZoneActive = 0;
    CodeOSWindowManager *m_windowManager = nullptr;
    CodeOSFileManager *m_fileManager = nullptr;
    CodeOSTerminal *m_terminal = nullptr;
    QTimer *m_clockTimer = nullptr;
    lvgl_wm_t m_wm;
    QStringList m_appNames;
    int m_appRunning[APP_COUNT] = {};
    QtAppWindow *m_appWindows[APP_COUNT] = {};
    QtTilingManager *m_tilingManager = nullptr;
    int m_currentWorkspace = 0;
    bool m_autoTiling = true;

    /* Theme animation */
    float m_themeBlend = 0.0f;
    float m_themeTarget = 0.0f;
    bool m_themeAnimating = false;
    QTimer m_themeTimer;
};

#endif
