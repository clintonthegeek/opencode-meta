#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

#include "models/Role.h"

// Simple editor for a single Role's basic fields.
//
// Current focus is on the Role name and primary system prompt string.
// More advanced fields (permissions, tools, metadata) are preserved
// round-trip but not directly editable here.
class RoleEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RoleEditorDialog(const Role &role, QWidget *parent = nullptr);

    // Returns a copy of the Role with edits from the dialog applied.
    Role roleData() const;

    void accept() override;

private:
    void setupUi();
    void loadFromRole(const Role &role);
    void applyToRole(Role &role) const;

    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_systemPromptEdit = nullptr;

    Role m_initialRole;
};
