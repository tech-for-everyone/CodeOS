#include "codeos_terminal.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QTimer>
#include <QTextCursor>
#include <QTextFormat>
#include <QDebug>
#include <QRegularExpression>

CodeOSTerminal::CodeOSTerminal(QWidget *parent)
    : QWidget(parent), m_process(nullptr), m_cursorTimer(new QTimer(this)),
      m_cursorVisible(true), m_rows(24), m_cols(80), m_fontSize(11),
      m_scrollPosition(0), m_mouseTracking(false), m_selecting(false),
      m_cursorShape(Block) {
    setupUI();
    setupProcess();
}

CodeOSTerminal::~CodeOSTerminal() {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->terminate();
        m_process->waitForFinished(1000);
    }
    delete m_process;
}

void CodeOSTerminal::setupUI() {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_display = new QPlainTextEdit(this);
    m_display->setReadOnly(true);
    m_display->setUndoRedoEnabled(false);
    m_display->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_display->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_display->setStyleSheet(
        "QPlainTextEdit {"
        "   background-color: #1e1e2e;"
        "   color: #cdd6f4;"
        "   border: none;"
        "   font-family: 'Liberation Mono', 'Monospace';"
        "   font-size: 11pt;"
        "   selection-background-color: #45475a;"
        "}"
        "QScrollBar:vertical {"
        "   background: #1e1e2e;"
        "   width: 10px;"
        "   border: none;"
        "}"
        "QScrollBar::handle:vertical {"
        "   background: #45475a;"
        "   min-height: 30px;"
        "   border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "   background: #585b70;"
        "}"
    );

    QFont font("Liberation Mono", m_fontSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    m_display->setFont(font);

    m_display->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_display, &QPlainTextEdit::customContextMenuRequested,
            this, &CodeOSTerminal::handleContextMenu);

    connect(m_display->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &CodeOSTerminal::onScrollBarChanged);

    m_layout->addWidget(m_display);

    m_cursorTimer->setInterval(500);
    connect(m_cursorTimer, &QTimer::timeout, this, &CodeOSTerminal::updateCursorBlink);
    m_cursorTimer->start();

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void CodeOSTerminal::setupProcess() {
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CodeOSTerminal::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &CodeOSTerminal::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CodeOSTerminal::onProcessFinished);
}

void CodeOSTerminal::startShell(const QString &shell) {
    QString shellPath = shell.isEmpty() ? "/bin/shell" : shell;
    QStringList args;

    m_process->start(shellPath, args);
    if (!m_process->waitForStarted(2000)) {
        appendOutput(QByteArray("Failed to start shell: ") + m_process->errorString().toUtf8() + "\n");
    }
}

void CodeOSTerminal::sendText(const QString &text) {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->write(text.toUtf8());
    }
}

void CodeOSTerminal::setFontSize(int pt) {
    m_fontSize = pt;
    QFont font("Liberation Mono", m_fontSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    m_display->setFont(font);
}

void CodeOSTerminal::onReadyReadStdout() {
    QByteArray data = m_process->readAllStandardOutput();
    appendOutput(data);
}

void CodeOSTerminal::onReadyReadStderr() {
    QByteArray data = m_process->readAllStandardError();
    appendOutput(data);
}

void CodeOSTerminal::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    appendOutput(QByteArray("\n[Process exited with code ") + QByteArray::number(exitCode) + "]\n");
    emit processExited(exitCode);
}

void CodeOSTerminal::onScrollBarChanged(int value) {
    m_scrollPosition = value;
}

void CodeOSTerminal::onCopy() {
    m_display->copy();
}

void CodeOSTerminal::onPaste() {
    QString text = QApplication::clipboard()->text();
    if (!text.isEmpty()) {
        sendText(text);
    }
}

void CodeOSTerminal::onSelectAll() {
    m_display->selectAll();
}

void CodeOSTerminal::onClear() {
    m_display->clear();
}

void CodeOSTerminal::onFontSizeChanged(bool) {
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;
    int size = action->data().toInt();
    setFontSize(size);
}

void CodeOSTerminal::onBell() {
    QApplication::beep();
}

