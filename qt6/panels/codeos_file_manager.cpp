#include "codeos_file_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QActionGroup>
#include <QMimeData>

static void reloadDirectory(QFileSystemModel *model, const QString &path) {
    model->setRootPath(QString());
    model->setRootPath(path);
}

static bool copyDirectory(const QString &srcDir, const QString &dstDir) {
    QDir src(srcDir);
    QDir dst(dstDir);
    if (!dst.mkpath(dstDir)) return false;
    const QFileInfoList entries = src.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &fi : entries) {
        const QString dstPath = dstDir + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyDirectory(fi.absoluteFilePath(), dstPath)) return false;
        } else if (!QFile::copy(fi.absoluteFilePath(), dstPath)) {
            return false;
        }
    }
    return true;
}

CodeOSFileManager::CodeOSFileManager(QWidget *parent)
    : QWidget(parent), m_currentPath(QDir::homePath()) {
    setupUI();
    setupModels();
    setupToolbar();
    setupContextMenu();
    setRootPath(m_currentPath);
}

CodeOSFileManager::~CodeOSFileManager() {
}

void CodeOSFileManager::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setStyleSheet(
        "QToolBar { background: #2d2d3d; border: none; spacing: 4px; padding: 4px; }"
        "QToolButton { color: #ffffff; background: transparent; border: none; border-radius: 4px; padding: 6px; }"
        "QToolButton:hover { background: #3d3d4d; }"
        "QToolButton:pressed { background: #4d4d5d; }"
        "QLineEdit { background: #1e1e2e; color: #ffffff; border: 1px solid #3d3d4d; border-radius: 4px; padding: 6px; font-size: 13px; }"
    );
    mainLayout->addWidget(m_toolbar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background: #3d3d4d; width: 1px; }"
    );

    m_treeView = new QTreeView(m_splitter);
    m_treeView->setHeaderHidden(true);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(16);
    m_treeView->setStyleSheet(
        "QTreeView { background: #252535; color: #ffffff; border: none; font-size: 13px; }"
        "QTreeView::item { padding: 4px; }"
        "QTreeView::item:selected { background: #3b82f6; }"
        "QTreeView::item:hover { background: #3d3d4d; }"
        "QTreeView::branch:has-children:!has-siblings:closed, "
        "QTreeView::branch:has-children:has-siblings:closed { border-image: none; image: url(:/icons/right_arrow.png); }"
        "QTreeView::branch:open:has-children:!has-siblings, "
        "QTreeView::branch:open:has-children:has-siblings { border-image: none; image: url(:/icons/down_arrow.png); }"
    );

    m_listView = new QListView(m_splitter);
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setIconSize(QSize(48, 48));
    m_listView->setGridSize(QSize(80, 72));
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setMovement(QListView::Static);
    m_listView->setUniformItemSizes(true);
    m_listView->setWordWrap(true);
    m_listView->setStyleSheet(
        "QListView { background: #1e1e2e; color: #ffffff; border: none; font-size: 13px; }"
        "QListView::item { padding: 4px; }"
        "QListView::item:selected { background: #3b82f6; border-radius: 4px; }"
        "QListView::item:hover { background: #3d3d4d; border-radius: 4px; }"
    );

    m_splitter->addWidget(m_treeView);
    m_splitter->addWidget(m_listView);
    m_splitter->setSizes({250, 550});

    mainLayout->addWidget(m_splitter, 1);

    connect(m_treeView, &QTreeView::clicked, this, &CodeOSFileManager::onTreeClicked);
    connect(m_listView, &QListView::doubleClicked, this, &CodeOSFileManager::onListDoubleClicked);
    connect(m_listView, &QListView::customContextMenuRequested, this, &CodeOSFileManager::onContextMenu);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
}

void CodeOSFileManager::setupModels() {
    m_model = new QFileSystemModel(this);
    m_model->setRootPath("");
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files | QDir::Drives);
    m_model->setNameFilterDisables(false);

    m_treeView->setModel(m_model);
    m_treeView->setRootIndex(m_model->index(m_currentPath));

    for (int i = 1; i < m_model->columnCount(); ++i) {
        m_treeView->hideColumn(i);
    }

    m_listView->setModel(m_model);
    m_listView->setRootIndex(m_model->index(m_currentPath));
}

