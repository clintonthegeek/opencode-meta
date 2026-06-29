#include "TeamsDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "models/Team.h"
#include "storage/StorageManager.h"

namespace {

QLabel *makeStockBadge(const QString &text, QWidget *parent)
{
    auto *badge = new QLabel(text, parent);
    badge->setObjectName(QStringLiteral("stockBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setToolTip(QObject::tr("Stock seed item - cannot be modified or deleted. Created automatically for new users."));
    badge->setStyleSheet(QStringLiteral(
        "QLabel#stockBadge {"
        " background-color: #eef2ff;"
        " color: #4338ca;"
        " border: 1px solid #c7d2fe;"
        " border-radius: 8px;"
        " padding: 1px 6px;"
        " font-size: 10px;"
        " font-weight: 600;"
        " }"));
    return badge;
}

QLabel *makeDefaultAgentBadge(QWidget *parent)
{
    auto *badge = new QLabel(QStringLiteral("★"), parent);
    badge->setObjectName(QStringLiteral("defaultAgentBadge"));
    badge->setToolTip(QObject::tr("Default agent"));
    badge->setStyleSheet(QStringLiteral(
        "QLabel#defaultAgentBadge { color: #d97706; font-weight: 700; }"));
    return badge;
}

QWidget *makeTeamNameCell(const QString &name, bool stockTeam, bool defaultAgent, QWidget *parent)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *nameLabel = new QLabel(name, cell);
    layout->addWidget(nameLabel);

    if (stockTeam) {
        layout->addWidget(makeStockBadge(QObject::tr("Stock"), cell));
    }

    if (defaultAgent) {
        layout->addWidget(makeDefaultAgentBadge(cell));
    }

    layout->addStretch(1);
    return cell;
}

} // namespace

TeamsDialog::TeamsDialog(StorageManager &storageManager, QWidget *parent)
    : QDialog(parent)
    , m_storageManager(storageManager)
{
    setWindowTitle(tr("Apply Team to Project"));
    resize(700, 400);

    auto *mainLayout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Select a Team to apply to a project."), this);
    label->setWordWrap(true);
    mainLayout->addWidget(label);

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
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_table, 1);

    auto *buttonBox = new QDialogButtonBox(this);
    m_applyButton = new QPushButton(tr("Apply Selected Team to Project..."), this);
    buttonBox->addButton(m_applyButton, QDialogButtonBox::AcceptRole);

    auto *closeButton = buttonBox->addButton(QDialogButtonBox::Close);
    closeButton->setText(tr("Close"));

    mainLayout->addWidget(buttonBox);

    connect(m_applyButton, &QPushButton::clicked,
            this, &TeamsDialog::onApplyClicked);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &TeamsDialog::reject);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TeamsDialog::onSelectionChanged);

    refreshTeams();
    onSelectionChanged();
}

void TeamsDialog::refreshTeams()
{
    const QList<Team> teams = m_storageManager.listTeams();

    m_table->setRowCount(teams.size());

    int preselectedRow = -1;

    for (int row = 0; row < teams.size(); ++row) {
        const Team &team = teams.at(row);
        const bool stockTeam = m_storageManager.isStockTeam(team);
        const QString defaultAgentId = team.metadata.value(QStringLiteral("default_agent")).toString();
        const bool defaultAgent = !defaultAgentId.isEmpty();

        auto *idItem = new QTableWidgetItem(team.id);
        // Store id redundantly in user data in case the text changes later.
        idItem->setData(Qt::UserRole, team.id);
        idItem->setData(Qt::UserRole + 1, stockTeam);
        m_table->setItem(row, 0, idItem);

        auto *nameItem = new QTableWidgetItem(team.name);
        if (stockTeam || defaultAgent) {
            const QString displayName = team.name.isEmpty() ? team.id : team.name;
            nameItem->setText(displayName);
            QString tooltip;
            if (stockTeam) {
                tooltip = tr("Stock team");
            }
            if (defaultAgent) {
                tooltip += tooltip.isEmpty()
                    ? tr("Default agent: %1").arg(defaultAgentId)
                    : tr("\nDefault agent: %1").arg(defaultAgentId);
            }
            if (!tooltip.isEmpty()) {
                nameItem->setToolTip(tooltip);
            }
            m_table->setCellWidget(row,
                                   1,
                                   makeTeamNameCell(displayName,
                                                    stockTeam,
                                                    defaultAgent,
                                                    m_table));
        }
        m_table->setItem(row, 1, nameItem);

        auto *descItem = new QTableWidgetItem(team.description);
        m_table->setItem(row, 2, descItem);

        const int primaryCount = team.primarySpecialistIds.size();
        auto *primaryItem = new QTableWidgetItem(QString::number(primaryCount));
        m_table->setItem(row, 3, primaryItem);

        if (!m_pendingPreselectedTeamId.isEmpty() && team.id == m_pendingPreselectedTeamId) {
            preselectedRow = row;
        }
    }

    m_table->resizeColumnsToContents();

    // Apply any pending pre-selection now that the rows exist. Doing this
    // here (instead of in setPreselectedTeamId) keeps the no-arg path
    // working unchanged even when the dialog is reused across calls.
    if (preselectedRow >= 0) {
        m_table->setCurrentCell(preselectedRow, 0);
    }
    m_pendingPreselectedTeamId.clear();
}

