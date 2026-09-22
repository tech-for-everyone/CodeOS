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
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QPlainTextEdit>

extern "C" void kprintf(const char *fmt, ...);

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
#include "udp.h"
#include "ip.h"
#include <vector>
}

#include <string.h>

extern "C" {
/* Kernel TCP socket layer (declared here to avoid re-defining the
 * TCP_* flag macros that the legacy net.h already provides). */
int tcp_socket(void);
int tcp_bind(int fd, uint32_t ip, uint16_t port);
int tcp_listen(int fd, int backlog);
int tcp_accept(int fd);
int tcp_connect(uint32_t ip, uint16_t port);
int tcp_pending(int fd);
int tcp_poll_recv(int fd, int timeout_ms);
int tcp_recv(int fd, void *buf, int len);
int tcp_send(int fd, const void *data, int len);
int tcp_close(int fd);
int tcp_reset(int fd);
/* Scheduler (declared explicitly: the kernel sched.h conflicts with the
 * host pthread.h sched_yield when pulled in after Qt headers). */
int sched_create_thread(const char *name, void (*entry)(void));
void sched_exit(int code);
void sched_sleep_ms(uint64_t ms);
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
   NetBeam — AirDrop-style peer-to-peer file sharing.

   Protocol (NetBeam 1):
     Discovery  : UDP broadcast on 255.255.255.255:54917, beacon
                  "NB1|<device-name>\n". Peers answer with their own
                  beacon so discovery is near-instant in both directions.
     Transfer   : TCP on port 54918. Sender connects and streams
                  "NB1|<filename>|<size>\n" followed by <size> raw bytes.
                  The receiver auto-accepts into /NetBeam/Incoming/.

   Networking uses the kernel socket layer directly (host-order
   address/port integers) and runs outside the Qt thread: one kernel
   thread pumps the TCP listener, sends run in their own kernel thread,
   and the Qt paint loop only polls shared state — the UI never blocks.
   ═══════════════════════════════════════════════════════════════════ */

#define NB_DISCOVER_PORT 54917
#define NB_TRANSFER_PORT 54918
#define NB_MAGIC        "NB1"
#define NB_MAX_PAYLOAD  1200          /* < FS_CONTENT_MAX (1284) */
#define NB_BEACON_MS    1000          /* beacon interval              */
#define NB_PEER_TTL_MS  7000          /* drop peers not heard from    */
#define NB_NAME_MAX     31

/* Hash/colour a peer ip into a stable accent colour. */
static QColor nbColorForIp(uint32_t ip) {
    uint32_t h = (ip * 2654435761u) ^ (ip >> 16);
    const QColor pal[6] = {
        QColor(0xFF,0x5A,0x36), QColor(0x30,0xD1,0x58), QColor(0xBF,0x5A,0xF2),
        QColor(0x64,0xD2,0xFF), QColor(0xFF,0x9F,0x0A), QColor(0x34,0xC7,0x59),
    };
    return pal[h % 6];
}

/* IP integer ↔ dotted-string helpers (kernel socket layer is host-order:
 * octet1 | octet2<<8 | octet3<<16 | octet4<<24). */
static void nbIpText(uint32_t ip, char *out, int max) {
    int n = 0;
    static char t[32];
    n = 0;
    t[0] = 0;
    /* stack-safe small snprintf loop */
    unsigned char b[4];
    b[0] = (unsigned char)(ip & 0xFF);
    b[1] = (unsigned char)(ip >> 8);
    b[2] = (unsigned char)(ip >> 16);
    b[3] = (unsigned char)(ip >> 24);
    for (int i = 0; i < 4 && n < max - 8; i++) {
        if (i) t[n++] = '.';
        int v = b[i];
        char tmp[4]; int tn = 0;
        do { tmp[tn++] = (char)('0' + v % 10); v /= 10; } while (v && tn < 3);
        while (tn) t[n++] = tmp[--tn];
    }
    t[n] = 0;
    for (int i = 0; i <= n && i < max; i++) out[i] = t[i];
}

/* ── Cross-thread shared transfer state (single-word stores only) ── */
struct NbState {
    volatile int active;     /* 0 idle, 1 sending, 2 receiving           */
    volatile int done;       /* 0 in progress, 1 ok, -1 failed           */
    volatile int progress;   /* 0..100                                   */
    volatile int incoming;   /* 1 = this transfer is inbound             */
    uint32_t     ip;         /* peer ip (for sends)                      */
    char         peer[NB_NAME_MAX+1];
    char         file[NB_NAME_MAX+1];
    int          size;
    char         msg[96];
};
static struct NbState g_nb;
static char g_send_buf[NB_MAX_PAYLOAD];
static char g_recv_buf[NB_MAX_PAYLOAD];
static int  g_udp_fd = -1;

/* TCP listener: accepts inbound transfers and writes them to the VFS. */
static void nb_listen_thread(void) {
    for (;;) {
        static int ls = -1;
        if (ls < 0) {
            ls = tcp_socket();
            if (ls < 0) { sched_sleep_ms(500); continue; }
            if (tcp_bind(ls, 0, NB_TRANSFER_PORT) < 0) { 
                tcp_reset(ls); ls = -1; sched_sleep_ms(500); continue;
            }
            tcp_listen(ls, 4);
        }

        while (tcp_pending(ls) <= 0) sched_sleep_ms(20);

        int c = tcp_accept(ls);
        if (c < 0) continue;

        /* Read header line "NB1|name|size\n" */
        char hdr[160];
        int hl = 0;
        int closed = 0;
        while (hl < (int)sizeof(hdr) - 1) {
            int avail = tcp_poll_recv(c, 2000);
            if (avail < 0) { closed = 1; break; }
            char ch = 0;
            if (tcp_recv(c, &ch, 1) != 1) { closed = 1; break; }
            hdr[hl++] = ch;
            if (ch == '\n') break;
        }
        hdr[hl] = 0;
        kprintf("NBHDR len=%d closed=%d :: [%s]\n", hl, closed, hdr);

        g_nb.active = 2;
        g_nb.done = 0;
        g_nb.incoming = 1;
        g_nb.progress = 0;
        g_nb.msg[0] = 0;

        char fname[NB_NAME_MAX+1];
        int  fsize = 0;
        int  ok = 0;
        char *magic = 0, *namep = 0, *szp = 0;
        if (!closed && hl > 4) {
            char *p = hdr;
            /* token split on '|' */
            for (int part = 0, i = 0; part < 3 && i < hl; part++) {
                char *start = p + i;
                while (i < hl && hdr[i] != '|' && hdr[i] != '\n') i++;
                if (i < hl) {
                    char c = hdr[i];
                    hdr[i] = 0;
                    if (c == '|') i++;
                }
                if (part == 0) magic = start;
                else if (part == 1) namep = start;
                else szp = start;
            }
            if (magic && namep && szp && strcmp(magic, NB_MAGIC) == 0) {
                int sl = 0;
                for (char *q = namep; *q && sl < NB_NAME_MAX; q++) {
                    if (*q == '/' || *q == '\\' || *q == ' ') continue;
                    fname[sl++] = *q;
                }
                fname[sl] = 0;
                int v = 0;
                for (char *q = szp; *q && *q != '\n'; q++) {
                    if (*q > '9' || *q < '0') break;
                    v = v * 10 + (*q - '0');
                }
                fsize = v;
                if (fname[0] && fsize > 0 && fsize <= NB_MAX_PAYLOAD) ok = 1;
            }
        }
        kprintf("NBHDR2 m='%s' n='%s' s='%s' fname='%s' fsize=%d ok=%d\n",
                magic ? magic : "-", namep ? namep : "-", szp ? szp : "-",
                fname, fsize, ok);

        if (!ok) {
            g_nb.done = -1;
            snprintf(g_nb.msg, sizeof(g_nb.msg), "Invalid handshake");
        } else {
            int want = fsize;
            int got = 0;
            while (got < want) {
                int avail = tcp_poll_recv(c, 3000);
                if (avail < 0) break;
                if (avail == 0) continue;
                int n = tcp_recv(c, g_recv_buf + got, want - got);
                if (n <= 0) break;
                got += n;
                g_nb.progress = got * 100 / want;
            }
            g_nb.size = got;
            if (got == want) {
                /* Save to /NetBeam/Incoming/<file> via kernel VFS. */
                fs_mkdir("/NetBeam");
                fs_mkdir("/NetBeam/Incoming");
                char path[96];
                snprintf(path, sizeof(path), "/NetBeam/Incoming/%s", fname);
                if (fs_mkfile(path) == 0 &&
                    fs_write(path, g_recv_buf, got) == got) {
                    g_nb.done = 1;
                    snprintf(g_nb.msg, sizeof(g_nb.msg),
                             "Received %s from %s", fname, g_nb.peer);
                    kprintf("NBRECV ok file=%s size=%d saved=%s\n",
                            fname, got, path);
                } else {
                    g_nb.done = -1;
                    snprintf(g_nb.msg, sizeof(g_nb.msg), "Save failed");
                }
            } else {
                g_nb.done = -1;
                snprintf(g_nb.msg, sizeof(g_nb.msg), "Connection dropped");
            }
        }

        tcp_close(c);
        g_nb.active = 0;
    }
}

/* One-shot sender: connects to peer and streams the pre-loaded payload. */
static void nb_send_thread(void) {
    int s = tcp_connect(g_nb.ip, NB_TRANSFER_PORT);
    if (s < 0) {
        g_nb.done = -1;
        snprintf(g_nb.msg, sizeof(g_nb.msg), "Peer unreachable");
        g_nb.active = 0;
        sched_exit(0);
        return;
    }
    kprintf("NBSEND connect ip=%u file=%s size=%d\n", g_nb.ip, g_nb.file, g_nb.size);
    char hdr[96];
    int hl = snprintf(hdr, sizeof(hdr), "%s|%s|%d\n", NB_MAGIC, g_nb.file, g_nb.size);
    if (tcp_send(s, hdr, hl) < 0) {
        g_nb.done = -1;
        snprintf(g_nb.msg, sizeof(g_nb.msg), "Send failed");
        tcp_close(s);
        g_nb.active = 0;
        sched_exit(0);
        return;
    }
    int off = 0;
    while (off < g_nb.size) {
        int n = g_nb.size - off;
        if (n > 512) n = 512;
        if (tcp_send(s, g_send_buf + off, n) < 0) {
            g_nb.done = -1;
            snprintf(g_nb.msg, sizeof(g_nb.msg), "Send failed");
            break;
        }
        off += n;
        g_nb.progress = off * 100 / g_nb.size;
    }
    tcp_close(s);
    if (g_nb.done != -1) {
        g_nb.done = 1;
        snprintf(g_nb.msg, sizeof(g_nb.msg), "Sent %s to %s", g_nb.file, g_nb.peer);
        kprintf("NBSEND done file=%s peer=%s size=%d\n", g_nb.file, g_nb.peer, g_nb.size);
    }
    g_nb.active = 0;
    sched_exit(0);
}

/* ═══════════════════════════════════════════════════════════════════
   QtNetBeamWidget — local file sharing and device handoff
   ═══════════════════════════════════════════════════════════════════ */

QtNetBeamWidget::QtNetBeamWidget(QWidget *parent) : QtAppWindow("NetBeam", parent) {
    resize(700, 520);

    /* Local identity */
    m_myIp = ip_get_addr();
    {
        char ipbuf[24];
        nbIpText(m_myIp, ipbuf, sizeof(ipbuf));
        m_myName = QString("CodeOS-%1").arg((m_myIp >> 24) & 0xFF);
    }

    /* Discovery socket (GUI-thread only) */
    if (g_udp_fd < 0) {
        g_udp_fd = udp_socket_create();
        if (g_udp_fd >= 0) udp_bind(g_udp_fd, 0, NB_DISCOVER_PORT);
    }

    /* Inbound TCP listener thread (persistent kernel thread) */
    sched_create_thread("nbnet", nb_listen_thread);

    m_log << "NetBeam ready. Nearby devices appear here." << "";

    connect(&m_discoverTimer, &QTimer::timeout, this, [this]() {
        pollNetwork();
        if (m_discovering) {
            m_discoverPhase++;
            if (m_discoverPhase >= 8) m_discovering = false;
        }
        for (int i = 0; i < m_devices.size(); i++) {
            uint32_t age = timer_get_milliseconds() - m_devices[i].lastSeenMs;
            int sig = (age < 1300) ? 95 : (age < 2600) ? 75 : (age < 5000) ? 45 : 15;
            bool tr = false;
            int prog = 0;
            if (g_nb.active == 1 && g_nb.ip && m_devices[i].ip == g_nb.ip) {
                tr = true; prog = g_nb.progress;
            }
            if (m_devices[i].signalStrength != sig ||
                m_devices[i].transferring != tr ||
                m_devices[i].progress != prog)
                update();
            m_devices[i].signalStrength = sig;
            m_devices[i].transferring = tr;
            m_devices[i].progress = prog;
        }
        /* Report completed transfers once into the log / history */
        if (!g_nb.active && g_nb.done != 0 && m_xferReported != g_nb.done) {
            m_xferReported = g_nb.done;
            if (g_nb.done == 1) {
                m_history.append(g_nb.incoming
                     ? QString("From %1 · %2").arg(g_nb.peer).arg(g_nb.file)
                     : QString("To %1 · %2").arg(g_nb.peer).arg(g_nb.file));
                m_log << (g_nb.incoming
                     ? QString("Received %1 from %2").arg(g_nb.file).arg(g_nb.peer)
                     : QString("Sent %1 to %2").arg(g_nb.file).arg(g_nb.peer));
                update();
            } else {
                m_log << QString("Transfer failed: %1").arg(g_nb.msg);
                update();
            }
        }
    });
    m_discoverTimer.start(50);
    discoverDevices();
}

void QtNetBeamWidget::discoverDevices() {
    m_discovering = true;
    m_discoverPhase = 0;
    m_log << "Searching for nearby devices...";
    update();
}

/* Called every ~50 ms: beacon + drain inbound beacons + expire peers. */
void QtNetBeamWidget::pollNetwork() {
    if (g_udp_fd < 0) return;

    /* Announce ourselves (limited broadcast). */
    static uint32_t next_beacon = 0;
    uint32_t now = timer_get_milliseconds();
    if (now >= next_beacon) {
        next_beacon = now + NB_BEACON_MS;
        char beacon[64];
        QByteArray nm = m_myName.toUtf8();
        int bl = snprintf(beacon, sizeof(beacon), "%s|%s|%d\n",
                          NB_MAGIC, nm.constData(), NB_TRANSFER_PORT);
        udp_sendto(g_udp_fd, beacon, bl, 0xFFFFFFFF, NB_DISCOVER_PORT);
    }

    /* Drain whatever beacons arrived (poll-style, ~1 ms per call). */
    for (int i = 0; i < 8; i++) {
        char buf[128];
        uint32_t src_ip = 0;
        uint16_t src_port = 0;
        int n = udp_recv_timeout(g_udp_fd, buf, sizeof(buf) - 1,
                                 &src_ip, &src_port, 0);
        if (n <= 0) break;
        buf[n] = 0;
        if (src_ip == m_myIp) continue;          /* own echo */
        if (n < 5 || memcmp(buf, NB_MAGIC, 3) != 0) continue;
        if (buf[3] != '|') continue;

        /* Parse device name. */
        char name[NB_NAME_MAX+1];
        int nl = 0;
        for (int j = 4; j < n && nl < NB_NAME_MAX; j++) {
            if (buf[j] == '\n' || buf[j] == '|') break;
            name[nl++] = buf[j];
        }
        name[nl] = 0;
        if (!nl) continue;

        /* Add or refresh peer. */
        int idx = -1;
        for (int k = 0; k < m_devices.size(); k++)
            if (m_devices[k].ip == src_ip) { idx = k; break; }
        if (idx < 0) {
            m_devices.append({ QString::fromUtf8(name), "laptop",
                               nbColorForIp(src_ip), 95, false, 0,
                               src_ip, now });
            char ipbuf[24];
            nbIpText(src_ip, ipbuf, sizeof(ipbuf));
            m_log << QString("Found %1 (%2)").arg(QString::fromUtf8(name)).arg(ipbuf);
            kprintf("NBPEER found ip=%s name=%s\n", ipbuf, name);
            update();
        } else {
            m_devices[idx].name = QString::fromUtf8(name);
            m_devices[idx].lastSeenMs = now;
        }

        /* Reply so the other side sees us instantly (they asked via beacon). */
        char reply[64];
        QByteArray nm = m_myName.toUtf8();
        int bl = snprintf(reply, sizeof(reply), "%s|%s|%d\n", NB_MAGIC, nm.constData(), NB_TRANSFER_PORT);
        udp_sendto(g_udp_fd, reply, bl, src_ip, NB_DISCOVER_PORT);
    }

    /* Expire silent peers. */
    for (int i = m_devices.size() - 1; i >= 0; i--) {
        if ((int)(now - m_devices[i].lastSeenMs) > NB_PEER_TTL_MS) {
            char ipbuf[24];
            nbIpText(m_devices[i].ip, ipbuf, sizeof(ipbuf));
            m_log << QString("%1 (%2) went away").arg(m_devices[i].name).arg(ipbuf);
            m_devices.removeAt(i);
            update();
        }
    }
}

static void drawWineSectionHeader(QPainter &p, int x, int y, int w, const QString &label) {
    QFont hf; hf.setPointSize(10); hf.setBold(true); p.setFont(hf); p.setPen(c_subtext);
    p.drawText(x, y + 12, label);
    p.setPen(QPen(QColor(0x48,0x48,0x4A, 60), 1));
    p.drawLine(x, y + 22, x + w, y + 22);
}

static void drawDeviceCard(QPainter &p, const QRect &r, const QString &name,
                           const QString &type, const QColor &color, int signal,
                           bool selected, bool hovered) {
    /* Card background */
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    if (selected) {
        bg.setColorAt(0.0, QColor(0xFF,0x5A,0x36, 50));
        bg.setColorAt(1.0, QColor(0xFF,0x5A,0x36, 25));
    } else if (hovered) {
        bg.setColorAt(0.0, QColor(0x3A,0x3A,0x3C, 180));
        bg.setColorAt(1.0, QColor(0x2C,0x2C,0x2E, 170));
    } else {
        bg.setColorAt(0.0, QColor(0x23,0x23,0x25, 220));
        bg.setColorAt(1.0, QColor(0x1C,0x1C,0x1E, 200));
    }
    p.setPen(QPen(selected ? QColor(0xFF,0x5A,0x36, 100) : QColor(0x63,0x63,0x66, 60), 1));
    p.setBrush(bg);
    p.drawRoundedRect(r, 12, 12);

    /* Color accent strip at top */
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRect(r.x()+1, r.y(), r.width()-2, 3), 1, 1);

    /* Device icon circle */
    int iconSize = 36;
    QRect iconRect(r.left()+16, r.top()+(r.height()-iconSize)/2, iconSize, iconSize);
    QRadialGradient iconGrad(iconRect.center(), iconSize/2.0);
    iconGrad.setColorAt(0.0, color.lighter(130));
    iconGrad.setColorAt(1.0, color);
    p.setPen(Qt::NoPen); p.setBrush(iconGrad);
    p.drawEllipse(iconRect);

    /* Device type letter */
    QFont tf; tf.setPointSize(14); tf.setBold(true); p.setFont(tf);
    p.setPen(QColor(255,255,255,240));
    QString letter = (type == "phone") ? "P" : (type == "server") ? "S" : "L";
    p.drawText(iconRect, Qt::AlignCenter, letter);

    /* Name */
    QFont nf; nf.setPointSize(11); nf.setBold(true); p.setFont(nf); p.setPen(c_text);
    p.drawText(r.left()+62, r.top()+16, name);

    /* Signal bars */
    QFont sf; sf.setPointSize(9); p.setFont(sf); p.setPen(c_subtext);
    p.drawText(r.left()+62, r.top()+34, QString("%1% signal").arg(signal));

    /* Signal strength dots */
    int dotX = r.right()-50;
    int dotY = r.center().y();
    for (int i = 0; i < 4; i++) {
        int dh = 4 + i*2;
        int filled = (signal > (i+1)*25);
        p.setPen(Qt::NoPen);
        p.setBrush(filled ? color : QColor(0x48,0x48,0x4A, 100));
        p.drawRoundedRect(QRect(dotX+i*8, dotY+dh/2-dh, 5, dh), 1, 1);
    }
}

void QtNetBeamWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Title ── */
    f.setPointSize(14); f.setBold(true); p.setFont(f); p.setPen(c_text);
    p.drawText(r.left()+16, r.top()+24, "NetBeam");

    /* ── Tab bar ── */
    int tabY = r.top()+38;
    const char *tabs[] = {"Devices", "Send", "History"};
    for (int i = 0; i < 3; i++) {
        int tw = 72;
        QRect tr(r.left()+16+i*(tw+4), tabY, tw, 26);
        m_btnRects[i] = tr;
        if (i == m_tab) {
            p.setPen(Qt::NoPen); p.setBrush(QColor(0xFF,0x5A,0x36, 50));
            p.drawRoundedRect(tr, 6, 6);
        }
        f.setPointSize(10); f.setBold(i == m_tab); p.setFont(f);
        p.setPen(i == m_tab ? c_text : c_subtext);
        p.drawText(tr, Qt::AlignCenter, tabs[i]);
    }

    int contentTop = tabY + 34;

    if (m_tab == 0) {
        /* ── Devices tab ── */
        /* Discover button */
        int btnX = r.right()-120;
        QRect discBtn(btnX, tabY-2, 104, 28);
        m_btnRects[3] = discBtn;
        if (m_discovering) {
            float pulse = 0.5f + 0.5f * qSin(m_discoverPhase * 0.6f);
            drawGlassButton(p, discBtn, "Scanning...", BtnHover, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xFF,0x5A,0x36, (int)(pulse * 60)));
            p.drawEllipse(discBtn.center(), 20 + (int)(pulse*8), 20 + (int)(pulse*8));
        } else {
            drawGlassButton(p, discBtn, "Discover", m_hoveredBtn == 3 ? BtnHover : BtnNormal, true);
        }

        /* Device grid */
        int cols = 2;
        int cardW = (r.width()-40)/cols;
        int cardH = 56;
        int gap = 8;
        int y = contentTop + 4;

        if (m_devices.isEmpty() && !m_discovering) {
            p.setPen(c_subtext); f.setPointSize(11); f.setBold(false); p.setFont(f);
            p.drawText(QRect(r.left()+20, contentTop+40, r.width()-40, 80),
                       Qt::AlignCenter, "No devices found.\nMake sure nearby CodeOS machines have NetBeam open.");
        } else {
            int shown = 0;
            for (int i = 0; i < m_devices.size(); i++) {
                int col = shown % cols;
                int row = shown / cols;
                QRect cr(r.left()+12+col*(cardW+gap), y+row*(cardH+gap), cardW-gap, cardH);
                shown++;

                /* Progress bar during transfer */
                if (m_devices[i].transferring) {
                    QRect progBg(cr.left()+16, cr.bottom()-8, cr.width()-32, 4);
                    p.setPen(Qt::NoPen); p.setBrush(QColor(0x1C,0x1C,0x1E));
                    p.drawRoundedRect(progBg, 2, 2);
                    QRect progFill(progBg);
                    progFill.setWidth(progBg.width() * m_devices[i].progress / 100);
                    p.setBrush(QColor(0xFF,0x5A,0x36));
                    p.drawRoundedRect(progFill, 2, 2);
                }

                drawDeviceCard(p, cr, m_devices[i].name, m_devices[i].type,
                              m_devices[i].color, m_devices[i].signalStrength,
                              i == m_selectedDevice, i == m_hoveredDevice);

                /* Peer IP address caption under the name */
                char ipbuf[24];
                nbIpText(m_devices[i].ip, ipbuf, sizeof(ipbuf));
                QFont ipf = font(); ipf.setPointSize(8);
                p.setFont(ipf); p.setPen(QColor(0x99,0x99,0x9C, 200));
                p.drawText(QRect(cr.left()+62, cr.top()+40, cr.width()-70, 14),
                           Qt::AlignLeft, ipbuf);
            }
        }

        /* Scan progress indicator */
        if (m_discovering) {
            int progY = r.bottom()-60;
            QRect progBar(r.left()+40, progY, r.width()-80, 6);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0x1C,0x1C,0x1E));
            p.drawRoundedRect(progBar, 3, 3);
            int filled = progBar.width() * m_discoverPhase / 8;
            QLinearGradient progG(progBar.topLeft(), progBar.topRight());
            progG.setColorAt(0.0, QColor(0xFF,0x5A,0x36));
            progG.setColorAt(1.0, QColor(0x30,0xD1,0x58));
            p.setBrush(progG);
            p.drawRoundedRect(QRect(progBar.x(), progBar.y(), filled, progBar.height()), 3, 3);

            f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
            p.drawText(QRect(r.left()+40, progY+12, r.width()-80, 20), Qt::AlignCenter,
                       QString("Scanning... %1%").arg(m_discoverPhase*12));
        }

    } else if (m_tab == 1) {
        /* ── Send tab ── */
        QRect card(r.left()+12, contentTop, r.width()-24, r.height()-(contentTop-r.top())-40);
        p.setBrush(QColor(0x14,0x14,0x16)); p.setPen(QPen(glass_border(40), 1));
        p.drawRoundedRect(card, 10, 10);

        QRect chooseBtn(card.left()+20, card.top()+16, 150, 30);
        m_btnRects[4] = chooseBtn;
        drawGlassButton(p, chooseBtn, "Choose files", m_hoveredBtn == 4 ? BtnHover : BtnNormal, true);

        f.setPointSize(10); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
        QString selectedSummary = m_selectedFiles.isEmpty()
            ? "No files selected"
            : QString("%1 file selected · %2 bytes")
                  .arg(m_selectedFiles.size())
                  .arg(QFileInfo(m_selectedFiles.first()).size());
        p.drawText(QRect(chooseBtn.right()+12, chooseBtn.top(), card.width()-210, 30),
                   Qt::AlignVCenter|Qt::AlignLeft, selectedSummary);

        QRect dropZone(card.left()+20, card.top()+20, card.width()-40, card.height()-40);
        p.setPen(QPen(QColor(0x48,0x48,0x4A, 80), 2, Qt::DashLine));
        p.setBrush(QColor(0x1C,0x1C,0x1E, 60));
        p.drawRoundedRect(dropZone, 16, 16);

        int arrowY = dropZone.center().y() - 20;
        p.setPen(QPen(QColor(0x48,0x48,0x4A), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawLine(dropZone.center().x(), arrowY, dropZone.center().x(), arrowY+30);
        p.drawLine(dropZone.center().x()-10, arrowY+20, dropZone.center().x(), arrowY+30);
        p.drawLine(dropZone.center().x()+10, arrowY+20, dropZone.center().x(), arrowY+30);

        f.setPointSize(12); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
        p.drawText(QRect(dropZone.left(), arrowY+40, dropZone.width(), 24),
                   Qt::AlignHCenter, "Choose a file, then click a nearby device");
        f.setPointSize(9); p.setFont(f); p.setPen(QColor(0x48,0x48,0x4A));
        p.drawText(QRect(dropZone.left(), arrowY+62, dropZone.width(), 20),
                   Qt::AlignHCenter, "NetBeam streams it over your network. Files up to 1.2 KB.");

    } else {
        /* ── History tab ── */
        QRect card(r.left()+12, contentTop, r.width()-24, r.height()-(contentTop-r.top())-40);
        p.setBrush(QColor(0x14,0x14,0x16)); p.setPen(QPen(glass_border(40), 1));
        p.drawRoundedRect(card, 10, 10);

        drawWineSectionHeader(p, card.left()+14, card.top()+8, card.width()-28, "Transfer History");

        int fy = card.top() + 34;
        if (m_history.isEmpty()) {
            f.setPointSize(10); f.setBold(false); p.setFont(f); p.setPen(c_subtext);
            p.drawText(QRect(card.left()+14, fy, card.width()-28, 30),
                       Qt::AlignLeft, "No transfers yet. Send a file to a nearby device.");
        }
        for (int i = 0; i < m_history.size(); i++) {
            QRect row(card.left()+14, fy, card.width()-28, 36);
            if (i % 2 == 0) {
                p.setPen(Qt::NoPen); p.setBrush(QColor(0x23,0x23,0x25, 50));
                p.drawRoundedRect(row, 6, 6);
            }
            bool sent = m_history[i].startsWith("To");
            p.setPen(sent ? c_green : c_accent);
            f.setPointSize(9); f.setBold(true); p.setFont(f);
            p.drawText(row.adjusted(8, 4, -8, -16), Qt::AlignLeft|Qt::AlignTop,
                       sent ? m_history[i] : m_history[i]);
            fy += 42;
            if (fy > card.bottom() - 48) break;
        }
    }

    /* ── Log bar ── */
    QRect logR(r.left()+8, r.bottom()-30, r.width()-16, 24);
    p.setBrush(QColor(0x0A,0x0A,0x0C)); p.setPen(QPen(glass_border(40), 1));
    p.drawRoundedRect(logR, 6, 6);
    QFont logF; logF.setPointSize(9); p.setFont(logF); p.setPen(QColor(0x30,0xD1,0x58));
    if (!m_log.isEmpty())
        p.drawText(logR.adjusted(10, 0, -10, 0), Qt::AlignVCenter|Qt::AlignLeft, m_log.last());
}

static QStringList pickFiles() {
    /* The embedded Qt file engine cannot enumerate the CodeOS VFS, so the
     * stock QFileDialog always looks empty here. When that's the case, fall
     * back to pulling the demo file straight from the kernel VFS. */
    if (QDir("/").entryList().size() == 0) {
        int rsz = 0, rdir = 0;
        if (fs_get_info("/NetBeam-hello.txt", &rsz, &rdir) == 0 && !rdir &&
            rsz > 0 && rsz <= NB_MAX_PAYLOAD) {
            QStringList files;
            files << "/NetBeam-hello.txt";
            kprintf("NBDEMO autopick size=%d\n", rsz);
            return files;
        }
    }
    return QFileDialog::getOpenFileNames(
        nullptr, "Choose files to share", "/", "All files (*)");
}

void QtNetBeamWidget::chooseFiles() {
    if (g_nb.active) {
        m_log << "Wait until the current transfer finishes.";
        update();
        return;
    }
    QStringList files = pickFiles();
    kprintf("NBDIR ent=%d home=%s exists=%d\n",
            QDir("/").entryList().size(),
            QDir("/").path().toUtf8().constData(),
            QFile::exists("/NetBeam-hello.txt") ? 1 : 0);
    kprintf("NBCHOOSE home=%s argc=%d first=%s\n",
            QDir::homePath().toUtf8().constData(), files.size(),
            files.isEmpty() ? "" : files.first().toUtf8().constData());
    if (files.isEmpty()) return;
    const QString first = files.first();
    qint64 sz = QFileInfo(first).size();
    if (sz <= 0) {
        int isz = 0, idir = 0;
        if (fs_get_info(first.toUtf8().constData(), &isz, &idir) == 0) sz = isz;
    }
    if (sz > NB_MAX_PAYLOAD) {
        m_log << QString("'%1' is %2 bytes — NetBeam limit is %3 bytes")
                     .arg(QFileInfo(first).fileName()).arg(sz).arg(NB_MAX_PAYLOAD);
        m_selectedFiles.clear();
        update();
        return;
    }
    m_selectedFiles = files;
    m_log << QString("Ready to send %1 (%2 bytes)").arg(QFileInfo(first).fileName()).arg(sz);
    update();
}

void QtNetBeamWidget::sendFilesToDevice(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= m_devices.size()) return;
    if (g_nb.active) {
        m_log << "Wait until the current transfer finishes.";
        update();
        return;
    }
    if (m_selectedFiles.isEmpty()) {
        m_log << "Choose at least one file before selecting a device.";
        m_tab = 1;
        update();
        return;
    }
    const Device &peer = m_devices[deviceIndex];
    if (peer.ip == 0 || !peer.signalStrength) {
        m_log << QString("%1 is not reachable right now").arg(peer.name);
        update();
        return;
    }

    const QString src = m_selectedFiles.first();
    QByteArray srcPath = src.toUtf8();
    qint64 n = 0;
    QFile f(src);
    if (f.open(QIODevice::ReadOnly)) {
        n = f.read(g_send_buf, NB_MAX_PAYLOAD);
        f.close();
    } else {
        n = fs_read(srcPath.constData(), g_send_buf, NB_MAX_PAYLOAD);
        kprintf("NBSEND fsread n=%lld\n", (long long)n);
    }
    if (n <= 0) {
        m_log << "The selected file is empty.";
        update();
        return;
    }

    g_nb.active = 1;
    g_nb.done = 0;
    g_nb.incoming = 0;
    g_nb.progress = 2;
    g_nb.ip = peer.ip;
    g_nb.size = (int)n;
    g_nb.peer[0] = 0;
    {
        QByteArray nm = peer.name.toUtf8();
        int k = 0;
        for (; k < NB_NAME_MAX && nm.constData()[k]; k++) g_nb.peer[k] = nm.constData()[k];
        g_nb.peer[k] = 0;
    }
    g_nb.file[0] = 0;
    {
        QByteArray fn = QFileInfo(src).fileName().toUtf8();
        int k = 0;
        for (; k < NB_NAME_MAX && fn.constData()[k]; k++) {
            char ch = fn.constData()[k];
            if (ch == '/' || ch == '\\') ch = '_';
            g_nb.file[k] = ch;
        }
        g_nb.file[k] = 0;
    }
    m_xferReported = 0;
    m_selectedDevice = deviceIndex;
    m_log << QString("Sending %1 to %2...").arg(g_nb.file).arg(peer.name);
    update();
    if (sched_create_thread("nbsend", nb_send_thread) < 0) {
        g_nb.active = 0;
        g_nb.done = -1;
        snprintf(g_nb.msg, sizeof(g_nb.msg), "No thread slots");
        m_log << "Transfer failed: no free thread";
        update();
        return;
    }
}

