#ifndef CODEOS_TERMINAL_H
#define CODEOS_TERMINAL_H

#include <QWidget>
#include <QProcess>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QTimer>
#include <QRegularExpression>
#include <QVBoxLayout>

class CodeOSTerminal : public QWidget {
    Q_OBJECT
public:
    explicit CodeOSTerminal(QWidget *parent = nullptr);
    ~CodeOSTerminal();

    void startShell(const QString &shell = QString());
    void sendText(const QString &text);
    void setFontSize(int pt);

signals:
    void processExited(int exitCode);
    void titleChanged(const QString &title);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onScrollBarChanged(int value);
    void onCopy();
    void onPaste();
    void onSelectAll();
    void onClear();
    void onFontSizeChanged(bool checked = false);
    void onBell();
    void updateCursorBlink();

protected:
    void keyPressEvent(QKeyEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);

private:
    void setupUI();
    void setupProcess();
    void writeToProcess(const QByteArray &data);
    void handleKeyPress(QKeyEvent *event);
    void handleKeyRelease(QKeyEvent *event);
    void handleMousePress(QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);
    void handleMouseRelease(QMouseEvent *event);
    void handleWheel(QWheelEvent *event);
    void handleContextMenu(const QPoint &pos);

    void appendOutput(const QByteArray &data);
    void processEscapeSequences(QString &text);
    QPoint cursorPositionFromPoint(const QPoint &pos) const;

    QVBoxLayout *m_layout;
    QPlainTextEdit *m_display;
    QProcess *m_process;
    QTimer *m_cursorTimer;
    bool m_cursorVisible;
    int m_rows;
    int m_cols;
    int m_fontSize;
    QString m_currentLine;
    int m_scrollPosition;
    bool m_mouseTracking;
    int m_selectionStartPos;
    bool m_selecting;

    enum CursorShape { Block, Underline, Bar };
    CursorShape m_cursorShape;
};

#endif // CODEOS_TERMINAL_H