void TeamsDialog::setPreselectedTeamId(const QString &teamId)
{
    if (teamId.isEmpty()) {
        return;
    }
    m_pendingPreselectedTeamId = teamId;

    // If the table is already populated (host called us after construction),
    // try to apply the pre-selection immediately. Otherwise refreshTeams()
    // picks it up on construction.
    if (!m_table) {
        return;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (!idItem) {
            continue;
        }
        const QString id = idItem->data(Qt::UserRole).toString().isEmpty()
                               ? idItem->text()
                               : idItem->data(Qt::UserRole).toString();
        if (id == teamId) {
            m_table->setCurrentCell(row, 0);
            m_pendingPreselectedTeamId.clear();
            return;
        }
    }
}

void TeamsDialog::setProjectPath(const QString &path)
{
    // F5: stash the project directory that should be used when the user
    // (or the test harness) clicks "Apply...". Empty path disables the
    // override so the native QFileDialog::getExistingDirectory() is
    // popped, matching the existing flow.
    m_pendingProjectPath = path;
}

void TeamsDialog::onSelectionChanged()
{
    const bool hasSelection = m_table && m_table->currentRow() >= 0;
    if (m_applyButton) {
        m_applyButton->setEnabled(hasSelection);
    }
}

void TeamsDialog::onApplyClicked()
{
    if (!m_table) {
        return;
    }

    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }

    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem) {
        return;
    }

    const QString teamId = idItem->data(Qt::UserRole).toString().isEmpty()
                               ? idItem->text()
                               : idItem->data(Qt::UserRole).toString();
    if (teamId.isEmpty()) {
        QMessageBox::warning(this, tr("Apply Team"), tr("Selected team has no id."));
        return;
    }

    // F5: if the host pre-stashed a project path (typically the cross-
    // view smoke test harness), use it directly. Empty falls through to
    // the native QFileDialog so the existing UX is unchanged.
    QString projectPath = m_pendingProjectPath;
    if (projectPath.isEmpty()) {
        projectPath = QFileDialog::getExistingDirectory(
            this,
            tr("Select Project Directory"),
            QDir::homePath());
    }

    if (projectPath.isEmpty()) {
        return; // User cancelled (or test passed an empty path).
    }

    const bool ok = m_storageManager.applyTeamToProject(projectPath, teamId);
    if (!ok) {
        QMessageBox::warning(this,
                             tr("Apply Team"),
                             tr("Failed to apply team '%1' to project:\n%2")
                                 .arg(teamId,
                                      QDir::toNativeSeparators(projectPath)));
        return;
    }

    const QDir projectDir(projectPath);
    const QString configPath = projectDir.filePath(QStringLiteral("opencode.json"));

    QMessageBox::information(this,
                             tr("Apply Team"),
                             tr("Team '%1' applied successfully.\n\nConfig written to:\n%2")
                                 .arg(teamId,
                                      QDir::toNativeSeparators(configPath)));
}
