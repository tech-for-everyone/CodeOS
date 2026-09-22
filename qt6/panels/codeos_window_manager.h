#ifndef CODEOS_WINDOW_MANAGER_H
#define CODEOS_WINDOW_MANAGER_H

#include <QObject>
#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QLabel>

class QWindow;

class CodeOSWindowManager : public QObject {
    Q_OBJECT
public:
    explicit CodeOSWindowManager(QApplication *app, QObject *parent = nullptr);
    ~CodeOSWindowManager();

    QWidget *createWindow(const QString &title, const QRect &geometry = QRect(),
                          Qt::WindowFlags flags = Qt::Window);
    void closeWindow(QWidget *window);
    void minimizeWindow(QWidget *window);
    void maximizeWindow(QWidget *window);
    void restoreWindow(QWidget *window);

    QList<QWidget*> windows() const;
    QWidget *activeWindow() const;

    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const;

private:
    struct WindowData {
        QWidget *widget = nullptr;
        QWidget *titleBar = nullptr;
        QWidget *content = nullptr;
        QPushButton *closeBtn = nullptr;
        QPushButton *minimizeBtn = nullptr;
        QPushButton *maximizeBtn = nullptr;
        QLabel *titleLabel = nullptr;
        QRect savedGeometry;
        bool maximized = false;
        bool dragging = false;
        QPoint dragStartPos;
        int dragEdge = 0; // 1=left, 2=right, 4=top, 8=bottom
    };

    QApplication *m_app;
    QWidget *m_desktop = nullptr;
    QMap<QWidget*, WindowData> m_windows;
    QWidget *m_activeWindow = nullptr;
    bool m_snapEnabled = true;

    WindowData createWindowDecorations(QWidget *window, const QString &title);
    void updateWindowFrame(QWidget *window);
    void handleMousePress(QWidget *window, QMouseEvent *event);
    void handleMouseMove(QWidget *window, QMouseEvent *event);
    void handleMouseRelease(QWidget *window, QMouseEvent *event);
    void raiseWindow(QWidget *window);
    void setActiveWindow(QWidget *window);
    int hitTestResizeEdge(const QRect &frame, const QPoint &pos);
    void applySnap(QWidget *window, const QPoint &pos);

    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // CODEOS_WINDOW_MANAGER_H