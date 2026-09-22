/* CodeOS Bitmap Font Engine for Qt6
 * Uses CodeOS's built-in 8x16 bitmap font for glyph rendering */

#include <QtGui/qfontdatabase.h>
#include <QtGui/qfont.h>
#include <QtGui/qpa/qplatformfontdatabase.h>
#include <QtGui/private/qfontengine_p.h>
#include <QtGui/private/qfontdatabase_p.h>
#include <QtCore/qstring.h>
#include <QtCore/qbytearray.h>
#include <QImage>
#include <QPainterPath>
#include <QtGui/qguiapplication.h>

#include "codeos_font.h"

#include <string.h>

extern "C" void kprintf(const char *fmt, ...);

class CodeOSFontEngine : public QFontEngine {
public:
    explicit CodeOSFontEngine(int pixelSize);
    ~CodeOSFontEngine() override;

    int stringToCMap(const QChar *str, int len, QGlyphLayout * glyphs,
                     int *numGlyphs, ShaperFlags flags) const override;
    void recalcAdvances(QGlyphLayout *, ShaperFlags) const override {}
    void addGlyphsToPath(glyph_t *glyphs, QFixedPoint *positions, int nglyphs,
                         QPainterPath *path, QTextItem::RenderFlags flags) override;

    glyph_t glyphIndex(uint ucs4) const override;
    glyph_metrics_t boundingBox(glyph_t glyph) override;
    void getUnscaledGlyph(glyph_t glyph, QPainterPath *path, glyph_metrics_t *metrics) override;

    QImage alphaMapForGlyph(glyph_t glyph, const QFixedPoint &subPixelPosition, const QTransform &t) override;
    QImage alphaMapForGlyph(glyph_t glyph) override;

    QFontEngine *cloneWithSize(qreal pixelSize) const override;

    /* Font metrics -- the base-class implementations read lazily-filled
       members that stay 0 unless a real font backend fills them, which
       collapses QTextLayout line geometry and makes rect-based drawText()
       push glyphs outside the clip rect at larger pixel sizes. */
    QFixed ascent() const override { return QFixed(m_ascent); }
    QFixed descent() const override { return QFixed(m_descent); }
    QFixed leading() const override { return QFixed(m_pixelSize / 8); }
    QFixed xHeight() const override { return QFixed(m_pixelSize / 2); }
    QFixed averageCharWidth() const override { return QFixed(8 * m_pixelSize / 16); }

    QFixed capHeight() const override;
    qreal maxCharWidth() const override;

    Properties properties() const override;

private:
    int m_pixelSize;
    int m_ascent;
    int m_descent;
    int m_height;
    codeos_font_face_t *m_face;
};

CodeOSFontEngine::CodeOSFontEngine(int pixelSize)
    : QFontEngine(Box),
      m_pixelSize(pixelSize),
      m_ascent(pixelSize * 3 / 4),
      m_descent(pixelSize / 4),
      m_height(pixelSize),
      m_face(nullptr)
{
    m_face = codeos_font_default();
    /* Advertise A8 glyphs so the raster engine's cached-glyph path
       (used whenever antialiasing is on) actually renders. */
    glyphFormat = QFontEngine::Format_A8;
}

CodeOSFontEngine::~CodeOSFontEngine() = default;

int CodeOSFontEngine::stringToCMap(const QChar *str, int len, QGlyphLayout *glyphs,
                                   int *numGlyphs, ShaperFlags flags) const
{
    Q_UNUSED(flags);
    if (*numGlyphs < len) {
        *numGlyphs = len;
        return false;
    }
    *numGlyphs = 0;
    QFixed advance = QFixed(8 * m_pixelSize) / 16;
    for (int i = 0; i < len; ++i) {
        uint ucs4 = str[i].unicode();
        glyph_t g = (ucs4 < 256) ? (glyph_t)ucs4 : 0;
        glyphs->glyphs[*numGlyphs] = g;
        glyphs->advances[*numGlyphs] = advance;
        glyphs->offsets[*numGlyphs] = QFixedPoint();
        (*numGlyphs)++;
    }
    return true;
}

