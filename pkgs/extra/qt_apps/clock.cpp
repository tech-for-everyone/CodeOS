#include "qt_panels.h"
#include "codeos_platform.h"

#include <QDateTime>
#include <QTimer>
#include <QtMath>

/* ═══════════════════════════════════════════════════════════════════
   QtClockWidget — live digital clock with date + analog face
   ═══════════════════════════════════════════════════════════════════ */

QtClockWidget::QtClockWidget(QWidget *parent) : QtAppWindow("Clock", parent) {
    resize(380, 300);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() { update(); });
    m_timer->start(500);
}

void QtClockWidget::paintContent(QPainter &p, const QRect &r) {
    QDateTime now = QDateTime::currentDateTime();

    /* Analog face */
    int side = qMin(r.height() - 140, 170);
    QRect face(r.x() + 10, r.y() + 10, side, side);
    double sec = now.time().second() + now.time().msec() / 1000.0;
    double min = now.time().minute() + sec / 60.0;
    double hr  = (now.time().hour() % 12) + min / 60.0;

    QColor dial(0x20, 0x20, 0x26), tick(0x9A, 0x9A, 0xA2), hand(0xE8, 0xE8, 0xEA);
    p.setPen(Qt::NoPen);
    p.setBrush(dial);
    p.drawEllipse(face);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(tick, 2));
    for (int i = 0; i < 12; ++i) {
        double a = (i * 30 - 90) * M_PI / 180.0;
        int x1 = face.center().x() + int(cos(a) * (side / 2 - 6));
        int y1 = face.center().y() + int(sin(a) * (side / 2 - 6));
        int x2 = face.center().x() + int(cos(a) * (side / 2 - 14));
        int y2 = face.center().y() + int(sin(a) * (side / 2 - 14));
        if (i % 3 == 0) { p.setPen(QPen(tick, 3)); p.drawLine(x1, y1, x2, y2); p.setPen(QPen(tick, 2)); }
        else p.drawLine(x1, y1, x2, y2);
    }
    auto drawHand = [&](double ang, int len, int w, const QColor &c) {
        double a = (ang * 6 - 90) * M_PI / 180.0;
        p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(face.center(), QPoint(face.center().x() + int(cos(a) * len),
                                         face.center().y() + int(sin(a) * len)));
    };
    drawHand(hr, side / 3, 6, hand);
    drawHand(min, side / 2 - 14, 4, hand);
    drawHand(sec, side / 2 - 8, 2, QColor(0xFF, 0x6B, 0x6B));
    p.setBrush(hand); p.setPen(Qt::NoPen);
    p.drawEllipse(face.center(), 5, 5);

    /* Digital read-out */
    QFont f = font(); f.setPointSize(34); f.setBold(true);
    p.setFont(f); p.setPen(QColor(0xF2, 0xF2, 0xF4));
    p.drawText(QRect(r.x() + side + 22, r.y() + 14, r.width() - side - 34, 44),
               Qt::AlignVCenter | Qt::AlignLeft,
               now.toString("hh:mm:ss"));

    QFont d = font(); d.setPointSize(14);
    p.setFont(d); p.setPen(QColor(0xC8, 0xC8, 0xD0));
    p.drawText(QRect(r.x() + side + 22, r.y() + 64, r.width() - side - 34, 26),
               Qt::AlignVCenter | Qt::AlignLeft, now.toString("ddd, MMM d yyyy"));
    p.setPen(QColor(0x8A, 0x8A, 0x92));
    p.drawText(QRect(r.x() + side + 22, r.y() + 96, r.width() - side - 34, 22),
               Qt::AlignVCenter | Qt::AlignLeft, "CodeOS time");
}

QtClockWidget::~QtClockWidget() { delete m_timer; }