void CodeOSTerminal::updateCursorBlink() {
    m_cursorVisible = !m_cursorVisible;
    QTextCursor cursor = m_display->textCursor();
    QTextCharFormat format;
    if (m_cursorVisible) {
        format.setBackground(Qt::white);
        format.setForeground(Qt::black);
    } else {
        format.setBackground(cursor.charFormat().background());
        format.setForeground(cursor.charFormat().foreground());
    }
    cursor.mergeCharFormat(format);
    m_display->setTextCursor(cursor);
}

void CodeOSTerminal::appendOutput(const QByteArray &data) {
    QString text = QString::fromUtf8(data);
    processEscapeSequences(text);

    QScrollBar *vbar = m_display->verticalScrollBar();
    bool atBottom = (vbar->value() == vbar->maximum());

    m_display->moveCursor(QTextCursor::End);
    m_display->insertPlainText(text);

    if (atBottom) {
        vbar->setValue(vbar->maximum());
    }
}

void CodeOSTerminal::processEscapeSequences(QString &text) {
    QString cleaned;
    cleaned.reserve(text.size());
    const int n = text.size();
    int i = 0;
    while (i < n) {
        QChar c = text.at(i);
        if (c == QChar(0x07)) {
            onBell();
            ++i;
            continue;
        }
        if (c == QChar(0x1b) && i + 1 < n) {
            QChar next = text.at(i + 1);
            if (next == QChar('[')) {
                int j = i + 2;
                while (j < n && (text.at(j).isDigit() || text.at(j) == QChar(';'))) ++j;
                if (j < n) {
                    QString seq = text.mid(i, j - i + 1);
                    if (seq.endsWith(QChar('m')) && seq.startsWith(QStringLiteral("\x1b[48;2;")) ||
                        seq.endsWith(QChar('m')) && seq.startsWith(QStringLiteral("\x1b[38;2;"))) {
                        if (j + 1 < n && text.at(j + 1) == QChar(';')) {
                            int k = j + 2;
                            while (k < n && text.at(k).isDigit()) ++k;
                            int m = k;
                            while (m < n && (text.at(m).isDigit() || text.at(m) == QChar(';'))) ++m;
                            if (m < n && text.at(m) == QChar('m')) {
                                i = m + 1;
                                continue;
                            }
                        }
                    }
                    i = j + 1;
                    continue;
                }
            } else if (next == QChar(']')) {
                int j = i + 2;
                while (j < n && text.at(j) != QChar(0x07)) ++j;
                if (j < n) {
                    QString seq = text.mid(i, j - i + 1);
                    if (seq.startsWith(QStringLiteral("\x1b]0;"))) {
                        QString title = seq.mid(4, seq.length() - 5);
                        if (!title.isEmpty()) emit titleChanged(title);
                    }
                    i = j + 1;
                    continue;
                }
            }
        }
        cleaned.append(c);
        ++i;
    }
    text = cleaned;
}