void QtNetBeamWidget::mousePressEvent(QMouseEvent *e) {
    kprintf("NBPRESS local=(%d,%d) tab=%d mod=%d\n",
            e->pos().x(), e->pos().y(), m_tab, (int)e->modifiers());
    /* Tab clicks */
    for (int i = 0; i < 3; i++)
        if (m_btnRects[i].isValid() && m_btnRects[i].contains(e->pos())) {
            m_tab = i; update(); return;
        }

    /* Discover button */
    if (m_btnRects[3].isValid() && m_btnRects[3].contains(e->pos())) {
        discoverDevices(); update(); return;
    }
    if (m_btnRects[4].isValid() && m_btnRects[4].contains(e->pos())) {
        chooseFiles(); return;
    }

    /* Device clicks */
    if (m_tab == 0) {
        int cols = 2;
        int cardW = (width()-40)/cols;
        int cardH = 56;
        int gap = 8;
        int contentTop = height()/8 + 60;
        int y = contentTop + 4;
        int shown = 0;
        for (int i = 0; i < m_devices.size(); i++) {
            int col = shown % cols;
            int row2 = shown / cols;
            shown++;
            QRect cr(12+col*(cardW+gap), y+row2*(cardH+gap), cardW-gap, cardH);
            if (cr.contains(e->pos())) {
                m_selectedDevice = i;
                sendFilesToDevice(i);
                update(); return;
            }
        }
    }

    QtAppWindow::mousePressEvent(e);
}

