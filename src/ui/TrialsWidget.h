#pragma once

#include <QWidget>

class FilterProxyModel;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableWidget;
class QPushButton;
class StorageManager;

// Trials view: history of applied Teams to projects.
//
// Minimal first-cut widget that lists recorded Trials (if any) and
// exposes actions to compare Trials, promote a winning Team, and
// delete Trials. Data wiring and richer comparison visuals can be
// layered on later without changing the public API.
class TrialsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrialsWidget(StorageManager &storageManager, QWidget *parent = nullptr);

signals:
    // Emitted when the user requests a comparison between two Trials.
    // The list is expected to contain exactly two trial ids.
    void compareTrialsRequested(const QStringList &trialIds);

    // Emitted when the user promotes the Team associated with a Trial.
    // The Teams view can listen for this and handle duplication/promotion.
    void promoteTeamRequested(const QString &teamId);

private slots:
    void refreshTrials();
    void compareSelectedTrials();
    void promoteWinningTeam();
    void deleteSelectedTrial();
    void onSelectionChanged();
    // ROADMAP P2-2: case-insensitive dynamic filtering on every column.
    void applyFilter(const QString &text);

private:
    QStringList selectedTrialIds() const;
    QString trialIdForRow(int row) const;

    StorageManager &m_storageManager;

    QTableWidget *m_table = nullptr;
    QLabel *m_placeholderLabel = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    FilterProxyModel *m_filterProxy = nullptr;
    QPushButton *m_compareButton = nullptr;
    QPushButton *m_promoteButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
};