void CodeOSFileManager::setupToolbar() {
    m_upAction = m_toolbar->addAction("↑ Up");
    m_upAction->setToolTip("Go up one level (Alt+Up)");
    m_upAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_upAction, &QAction::triggered, this, &CodeOSFileManager::onUpButtonClicked);

    m_homeAction = m_toolbar->addAction("⌂ Home");
    m_homeAction->setToolTip("Go to home directory (Alt+Home)");
    m_homeAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Home));
    connect(m_homeAction, &QAction::triggered, this, &CodeOSFileManager::onHomeButtonClicked);

    m_refreshAction = m_toolbar->addAction("⟳ Refresh");
    m_refreshAction->setToolTip("Refresh (F5)");
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_refreshAction, &QAction::triggered, this, &CodeOSFileManager::onRefreshButtonClicked);

    m_toolbar->addSeparator();

    m_pathEdit = new QLineEdit(m_toolbar);
    m_pathEdit->setMinimumWidth(300);
    m_pathEdit->setPlaceholderText("Path...");
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &CodeOSFileManager::onPathReturnPressed);
    m_toolbar->addWidget(m_pathEdit);

    m_toolbar->addSeparator();

    QMenu *viewMenu = new QMenu(this);
    m_iconViewAction = viewMenu->addAction("Icons");
    m_iconViewAction->setCheckable(true);
    m_iconViewAction->setChecked(true);
    m_listViewAction = viewMenu->addAction("List");
    m_listViewAction->setCheckable(true);
    m_detailsViewAction = viewMenu->addAction("Details");
    m_detailsViewAction->setCheckable(true);

    QActionGroup *viewGroup = new QActionGroup(this);
    viewGroup->addAction(m_iconViewAction);
    viewGroup->addAction(m_listViewAction);
    viewGroup->addAction(m_detailsViewAction);
    viewGroup->setExclusive(true);

    connect(viewGroup, &QActionGroup::triggered, this, &CodeOSFileManager::onViewModeChanged);

    QAction *viewAction = m_toolbar->addAction("☰ View");
    viewAction->setMenu(viewMenu);

    m_showHiddenAction = m_toolbar->addAction("👁 Show Hidden");
    m_showHiddenAction->setCheckable(true);
    m_showHiddenAction->setToolTip("Toggle hidden files (Ctrl+H)");
    m_showHiddenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(m_showHiddenAction, &QAction::triggered, this, &CodeOSFileManager::onShowHiddenToggled);
}

void CodeOSFileManager::setupContextMenu() {
    m_contextMenu = new QMenu(this);
    m_contextMenu->setStyleSheet(
        "QMenu { background: #2d2d3d; color: #ffffff; border: 1px solid #3d3d4d; padding: 4px; }"
        "QMenu::item { padding: 8px 24px; border-radius: 4px; }"
        "QMenu::item:selected { background: #3b82f6; }"
        "QMenu::separator { height: 1px; background: #3d3d4d; margin: 4px 8px; }"
    );

    m_newFolderAction = m_contextMenu->addAction("📁 New Folder");
    m_newFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(m_newFolderAction, &QAction::triggered, this, &CodeOSFileManager::onNewFolder);

    m_contextMenu->addSeparator();

    m_copyAction = m_contextMenu->addAction("📋 Copy");
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &CodeOSFileManager::onCopy);

    m_pasteAction = m_contextMenu->addAction("📄 Paste");
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, &CodeOSFileManager::onPaste);

    m_contextMenu->addSeparator();

    m_renameAction = m_contextMenu->addAction("✏️ Rename");
    m_renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameAction, &QAction::triggered, this, &CodeOSFileManager::onRename);

    m_deleteAction = m_contextMenu->addAction("🗑 Delete");
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &CodeOSFileManager::onDelete);

    m_contextMenu->addSeparator();

    m_propertiesAction = m_contextMenu->addAction("ℹ️ Properties");
    m_propertiesAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    connect(m_propertiesAction, &QAction::triggered, this, &CodeOSFileManager::onProperties);
}

void CodeOSFileManager::setRootPath(const QString &path) {
    QDir dir(path);
    if (!dir.exists()) return;

    m_currentPath = dir.absolutePath();
    updatePathBar();

    QModelIndex idx = m_model->index(m_currentPath);
    if (idx.isValid()) {
        m_treeView->setRootIndex(idx);
        m_listView->setRootIndex(idx);
        m_treeView->scrollTo(idx);
    }

    emit directoryChanged(m_currentPath);
}

QString CodeOSFileManager::currentPath() const {
    return m_currentPath;
}

void CodeOSFileManager::updatePathBar() {
    m_pathEdit->setText(m_currentPath);
}

