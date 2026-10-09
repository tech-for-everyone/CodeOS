#ifndef CODEOS_FILE_MANAGER_H
#define CODEOS_FILE_MANAGER_H

#include <QWidget>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QSplitter>
#include <QToolBar>
#include <QLineEdit>
#include <QAction>
#include <QMenu>
#include <QFileInfo>

class CodeOSFileManager : public QWidget {
    Q_OBJECT
public:
    explicit CodeOSFileManager(QWidget *parent = nullptr);
    ~CodeOSFileManager();

    void setRootPath(const QString &path);
    QString currentPath() const;

signals:
    void fileOpened(const QString &path);
    void directoryChanged(const QString &path);

private slots:
    void onTreeClicked(const QModelIndex &index);
    void onListDoubleClicked(const QModelIndex &index);
    void onPathReturnPressed();
    void onUpButtonClicked();
    void onHomeButtonClicked();
    void onRefreshButtonClicked();
    void onViewModeChanged(QAction *action);
    void onShowHiddenToggled(bool checked);
    void onContextMenu(const QPoint &pos);
    void onNewFolder();
    void onDelete();
    void onRename();
    void onCopy();
    void onPaste();
    void onProperties();

private:
    void setupUI();
    void setupModels();
    void setupToolbar();
    void setupContextMenu();
    void updatePathBar();

    QSplitter *m_splitter;
    QTreeView *m_treeView;
    QListView *m_listView;
    QFileSystemModel *m_model;
    QToolBar *m_toolbar;
    QLineEdit *m_pathEdit;
    QAction *m_upAction;
    QAction *m_homeAction;
    QAction *m_refreshAction;
    QAction *m_iconViewAction;
    QAction *m_listViewAction;
    QAction *m_detailsViewAction;
    QAction *m_showHiddenAction;
    QMenu *m_contextMenu;
    QString m_currentPath;
    QAction *m_newFolderAction;
    QAction *m_deleteAction;
    QAction *m_renameAction;
    QAction *m_copyAction;
    QAction *m_pasteAction;
    QAction *m_propertiesAction;
};

#endif // CODEOS_FILE_MANAGER_H