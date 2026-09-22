#include "qt_panels.h"
#include "codeos_plugin.h"
#include "codeos_platform.h"
#include "codeos_window_manager.h"
#include "codeos_file_manager.h"
#include "codeos_terminal.h"

#include <private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformtheme.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QStyleFactory>
#include <QPalette>
#include <QProxyStyle>
#include <QSplitter>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QPlainTextEdit>

extern "C" {
#include "mouse.h"
#include "keyboard.h"
#include "input.h"
#include "timer.h"
#include "kprintf.h"
#include "fb.h"
#include "version.h"
#include "mm.h"
#include "pmm.h"
#include "process.h"
#include "security.h"
#include "pkg.h"
#include "fs.h"
#include "ai.h"
#include "net.h"
#include "ow_http.h"
#include "ow_html.h"
#include "apphost.h"
#include "block.h"
#include "io.h"
#include "updater.h"
#include "lvgl_launcher.h"
#include "wifi.h"
#include "usb_audio.h"
#include <vector>
}

extern "C" {
int sched_thread_count(void);
void installer_run(void);
void installer_detect_disk(void);
void installer_set_user(const char *full_name, const char *username, const char *password);
void installer_set_wifi(const char *ssid, const char *password);
void installer_set_locale(const char *locale);
}

#include "installer_qt.h"

/* ═══════════════════════════════════════════════════════════════════
   QtZiggyWidget — privacy-first voice assistant shell

   The kernel already exposes USB Audio input. Ziggy owns the microphone
   only while the button is held; speech-to-text can be attached later
   without changing the capture lifecycle or UI contract.
   ═══════════════════════════════════════════════════════════════════ */

QtZiggyWidget::QtZiggyWidget(QWidget *parent) : QtAppWindow("Ziggy", parent) {
    resize(560, 640);
    setMouseTracking(true);
    m_messages.append({true, "Hello! I'm Ziggy. Ask me about CodeOS or say 'help'."});
    connect(&m_micTimer, &QTimer::timeout, this, [this]() { pollMicrophone(); });
}

QtZiggyWidget::~QtZiggyWidget() {
    setListening(false);
}

void QtZiggyWidget::setListening(bool enabled) {
    if (enabled == m_listening) return;
    if (enabled) {
        m_micStream = -1;
        for (int i = 0; i < usb_audio_get_stream_count(); i++) {
            if (usb_audio_open_input(i, 16000, 1, 16) == 0) {
                m_micStream = i;
                break;
            }
        }
        if (m_micStream < 0) {
            m_status = "No compatible microphone found";
            m_transcript = "Ziggy needs a USB microphone with an input stream.";
            update();
            return;
        }
        m_listening = true;
        m_status = "Listening...";
        m_transcript = "Audio captured locally. Speech recognition is not configured yet.";
        m_micTimer.start(30);
    } else {
        m_micTimer.stop();
        if (m_micStream >= 0) usb_audio_close(m_micStream);
        m_micStream = -1;
        m_listening = false;
        m_level = 0;
        m_status = "Ready";
    }
    update();
}

bool QtZiggyWidget::handleVoiceCommand(const QString &text) {
    const QString command = text.trimmed().toLower();
    if (command.isEmpty()) return false;

    struct VoiceAction { const char *name; int app; };
    static const VoiceAction actions[] = {
        {"terminal", 0}, {"about", 1}, {"calculator", 2}, {"calc", 2},
        {"settings", 4}, {"explorer", 6}, {"files", 6},
        {"system monitor", 9}, {"sysmon", 9}, {"firewall", 10},
        {"installer", 11}, {"install codeos", 11}, {"steam", 12},
        {"dos mode", 13}, {"lt", 14}, {"netbeam", 15},
        {"file sharing", 15}, {"wine", 12}, {"ziggy", 16}
    };

    for (const VoiceAction &action : actions) {
        if (command.contains(QString::fromLatin1(action.name))) {
            QtDesktopManager *desktop = QtDesktopManager::instance();
            if (desktop) {
                desktop->launchApp(action.app);
                desktop->showToast(QString("Opening %1...").arg(
                    desktop->appNames().value(action.app)), QColor(C_BLUE));
            }
            m_status = "Command complete";
            m_transcript = QString("You said: %1").arg(text.trimmed());
            return true;
        }
    }

    if (command.contains("hello") || command.contains("hi ziggy")) {
        m_status = "Ready";
        m_transcript = "Hello! Try saying: open terminal, open settings, or show files.";
        return true;
    }
    if (command.contains("what can you do") || command.contains("help")) {
        m_status = "Ready";
        m_transcript = "I can open Terminal, Settings, Files, Calculator, SysMon, and more.";
        return true;
    }

    m_status = "Command not recognized";
    m_transcript = QString("I heard: %1").arg(text.trimmed());
    return false;
}

