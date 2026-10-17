/* DevStore — the CodeOS application store, as a live Qt panel.
 *
 * This is a from-scratch remake of the old C panel that used to live in
 * pkgs/core/panels/src/devstore.c. That file drew straight to the framebuffer
 * with fb.h and was never reachable: the desktop is Qt, so the panel had to
 * be rewritten in terms of the live UI. The behaviour it encoded is preserved
 * here:
 *
 *   - the catalogue is read from the package manager's static repo tables
 *     (core/extra/dev/ccp/aur/android), not a hardcoded list;
 *   - "installed" is answered by the package manager (pkg_get_info), not by
 *     looking at /bin;
 *   - installing is the ported "download, then install" step from the
 *     original installer, now fetch_install_package() — a native downloader
 *     and package-manager call with no curl, no popen and no sudo;
 *   - the interactive confirmation ("Proceed? [Y/N]") is kept as an armed
 *     overlay: pressing Install arms it, Y installs, N cancels.
 */

#include "qt_panels.h"
#include "qt_panels_common.h"

#include <QFontMetrics>
#include <QList>
#include <QString>

extern "C" {
#include "pkg.h"
#include "fetch.h"
}

/* ── geometry, shared by the painter and the hit-tests ── */
static const int DS_ROW_H   = 52;
static const int DS_PAD     = 14;
static const int DS_TAB_W   = 118;
static const int DS_TAB_H   = 30;
static const int DS_BTN_W   = 130;
static const int DS_BTN_H   = 34;

QtDevStoreWidget::QtDevStoreWidget(QWidget *parent) : QtAppWindow("DevStore", parent) {
    resize(820, 600);
    reload();
}

/* ── data ───────────────────────────────────────────────────────────── */

void QtDevStoreWidget::reload() {
    m_entries.clear();

    auto appendTable = [this](const pkg_repo_t *table) {
        if (!table) return;
        for (int i = 0; table[i].name && i < PKG_MAX_PACKAGES; i++) {
            const pkg_repo_t *p = &table[i];
            Entry e;
            e.name    = QString::fromUtf8(p->name);
            e.version = p->version ? QString::fromUtf8(p->version) : QString();
            e.desc    = p->desc    ? QString::fromUtf8(p->desc)    : QString();

            pkg_info_t info;
            e.installed = (pkg_get_info(p->name, &info) == 0);
            e.remote    = false;
            m_entries.append(e);
        }
    };

    appendTable(pkg_repo_core);
    appendTable(pkg_repo_extra);
    appendTable(pkg_repo_dev);
    appendTable(pkg_repo_ccp);
    appendTable(pkg_repo_aur);
    appendTable(pkg_repo_android);

    /* Packages discovered over the network by pkg_repo_update() ("Sync repos").
     * These are the ones whose Install actually downloads a .stone over
     * http(s). Skip any that a static table already lists, so a package that
     * is both shipped and published does not appear twice. */
    for (int i = 0; i < pkg_remote_count(); i++) {
        const pkg_repo_t *p = pkg_remote_at(i);
        if (!p || !p->name) continue;
        bool dup = false;
        for (const Entry &e : m_entries)
            if (e.name == QString::fromUtf8(p->name)) { dup = true; break; }
        if (dup) continue;

        Entry e;
        e.name    = QString::fromUtf8(p->name);
        e.version = p->version ? QString::fromUtf8(p->version) : QString();
        e.desc    = p->desc    ? QString::fromUtf8(p->desc)    : QString();
        pkg_info_t info;
        e.installed = (pkg_get_info(p->name, &info) == 0);
        e.remote    = true;
        m_entries.append(e);
    }

    /* The Installed tab is the same catalogue filtered down to what the
     * package manager reports as present. */
    if (m_mode == 1) {
        QList<Entry> only;
        for (const Entry &e : m_entries)
            if (e.installed) only.append(e);
        m_entries = only;
    }

    if (m_selected >= m_entries.size()) m_selected = -1;
    if (m_selected < 0 && !m_entries.isEmpty()) m_selected = 0;
    m_scroll = 0;
}

int QtDevStoreWidget::rowAt(const QPoint &pos) const {
    if (!m_listRect.contains(pos)) return -1;
    int idx = (pos.y() - m_listRect.y() + m_scroll) / DS_ROW_H;
    if (idx < 0 || idx >= m_entries.size()) return -1;
    return idx;
}

void QtDevStoreWidget::ensureVisible() {
    if (m_selected < 0) return;
    int top = m_selected * DS_ROW_H;
    int bot = top + DS_ROW_H;
    if (top < m_scroll) m_scroll = top;
    if (bot > m_scroll + m_listRect.height())
        m_scroll = bot - m_listRect.height();
    if (m_scroll < 0) m_scroll = 0;
}

