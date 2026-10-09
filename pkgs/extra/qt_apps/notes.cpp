#include "qt_panels.h"
#include "codeos_platform.h"

#include <QPlainTextEdit>
#include <QFrame>
#include <QFile>
#include <QDir>

/* ═══════════════════════════════════════════════════════════════════
   QtNotesWidget — a lightweight persistent textpad that saves to /apps
   ═══════════════════════════════════════════════════════════════════ */

QtNotesWidget::QtNotesWidget(QWidget *parent) : QtAppWindow("Notes", parent) {
    resize(560, 460);
    m_path = "/apps/notes.txt";

    m_editor = new QPlainTextEdit(this);
    m_editor->setFrameShape(QFrame::NoFrame);
    QFont ef = font(); ef.setPointSize(11);
    m_editor->setFont(ef);
    QPalette pal = m_editor->palette();
    pal.setColor(QPalette::Base, QColor(0x18, 0x18, 0x1C));
    pal.setColor(QPalette::Text, QColor(0xE8, 0xE8, 0xEA));
    m_editor->setPalette(pal);
    m_editor->setPlaceholderText("Write anything — saved automatically when the window closes (Ctrl+S to save now)");
    loadNotes();
}

void QtNotesWidget::loadNotes() {
    if (!m_editor || m_path.isEmpty()) return;
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_editor->setPlainText(QString::fromUtf8(f.readAll()));
        f.close();
    }
}

void QtNotesWidget::saveNotes() {
    if (!m_editor || m_path.isEmpty()) return;
    QDir().mkpath("/apps");
    QFile f(m_path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(m_editor->toPlainText().toUtf8());
        f.close();
    }
}

void QtNotesWidget::paintContent(QPainter &p, const QRect &r) {
    /* Footer hint strip below the editor. */
    QFont f = font(); f.setPointSize(9); p.setFont(f);
    p.setPen(QColor(0x99, 0x99, 0x9E, 200));
    p.drawText(QRect(r.x() + 10, r.bottom() - 22, r.width() - 20, 18),
               Qt::AlignRight, "saved to " + m_path);
}

void QtNotesWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!m_editor) return;
    /* Content rect mirrors QtAppWindow::paintEvent: tb=30 cr=12 sh=6 */
    int cr = 12, sh = 6, tb = 30;
    m_editor->setGeometry(cr + sh, tb + sh + 2, width() - cr * 2 - sh * 2,
                          height() - tb - cr - sh * 2 - 20);
}

void QtNotesWidget::keyPressEvent(QKeyEvent *event) {
    if ((event->modifiers() & Qt::ControlModifier) &&
        (event->key() == Qt::Key_S || event->key() == Qt::Key_Return)) {
        saveNotes();
        if (m_editor) m_editor->setPlaceholderText("Saved.");
        return;
    }
    QtAppWindow::keyPressEvent(event);
}

void QtNotesWidget::closeEvent(QCloseEvent *event) {
    saveNotes();
    QtAppWindow::closeEvent(event);
}