void QtNetBeamWidget::mouseMoveEvent(QMouseEvent *e) {
    QtAppWindow::mouseMoveEvent(e);
    int old = m_hoveredBtn;
    int oldD = m_hoveredDevice;
    m_hoveredBtn = -1;
    m_hoveredDevice = -1;

    for (int i = 0; i < 5; i++)
        if (m_btnRects[i].isValid() && m_btnRects[i].contains(e->pos()))
            m_hoveredBtn = i;

    if (m_tab == 0) {
        int cols = 2;
        int cardW = (width()-40)/cols;
        int cardH = 56;
        int gap = 8;
        int contentTop = height()/8 + 60;
        int y = contentTop + 4;
        int shown = 0;
        for (int i = 0; i < m_devices.size(); i++) {
            int col = shown % cols;
            int row2 = shown / cols;
            shown++;
            QRect cr(12+col*(cardW+gap), y+row2*(cardH+gap), cardW-gap, cardH);
            if (cr.contains(e->pos())) m_hoveredDevice = i;
        }
    }

    if (old != m_hoveredBtn || oldD != m_hoveredDevice) update();
}

void QtNetBeamWidget::leaveEvent(QEvent *) {
    if (m_hoveredBtn != -1 || m_hoveredDevice != -1) {
        m_hoveredBtn = -1; m_hoveredDevice = -1; update();
    }
}