void QtDevStoreWidget::beginInstall(int idx) {
    if (idx < 0 || idx >= m_entries.size()) return;
    if (m_entries[idx].installed) {
        m_status = m_entries[idx].name + " is already installed.";
        m_statusOk = true;
        return;
    }
    m_confirm = true;
    m_confirmName = m_entries[idx].name;
    m_status.clear();
}

void QtDevStoreWidget::runInstall() {
    m_confirm = false;
    QString name = m_confirmName;
    m_confirmName.clear();
    if (name.isEmpty()) return;

    /* reload() rebuilds the list below, so capture where this package came
     * from first — it decides which step the failure text should name. */
    bool remote = false;
    for (const Entry &e : m_entries)
        if (e.name == name) { remote = e.remote; break; }

    m_status = (remote ? "Downloading " : "Installing ") + name + "…";
    m_statusOk = true;
    update();

    /* The native package-manager flow: download, verify, install, clean up.
     * Same three steps the original console helper performed with curl and
     * installer, reapified onto http_get + pkg_install_with_deps. For a
     * remote package pkg_fetch() really transfers the .stone over http(s);
     * for a static one it synthesizes the stone from the bundled metadata. */
    int rc = fetch_install_package(name.toUtf8().constData());

    if (rc == 0)
        m_status = name + " installed.";
    else if (remote)
        m_status = "Download of " + name + " failed — check the repo URL and network.";
    else
        m_status = "Could not install " + name + ".";
    m_statusOk = (rc == 0);
    reload();
    update();
}

void QtDevStoreWidget::runSync() {
    if (m_syncing) return;
    m_syncing = true;
    m_status = "Syncing repository metadata…";
    m_statusOk = true;
    update();

    /* Fetches pkg.index from every enabled http(s) repo; file:// repos are
     * static tables and are skipped. Discovered packages land in the package
     * manager's remote repo, which reload() then lists. */
    pkg_repo_update();

    m_syncing = false;
    reload();

    int remote = pkg_remote_count();
    if (remote > 0) {
        m_status = QString::number(remote) +
                   " package(s) discovered from remote repos.";
        m_statusOk = true;
    } else {
        m_status = "Sync done — no http(s) repo returned packages.";
        m_statusOk = false;
    }
    update();
}

/* ── painting ───────────────────────────────────────────────────────── */

