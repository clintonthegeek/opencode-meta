#pragma once

#include <QWidget>

class FilterProxyModel;
class QCheckBox;
class QFrame;
class QLineEdit;
class QLabel;
class QSortFilterProxyModel;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QAction;
class StorageManager;

// Roles view: library of Role definitions (job descriptions + prompts).
//
// This widget is responsible for CRUD operations on Roles via
// StorageManager. It intentionally does not edit any Team-level
// prompt overrides; those belong in the Teams view.
class RolesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RolesWidget(StorageManager &storageManager, QWidget *parent = nullptr);

public slots:
    // F2: opened pre-seeded with `proposedName` as the Role name. Routes
    // through the same RoleEditorDialog flow as the existing Create
    // button; an empty proposedName is acceptable (the editor exposes a
    // name field for the user to fill in).
    void createRole(const QString &proposedName);

private slots:
    void refreshRoles();
    void createRole();
    void editSelectedRole();
    void duplicateSelectedRole();
    void deleteSelectedRole();
    void onSelectionChanged();
    void onItemDoubleClicked(QTableWidgetItem *item);
    void showAboutThisStockItem();
    void installShortcuts();
    void updateStockVisibilityStatus();
    // ROADMAP P2-2: case-insensitive dynamic filtering across all visible
    // columns. Empty text shows every row.
    void applyFilter(const QString &text);

private:
    QString selectedRoleId() const;
    bool selectedRoleIsStock() const;
    bool rowIsStock(int row) const;

    StorageManager &m_storageManager;

    QTableWidget *m_table = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QCheckBox *m_showStockCheck = nullptr;
    QFrame *m_stockHiddenBanner = nullptr;
    QLabel *m_stockHiddenBannerLabel = nullptr;
    QLabel *m_stockStatusLabel = nullptr;
    FilterProxyModel *m_filterProxy = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QAction *m_aboutStockItemAction = nullptr;
    QAction *m_toggleShowStockAction = nullptr;
};