void QtZiggyWidget::submitText() {
    const QString prompt = m_input.trimmed();
    if (prompt.isEmpty()) return;
    m_messages.append({false, prompt});
    m_input.clear();

    char response[512];
    response[0] = 0;
    int length = ai_query(prompt.toUtf8().constData(), response, sizeof(response));
    if (length > 0) {
        response[sizeof(response) - 1] = 0;
        const QString answer = QString::fromUtf8(response);
        if (answer == "__CLEAR__") {
            m_messages.clear();
            m_messages.append({true, "Conversation cleared. What would you like to know?"});
        } else {
            m_messages.append({true, answer});
        }
    } else {
        m_messages.append({true, "I couldn't answer that yet. Try 'help' or ask about CodeOS."});
    }
    m_transcript = QString("You said: %1").arg(prompt);
    update();
}

void QtZiggyWidget::pollMicrophone() {
    if (!m_listening || m_micStream < 0) return;
    int16_t samples[128];
    int bytes = usb_audio_read(m_micStream, samples, sizeof(samples));
    if (bytes <= 0) {
        m_level = 0;
    } else {
        int peak = 0;
        int count = bytes / (int)sizeof(int16_t);
        for (int i = 0; i < count; i++) {
            int value = samples[i] < 0 ? -samples[i] : samples[i];
            if (value > peak) peak = value;
        }
        m_level = qMin(100, (peak * 100) / 32768);
    }
    update();
}

void QtZiggyWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    drawGlassPanel(p, r.adjusted(12, 12, -12, -12), 18, 185);

    QFont title = font(); title.setPointSize(26); title.setBold(true);
    p.setFont(title); p.setPen(c_text);
    p.drawText(QRect(r.left(), r.top() + 34, r.width(), 38), Qt::AlignCenter, "Ziggy");

    QFont sub = font(); sub.setPointSize(11); p.setFont(sub); p.setPen(c_subtext);
    p.drawText(QRect(r.left() + 30, r.top() + 78, r.width() - 60, 28),
               Qt::AlignCenter, "Your private CodeOS assistant");

    QRect statusChip(r.center().x() - 48, r.top() + 108, 96, 24);
    p.setPen(QPen(glass_border(45), 1));
    p.setBrush(m_listening ? QColor(0xBF, 0x5A, 0xF2, 45)
                           : QColor(0x48, 0x48, 0x4A, 90));
    p.drawRoundedRect(statusChip, 12, 12);
    p.setPen(m_listening ? c_mauve : c_subtext);
    QFont chipFont = font(); chipFont.setPointSize(9); chipFont.setBold(true);
    p.setFont(chipFont);
    p.drawText(statusChip, Qt::AlignCenter, m_listening ? "LISTENING" : "READY");

    int cx = r.center().x();
    int cy = r.top() + 190;
    int radius = m_listening ? 74 : 64;
    p.setPen(Qt::NoPen);
    p.setBrush(m_listening ? QColor(0xBF, 0x5A, 0xF2, 55) : QColor(0x10, 0x84, 0xFF, 35));
    p.drawEllipse(QPoint(cx, cy), radius + 14, radius + 14);
    p.setBrush(m_listening ? c_mauve : QColor(C_BLUE));
    p.drawEllipse(QPoint(cx, cy), radius, radius);
    p.setPen(QPen(QColor(255, 255, 255, 90), 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(cx, cy), radius - 8, radius - 8);

    p.setPen(c_text);
    QFont glyph = font(); glyph.setPointSize(34); glyph.setBold(true); p.setFont(glyph);
    p.drawText(QRect(cx - 40, cy - 28, 80, 56), Qt::AlignCenter, m_listening ? "•••" : "Z");

    p.setPen(m_listening ? c_mauve : c_subtext);
    p.setFont(sub);
    p.drawText(QRect(r.left() + 30, cy + radius + 20, r.width() - 60, 24),
               Qt::AlignCenter, m_status);

    QRect meter(r.left() + 48, cy + radius + 62, r.width() - 96, 10);
    p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 22)); p.drawRoundedRect(meter, 5, 5);
    QRect fill = meter; fill.setWidth((meter.width() * m_level) / 100);
    p.setBrush(c_mauve); if (fill.width() > 0) p.drawRoundedRect(fill, 5, 5);

    /* Keep the voice control above the transcript and text composer. */
    m_listenButton = QRect(r.center().x() - 110, r.bottom() - 128, 220, 38);
    drawGlassButton(p, m_listenButton, m_listening ? "Release to stop" : "Hold to talk",
                    m_listening ? BtnPressed : BtnNormal, true);

    p.setPen(c_subtext); p.setFont(sub);
    p.drawText(QRect(r.left() + 34, r.bottom() - 174, r.width() - 68, 34),
               Qt::TextWordWrap | Qt::AlignCenter, m_transcript);

    /* Show the latest conversation entries above the shared text input. */
    QRect chat(r.left() + 26, r.bottom() - 292, r.width() - 52, 92);
    p.setPen(QPen(glass_border(35), 1));
    p.setBrush(QColor(0x14, 0x14, 0x16, 150));
    p.drawRoundedRect(chat, 10, 10);
    QFont chatFont = font(); chatFont.setPointSize(9); p.setFont(chatFont);
    int cy2 = chat.top() + 8;
    int first = qMax(0, m_messages.size() - 3);
    for (int i = first; i < m_messages.size() && cy2 < chat.bottom(); i++) {
        QString line = QString(m_messages[i].fromAi ? "Ziggy: " : "You: ") + m_messages[i].text;
        if (line.size() > 72) line = line.left(69) + "...";
        QRect bubble = m_messages[i].fromAi
            ? QRect(chat.left() + 8, cy2, chat.width() - 70, 20)
            : QRect(chat.right() - chat.width() + 70, cy2, chat.width() - 78, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(m_messages[i].fromAi ? QColor(0xBF, 0x5A, 0xF2, 38)
                                        : QColor(0x10, 0x84, 0xFF, 42));
        p.drawRoundedRect(bubble, 8, 8);
        p.setPen(m_messages[i].fromAi ? c_mauve : c_sky);
        p.drawText(bubble.adjusted(8, 0, -8, 0), Qt::AlignVCenter, line);
        cy2 += 23;
    }

    QRect inputBar(r.left() + 26, r.bottom() - 66, r.width() - 52, 38);
    p.setPen(QPen(m_input.isEmpty() ? glass_border(50) : c_sky, 1));
    p.setBrush(QColor(0x23, 0x23, 0x25, 190));
    p.drawRoundedRect(inputBar, 9, 9);
    m_sendRect = QRect(inputBar.right() - 72, inputBar.y() + 4, 64, 30);
    p.setPen(m_input.isEmpty() ? c_subtext : c_text);
    p.drawText(inputBar.adjusted(12, 0, -82, 0), Qt::AlignVCenter,
               m_input.isEmpty() ? "Ask Ziggy..." : m_input);
    drawGlassButton(p, m_sendRect, "Send",
                    m_hoveredBtn == 1 ? BtnHover : BtnNormal, true);
}

void QtZiggyWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_sendRect.contains(event->pos())) {
        submitText();
        return;
    }
    if (event->button() == Qt::LeftButton && m_listenButton.contains(event->pos())) {
        setListening(true);
        return;
    }
    QtAppWindow::mousePressEvent(event);
}

void QtZiggyWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_listening) {
        setListening(false);
        return;
    }
    QtAppWindow::mouseReleaseEvent(event);
}

void QtZiggyWidget::mouseMoveEvent(QMouseEvent *event) {
    const int old = m_hoveredBtn;
    m_hoveredBtn = m_sendRect.contains(event->pos()) ? 1 : -1;
    if (old != m_hoveredBtn) update();
    QtAppWindow::mouseMoveEvent(event);
}

void QtZiggyWidget::leaveEvent(QEvent *) {
    if (m_hoveredBtn != -1) { m_hoveredBtn = -1; update(); }
}

void QtZiggyWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        submitText();
    } else if (event->key() == Qt::Key_Backspace) {
        if (!m_input.isEmpty()) m_input.chop(1);
    } else if (event->key() >= Qt::Key_Space && event->key() <= Qt::Key_AsciiTilde) {
        m_input += event->text();
    }
    update();
}