void QtDevStoreWidget::paintContent(QPainter &p, const QRect &r) {
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();

    /* ── Tabs ── */
    int tabsY = r.y() + 12;
    m_storeTab     = QRect(r.x() + DS_PAD, tabsY, DS_TAB_W, DS_TAB_H);
    m_installedTab = QRect(m_storeTab.right() + 8, tabsY, DS_TAB_W, DS_TAB_H);

    auto drawTab = [&](const QRect &tr, const QString &label, bool active) {
        p.setPen(Qt::NoPen);
        p.setBrush(active ? c_accent : QColor(0x38, 0x38, 0x3A, 170));
        p.drawRoundedRect(tr, 8, 8);
        QFont tf = f; tf.setPointSize(11); tf.setBold(active); p.setFont(tf);
        p.setPen(active ? QColor(0xFF, 0xFF, 0xFF) : c_subtext);
        p.drawText(tr, Qt::AlignCenter, label);
    };
    drawTab(m_storeTab, "Store", m_mode == 0);
    drawTab(m_installedTab, "Installed", m_mode == 1);

    /* "Sync repos" — pulls pkg.index from the http(s) repositories so that
     * network-discovered packages show up in the Store. */
    m_syncBtn = QRect(r.right() - DS_PAD - 122, tabsY, 122, DS_TAB_H);
    drawGlassButton(p, m_syncBtn, m_syncing ? "Syncing…" : "Sync repos",
                    m_syncing ? BtnDisabled
                              : (m_hoverBtn == 2 ? BtnHover : BtnNormal),
                    /*primary=*/false);

    /* Count of what the active tab is showing. */
    {
        QFont cf = f; cf.setPointSize(10); p.setFont(cf);
        p.setPen(c_subtext);
        QString caption = QString::number(m_entries.size()) + " packages";
        int cx = m_installedTab.right() + 10;
        p.drawText(QRect(cx, tabsY, qMax(0, m_syncBtn.left() - cx - 10), DS_TAB_H),
                   Qt::AlignVCenter | Qt::AlignLeft, caption);
    }

    /* ── List area ── */
    int listTop    = tabsY + DS_TAB_H + 18;
    int footerY    = r.bottom() - 12 - DS_BTN_H;
    int listBottom = footerY - 10;
    m_listRect = QRect(r.x() + DS_PAD, listTop, r.width() - 2 * DS_PAD,
                       qMax(0, listBottom - listTop));

    /* Header rule */
    p.setPen(QPen(QColor(0x48, 0x48, 0x4A, 90), 1));
    p.drawLine(r.x() + DS_PAD, listTop - 8, r.right() - DS_PAD, listTop - 8);

    p.save();
    p.setClipRect(m_listRect);
    for (int i = 0; i < m_entries.size(); i++) {
        int y = m_listRect.y() - m_scroll + i * DS_ROW_H;
        QRect row(m_listRect.x(), y, m_listRect.width(), DS_ROW_H - 2);
        if (row.bottom() < m_listRect.y() || row.top() > m_listRect.bottom())
            continue;

        bool sel = (i == m_selected);
        bool hov = (i == m_hoverRow);
        if (sel) {
            p.setPen(QPen(QColor(0xFF, 0x5A, 0x36, 160), 1));
            p.setBrush(QColor(0xFF, 0x5A, 0x36, 55));
            p.drawRoundedRect(row, 9, 9);
        } else if (hov) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x48, 0x48, 0x4A, 90));
            p.drawRoundedRect(row, 9, 9);
        }

        /* App tile */
        QRect iconRect(row.x() + 10, row.y() + 8, 34, 34);
        p.setPen(Qt::NoPen);
        p.setBrush(appIconColor(m_entries[i].name));
        p.drawRoundedRect(iconRect, 9, 9);
        QFont lf = f; lf.setPointSize(15); lf.setBold(true); p.setFont(lf);
        p.setPen(QColor(0xFF, 0xFF, 0xFF));
        p.drawText(iconRect, Qt::AlignCenter, appIconLetter(m_entries[i].name));

        /* Name + description */
        int nameX = row.x() + 54;
        int nameW = qMax(0, row.width() - 54 - 110);
        QFont nf = f; nf.setPointSize(11); nf.setBold(true); p.setFont(nf);
        p.setPen(c_text);
        p.drawText(QRect(nameX, row.y() + 7, nameW, 16),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(nf).elidedText(m_entries[i].name, Qt::ElideRight, nameW));

        QFont df = f; df.setPointSize(9); df.setBold(false); p.setFont(df);
        p.setPen(c_subtext);
        p.drawText(QRect(nameX, row.y() + 27, nameW, 14),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(df).elidedText(m_entries[i].desc, Qt::ElideRight, nameW));

        /* Version (top-right) */
        p.setPen(QColor(0x8E, 0x8E, 0x93));
        p.drawText(QRect(row.right() - 100, row.y() + 7, 92, 16),
                   Qt::AlignVCenter | Qt::AlignRight, m_entries[i].version);

        /* Status badge (bottom-right): Installed wins, else the package is a
         * network one whose Install will download it. */
        if (m_entries[i].installed || m_entries[i].remote) {
            bool inst = m_entries[i].installed;
            QColor fg = inst ? QColor(0x30, 0xD1, 0x58) : c_sky;
            QColor bg = fg; bg.setAlpha(inst ? 60 : 55);
            QRect b(row.right() - 100, row.y() + 27, 92, 16);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(b, 8, 8);
            QFont bf = f; bf.setPointSize(9); bf.setBold(true); p.setFont(bf);
            p.setPen(fg);
            p.drawText(b, Qt::AlignCenter, inst ? "Installed" : "Remote");
        }
    }
    p.restore();

    if (m_entries.isEmpty()) {
        QFont ef = f; ef.setPointSize(11); p.setFont(ef);
        p.setPen(c_overlay);
        p.drawText(m_listRect, Qt::AlignCenter,
                   m_mode == 1 ? "Nothing installed yet" : "No packages in the repositories");
    }

    /* ── Footer ── */
    QFont sf = f; sf.setPointSize(10); p.setFont(sf);
    p.setPen(m_status.isEmpty() ? c_subtext : (m_statusOk ? c_green : c_red));
    p.drawText(QRect(r.x() + DS_PAD, footerY, r.width() - 2 * DS_PAD - DS_BTN_W - 12, DS_BTN_H),
               Qt::AlignVCenter | Qt::AlignLeft, m_status);

    bool selected = (m_selected >= 0 && m_selected < m_entries.size());
    bool installed = selected && m_entries[m_selected].installed;
    bool remote = selected && m_entries[m_selected].remote;
    m_installBtn = QRect(r.right() - DS_PAD - DS_BTN_W, footerY, DS_BTN_W, DS_BTN_H);

    QString label = !selected ? "Select a package"
                             : (installed ? "Installed"
                                          : (remote ? "Download" : "Install"));
    ButtonState st = (!selected || installed) ? BtnDisabled
                   : (m_hoverBtn == 1 ? BtnHover : BtnNormal);
    drawGlassButton(p, m_installBtn, label, st, /*primary=*/true);

    /* ── Confirmation overlay (the ported Proceed? [Y/N]) ── */
    if (m_confirm) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x00, 0x00, 0x00, 90));
        p.drawRect(r);

        QRect panel(r.center().x() - 170, r.center().y() - 85, 340, 170);
        p.setPen(QPen(QColor(0x63, 0x63, 0x66, 120), 1));
        p.setBrush(QColor(0x2A, 0x2A, 0x2E, 240));
        p.drawRoundedRect(panel, 14, 14);

        QFont hf = f; hf.setPointSize(13); hf.setBold(true); p.setFont(hf);
        p.setPen(c_text);
        p.drawText(QRect(panel.x(), panel.y() + 22, panel.width(), 22),
                   Qt::AlignCenter, "Install package?");

        QFont pf = f; pf.setPointSize(12); pf.setBold(true); p.setFont(pf);
        p.setPen(c_sky);
        p.drawText(QRect(panel.x(), panel.y() + 50, panel.width(), 22),
                   Qt::AlignCenter, m_confirmName);

        QFont qf = f; qf.setPointSize(10); p.setFont(qf);
        p.setPen(c_subtext);
        p.drawText(QRect(panel.x(), panel.y() + 76, panel.width(), 18),
                   Qt::AlignCenter, "Proceed? [Y/N]");

        m_yesBtn = QRect(panel.x() + 24, panel.bottom() - 58, 130, 38);
        m_noBtn  = QRect(panel.right() - 24 - 130, panel.bottom() - 58, 130, 38);
        drawGlassButton(p, m_yesBtn, "Install (Y)", BtnNormal, true);
        drawGlassButton(p, m_noBtn,  "Cancel (N)",  BtnNormal, false);
    }
}

