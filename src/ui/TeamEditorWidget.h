// TeamEditorWidget: core Team/Specialist editor surface.
//
// Stage 2 implementation focuses on rendering the Specialists table and
// supporting primary toggles. Add/remove flows, model picker, prompt
// overrides, and the lightweight revert/dirty affordance are layered on
// in later stages.

#pragma once

#include <QWidget>
#include <QString>

#include "models/Team.h"
#include "models/ModelInfo.h" // for ModelsCache

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class StorageManager;

class TeamEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeamEditorWidget(StorageManager &storageManager,
                              QWidget *parent = nullptr);

    // F1: Public getter for the currently loaded Team id. Returns the
    // empty string when no Team is loaded (the placeholder state). Used
    // by the "Apply Team..." footer button to know which Team to emit on
    // click without leaking the private m_team member.
    QString teamId() const;

signals:
    // F2: Emitted when the user chooses a flow that requires authoring a
    // new Role (e.g. an Add Specialist step with no matching Role). Carries
    // a proposed name seeded from the requester (empty is acceptable --
    // the Role editor exposes a name field).
    void createRoleRequested(const QString &proposedName);

    // F1: Emitted when the user clicks the footer "Apply Team..." button.
    // Carries the currently loaded Team id (empty if no Team is loaded,
    // in which case the host can decide whether to surface the dialog
    // empty or fall back to its own selection).
    void applyTeamRequested(const QString &teamId);

    // Emitted after "Duplicate as Variant" successfully creates a new
    // Team that is a clone of the currently displayed one. Hosts can use
    // this to refresh their list and switch to the new variant.
    void teamVariantCreated(const QString &newTeamId);

    // Emitted after a Team save completes successfully.
    void teamUpdated(const QString &teamId);

    // Emitted when a Specialist binding changes and the Team save
    // succeeds.
    void specialistUpdated(const QString &specialistId);

    // Emitted when the current Team is discarded/reverted from storage.
    void teamReverted(const QString &teamId,
                      const QString &snapshotId);

    // Emitted when the widget wants to surface a brief success message
    // through the host status bar.
    void statusMessageRequested(const QString &message);

public slots:
    // Load and display the Team with the given id. An empty id clears
    // the editor to its placeholder state.
    void setTeamId(const QString &teamId);

    // Mirror the TeamsWidget stock toggle so stock Specialists can be
    // hidden consistently with stock Teams/Roles.
    void setShowStock(bool showStock);

private slots:
    // React to user toggling the primary checkbox column.
    void onPrimaryItemChanged(QTableWidgetItem *item);

    // Entry point for the "Add Specialist" flow (Stage 3): pick Role,
    // bind model via ModelsBrowserWidget picker, optional prompt override.
    void onAddSpecialist();

    void onRemoveSpecialist();
    void onMoveUp();
    void onMoveDown();
    void onDuplicateVariant();
    void onResetToStock();
    void onCompare();
    void onApplyTeam(); // F1 footer "Apply Team..." click handler
    void onRevertChanges();
    void reloadTeamFromStorage();

private:
    void refreshSpecialistsTable();
    QString formatModelDisplay(const QString &modelId) const;
    QString formatCostBadge(const QString &modelId) const;
    bool rowIsStock(int row) const;
    void updateActionButtons();
    bool hasDirtyChanges() const;
    int currentSpecialistRow() const;
    QString specialistIdAtRow(int row) const;

    StorageManager &m_storageManager;

    Team m_team;              // Currently loaded Team (empty when none selected)
    ModelsCache m_modelsCache; // Snapshot of models cache for model/cost display

    QTableWidget *m_table = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_dirtyIndicator = nullptr;

    // Button row under the Specialists table. These actions are wired
    // incrementally as the Team editor gains more save flows.
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_moveUpButton = nullptr;
    QPushButton *m_moveDownButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_compareButton = nullptr;
    QPushButton *m_revertButton = nullptr;
    QPushButton *m_applyButton = nullptr; // F1 footer "Apply Team..."

    bool m_showStockSpecialists = false;
    bool m_updatingTable = false; // guard to avoid feedback loops while populating
};
