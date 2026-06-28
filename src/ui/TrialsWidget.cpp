#include "ui/TrialsWidget.h"

#include <QDateTime>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>

#include "models/Trial.h"
#include "models/Team.h"
#include "storage/StorageManager.h"

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

QString formatTrialSummary(const Trial &trial,
                           const StorageManager &storage)
{
    QString result;

    result += QObject::tr("Trial ID: %1\n")
                  .arg(trial.id.isEmpty() ? QObject::tr("(unknown)") : trial.id);

    QString teamLabel;
    if (!trial.teamId.isEmpty()) {
        const Team team = storage.loadTeam(trial.teamId);
        if (!team.id.isEmpty()) {
            if (!team.name.isEmpty()) {
                teamLabel = QObject::tr("%1 (%2)")
                                .arg(team.name, team.id);
            } else {
                teamLabel = team.id;
            }
        } else {
            teamLabel = trial.teamId;
        }
    }

    result += QObject::tr("Team: %1\n")
                  .arg(teamLabel.isEmpty() ? QObject::tr("(none)") : teamLabel);

    result += QObject::tr("Project: %1\n")
                  .arg(trial.projectPath.isEmpty()
                           ? QObject::tr("(none)")
                           : trial.projectPath);

    const QString ts = trial.timestamp.isValid()
                           ? trial.timestamp.toString(Qt::ISODate)
                           : QObject::tr("(unknown)");
    result += QObject::tr("Timestamp: %1\n\n").arg(ts);

    result += QObject::tr("Notes:\n%1\n\n")
                  .arg(trial.notes.isEmpty()
                           ? QObject::tr("(none)")
                           : trial.notes);

    result += QObject::tr("Ratings summary: %1\n")
                  .arg(summarizeRatings(trial.ratings));

    return result;
}

class TrialCompareDialog : public QDialog
{
public:
    TrialCompareDialog(StorageManager &storage,
                       const QString &leftId,
                       const QString &rightId,
                       QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("Compare Trials (stub)"));
        resize(900, 500);

        auto *mainLayout = new QVBoxLayout(this);

        auto *label = new QLabel(tr("Side-by-side comparison is not fully implemented yet.\n"
                                    "This stub shows basic details for both Trials."),
                                 this);
        label->setWordWrap(true);
        mainLayout->addWidget(label);

        auto *contentLayout = new QHBoxLayout();

        Trial leftTrial = storage.loadTrial(leftId);
        if (leftTrial.id.isEmpty()) {
            leftTrial.id = leftId;
        }
        Trial rightTrial = storage.loadTrial(rightId);
        if (rightTrial.id.isEmpty()) {
            rightTrial.id = rightId;
        }

        auto *leftEdit = new QTextEdit(this);
        leftEdit->setReadOnly(true);
        leftEdit->setPlainText(formatTrialSummary(leftTrial, storage));

        auto *rightEdit = new QTextEdit(this);
        rightEdit->setReadOnly(true);
        rightEdit->setPlainText(formatTrialSummary(rightTrial, storage));

        contentLayout->addWidget(leftEdit, 1);
        contentLayout->addWidget(rightEdit, 1);

        mainLayout->addLayout(contentLayout, 1);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttonBox, &QDialogButtonBox::rejected,
                this, &TrialCompareDialog::reject);
        mainLayout->addWidget(buttonBox);
    }
};

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

    TrialCompareDialog dlg(m_storageManager, ids.at(0), ids.at(1), this);
    dlg.exec();
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

    // For now, mirror the simple deletion approach used in RolesWidget:
    // directly remove the JSON file under ~/.opencode-meta/trials/.
    const QString path = QDir::homePath() +
                         QStringLiteral("/.opencode-meta/trials/%1.json").arg(trialId);
    QFile file(path);
    if (file.exists() && !file.remove()) {
        QMessageBox::warning(this,
                             tr("Delete Trial"),
                             tr("Failed to delete trial file:\n%1")
                                 .arg(QDir::toNativeSeparators(path)));
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
    const int count = selection.size();

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