/* ── input ──────────────────────────────────────────────────────────── */

void QtDevStoreWidget::mousePressEvent(QMouseEvent *e) {
    QPoint pos = e->pos();

    if (m_confirm) {
        if (m_yesBtn.contains(pos)) { runInstall(); return; }
        if (m_noBtn.contains(pos))  { m_confirm = false; update(); return; }
        return;                     /* the prompt is modal within the window */
    }

    if (m_storeTab.contains(pos))     { m_mode = 0; reload(); update(); return; }
    if (m_installedTab.contains(pos)) { m_mode = 1; reload(); update(); return; }
    if (m_syncBtn.contains(pos))      { runSync(); return; }

    if (m_installBtn.contains(pos)) {
        beginInstall(m_selected);
        update();
        return;
    }

    int row = rowAt(pos);
    if (row >= 0) { m_selected = row; update(); return; }

    QtAppWindow::mousePressEvent(e);
}

void QtDevStoreWidget::mouseMoveEvent(QMouseEvent *e) {
    QPoint pos = e->pos();
    m_hoverRow = rowAt(pos);
    m_hoverBtn = m_installBtn.contains(pos) ? 1
               : (m_syncBtn.contains(pos) ? 2 : -1);
    update();
    QtAppWindow::mouseMoveEvent(e);
}

void QtDevStoreWidget::leaveEvent(QEvent *e) {
    m_hoverRow = -1;
    m_hoverBtn = -1;
    update();
    QtAppWindow::leaveEvent(e);
}

void QtDevStoreWidget::wheelEvent(QWheelEvent *e) {
    int maxScroll = qMax(0, m_entries.size() * DS_ROW_H - m_listRect.height());
    m_scroll -= e->angleDelta().y() / 4;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
    update();
}

void QtDevStoreWidget::keyPressEvent(QKeyEvent *e) {
    if (m_confirm) {
        int k = e->key();
        if (k == Qt::Key_Y || k == Qt::Key_Return || k == Qt::Key_Enter) { runInstall(); return; }
        if (k == Qt::Key_N || k == Qt::Key_Escape) { m_confirm = false; update(); return; }
        return;
    }

    switch (e->key()) {
    case Qt::Key_Up:
        if (m_selected > 0) m_selected--;
        ensureVisible(); update(); return;
    case Qt::Key_Down:
        if (m_selected + 1 < m_entries.size()) m_selected++;
        ensureVisible(); update(); return;
    case Qt::Key_PageUp:
        m_selected = qMax(0, m_selected - 6); ensureVisible(); update(); return;
    case Qt::Key_PageDown:
        m_selected = qMin((int)m_entries.size() - 1, m_selected + 6);
        ensureVisible(); update(); return;
    case Qt::Key_Tab:
        m_mode = (m_mode == 0) ? 1 : 0; reload(); update(); return;
    case Qt::Key_F5:
        runSync(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        beginInstall(m_selected); update(); return;
    default:
        break;
    }
    QtAppWindow::keyPressEvent(e);
}
