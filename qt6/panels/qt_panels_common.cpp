#include "qt_panels_common.h"
#include <QLinearGradient>
#include <QFont>
#include <QCache>
#include <QPixmap>
#include <QRadialGradient>

/* ── Glass helpers ── */
QColor glass_bg(int alpha)       { return QColor(30, 30, 32, alpha); }
QColor glass_border(int alpha)   { return QColor(153, 153, 155, alpha); }
QColor glass_highlight(int alpha){ return QColor(255, 255, 255, alpha); }

/* ── Named QColor palette ── */
const QColor c_red     = QColor(0xFF, 0x45, 0x3A);
const QColor c_green   = QColor(0x30, 0xD1, 0x58);
const QColor c_yellow  = QColor(0xFF, 0xD6, 0x0A);
const QColor c_accent    = QColor(0xFF, 0x5A, 0x36);  /* GG Orange — renamed to c_accent for compat */
const QColor c_text    = QColor(0xF5, 0xF5, 0xF7);
const QColor c_subtext = QColor(0x8E, 0x8E, 0x93);
const QColor c_surface = QColor(0x2C, 0x2C, 0x2E);
const QColor c_surface1 = QColor(0x3A, 0x3A, 0x3C);
const QColor c_surface2 = QColor(0x48, 0x48, 0x4A);
const QColor c_overlay  = QColor(0x63, 0x63, 0x66);
const QColor c_mauve   = QColor(0xBF, 0x5A, 0xF2);
const QColor c_sky     = QColor(0x64, 0xD2, 0xFF);
const QColor c_orange  = QColor(0xFF, 0x9F, 0x0A);
const QColor c_golden  = QColor(0xE8, 0x8A, 0x1A);

/* ── App icon color mapping ── */
QColor appIconColor(const QString &name) {
    if (name == "Terminal")    return QColor(0x00, 0x7A, 0xCC);
    if (name == "OpenWeb")     return QColor(0xFF, 0x95, 0x00);
    if (name == "Settings")    return QColor(0x5E, 0x5C, 0xE6);
    if (name == "Calc")        return QColor(0x34, 0xC7, 0x59);
    if (name == "About")       return QColor(0xAF, 0x52, 0xDE);
    if (name == "Explorer")    return QColor(0xFF, 0x2D, 0x55);
    if (name == "Exit")        return QColor(0xFF, 0x3B, 0x30);
    if (name == "Sys Info")    return QColor(0xFF, 0x5A, 0x36);
    if (name == "SysMon")      return QColor(0xFF, 0x9F, 0x0A);
    if (name == "LT")          return QColor(0x64, 0xD2, 0xFF);
    if (name == "Wine")        return QColor(0xA0, 0x30, 0x50);
    if (name == "NetBeam")     return QColor(0x00, 0x7A, 0xFF);
    if (name == "Notes")       return QColor(0xFD, 0xD8, 0x35);
    if (name == "Clock")       return QColor(0x22, 0x2E, 0x3B);
    if (name == "Convert")     return QColor(0x7E, 0xE7, 0x87);
    if (name == "Android")     return QColor(0x3D, 0xDC, 0x84);
    if (name == "LaunchApp")   return QColor(0x5E, 0x5C, 0xE6);
    return QColor(0x89, 0xB4, 0xFA);
}

/* ── App icon letter ── */
QString appIconLetter(const QString &name) {
    if (name == "Sys Info") return "i";
    if (name == "SysMon")   return "S";
    if (name == "Exit")     return "X";
    if (name == "LT")       return "L";
    if (name == "Wine")     return "W";
    if (name == "NetBeam")  return "N";
    return name.left(1);
}

/* ═══════════════════════════════════════════════════════════════════
   drawAppIcon — macOS-style procedural icons with real depth
   ═══════════════════════════════════════════════════════════════════ */

/* Pixmap cache: keyed by "name@size" */
static QCache<QString, QPixmap> s_iconCache;

