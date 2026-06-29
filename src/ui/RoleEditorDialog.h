#pragma once

#include <QDialog>

#include <QJsonObject>
#include <QJsonValue>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

#include "models/Role.h"

// Tabbed editor for a single Role. The header area carries the basic
// identity fields (id / name / description / mode); a QTabWidget then
// hosts the prompt body, the permissions table, the tools list, and the
// metadata table. Permissions/Tools/Metadata tabs intentionally keep
// their surfaces minimal — the edit affordances themselves are added
// in later phases, the Phase 1 scope is "structure exists, fields
// round-trip cleanly through loadFromRole / applyToRole".
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
    void setupTabs();
    void loadFromRole(const Role &role);
    void applyToRole(Role &role) const;

    // QJsonValue <-> text helpers used for the systemPrompt, permissions,
    // and metadata surfaces.
    static QString jsonValueToDisplayText(const QJsonValue &value);
    static QJsonValue parseMetadataValue(const QString &text);

    // Header widgets (always visible).
    QLabel *m_idLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_descriptionEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;

    // Tabbed surfaces.
    QTabWidget *m_tabWidget = nullptr;
    QPlainTextEdit *m_systemPromptEdit = nullptr;

    QTableWidget *m_permissionsTable = nullptr;

    QLineEdit *m_toolNameEdit = nullptr;
    QPushButton *m_addToolButton = nullptr;
    QListWidget *m_toolsList = nullptr;

    QTableWidget *m_metadataTable = nullptr;

    Role m_initialRole;
};
