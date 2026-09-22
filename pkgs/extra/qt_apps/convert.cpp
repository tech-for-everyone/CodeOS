#include "qt_panels.h"
#include "codeos_platform.h"

#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QWidget>

/* ═══════════════════════════════════════════════════════════════════
   QtConvertWidget — unit converter (length / weight / temperature)
   ═══════════════════════════════════════════════════════════════════ */

struct UnitDef { const char *name; double factor; int tempCode; };

static const UnitDef kLength[] = {
    {"mm", 0.001, 0}, {"cm", 0.01, 0}, {"m", 1.0, 0}, {"km", 1000.0, 0},
    {"in", 0.0254, 0}, {"ft", 0.3048, 0}, {"yd", 0.9144, 0}, {"mi", 1609.344, 0},
};
static const UnitDef kWeight[] = {
    {"mg", 0.001, 0}, {"g", 1.0, 0}, {"kg", 1000.0, 0},
    {"oz", 28.349523125, 0}, {"lb", 453.59237, 0}, {"t", 1.0e6, 0},
};
static const UnitDef kTemp[] = {
    {"Celsius", 0, 1}, {"Fahrenheit", 0, 2}, {"Kelvin", 0, 3},
};

QtConvertWidget::QtConvertWidget(QWidget *parent) : QtAppWindow("Convert", parent) {
    resize(440, 260);

    m_body = new QWidget(this);
    m_cat = new QComboBox(m_body);
    m_cat->addItems({"Length", "Weight", "Temperature"});
    m_from = new QComboBox(m_body);
    m_to = new QComboBox(m_body);
    m_in = new QLineEdit(m_body);
    m_in->setPlaceholderText("0");
    m_out = new QLabel("—", m_body);
    m_out->setStyleSheet("color: #7EE787; font-weight: bold; font-size: 22px;");

    QGridLayout *lay = new QGridLayout(m_body);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setVerticalSpacing(8);
    lay->setHorizontalSpacing(10);
    lay->addWidget(new QLabel("Category", m_body), 0, 0);
    lay->addWidget(m_cat, 0, 1, 1, 3);
    lay->addWidget(new QLabel("From", m_body), 1, 0);
    lay->addWidget(m_from, 1, 1);
    lay->addWidget(new QLabel("To", m_body), 1, 2);
    lay->addWidget(m_to, 1, 3);
    lay->addWidget(new QLabel("Value", m_body), 2, 0);
    lay->addWidget(m_in, 2, 1, 1, 3);
    lay->addWidget(m_out, 3, 1, 1, 3, Qt::AlignLeft);
    lay->setRowStretch(4, 1);

    connect(m_cat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { fillUnits(); convert(); });
    connect(m_from, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { convert(); });
    connect(m_to, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { convert(); });
    connect(m_in, &QLineEdit::textChanged, this, [this](const QString &) { convert(); });

    fillUnits();
    m_in->setText("1");
}

void QtConvertWidget::fillUnits() {
    int cat = m_cat->currentIndex();
    m_from->clear(); m_to->clear();
    const UnitDef *units;
    int n;
    if (cat == 0)      { units = kLength; n = int(sizeof(kLength)/sizeof(kLength[0])); }
    else if (cat == 1) { units = kWeight; n = int(sizeof(kWeight)/sizeof(kWeight[0])); }
    else               { units = kTemp;   n = int(sizeof(kTemp)/sizeof(kTemp[0])); }
    for (int i = 0; i < n; ++i) { m_from->addItem(units[i].name); m_to->addItem(units[i].name); }
    if (n > 1) m_to->setCurrentIndex(1);
}

void QtConvertWidget::convert() {
    bool ok = false;
    double v = m_in->text().trimmed().toDouble(&ok);
    if (!ok) { m_out->setText("—"); return; }
    int cat = m_cat->currentIndex();
    int fi = m_from->currentIndex(), ti = m_to->currentIndex();
    if (fi < 0 || ti < 0) { m_out->setText("—"); return; }
    const UnitDef *units;
    if (cat == 0)      units = kLength;
    else if (cat == 1) units = kWeight;
    else               units = kTemp;
    double out;
    if (cat == 2) {
        double c;
        switch (units[fi].tempCode) {
            case 2: c = (v - 32.0) * 5.0 / 9.0; break;
            case 3: c = v - 273.15; break;
            default: c = v;
        }
        switch (units[ti].tempCode) {
            case 2: out = c * 9.0 / 5.0 + 32.0; break;
            case 3: out = c + 273.15; break;
            default: out = c;
        }
    } else {
        out = v * units[fi].factor / units[ti].factor;
    }
    m_out->setText(QString("%1 %2").arg(QString::number(out, 'g', 9), QString::fromUtf8(units[ti].name)));
}

void QtConvertWidget::paintContent(QPainter &p, const QRect &r) {
    Q_UNUSED(p); Q_UNUSED(r);
}

void QtConvertWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!m_body) return;
    int cr = 12, sh = 6, tb = 30;
    m_body->setGeometry(cr + sh, tb + sh + 2, width() - cr * 2 - sh * 2,
                        height() - tb - cr - sh * 2);
}