void drawAppIcon(QPainter &p, const QRect &r, const QString &name, int sz) {
    /* Check cache first */
    QString key = name + "@" + QString::number(sz);
    QPixmap *cached = s_iconCache.object(key);
    if (!cached) {
        cached = new QPixmap(sz, sz);
        cached->fill(Qt::transparent);
        QPainter cp(cached);
        cp.setRenderHint(QPainter::Antialiasing);

        QColor bg = appIconColor(name);
        int cr = sz / 5;
        int pad = qMax(2, sz / 32);   /* 1px outer stroke space */

        /* ── Drop shadow (3-layer soft) ── */
        cp.setPen(Qt::NoPen);
        cp.setBrush(QColor(0, 0, 0, 18));
        cp.drawRoundedRect(QRect(pad+1, pad+3, sz-pad*2, sz-pad*2), cr, cr);
        cp.setBrush(QColor(0, 0, 0, 12));
        cp.drawRoundedRect(QRect(pad, pad+2, sz-pad*2, sz-pad*2), cr, cr);
        cp.setBrush(QColor(0, 0, 0, 8));
        cp.drawRoundedRect(QRect(pad, pad+1, sz-pad*2, sz-pad*2-1), cr, cr);

        /* ── Background: two-tone vertical gradient (top lighter, bottom saturated) ── */
        QRect body(pad, pad, sz - pad*2, sz - pad*2);
        QLinearGradient bg2(body.topLeft(), body.bottomLeft());
        QColor bgLight = bg.lighter(125);
        bgLight.setAlpha(255);
        QColor bgDark = bg.darker(110);
        bgDark.setAlpha(255);
        bg2.setColorAt(0.0, bgLight);
        bg2.setColorAt(0.45, bg);
        bg2.setColorAt(1.0, bgDark);
        cp.setPen(Qt::NoPen);
        cp.setBrush(bg2);
        cp.drawRoundedRect(body, cr, cr);

        /* ── 1px outer highlight stroke (top half lighter) ── */
        QLinearGradient strokeGrad(body.topLeft(), body.bottomLeft());
        strokeGrad.setColorAt(0.0, QColor(255, 255, 255, 50));
        strokeGrad.setColorAt(0.5, QColor(255, 255, 255, 15));
        strokeGrad.setColorAt(1.0, QColor(0, 0, 0, 20));
        cp.setPen(QPen(strokeGrad, 1));
        cp.setBrush(Qt::NoBrush);
        cp.drawRoundedRect(body.adjusted(0,0,-1,-1), cr, cr);

        /* ── Curved gloss reflection (ellipse-based, not rectangle) ── */
        QRect glossRect(body.x() + body.width()/6,
                        body.y() + 1,
                        body.width()*2/3,
                        body.height()/2);
        QRadialGradient gloss(glossRect.center(), glossRect.width()/2.0);
        gloss.setColorAt(0.0, QColor(255, 255, 255, 55));
        gloss.setColorAt(0.4, QColor(255, 255, 255, 20));
        gloss.setColorAt(0.8, QColor(255, 255, 255, 3));
        gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
        cp.setPen(Qt::NoPen);
        cp.setBrush(gloss);
        cp.drawRoundedRect(glossRect, cr-2, cr-2);

        /* ── Bottom specular rim ── */
        QRect rim(body.x()+body.width()/4, body.bottom()-body.height()/5,
                  body.width()/2, body.height()/10);
        QLinearGradient rimG(rim.topLeft(), rim.bottomLeft());
        rimG.setColorAt(0.0, QColor(255, 255, 255, 12));
        rimG.setColorAt(1.0, QColor(255, 255, 255, 0));
        cp.setBrush(rimG);
        cp.drawRoundedRect(rim, 4, 4);

        /* ── Translucent letter tile ── */
        int q = body.width() / 4;
        cp.setPen(Qt::NoPen);
        cp.setBrush(QColor(255, 255, 255, 22));
        cp.drawRoundedRect(body.adjusted(q/2, q/2, -q/2, -q/2), cr/2, cr/2);
        QFont lf("sans-serif");
        lf.setPixelSize(q*5/4);
        lf.setBold(true);
        cp.setFont(lf);
        cp.setPen(QColor(255, 255, 255, 235));
        cp.drawText(body, Qt::AlignCenter, appIconLetter(name));

        cp.end();
        s_iconCache.insert(key, cached);
    }

    p.drawPixmap(r, *cached);
}

/* ═══════════════════════════════════════════════════════════════════
   drawGlassButton — macOS Tahoe-style glass pill buttons
   ═══════════════════════════════════════════════════════════════════ */

QRect glassButtonRect(const QRect &r) {
    return r;
}