void CodeOSFontEngine::addGlyphsToPath(glyph_t *glyphs, QFixedPoint *positions, int nglyphs,
                                       QPainterPath *path, QTextItem::RenderFlags flags)
{
    /* Convert each glyph's 8x16 bitmap (scaled to m_pixelSize) into run-length
     * rectangles so the fill-based text path (e.g. antialiased drawing) has
     * real geometry to paint. */
    for (int i = 0; i < nglyphs; ++i) {
        const uint8_t *data = codeos_font_get_glyph(m_face, (int)glyphs[i]);
        if (!data) continue;
        const qreal scale = (qreal)m_pixelSize / 16.0;
        const qreal ox = positions[i].x.toReal();
        const qreal oy = positions[i].y.toReal();
        for (int y = 0; y < 16; y++) {
            uint8_t row = data[y];
            int x = 0;
            while (x < 8) {
                if (row & (0x80 >> x)) {
                    int run = 1;
                    while (x + run < 8 && (row & (0x80 >> (x + run)))) run++;
                    path->addRect(QRectF(ox + x * scale, oy + y * scale - m_ascent,
                                         run * scale, scale));
                    x += run;
                } else {
                    x++;
                }
            }
        }
    }
}

glyph_t CodeOSFontEngine::glyphIndex(uint ucs4) const
{
    if (ucs4 < 256)
        return (glyph_t)ucs4;
    return 0;
}

glyph_metrics_t CodeOSFontEngine::boundingBox(glyph_t glyph)
{
    const uint8_t *data = codeos_font_get_glyph(m_face, (int)glyph);
    if (!data) {
        return glyph_metrics_t(QFixed(0), QFixed(-m_ascent), QFixed(0), QFixed(m_height), QFixed(0), QFixed(0));
    }

    /* Keep these EXACTLY in sync with the bitmap returned by
       alphaMapForGlyph(): the glyph cache sizes its entries from these
       metrics, and any mismatch garbles coverage sampling. */
    const int gw = qMax(1, m_pixelSize / 2);   /* 8 cols  * pixelSize/16 */
    const int gh = qMax(1, m_pixelSize);       /* 16 rows * pixelSize/16 */

    return glyph_metrics_t(QFixed(0), QFixed(-m_ascent), QFixed(gw), QFixed(gh),
                           QFixed(gw), QFixed(0));
}

void CodeOSFontEngine::getUnscaledGlyph(glyph_t glyph, QPainterPath *path, glyph_metrics_t *metrics)
{
    Q_UNUSED(glyph);
    Q_UNUSED(path);
    Q_UNUSED(metrics);
}

QImage CodeOSFontEngine::alphaMapForGlyph(glyph_t glyph, const QFixedPoint &subPixelPosition, const QTransform &t)
{
    Q_UNUSED(subPixelPosition);
    Q_UNUSED(t);

    const uint8_t *data = codeos_font_get_glyph(m_face, (int)glyph);
    if (!data) {
        return QImage();
    }

    int width = 8;
    int height = 16;

    /* NOTE: QImage::setPixel silently mis-stores plain ints on indexed
       formats -- setPixel(x,y,255) writes ARGB "blue" which converts to
       gray 39 (!), leaving every glyph at ~15% opacity.  Verified on
       host Qt6.  Write coverage bytes through the raw bits instead. */
    const int gw = qMax(1, m_pixelSize / 2);
    const int gh = qMax(1, m_pixelSize);

    QImage img(width, height, QImage::Format_Grayscale8);
    img.fill(0);

    uchar *bits = img.bits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < height; y++) {
        uint8_t row = data[y];
        if (!row) continue;
        uchar *out = bits + y * bpl;
        for (int x = 0; x < width; x++) {
            if (row & (0x80 >> x)) {
                out[x] = 255;
            }
        }
    }

    if (gw != width || gh != height)
        return img.scaled(gw, gh, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    return img;
}

