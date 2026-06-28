#pragma once

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
class QPushButton;
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

private:
    QString selectedRoleId() const;

    StorageManager &m_storageManager;

    QTableWidget *m_table = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
};
