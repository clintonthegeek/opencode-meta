#include "ui/TeamsWidget.h"

#include <QAction>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/TeamEditorWidget.h"

namespace {

// Slugify a user-typed Team name into a filesystem-safe id candidate.
// Falls back to "team" if the result would be empty.
QString slugifyForTeamId(const QString &raw)
{
    QString clean;
    clean.reserve(raw.size());
    for (QChar c : raw) {
        if (c.isLetterOrNumber()) {
            clean.append(c.toLower());
        } else if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            if (!clean.endsWith(QLatin1Char('-'))) {
                clean.append(QLatin1Char('-'));
            }
        }
    }

    while (clean.endsWith(QLatin1Char('-'))) {
        clean.chop(1);
    }

    if (clean.isEmpty()) {
        return QStringLiteral("team");
    }

    return clean;
}

// Produce a unique Team id under the given storage root, starting from
// the slugified base and appending "-N" as needed.
QString generateUniqueTeamId(StorageManager &storage, const QString &base)
{
    QString baseId = slugifyForTeamId(base);
    QString candidate = baseId;
    int suffix = 1;
    while (true) {
        const Team existing = storage.loadTeam(candidate);
        if (existing.id.isEmpty()) {
            return candidate;
        }
        candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix++);
    }
}

} // namespace

TeamsWidget::TeamsWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    auto *layout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Teams: reusable AI coding lineups."), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *contentLayout = new QHBoxLayout();

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << tr("ID")
            << tr("Name")
            << tr("Description")
            << tr("Primary Specialists");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    contentLayout->addWidget(m_table, 1);

    m_editor = new TeamEditorWidget(m_storageManager, this);
    contentLayout->addWidget(m_editor, 1);

    connect(m_editor,
            &TeamEditorWidget::teamVariantCreated,
            this,
            [this](const QString &newTeamId) {
                refreshTeams();
                selectTeamById(newTeamId);
            });
    // F2: forward the editor's createRoleRequested signal so MainWindow
    // can switch to the Roles tab and open Role authoring flow.
    connect(m_editor, &TeamEditorWidget::createRoleRequested,
            this, &TeamsWidget::createRoleRequested);

    // F1: forward the editor's applyTeamRequested signal so MainWindow
    // can open the existing apply dialog (TeamsDialog) with the current
    // Team pre-selected, mirroring the Team menu's "Apply Team..." path.
    connect(m_editor, &TeamEditorWidget::applyTeamRequested,
            this, &TeamsWidget::applyTeamRequested);

    layout->addLayout(contentLayout, 1);

    auto *buttonRow = new QHBoxLayout();
    m_newButton = new QPushButton(tr("New Team"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    buttonRow->addWidget(m_newButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(m_newButton, &QPushButton::clicked,
            this, &TeamsWidget::createTeam);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &TeamsWidget::deleteSelectedTeam);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TeamsWidget::onSelectionChanged);

    installShortcuts();

    refreshTeams();
    updateActionStates();
}

