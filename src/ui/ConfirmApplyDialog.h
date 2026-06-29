// ConfirmApplyDialog.h
//
// ROADMAP P1-5 confirmation gate before a Team is written to a project's
// local opencode.json. The dialog renders the existing file contents
// (when present) side-by-side with the freshly rendered Team JSON so the
// user can review EXACTLY what changes will happen before any file IO.
// Cancel closes the dialog without writing; Accepted is the signal to
// the caller that the apply may proceed.
//
// Design notes
// ------------
// * The dialog is a pure preview — it deliberately does NOT write to
//   disk. The hosting widget (ProjectsWidget::switchTeamForProject,
//   and any future host such as TeamsDialog) is responsible for the
//   actual commit, via StorageManager::applyTeamToProject(). Keeping
//   IO at the call-site makes the dialog cheap to test, makes the
//   gating reversible for dry-run previews, and matches the same
//   pattern used by EditSpecialistDialog (visual editor; caller
//   persists).
//
// * Rendering is re-done from Team + StorageManager rather than
//   accepting a pre-computed JSON string. That keeps the "what would
//   this Team write?" computation next to the dialog that displays
//   it, removes a duplicate rendering step from callers, and lets
//   future hosts reuse the dialog with their own Team + storage.
//
// * Diff highlighting reuses the line-by-line comparison already
//   in use by ProjectsWidget::viewTeamDiffsForProject: lines whose
//   left/right content differ are tinted on both sides (left in
//   red, right in green) so the user can scan for changes bottom-up.

#pragma once

#include <QDialog>

#include "models/Team.h"

class QTextEdit;

class StorageManager;

class ConfirmApplyDialog : public QDialog
{
    Q_OBJECT

public:
    // projectPath    : Absolute path to the project directory where the
    //                  rendered opencode.json will be written by the
    //                  caller after Accept.
    // team           : The Team being applied; used to render the right
    //                  pane and for the window + banner text.
    // storage        : Storage manager used to resolve the Team's
    //                  Specialist and Role records. Mutated only by
    //                  read-only calls (loadRole / loadSpecialist).
    // currentText    : UTF-8 contents of the existing opencode.json
    //                  file, or empty if no file exists.
    // currentIsJson  : True iff `currentText` parsed as a JSON object
    //                  (controls the choice of banner wording).
    //                  Ignored when `currentText` is empty.
    // parent         : Standard Qt parent.
    ConfirmApplyDialog(const QString &projectPath,
                       const Team &team,
                       StorageManager &storage,
                       const QString &currentText,
                       bool currentIsJson,
                       QWidget *parent = nullptr);

    QString projectPath() const { return m_projectPath; }
    QString teamId() const { return m_teamId; }
    QString teamName() const { return m_teamName; }

    // Surfaced so tests / callers can sanity-check that the dialog
    // produced the same bytes StorageManager::applyTeamToProject()
    // would write. Empty only when TeamRenderer::render failed.
    QString renderedText() const { return m_renderedText; }

    // Human-readable summary surfaced in the banner — useful in tests
    // and as a debug log before commit.
    QString summaryText() const { return m_summaryText; }

private:
    void populateDiffEditor(QTextEdit *edit,
                            const QStringList &lines,
                            const QVector<bool> &isDifferent,
                            const QColor &diffColor);

    QString m_projectPath;
    QString m_teamId;
    QString m_teamName;
    QString m_renderedText;
    QString m_summaryText;
};