void drawGlassButton(QPainter &p, const QRect &r, const QString &label,
                     ButtonState state, bool primary, int fontSize) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    int cr = qMin(r.height() / 2, 12);
    QRect br = r;

    /* ── Colors by state ── */
    QColor bodyTop, bodyBot, border, textCol;
    if (state == BtnDisabled) {
        bodyTop = QColor(0x3A, 0x3A, 0x3C, 140);
        bodyBot = QColor(0x2C, 0x2C, 0x2E, 130);
        border = QColor(0x48, 0x48, 0x4A, 60);
        textCol = QColor(0x8E, 0x8E, 0x93, 140);
    } else if (primary) {
        if (state == BtnPressed) {
            bodyTop = QColor(0xCC, 0x40, 0x20, 220);
            bodyBot = QColor(0xAA, 0x35, 0x18, 210);
            border = QColor(0xFF, 0x5A, 0x36, 160);
        } else if (state == BtnHover) {
            bodyTop = QColor(0xFF, 0x7A, 0x52, 230);
            bodyBot = QColor(0xFF, 0x5A, 0x36, 220);
            border = QColor(0xFF, 0x9F, 0x6A, 140);
        } else {
            bodyTop = QColor(0xFF, 0x5A, 0x36, 200);
            bodyBot = QColor(0xFF, 0x45, 0x30, 190);
            border = QColor(0xFF, 0x7A, 0x52, 100);
        }
        textCol = QColor(255, 255, 255, 245);
    } else {
        /* Glass (secondary) button — semi-transparent */
        if (state == BtnPressed) {
            bodyTop = QColor(0x28, 0x28, 0x2C, 130);
            bodyBot = QColor(0x1E, 0x1E, 0x22, 120);
            border = QColor(0x63, 0x63, 0x66, 100);
        } else if (state == BtnHover) {
            bodyTop = QColor(0x3A, 0x3A, 0x3E, 140);
            bodyBot = QColor(0x2C, 0x2C, 0x30, 130);
            border = QColor(0x63, 0x63, 0x66, 90);
        } else {
            bodyTop = QColor(0x30, 0x30, 0x34, 120);
            bodyBot = QColor(0x24, 0x24, 0x28, 110);
            border = QColor(0x63, 0x63, 0x66, 70);
        }
        textCol = QColor(0xF5, 0xF5, 0xF7, 230);
    }

    /* ── Drop shadow ── */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 20));
    p.drawRoundedRect(br.adjusted(0, 1, 0, 1), cr, cr);

    /* ── Body gradient ── */
    QLinearGradient bg(br.topLeft(), br.bottomLeft());
    bg.setColorAt(0.0, bodyTop);
    bg.setColorAt(1.0, bodyBot);
    p.setBrush(bg);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(br, cr, cr);

    /* ── Top specular highlight ── */
    if (state != BtnDisabled) {
        QRect hl(br.x()+4, br.y()+1, br.width()-8, br.height()/3);
        QLinearGradient hg(hl.topLeft(), hl.bottomLeft());
        hg.setColorAt(0.0, QColor(255, 255, 255, primary ? 35 : 20));
        hg.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(hg);
        p.drawRoundedRect(hl, cr-2, cr-2);
    }

    /* ── Pressed inner glow ── */
    if (state == BtnPressed) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 30));
        p.drawRoundedRect(br.adjusted(1,1,-1,-1), cr, cr);
    }

    /* ── Label ── */
    QFont f = p.font();
    f.setPointSize(fontSize);
    f.setBold(primary);
    p.setFont(f);
    p.setPen(textCol);
    p.drawText(br, Qt::AlignCenter, label);

    p.restore();
}

void drawGlassPanel(QPainter &p, const QRect &r, int radius, int alpha) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    /* Shadow — 2-layer */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0, alpha/4));
    p.drawRoundedRect(r.adjusted(-3,-5,3,5), radius+2, radius+2);
    p.setBrush(QColor(0,0,0, alpha/3));
    p.drawRoundedRect(r.adjusted(-1,-2,1,2), radius, radius);

    /* Glass body — gradient with wallpaper-show-through alpha */
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0.0, QColor(0x2C,0x2C,0x30, alpha));
    bg.setColorAt(0.3, QColor(0x26,0x26,0x2A, alpha - 10));
    bg.setColorAt(0.7, QColor(0x20,0x20,0x24, alpha - 20));
    bg.setColorAt(1.0, QColor(0x2C,0x2C,0x30, alpha));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255,255,255,20),1));
    p.drawRoundedRect(r, radius, radius);

    /* Top inner highlight */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,22));
    p.drawRect(r.x()+radius, r.y()+1, r.width()-radius*2, 1);

    /* Bottom inner shadow */
    p.setBrush(QColor(0,0,0,15));
    p.drawRect(r.x()+radius, r.bottom()-1, r.width()-radius*2, 1);

    p.restore();
}
