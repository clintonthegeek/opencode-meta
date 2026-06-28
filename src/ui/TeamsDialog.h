#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;

class StorageManager;

// TeamsDialog
//
// Minimal surface for exercising the new Team paradigm:
// - Lists available Teams from StorageManager::listTeams().
// - Lets the user pick a Team and a project directory.
// - Applies the Team via StorageManager::applyTeamToProject(...)
//   and reports success/failure.
class TeamsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TeamsDialog(StorageManager &storageManager,
                         QWidget *parent = nullptr);

    // F1: when invoked from TeamEditorWidget's footer "Apply Team..."
    // button, the host calls this with the currently loaded Team id
    // before exec() so the matching row is selected on open. Calling
    // after exec() has no effect. Empty id is a no-op (preserves the
    // existing no-pre-selection behavior).
    void setPreselectedTeamId(const QString &teamId);

    // F5: when invoked from the cross-view smoke test harness, the host
    // calls this with the destination project path BEFORE the test
    // triggers the apply click. After this is set, onApplyClicked()
    // uses the supplied path directly instead of popping the native
    // QFileDialog::getExistingDirectory(). Calling without an argument
    // (or with an empty path) preserves the existing UX — the user is
    // shown the directory picker.
    void setProjectPath(const QString &path);

private slots:
    void refreshTeams();
    void onApplyClicked();
    void onSelectionChanged();

private:
    QString m_pendingPreselectedTeamId;

    // F5: if non-empty, onApplyClicked() uses this path verbatim instead
    // of opening QFileDialog::getExistingDirectory(); empty preserves
    // the existing native dialog behavior.
    QString m_pendingProjectPath;

private:
    StorageManager &m_storageManager;
    QTableWidget *m_table = nullptr;
    QPushButton *m_applyButton = nullptr;
};