void CodeOSTerminal::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_C:
            if (m_display->textCursor().hasSelection()) {
                onCopy();
                return;
            }
            sendText("\x03"); // Ctrl+C
            return;
        case Qt::Key_V:
            onPaste();
            return;
        case Qt::Key_A:
            onSelectAll();
            return;
        case Qt::Key_L:
            sendText("\x0c"); // Ctrl+L clear
            return;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            setFontSize(m_fontSize + 1);
            return;
        case Qt::Key_Minus:
            setFontSize(qMax(6, m_fontSize - 1));
            return;
        case Qt::Key_0:
            setFontSize(11);
            return;
        default:
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        sendText("\r");
        break;
    case Qt::Key_Backspace:
        sendText("\x7f");
        break;
    case Qt::Key_Tab:
        sendText("\t");
        break;
    case Qt::Key_Escape:
        sendText("\x1b");
        break;
    case Qt::Key_Up:
        sendText("\x1b[A");
        break;
    case Qt::Key_Down:
        sendText("\x1b[B");
        break;
    case Qt::Key_Right:
        sendText("\x1b[C");
        break;
    case Qt::Key_Left:
        sendText("\x1b[D");
        break;
    case Qt::Key_Home:
        sendText("\x1b[H");
        break;
    case Qt::Key_End:
        sendText("\x1b[F");
        break;
    case Qt::Key_PageUp:
        sendText("\x1b[5~");
        break;
    case Qt::Key_PageDown:
        sendText("\x1b[6~");
        break;
    case Qt::Key_F1:
        sendText("\x1b[OP");
        break;
    case Qt::Key_F2:
        sendText("\x1b[OQ");
        break;
    case Qt::Key_F3:
        sendText("\x1b[OR");
        break;
    case Qt::Key_F4:
        sendText("\x1b[OS");
        break;
    case Qt::Key_F5:
        sendText("\x1b[15~");
        break;
    case Qt::Key_F6:
        sendText("\x1b[17~");
        break;
    case Qt::Key_F7:
        sendText("\x1b[18~");
        break;
    case Qt::Key_F8:
        sendText("\x1b[19~");
        break;
    case Qt::Key_F9:
        sendText("\x1b[20~");
        break;
    case Qt::Key_F10:
        sendText("\x1b[21~");
        break;
    case Qt::Key_F11:
        sendText("\x1b[23~");
        break;
    case Qt::Key_F12:
        sendText("\x1b[24~");
        break;
    default:
        if (!event->text().isEmpty()) {
            sendText(event->text());
        }
        break;
    }
}

void CodeOSTerminal::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        QTextCursor cursor = m_display->cursorForPosition(event->pos());
        m_selectionStartPos = cursor.position();
        m_display->setTextCursor(cursor);
    } else if (event->button() == Qt::RightButton) {
        sendText("\x1b[M"); // Mouse click encoding for xterm
    }
}

void CodeOSTerminal::mouseMoveEvent(QMouseEvent *event) {
    if (m_selecting) {
        QTextCursor cursor = m_display->cursorForPosition(event->pos());
        QTextCursor sel = m_display->textCursor();
        sel.setPosition(m_selectionStartPos, QTextCursor::KeepAnchor);
        sel.setPosition(cursor.position(), QTextCursor::KeepAnchor);
        m_display->setTextCursor(sel);
    }
}

void CodeOSTerminal::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_selecting = false;
    }
}

void CodeOSTerminal::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta > 0) {
            setFontSize(m_fontSize + 1);
        } else {
            setFontSize(qMax(6, m_fontSize - 1));
        }
    } else {
        QScrollBar *vbar = m_display->verticalScrollBar();
        vbar->setValue(vbar->value() - event->angleDelta().y() / 8);
    }
}

void CodeOSTerminal::handleContextMenu(const QPoint &pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d3d; color: #ffffff; border: 1px solid #3d3d4d; }"
        "QMenu::item { padding: 8px 24px; }"
        "QMenu::item:selected { background: #3b82f6; }"
    );

    QAction *copyAction = menu.addAction("Copy", this, &CodeOSTerminal::onCopy);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(m_display->textCursor().hasSelection());

    QAction *pasteAction = menu.addAction("Paste", this, &CodeOSTerminal::onPaste);
    pasteAction->setShortcut(QKeySequence::Paste);

    QAction *selectAllAction = menu.addAction("Select All", this, &CodeOSTerminal::onSelectAll);
    selectAllAction->setShortcut(QKeySequence::SelectAll);

    menu.addSeparator();

    QAction *clearAction = menu.addAction("Clear", this, &CodeOSTerminal::onClear);

    menu.addSeparator();

    QMenu *fontMenu = menu.addMenu("Font Size");
    for (int size : {8, 9, 10, 11, 12, 13, 14, 16, 18, 20}) {
        QAction *act = fontMenu->addAction(QString::number(size) + "pt");
        act->setData(size);
        act->setCheckable(true);
        act->setChecked(size == m_fontSize);
        connect(act, &QAction::triggered, this, &CodeOSTerminal::onFontSizeChanged);
    }

    menu.exec(m_display->mapToGlobal(pos));
}

QPoint CodeOSTerminal::cursorPositionFromPoint(const QPoint &pos) const {
    QTextCursor cursor = m_display->cursorForPosition(pos);
    return QPoint(cursor.columnNumber(), cursor.blockNumber());
}