// ConfigInspector: live rendered opencode.json viewer.
//
// Drives `TeamRenderer::render()` against the currently-edited Team and
// displays the resulting JSON in a read-only, syntax-highlighted view.
// Also offers "Copy to clipboard" and "Save as..." affordances so users
// can pull the rendered config out of the app without leaving the editor.
//
// Designed for embed inside TeamEditorWidget as the lower half of a
// vertical QSplitter; nothing in this widget mutates the Team — it
// re-renders whatever it was last handed via `setTeam()`.

#pragma once

#include <QWidget>

#include "models/Team.h" // Team member, full type required

class StorageManager;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class ConfigInspector : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigInspector(StorageManager &storageManager,
                             QWidget *parent = nullptr);

    // Re-render the inspector for the supplied Team. An empty `team.id`
    // clears the view and shows the placeholder state. Triggers a
    // refresh on the next event-loop tick — call repeatedly to feed
    // live edits.
    void setTeam(const Team &team);

private slots:
    void onCopyToClipboard();
    void onSaveAs();

private:
    QString renderCurrentText() const;

    StorageManager &m_storageManager;

    Team m_team;        // Last Team handed to setTeam() (empty => placeholder)

    QLabel *m_headerLabel = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_saveButton = nullptr;
};