void CodeOSFileManager::onTreeClicked(const QModelIndex &index) {
    QString path = m_model->filePath(index);
    if (m_model->isDir(index)) {
        setRootPath(path);
    }
}

void CodeOSFileManager::onListDoubleClicked(const QModelIndex &index) {
    QString path = m_model->filePath(index);
    if (m_model->isDir(index)) {
        setRootPath(path);
    } else {
        emit fileOpened(path);
    }
}

void CodeOSFileManager::onPathReturnPressed() {
    QString path = m_pathEdit->text().trimmed();
    if (!path.isEmpty()) {
        setRootPath(path);
    }
}

void CodeOSFileManager::onUpButtonClicked() {
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        setRootPath(dir.absolutePath());
    }
}

void CodeOSFileManager::onHomeButtonClicked() {
    setRootPath(QDir::homePath());
}

void CodeOSFileManager::onRefreshButtonClicked() {
    reloadDirectory(m_model, m_currentPath);
}

void CodeOSFileManager::onViewModeChanged(QAction *action) {
    if (action == m_iconViewAction) {
        m_listView->setViewMode(QListView::IconMode);
        m_listView->setIconSize(QSize(48, 48));
        m_listView->setGridSize(QSize(80, 72));
    } else if (action == m_listViewAction) {
        m_listView->setViewMode(QListView::ListMode);
    } else if (action == m_detailsViewAction) {
        // Use a custom delegate for details view
        m_listView->setViewMode(QListView::ListMode);
    }
}

void CodeOSFileManager::onShowHiddenToggled(bool checked) {
    QDir::Filters filter = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files | QDir::Drives;
    if (checked) filter |= QDir::Hidden;
    m_model->setFilter(filter);
}

void CodeOSFileManager::onContextMenu(const QPoint &pos) {
    QModelIndex index = m_listView->indexAt(pos);
    if (index.isValid()) {
        m_contextMenu->exec(m_listView->viewport()->mapToGlobal(pos));
    } else {
        m_contextMenu->exec(m_listView->viewport()->mapToGlobal(pos));
    }
}

void CodeOSFileManager::onNewFolder() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (ok && !name.isEmpty()) {
        QDir dir(m_currentPath);
        if (dir.mkdir(name)) {
            reloadDirectory(m_model, dir.absolutePath());
        }
    }
}

void CodeOSFileManager::onDelete() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    QString path = m_model->filePath(index);
    QString name = m_model->fileName(index);

    if (QMessageBox::question(this, "Delete", QString("Delete '%1'?").arg(name),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QFileInfo info(path);
        bool success = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (success) {
            reloadDirectory(m_model, m_currentPath);
        }
    }
}

void CodeOSFileManager::onRename() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    QString oldPath = m_model->filePath(index);
    QString oldName = m_model->fileName(index);

    bool ok;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, oldName, &ok);
    if (ok && !newName.isEmpty() && newName != oldName) {
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;
        if (QFile::rename(oldPath, newPath)) {
            reloadDirectory(m_model, m_currentPath);
        }
    }
}

void CodeOSFileManager::onCopy() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    QString path = m_model->filePath(index);
    QMimeData *mime = new QMimeData();
    mime->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(mime);
}

void CodeOSFileManager::onPaste() {
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile()) {
                QString src = url.toLocalFile();
                QFileInfo srcInfo(src);
                QString dst = m_currentPath + "/" + srcInfo.fileName();
                if (srcInfo.isDir()) {
                    copyDirectory(src, dst);
                } else {
                    QFile::copy(src, dst);
                }
            }
        }
        reloadDirectory(m_model, m_currentPath);
    }
}

void CodeOSFileManager::onProperties() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    QFileInfo info(m_model->filePath(index));
    QString details = QString(
        "Name: %1\n"
        "Path: %2\n"
        "Size: %3\n"
        "Type: %4\n"
        "Permissions: %5\n"
        "Created: %6\n"
        "Modified: %7\n"
        "Accessed: %8"
    ).arg(info.fileName())
     .arg(info.absoluteFilePath())
     .arg(info.isFile() ? QString::number(info.size()) + " bytes" : "—")
     .arg(info.isDir() ? "Directory" : "File")
     .arg(QString::number(info.permissions(), 8).right(4))
     .arg(info.birthTime().toString())
     .arg(info.lastModified().toString())
     .arg(info.lastRead().toString());

    QMessageBox::information(this, "Properties", details);
}