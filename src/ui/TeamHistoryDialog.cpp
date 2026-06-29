// TeamHistoryDialog.cpp -- ROADMAP P3-3 history + revert dialog.
//
// Self-contained QDialog that lists a Team's snapshot history and
// allows the user to revert the live Team to any prior snapshot
// either via direct StorageManager call (the default) or with a
// confirmation dialog before applying.
//
// We deliberately do NOT re-implement snapshot bookkeeping here:
// every revert flows through StorageManager::revertTeamToVersion,
// which is independently tested and also snapshots the to-be-clobbered
// Team first so reverting is itself reversible.

#include "ui/TeamHistoryDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include "storage/StorageManager.h"

namespace {

constexpr const char *kObjHeaderLabel    = "teamHistory.headerLabel";
constexpr const char *kObjEmptyLabel     = "teamHistory.emptyLabel";
constexpr const char *kObjTable          = "teamHistory.snapshotsTable";
constexpr const char *kObjDetailsEdit    = "teamHistory.detailsEdit";
constexpr const char *kObjRefreshButton  = "teamHistory.refreshButton";
constexpr const char *kObjRevertButton   = "teamHistory.revertButton";

// Stable object names make every widget reachable from tests via
// findChild<T*>(name) without leaking private pointers.
QString makeObj(const char *base) { return QString::fromLatin1(base); }

QString formatTimestamp(const QDateTime &dt)
{
    if (!dt.isValid()) {
        return QStringLiteral("(unknown)");
    }
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

TeamHistoryDialog::TeamHistoryDialog(const QString &teamId,
                                     StorageManager &storageManager,
                                     QWidget *parent)
    : QDialog(parent)
    , m_teamId(teamId)
    , m_storageManager(storageManager)
    , m_team(m_storageManager.loadTeam(teamId))
{
    setObjectName(QStringLiteral("teamHistoryDialog"));
    setWindowTitle(tr("Team History"));
    setModal(true);
    resize(960, 540);

    buildUi();
    refreshSnapshots();

    updateRevertEnabled();
}

void TeamHistoryDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    const QString headerName = m_team.name.isEmpty() ? m_team.id : m_team.name;
    m_headerLabel = new QLabel(
        tr("Team History: %1 (%2)").arg(headerName, m_team.id), this);
    m_headerLabel->setObjectName(makeObj(kObjHeaderLabel));
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setWordWrap(true);
    root->addWidget(m_headerLabel);

    auto *bodyLayout = new QHBoxLayout();
    root->addLayout(bodyLayout, 1);

    // Snapshot list on the left.
    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(leftPanel);
    m_table->setObjectName(makeObj(kObjTable));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({
        tr("Timestamp"),
        tr("Version"),
        tr("Reason"),
        tr("Specialists"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    leftLayout->addWidget(m_table, 1);

    m_emptyLabel = new QLabel(
        tr("No history yet. Edit or revert this Team to start a chain of snapshots."),
        leftPanel);
    m_emptyLabel->setObjectName(makeObj(kObjEmptyLabel));
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(m_emptyLabel);

    bodyLayout->addWidget(leftPanel, 1);

    // Details (right pane): rich-ish text preview of the selected
    // snapshot, including a JSON excerpt so users can see the
    // diff without leaving the dialog.
    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *detailsLabel = new QLabel(tr("Snapshot details:"), rightPanel);
    rightLayout->addWidget(detailsLabel);

    m_detailsEdit = new QPlainTextEdit(rightPanel);
    m_detailsEdit->setObjectName(makeObj(kObjDetailsEdit));
    m_detailsEdit->setReadOnly(true);
    m_detailsEdit->setPlaceholderText(tr(
        "Select a snapshot on the left to see its metadata and a "
        "preview of the Team JSON captured at that point."));
    rightLayout->addWidget(m_detailsEdit, 1);

    bodyLayout->addWidget(rightPanel, 1);

    // Action row.
    auto *buttonRow = new QHBoxLayout();
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    m_refreshButton->setObjectName(makeObj(kObjRefreshButton));
    m_refreshButton->setToolTip(tr("Re-read the snapshot list from disk."));
    m_refreshButton->setStatusTip(tr("Re-read the snapshot list from disk"));
    m_revertButton = new QPushButton(tr("Revert to Selected..."), this);
    m_revertButton->setObjectName(makeObj(kObjRevertButton));
    m_revertButton->setToolTip(tr(
        "Restore the selected snapshot as the live Team file. The "
        "current Team is itself recorded as a snapshot before the "
        "restore, so reverting is reversible."));
    m_revertButton->setStatusTip(tr("Restore the selected snapshot as the live Team"));
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addStretch(1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonBox->addButton(m_revertButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(m_refreshButton, QDialogButtonBox::ActionRole);
    buttonRow->addWidget(buttonBox);

    root->addLayout(buttonRow);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TeamHistoryDialog::onSnapshotSelectionChanged);
    connect(m_refreshButton, &QPushButton::clicked,
            this, &TeamHistoryDialog::onRefreshClicked);
    connect(m_revertButton, &QPushButton::clicked,
            this, &TeamHistoryDialog::onRevertClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void TeamHistoryDialog::refreshSnapshots()
{
    m_versions = m_storageManager.listTeamVersions(m_teamId);

    m_table->setRowCount(m_versions.size());

    int previousRow = (m_table->currentRow() >= 0) ? m_table->currentRow() : 0;

    for (int row = 0; row < m_versions.size(); ++row) {
        const TeamVersion &v = m_versions.at(row);

        auto *ts = new QTableWidgetItem(formatTimestamp(v.timestampUtc));
        ts->setData(Qt::UserRole, v.id);
        ts->setToolTip(v.id);
        m_table->setItem(row, 0, ts);

        auto *version = new QTableWidgetItem(v.team.version.isEmpty()
                                                 ? QStringLiteral("-")
                                                 : v.team.version);
        version->setToolTip(v.team.name);
        m_table->setItem(row, 1, version);

        auto *reason = new QTableWidgetItem(v.reason);
        reason->setToolTip(v.note);
        m_table->setItem(row, 2, reason);

        const int specCount = v.team.specialists.size();
        auto *specs = new QTableWidgetItem(QString::number(specCount));
        specs->setToolTip(tr("%1 Specialist binding(s)").arg(specCount));
        m_table->setItem(row, 3, specs);
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);

    const bool empty = m_versions.isEmpty();
    m_emptyLabel->setVisible(empty);
    m_table->setVisible(!empty);

    if (!empty) {
        const int clamped = qBound(0, previousRow, m_versions.size() - 1);
        m_table->blockSignals(true);
        m_table->setCurrentCell(clamped, 0);
        m_table->blockSignals(false);
        populateDetailsRow(clamped);
    } else {
        m_detailsEdit->clear();
        m_detailsEdit->setPlaceholderText(tr(
            "No snapshots yet for this Team. Make an edit (or "
            "duplicate as a variant) to create a history chain."));
    }

    updateRevertEnabled();
}

void TeamHistoryDialog::onSnapshotSelectionChanged()
{
    const int row = m_table ? m_table->currentRow() : -1;
    populateDetailsRow(row);
    updateRevertEnabled();
}

void TeamHistoryDialog::populateDetailsRow(int row)
{
    if (row < 0 || row >= m_versions.size()) {
        m_detailsEdit->clear();
        return;
    }

    const TeamVersion &v = m_versions.at(row);

    QString summary;
    QTextStream ts(&summary);
    ts << "Snapshot id: " << v.id << "\n";
    ts << "Team id:     " << v.teamId << "\n";
    ts << "Timestamp:   " << (v.timestampUtc.isValid()
                                  ? v.timestampUtc.toUTC().toString(Qt::ISODateWithMs)
                                  : QStringLiteral("(unknown)"))
       << "\n";
    if (!v.parentVersionId.isEmpty()) {
        ts << "Parent:      " << v.parentVersionId << "\n";
    }
    ts << "Reason:      " << (v.reason.isEmpty() ? QStringLiteral("(unspecified)") : v.reason) << "\n";
    if (!v.note.isEmpty()) {
        ts << "Note:        " << v.note << "\n";
    }
    ts << "\n";
    ts << "Team name:   " << (v.team.name.isEmpty() ? QStringLiteral("(no name)") : v.team.name) << "\n";
    ts << "Team version:" << (v.team.version.isEmpty() ? QStringLiteral("(unset)") : v.team.version) << "\n";
    ts << "Specialists: " << v.team.specialists.size() << "\n";
    ts << "Primaries:   " << v.team.primarySpecialistIds.size() << "\n";

    ts << "\n--- Team JSON ---\n";
    ts << QString::fromUtf8(QJsonDocument(v.team.toJson())
                                .toJson(QJsonDocument::Indented));

    m_detailsEdit->setPlainText(summary);
}

void TeamHistoryDialog::onRefreshClicked()
{
    refreshSnapshots();
}

void TeamHistoryDialog::onRevertClicked()
{
    const int row = m_table ? m_table->currentRow() : -1;
    if (row < 0 || row >= m_versions.size()) {
        return;
    }

    const TeamVersion target = m_versions.at(row);

    const QString headerName = m_team.name.isEmpty() ? m_team.id : m_team.name;
    const QString summary = tr(
        "Revert '%1' to its snapshot from %2?\n\n"
        "Current specialists: %3\n"
        "Snapshot specialists: %4\n\n"
        "The current Team is itself recorded as a snapshot before the "
        "restore, so this action is reversible.")
        .arg(headerName,
             formatTimestamp(target.timestampUtc),
             QString::number(m_team.specialists.size()),
             QString::number(target.team.specialists.size()));

    const auto reply = QMessageBox::question(
        this,
        tr("Revert Team"),
        summary,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    applyRevert(row);
}

bool TeamHistoryDialog::applyRevert(int row)
{
    if (row < 0 || row >= m_versions.size()) {
        return false;
    }

    const TeamVersion target = m_versions.at(row);
    const StorageManager::RevertResult result =
        m_storageManager.revertTeamToVersion(m_teamId, target.id);
    if (!result.ok) {
        QMessageBox::warning(this,
                             tr("Revert Team"),
                             tr("Failed to revert: %1").arg(result.errorString));
        return false;
    }

    m_lastRevertedFromId = target.id;

    const Team updated = m_storageManager.loadTeam(m_teamId);
    m_team = updated;

    m_lastRevertedSummary = tr("Reverted to snapshot from %1 (%2 specialists)")
                                .arg(formatTimestamp(target.timestampUtc),
                                     QString::number(target.team.specialists.size()));

    emit teamReverted(m_teamId, target.id);

    // Refresh the dialog state: the user just produced a new head
    // snapshot, so the list will have one more entry.
    refreshSnapshots();
    return true;
}

void TeamHistoryDialog::updateRevertEnabled()
{
    if (!m_revertButton) {
        return;
    }
    const bool hasSnapshot = m_table
        && m_table->currentRow() >= 0
        && !m_versions.isEmpty();
    m_revertButton->setEnabled(hasSnapshot);
}
