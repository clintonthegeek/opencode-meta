#include "ui/TrialsWidget.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "models/Trial.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/FilterBar.h"
#include "ui/TrialCompareDialog.h"

namespace {

QString summarizeRatings(const QJsonObject &ratings)
{
    if (ratings.isEmpty()) {
        return QObject::tr("n/a");
    }

    QStringList parts;
    const auto keys = ratings.keys();
    for (int i = 0; i < keys.size() && i < 3; ++i) {
        const QString &key = keys.at(i);
        const QJsonValue value = ratings.value(key);
        if (value.isDouble()) {
            parts.append(QStringLiteral("%1=%2")
                             .arg(key)
                             .arg(value.toDouble()));
        } else if (value.isString()) {
            parts.append(QStringLiteral("%1=%2")
                             .arg(key, value.toString()));
        }
    }

    if (parts.isEmpty()) {
        return QObject::tr("n/a");
    }

    if (keys.size() > parts.size()) {
        parts.append(QObject::tr("…"));
    }

    return parts.join(QStringLiteral(", "));
}

QString notesSnippet(const QString &notes)
{
    const int maxLen = 120;
    if (notes.length() <= maxLen) {
        return notes;
    }
    QString truncated = notes.left(maxLen).trimmed();
    truncated.append(QStringLiteral("…"));
    return truncated;
}

} // namespace

TrialsWidget::TrialsWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    auto *layout = new QVBoxLayout(this);

    auto *introLabel = new QLabel(
        tr("Trials history: compare Teams across projects and promote winners."),
        this);
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    m_placeholderLabel = new QLabel(
        tr("No Trials have been recorded yet.\n"
           "Apply a Team to a project to create Trials; they will appear here as a timeline."),
        this);
    m_placeholderLabel->setWordWrap(true);
    layout->addWidget(m_placeholderLabel);

    // ROADMAP P2-2: filter bar sits between the placeholder and the
    // trials table. Drives a QSortFilterProxyModel that hides rows the
    // user is not looking for.
    auto *filterBar = new FilterBar(tr("Filter trials..."), this);
    m_filterEdit = filterBar->findChild<QLineEdit *>();
    layout->addWidget(filterBar);
    connect(filterBar, &FilterBar::filterChanged,
            this, &TrialsWidget::applyFilter);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    QStringList headers;
    headers << tr("Date")
            << tr("Team")
            << tr("Project")
            << tr("Ratings")
            << tr("Notes");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table, 1);

    // FilterProxyModel drives row visibility; the QTableWidget remains
    // the view so existing item()/selection accessors stay intact (the
    // cross-view smoke test uses both).
    m_filterProxy = new FilterProxyModel(this);
    m_filterProxy->setSourceModel(m_table->model());
    m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filterProxy->setFilterKeyColumn(-1); // every column

    auto *buttonRow = new QHBoxLayout();
    m_compareButton = new QPushButton(tr("Compare Two Trials"), this);
    m_promoteButton = new QPushButton(tr("Promote Winning Team"), this);
    m_deleteButton = new QPushButton(tr("Delete Trial"), this);

    buttonRow->addWidget(m_compareButton);
    buttonRow->addWidget(m_promoteButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch(1);

    layout->addLayout(buttonRow);

    connect(m_compareButton, &QPushButton::clicked,
            this, &TrialsWidget::compareSelectedTrials);
    connect(m_promoteButton, &QPushButton::clicked,
            this, &TrialsWidget::promoteWinningTeam);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &TrialsWidget::deleteSelectedTrial);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TrialsWidget::onSelectionChanged);

    refreshTrials();
    onSelectionChanged();
}

void TrialsWidget::refreshTrials()
{
    const QList<Trial> trials = m_storageManager.listTrials();

    m_table->setRowCount(trials.size());

    for (int row = 0; row < trials.size(); ++row) {
        const Trial &trial = trials.at(row);

        const QString dateText = trial.timestamp.isValid()
                                     ? trial.timestamp.toString(Qt::ISODate)
                                     : tr("(unknown)");
        auto *dateItem = new QTableWidgetItem(dateText);
        // Store trial id as user data for retrieval later.
        dateItem->setData(Qt::UserRole, trial.id);
        m_table->setItem(row, 0, dateItem);

        QString teamText;
        if (!trial.teamId.isEmpty()) {
            const Team team = m_storageManager.loadTeam(trial.teamId);
            if (!team.id.isEmpty() && !team.name.isEmpty()) {
                teamText = tr("%1 (%2)").arg(team.name, team.id);
            } else {
                teamText = trial.teamId;
            }
        }
        auto *teamItem = new QTableWidgetItem(teamText);
        m_table->setItem(row, 1, teamItem);

        auto *projectItem = new QTableWidgetItem(trial.projectPath);
        m_table->setItem(row, 2, projectItem);

        auto *ratingsItem = new QTableWidgetItem(summarizeRatings(trial.ratings));
        m_table->setItem(row, 3, ratingsItem);

        auto *notesItem = new QTableWidgetItem(notesSnippet(trial.notes));
        m_table->setItem(row, 4, notesItem);
    }

    m_table->resizeColumnsToContents();

    const bool hasTrials = !trials.isEmpty();
    if (m_placeholderLabel) {
        m_placeholderLabel->setVisible(!hasTrials);
    }
    if (m_table) {
        m_table->setVisible(true); // keep table visible even when empty for headers
    }

    // Re-apply the active filter so freshly added trials honor the
    // current search before the user types again.
    applyFilter(m_filterEdit ? m_filterEdit->text() : QString());
}