QImage CodeOSFontEngine::alphaMapForGlyph(glyph_t glyph)
{
    return alphaMapForGlyph(glyph, QFixedPoint(), QTransform());
}

QFontEngine *CodeOSFontEngine::cloneWithSize(qreal pixelSize) const
{
    return new CodeOSFontEngine((int)pixelSize);
}

QFixed CodeOSFontEngine::capHeight() const
{
    return QFixed(m_ascent);
}

qreal CodeOSFontEngine::maxCharWidth() const
{
    return 8 * m_pixelSize / 16;
}

QFontEngine::Properties CodeOSFontEngine::properties() const
{
    Properties p;
    p.postscriptName = "LiberationSans";
    p.copyright = "";
    p.boundingBox = QRectF(0, -m_ascent, 8, 16);
    p.emSquare = 16;
    p.ascent = m_ascent;
    p.descent = m_descent;
    p.leading = 2;
    p.italicAngle = 0;
    p.capHeight = m_ascent;
    p.lineWidth = 1;
    return p;
}

class CodeOSFontDatabase : public QPlatformFontDatabase
{
public:
    void populateFontDatabase() override;
    QFontEngine *fontEngine(const QFontDef &fontDef, void *handle) override;
    QFontEngine *fontEngine(const QByteArray &fontData, double pixelSize, QFont::HintingPreference hintingPreference) override;

private:
    QFontEngine *getCachedEngine(const QFontDef &fontDef);
    QHash<QFontDef, QFontEngine *> m_cache;
};

extern "C" void kprintf(const char *fmt, ...);

void CodeOSFontDatabase::populateFontDatabase()
{
    kprintf("CodeOSFontDatabase: registering Liberation Sans\n");

    registerFont("Liberation Sans", "", "",
                 QFont::Normal,
                 QFont::StyleNormal,
                 QFont::AnyStretch,
                 true,
                 true,
                 16,
                 false,
                 false,
                 QSupportedWritingSystems(),
                 nullptr);

    registerAliasToFontFamily("Liberation Sans", "Sans Serif");
    registerAliasToFontFamily("Sans Serif", "Liberation Sans");
    registerAliasToFontFamily("Helvetica", "Liberation Sans");

    kprintf("CodeOSFontDatabase: done\n");
}

QFontEngine *CodeOSFontDatabase::getCachedEngine(const QFontDef &fontDef)
{
    if (fontDef.pixelSize <= 0) return nullptr;

    QFontDef key;
    key.families = fontDef.families;
    key.pixelSize = fontDef.pixelSize;
    key.pointSize = -1;
    key.styleStrategy = QFont::PreferDefault;
    key.hintingPreference = QFont::PreferNoHinting;
    key.weight = fontDef.weight;
    key.style = fontDef.style;
    key.stretch = fontDef.stretch;

    if (m_cache.contains(key)) {
        return m_cache[key];
    }

    CodeOSFontEngine *engine = new CodeOSFontEngine((int)fontDef.pixelSize);
    m_cache[key] = engine;
    return engine;
}

QFontEngine *CodeOSFontDatabase::fontEngine(const QFontDef &fontDef, void *handle)
{
    Q_UNUSED(handle);
    return getCachedEngine(fontDef);
}

QFontEngine *CodeOSFontDatabase::fontEngine(const QByteArray &fontData, double pixelSize, QFont::HintingPreference hintingPreference)
{
    Q_UNUSED(fontData);
    Q_UNUSED(hintingPreference);
    QFontDef def;
    def.pixelSize = pixelSize > 0 ? (int)pixelSize : 16;
    def.families << "Liberation Sans";
    return getCachedEngine(def);
}

extern "C" QPlatformFontDatabase *codeos_create_font_database()
{
    kprintf("codeos_create_font_database: created CodeOSFontDatabase\n");
    return new CodeOSFontDatabase();
}
