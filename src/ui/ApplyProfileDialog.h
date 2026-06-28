#pragma once

#include <QDialog>

class QLabel;
class QTextEdit;

// ApplyProfileDialog
//
// Lightweight "apply wizard" that shows:
// - Scope description (global vs project-local).
// - Warnings about the scope of changes and backups.
// - A simple text diff between the current config file and the
//   rendered opencode.json that will be written.
//
// The dialog does not perform any filesystem writes itself; callers
// are responsible for invoking applyConfigWithBackup(...) after
// exec() returns Accepted.
class ApplyProfileDialog : public QDialog
{
    Q_OBJECT

public:
    ApplyProfileDialog(const QString &scopeDescription,
                       const QString &warningsText,
                       const QString &summaryText,
                       const QString &currentConfigText,
                       const QString &renderedConfigText,
                       QWidget *parent = nullptr);

private:
    void populateDiff(const QString &currentConfigText,
                      const QString &renderedConfigText);

    QLabel *m_scopeLabel = nullptr;
    QLabel *m_warningsLabel = nullptr;
    QTextEdit *m_summaryEdit = nullptr;
    QTextEdit *m_currentEdit = nullptr;
    QTextEdit *m_renderedEdit = nullptr;
};
