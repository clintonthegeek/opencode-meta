// TeamsWidget: main workspace for viewing and editing Teams.
//
// Hosts the left-side Team table, New Team / Delete buttons, and the
// right-side TeamEditorWidget. Phase F4 wires New Team (Ctrl+N,
// name + first-Specialist placeholder) and Delete (confirm dialog).

#pragma once

#include <QWidget>

class QAction;
class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class FilterProxyModel;
class QTableWidget;
class QPushButton;
class StorageManager;
class TeamEditorWidget;

class TeamsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeamsWidget(StorageManager &storageManager,
                         QWidget *parent = nullptr);

public slots:
    // Create a new Team via a name prompt. If the user provides a name,
    // a Team record is persisted with an empty Specialists list (the
    // "first Specialist placeholder"), the table is refreshed, and the
    // new Team is selected in both the table and editor. The widget-
    // scoped Ctrl+N shortcut routes here.
    void createTeam();

    // Delete the currently selected Team after a confirmation dialog.
    // Wired to the Delete button; the same path also handles the
    // Delete key shortcut installed on the table.
    void deleteSelectedTeam();

    // Clone the currently selected stock Team into an editable copy.
    void cloneSelectedStockTeam();

    // Select the Team with the given id in the table, if present.
    void selectTeamById(const QString &teamId);

signals:
    // Emitted after a new Team is created and persisted. Carries the
    // new Team id so hosts (Lab Overview, future auditing) can react.
    void teamCreated(const QString &newTeamId);

    // Emitted after a Team is successfully deleted from storage.
    void teamDeleted(const QString &teamId);

    // F2: forwarded from TeamEditorWidget whenever a Role needs to be
    // authored inline (e.g. Add Specialist with no Role available).
    // Hosts (MainWindow) switch to the Roles tab and call
    // RolesWidget::createRole(proposedName).
    void createRoleRequested(const QString &proposedName);

    // F1: forwarded from TeamEditorWidget whenever the user clicks the
    // footer "Apply Team..." button. Carries the currently loaded Team
    // id so the host (MainWindow) can pre-select it in the existing
    // TeamsDialog apply path.
    void applyTeamRequested(const QString &teamId);

    // Forwarded brief status line requests from the Team editor.
    void statusMessageRequested(const QString &message);

private slots:
    void refreshTeams();
    void onSelectionChanged();
    void onDeleteKeyPressedOnTable();
    void showAboutThisStockItem();
    void updateStockVisibilityStatus();
    // ROADMAP P2-2: case-insensitive dynamic filtering on every column.
    void applyFilter(const QString &text);

private:
    QString selectedTeamId() const;
    bool selectedTeamIsStock() const;
    bool rowIsStock(int row) const;
    void updateActionStates();
    void installShortcuts();

    StorageManager &m_storageManager;

    QTableWidget *m_table = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QCheckBox *m_showStockCheck = nullptr;
    QFrame *m_stockHiddenBanner = nullptr;
    QLabel *m_stockHiddenBannerLabel = nullptr;
    QLabel *m_stockStatusLabel = nullptr;
    FilterProxyModel *m_filterProxy = nullptr;
    QPushButton *m_newButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_cloneButton = nullptr;
    TeamEditorWidget *m_editor = nullptr;
    QAction *m_newAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_aboutStockItemAction = nullptr;
    QAction *m_toggleShowStockAction = nullptr;
};
