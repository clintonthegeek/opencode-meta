// PromptPreview: read-only widget that shows the effective system prompt
// the TeamRenderer will send to the model for a single (Role, Specialist)
// pair — i.e. the Role.systemPrompt with the Specialist.promptOverride
// layered on top, plus an approximate token count for the merged text.
//
// Constructed without arguments so it can live inside TeamEditorWidget,
// AddSpecialistDialog, or any other read-only surface. The owner drives
// it via setPreview(); clear() resets back to a placeholder state. The
// widget never mutates storage and never talks to the network.
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;

class PromptPreview : public QWidget
{
    Q_OBJECT

public:
    explicit PromptPreview(QWidget *parent = nullptr);

    // Render the merged prompt for one Specialist binding. All strings
    // are display-only: the Role's `systemPrompt` (already extracted from
    // the QJsonValue at the call site) acts as the base, and `overrideText`
    // -- when non-empty -- is shown below a clear "Specialist Override"
    // separator so the user can see exactly which slice comes from where.
    //
    // Display strings (`roleName`, `specialistName`, `modelId`) are used
    // purely for the header line. Empty values are acceptable; the header
    // falls back to "—" / "(no role picked)" / etc. so the layout never
    // collapses.
    void setPreview(const QString &roleName,
                    const QString &roleId,
                    const QString &specialistName,
                    const QString &modelId,
                    const QString &roleSystemPrompt,
                    const QString &overrideText);

    // Reset back to placeholder/empty state. Called when no Specialist is
    // selected or when no Role is available in AddSpecialistDialog.
    void clear();

private:
    QLabel *m_headerLabel = nullptr;
    QPlainTextEdit *m_viewer = nullptr;
    QLabel *m_tokenLabel = nullptr;
};
