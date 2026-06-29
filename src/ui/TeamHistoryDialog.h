// TeamHistoryDialog.h
//
// ROADMAP P3-3: history view + revert action for a single Team.
//
// The dialog is self-contained: it accepts a Team id + a
// StorageManager reference, walks the team's snapshot list
// (newest-first), and lets the user rewind the live Team to any
// previous state. The dialog itself does NOT touch the live
// `teams/<id>.json` directly — it forwards revert requests to the
// StorageManager API so the recording/snapshotting pipeline stays
// in one place. The host (TeamEditorWidget) is responsible for
// loading the Team back into its editor surface after a revert.
//
// Layout:
//
//   +------------------------------------------------------+
//   |  Team History: <team-name> (<team-id>)               |
//   +------------------------+-----------------------------+
//   |  Snapshot table        |  Selected snapshot details  |
//   |  (timestamp, version,  |  (id, parent, timestamp,    |
//   |   reason, specialists) |   reason/note, JSON excerpt)|
//   +------------------------+-----------------------------+
//   |                                       Close  Revert |
//   +------------------------------------------------------+

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "models/Team.h"
#include "models/TeamVersion.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class StorageManager;

class TeamHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TeamHistoryDialog(const QString &teamId,
                               StorageManager &storageManager,
                               QWidget *parent = nullptr);
    ~TeamHistoryDialog() override = default;

    QString teamId() const { return m_teamId; }

    // Public read accessors for tests / power tools.
    QTableWidget    *snapshotTable()  const { return m_table; }
    QPlainTextEdit  *detailsEdit()    const { return m_detailsEdit; }
    QLabel          *headerLabel()    const { return m_headerLabel; }
    QPushButton     *revertButton()   const { return m_revertButton; }
    QPushButton     *refreshButton()  const { return m_refreshButton; }

    // Empty when the dialog has just opened; populated after a
    // successful revert (so callers can show before/after in feedback).
    QString lastRevertedFromSnapshotId() const { return m_lastRevertedFromId; }
    QString lastRevertedToLiveSummary()  const { return m_lastRevertedSummary; }

    // Reload the snapshot list from storage. Hosts can call this after
    // external state changes (e.g. a teammate just saved a snapshot)
    // without the user having to press the in-dialog refresh button.
    void refreshSnapshots();

    // Apply a revert for the snapshot at `row` without prompting the
    // user. Returns true on success. The on-screen slot wraps this
    // with a QMessageBox::question(); tests call it directly so the
    // headless harness does not have to drive a modal dialog.
    bool applyRevert(int row);

signals:
    // Fired AFTER revertTeamToVersion() succeeded and the storage
    // now has the new live Team file. Hosts reload the editor from
    // teamId() so the on-screen Team reflects the snapshot we just
    // rewound to.
    void teamReverted(const QString &teamId,
                      const QString &snapshotId);

private slots:
    void onSnapshotSelectionChanged();
    void onRevertClicked();
    void onRefreshClicked();

private:
    void buildUi();
    void populateDetailsRow(int row);
    void updateRevertEnabled();

    QString                m_teamId;
    StorageManager        &m_storageManager;
    Team                   m_team;
    QList<TeamVersion>     m_versions; // newest-first

    QLabel          *m_headerLabel   = nullptr;
    QLabel          *m_emptyLabel    = nullptr;
    QTableWidget    *m_table         = nullptr;
    QPlainTextEdit  *m_detailsEdit   = nullptr;
    QPushButton     *m_refreshButton = nullptr;
    QPushButton     *m_revertButton  = nullptr;

    QString m_lastRevertedFromId;
    QString m_lastRevertedSummary;
};
