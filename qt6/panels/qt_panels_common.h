#ifndef QT_PANELS_COMMON_H
#define QT_PANELS_COMMON_H

#include <QColor>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QPainterPath>
#include <QMap>

/* ── Layout constants ── */
#define MENUBAR_H   28
#define DOCK_H      80
#define APP_COUNT   18

/* "LaunchApp" — the dock's app-picker icon. It lives in m_appNames at this
 * index (so the dock and launcher grid pick it up) but is excluded from the
 * launcher grid and opens the fullscreen picker instead of a window. */
#define LAUNCHAPP_INDEX  16

/* "Android" — the dock's Android app-picker icon. It opens the launcher
 * overlay in Android mode; selecting an app hosts it asynchronously through
 * the appvm container + apphost (see waydroid_app_launch_async). */
#define ANDROID_INDEX    17

#define TOAST_W         340
#define TOAST_H         44
#define TOAST_GAP       4
#define TOAST_MAX       5
#define TOAST_LIFETIME_MS 3000
#define WINDOW_TB_H     30
#define WINDOW_CR       12
#define WINDOW_SHADOW   6
#define DOCK_ICON_BASE  56
#define DOCK_ICON_MAX   88

/* ── Color palette (macOS Big Sur / Tahoe dark) ── */
#define C_BASE      0xFF1C1C1E
#define C_MANTLE    0xFF2C2C2E
#define C_CRUST     0xFF141416
#define C_SURFACE0  0xFF2C2C2E
#define C_SURFACE1  0xFF3A3A3C
#define C_SURFACE2  0xFF48484A
#define C_OVERLAY0  0xFF636366
#define C_SUBTEXT0  0xFF8E8E93
#define C_SUBTEXT1  0xFFAEAEB2
#define C_TEXT      0xFFF5F5F7
#define C_BLUE      0xFF0A84FF
#define C_GREEN     0xFF30D158
#define C_YELLOW    0xFFFFD60A
#define C_RED       0xFFFF453A
#define C_MAUVE     0xFFBF5AF2
#define C_SKY       0xFF64D2FF
#define C_ORANGE    0xFFFF9F0A
#define C_GOLDEN    0xFFE88A1A
#define C_ACCENT    0xFFFF5A36

/* ── Glass helpers (defined in qt_panels_common.cpp) ── */
QColor glass_bg(int alpha = 220);
QColor glass_border(int alpha = 60);
QColor glass_highlight(int alpha = 20);

/* ── Named QColor palette ── */
extern const QColor c_red;
extern const QColor c_green;
extern const QColor c_yellow;
extern const QColor c_accent;
extern const QColor c_text;
extern const QColor c_subtext;
extern const QColor c_surface;
extern const QColor c_surface1;
extern const QColor c_surface2;
extern const QColor c_overlay;
extern const QColor c_mauve;
extern const QColor c_sky;
extern const QColor c_orange;
extern const QColor c_golden;

/* ── App icon helpers (defined in qt_panels_common.cpp) ── */
QColor appIconColor(const QString &name);
QString appIconLetter(const QString &name);
void drawAppIcon(QPainter &p, const QRect &r, const QString &name, int sz);

/* ── Button state enum ── */
enum ButtonState { BtnNormal, BtnHover, BtnPressed, BtnDisabled };

/* ── Glass button helper — draws a macOS-style pill button ── */
void drawGlassButton(QPainter &p, const QRect &r, const QString &label,
                     ButtonState state = BtnNormal, bool primary = false,
                     int fontSize = 12);

/* ── Returns the bounding rect of a glass button (for hit-testing) ── */
QRect glassButtonRect(const QRect &r);

/* Semi-transparent glass panel helper */
void drawGlassPanel(QPainter &p, const QRect &r, int radius, int alpha = 160);

/* ── Text drawing that bypasses Qt's antialiased cached-glyph path ──
 * The CodeOS bitmap font engine's glyph cache renders dim, desaturated
 * glyphs when QPainter::Antialiasing is on (the engine's coverage maps
 * get misread as color glyphs).  The direct (non-AA) path renders them
 * perfectly, so all custom-painted text goes through these wrappers,
 * which temporarily clear the AA hint. */
inline void drawTextNA(QPainter &p, int x, int y, const QString &s) {
    bool h = p.testRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawText(x, y, s);
    if (h) p.setRenderHint(QPainter::Antialiasing, true);
}
inline void drawTextNA(QPainter &p, const QPointF &pt, const QString &s) {
    bool h = p.testRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawText(pt, s);
    if (h) p.setRenderHint(QPainter::Antialiasing, true);
}
inline void drawTextNA(QPainter &p, const QRect &r, int flags, const QString &s) {
    bool h = p.testRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawText(r, flags, s);
    if (h) p.setRenderHint(QPainter::Antialiasing, true);
}
inline void drawTextNA(QPainter &p, const QRectF &r, int flags, const QString &s) {
    bool h = p.testRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawText(r, flags, s);
    if (h) p.setRenderHint(QPainter::Antialiasing, true);
}

#endif