QStringList TrialsWidget::selectedTrialIds() const
{
    QStringList ids;
    if (!m_table) {
        return ids;
    }

    const auto selection = m_table->selectionModel()->selectedRows();
    for (const QModelIndex &index : selection) {
        const int row = index.row();
        // Skip rows the active filter has hidden so the user cannot act
        // (compare/promote/delete) on a Trial they cannot see.
        if (m_table->isRowHidden(row)) {
            continue;
        }
        const QString id = trialIdForRow(row);
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }

    return ids;
}

QString TrialsWidget::trialIdForRow(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return QString();
    }

    QTableWidgetItem *item = m_table->item(row, 0);
    if (!item) {
        return QString();
    }

    const QVariant data = item->data(Qt::UserRole);
    const QString id = data.isValid() ? data.toString() : item->text();
    return id.trimmed();
}

void TrialsWidget::compareSelectedTrials()
{
    const QStringList ids = selectedTrialIds();
    if (ids.size() != 2) {
        QMessageBox::information(this,
                                 tr("Compare Trials"),
                                 tr("Please select exactly two Trials to compare."));
        return;
    }

    emit compareTrialsRequested(ids);

    // Non-modal viewer so the user can keep working in the Trials tab
    // (and open further comparisons) while still dismissing the dialog
    // with the standard window close button. Parent is TrialsWidget,
    // which means Qt deletes the dialog when the tab itself goes away.
    auto *dlg = new TrialCompareDialog(ids.at(0), ids.at(1),
                                       m_storageManager, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TrialsWidget::promoteWinningTeam()
{
    const QStringList ids = selectedTrialIds();
    if (ids.size() != 1) {
        QMessageBox::information(this,
                                 tr("Promote Team"),
                                 tr("Please select a single Trial to promote its Team."));
        return;
    }

    const QString trialId = ids.first();
    const Trial trial = m_storageManager.loadTrial(trialId);
    if (trial.id.isEmpty() || trial.teamId.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Promote Team"),
                             tr("Selected Trial has no associated Team."));
        return;
    }

    emit promoteTeamRequested(trial.teamId);
}

void TrialsWidget::deleteSelectedTrial()
{
    const QStringList ids = selectedTrialIds();
    if (ids.size() != 1) {
        QMessageBox::information(this,
                                 tr("Delete Trial"),
                                 tr("Please select a single Trial to delete."));
        return;
    }

    const QString trialId = ids.first();
    const auto reply = QMessageBox::question(this,
                                             tr("Delete Trial"),
                                             tr("Delete selected Trial?"));
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!m_storageManager.deleteTrial(trialId)) {
        QMessageBox::warning(this,
                             tr("Delete Trial"),
                             tr("Failed to delete trial '%1'.").arg(trialId));
        return;
    }

    refreshTrials();
}

void TrialsWidget::onSelectionChanged()
{
    if (!m_table) {
        return;
    }

    const auto selection = m_table->selectionModel()->selectedRows();
    int count = 0;
    for (const QModelIndex &index : selection) {
        if (!m_table->isRowHidden(index.row())) {
            ++count;
        }
    }

    if (m_compareButton) {
        m_compareButton->setEnabled(count >= 2);
    }
    if (m_promoteButton) {
        m_promoteButton->setEnabled(count == 1);
    }
    if (m_deleteButton) {
        m_deleteButton->setEnabled(count == 1);
    }
}

void TrialsWidget::applyFilter(const QString &text)
{
    if (!m_table || !m_filterProxy) {
        return;
    }

    const QString needle = text.trimmed();
    m_filterProxy->setFilterFixedString(needle);

    const QModelIndex parent;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool match = needle.isEmpty() || m_filterProxy->acceptsRow(row, parent);
        m_table->setRowHidden(row, !match);
    }

    // Visibility just changed; the button enabled-state depends on the
    // number of visible selected rows (not the raw selection count).
    onSelectionChanged();
}