void TeamsWidget::installShortcuts()
{
    // Ctrl+N: create a new Team. Widget-scoped so it does not clash with
    // whatever global shortcuts MainWindow may add later.
    m_newAction = new QAction(tr("New Team"), this);
    m_newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    m_newAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_newAction->setStatusTip(tr("Create a new Team (Ctrl+N)"));
    addAction(m_newAction);
    connect(m_newAction, &QAction::triggered, this, &TeamsWidget::createTeam);

    // Delete key on the table also drives the delete path so users do
    // not need to mouse over to the Delete button.
    m_deleteAction = new QAction(tr("Delete Team"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_deleteAction->setStatusTip(tr("Delete the selected Team"));
    addAction(m_deleteAction);
    connect(m_deleteAction, &QAction::triggered,
            this, &TeamsWidget::onDeleteKeyPressedOnTable);
}

void TeamsWidget::refreshTeams()
{
    const QList<Team> teams = m_storageManager.listTeams();

    m_table->setRowCount(teams.size());

    for (int row = 0; row < teams.size(); ++row) {
        const Team &team = teams.at(row);

        auto *idItem = new QTableWidgetItem(team.id);
        idItem->setData(Qt::UserRole, team.id);
        m_table->setItem(row, 0, idItem);

        auto *nameItem = new QTableWidgetItem(team.name);
        m_table->setItem(row, 1, nameItem);

        auto *descItem = new QTableWidgetItem(team.description);
        m_table->setItem(row, 2, descItem);

        const int primaryCount = team.primarySpecialistIds.size();
        auto *primaryItem = new QTableWidgetItem(QString::number(primaryCount));
        m_table->setItem(row, 3, primaryItem);
    }

    m_table->resizeColumnsToContents();
}

QString TeamsWidget::selectedTeamId() const
{
    if (!m_table) {
        return QString();
    }

    const int row = m_table->currentRow();
    if (row < 0) {
        return QString();
    }

    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem) {
        return QString();
    }

    const QVariant userData = idItem->data(Qt::UserRole);
    const QString id = userData.isValid() ? userData.toString() : idItem->text();
    return id.trimmed();
}

void TeamsWidget::createTeam()
{
    bool ok = false;
    const QString rawName = QInputDialog::getText(this,
                                                 tr("New Team"),
                                                 tr("Name for the new Team:"),
                                                 QLineEdit::Normal,
                                                 QString(),
                                                 &ok);
    if (!ok) {
        return;
    }

    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this,
                                 tr("New Team"),
                                 tr("Team name cannot be empty."));
        return;
    }

    Team team;
    team.id = generateUniqueTeamId(m_storageManager, name);
    team.name = name;
    team.description = QStringLiteral("Created on %1")
                           .arg(QDateTime::currentDateTimeUtc()
                                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    team.version = QStringLiteral("0.1.0");

    if (!m_storageManager.saveTeam(team)) {
        QMessageBox::warning(this,
                             tr("New Team"),
                             tr("Failed to save the new Team to disk."));
        return;
    }

    refreshTeams();
    selectTeamById(team.id);
    if (m_editor) {
        m_editor->setTeamId(team.id);
    }

    emit teamCreated(team.id);
}

void TeamsWidget::deleteSelectedTeam()
{
    const QString teamId = selectedTeamId();
    if (teamId.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Delete Team"),
                                 tr("Select a Team first."));
        return;
    }

    const Team team = m_storageManager.loadTeam(teamId);
    if (team.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Delete Team"),
                             tr("Could not load the selected Team; nothing to delete."));
        return;
    }

    QString summary = QStringLiteral("Delete Team '%1'?")
                          .arg(team.name.isEmpty() ? team.id : team.name);
    const int specialistCount = team.specialists.size();
    if (specialistCount > 0) {
        summary += QStringLiteral("\n\nThis Team has %1 Specialist binding(s). "
                                  "Deleting the Team does NOT remove the underlying "
                                  "Specialist records; they can still be reused by "
                                  "other Teams or by creating a new Team.")
                       .arg(specialistCount);
    } else {
        summary += QStringLiteral("\n\nThis Team has no Specialists yet.");
    }

    const auto reply = QMessageBox::question(
        this,
        tr("Delete Team"),
        summary,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!m_storageManager.deleteTeam(teamId)) {
        QMessageBox::warning(this,
                             tr("Delete Team"),
                             tr("Failed to remove the Team file from disk."));
        return;
    }

    if (m_editor) {
        m_editor->setTeamId(QString());
    }

    refreshTeams();
    updateActionStates();

    emit teamDeleted(teamId);
}

void TeamsWidget::onDeleteKeyPressedOnTable()
{
    // Only act on Delete when the table actually holds a selection, so
    // navigation keys are not consumed while focus is on the empty area.
    if (m_table && m_table->currentRow() >= 0) {
        deleteSelectedTeam();
    }
}

void TeamsWidget::selectTeamById(const QString &teamId)
{
    if (!m_table || teamId.isEmpty()) {
        return;
    }

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (!idItem) {
            continue;
        }

        const QVariant userData = idItem->data(Qt::UserRole);
        const QString id = userData.isValid() ? userData.toString() : idItem->text();
        if (id == teamId) {
            m_table->setCurrentCell(row, 0);
            return;
        }
    }
}

void TeamsWidget::onSelectionChanged()
{
    updateActionStates();
}

void TeamsWidget::updateActionStates()
{
    const bool hasSelection = (m_table && m_table->currentRow() >= 0);
    if (m_deleteButton) {
        m_deleteButton->setEnabled(hasSelection);
    }
    if (m_deleteAction) {
        m_deleteAction->setEnabled(hasSelection);
    }

    if (m_editor) {
        const QString id = hasSelection ? selectedTeamId() : QString();
        m_editor->setTeamId(id);
    }
}
