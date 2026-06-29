// ImportExportDialog.h
//
// ROADMAP P3-2: UI surface for choosing which Roles / Teams go into a
// bundle (Export) or for previewing an incoming bundle before letting
// the import land (Import).
//
// The dialog is intentionally minimal -- it does NOT perform any
// StorageManager or filesystem I/O. The caller (MainWindow) drives
// ImportExportManager against the values returned by selection() and
// zipPath(), and surfaces success/failure via the persistent
// status bar (P0-3) plus a summary message box on import. Splitting
// dialog from persistence keeps the QDialog class small enough to
// unit-test without spinning up StorageManager fakes.
//
// Two modes are supported via the Mode enum:
//   * ExportMode -- lists every available Role and Team; user
//     toggles checkable items to mark the ones they want in the
//     bundle. Filename is suggested from the first included Team.
//   * ImportMode -- the lists are populated but read-only; on file
//     selection the dialog reads the bundle's manifest and shows a
//     textual preview inside the dialog. The user must click OK to
//     confirm the overwrite. (The dialog cannot know in advance
//     whether imports will overwrite; that decision lives in the
//     caller, which has access to StorageManager.)

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "models/Role.h"
#include "models/Team.h"
#include "storage/ImportExportManager.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

class ImportExportDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode {
        Export,
        Import,
    };

    // Encapsulates user choices when the dialog closes via accept().
    // For Mode::Import, selection lists are empty (the importer
    // chooses ids from the bundle, not from the UI).
    struct Result {
        Mode mode;
        QList<QString> roleIds;
        QList<QString> teamIds;
        QString zipPath;
        QString notes;
    };

    explicit ImportExportDialog(Mode mode,
                                const QList<Role> &availableRoles,
                                const QList<Team> &availableTeams,
                                QWidget *parent = nullptr);
    ~ImportExportDialog() override = default;

    Mode mode() const { return m_mode; }
    Result result() const;

    // Suggest a default filename for ExportMode given a list of
    // Team ids. Stable + useful in QFileDialog::getSaveFileName's
    // initial-selected filter.
    static QString suggestExportFilename(const QList<QString> &teamIds);

private slots:
    void onBrowse();
    void onAutoRefreshImportPreview();

private:
    void buildUi();
    void populateRoleList();
    void populateTeamList();
    void refreshImportPreview();
    void updateAcceptEnabled();

    Mode m_mode;

    QListWidget   *m_roleList = nullptr;
    QListWidget   *m_teamList = nullptr;
    QLineEdit     *m_pathEdit = nullptr;
    QPushButton   *m_browseButton = nullptr;
    QLineEdit     *m_notesEdit = nullptr;
    QTextEdit     *m_importPreview = nullptr;
    QLabel        *m_summaryLabel = nullptr;
    QPushButton   *m_okButton     = nullptr;

    QList<Role>   m_availableRoles;
    QList<Team>   m_availableTeams;
};
