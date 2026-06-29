#pragma once

#include <QDialog>

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

#include "models/Role.h"

// Tabbed editor for a single Role. The header area carries the basic
// identity fields (id / name / description / mode); a QTabWidget then
// hosts the prompt body, the permissions table, the tools list, and
// the metadata table.
//
// Phase 2 expanded the Prompt tab: it now picks between an inline
// string form and a `{"file": "..."}` reference, drives a small live
// preview pane that mirrors the PromptPreview pattern, and round-trips
// the original QJsonValue type when the user did not touch the field.
//
// Phase 3 expanded the Permissions tab: the table now pre-populates the
// 15 canonical permission keys from PARADIGM.md §5.4 with a description
// column, the value column is a QComboBox bound to ask / allow / deny
// (color-coded green/yellow/red), an unknown/custom keys section is
// appended at the bottom, and a "Reset to defaults" button restores the
// canonical defaults (allow for read-only keys, ask for the rest).
class RoleEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RoleEditorDialog(const Role &role, QWidget *parent = nullptr);

    Role roleData() const;

    void accept() override;

    // 15 canonical permission keys from PARADIGM.md §5.4. Order is
    // preserved across load/apply/reset so the UI is stable.
    static QStringList canonicalPermissionKeys();
    static QSet<QString> readOnlyPermissionKeys();
    static QString defaultPermissionValueFor(const QString &key);
    static QString permissionDescriptionFor(const QString &key);

private:
    enum PromptMode {
        PromptModeInlineText,
        PromptModeReferenceFile,
    };

    void setupUi();
    void setupTabs();
    void loadFromRole(const Role &role);
    void applyToRole(Role &role) const;

    void rebuildPromptPreview();

    static QString jsonValueToDisplayText(const QJsonValue &value);
    static QJsonValue parseMetadataValue(const QString &text);

    static QString promptModeToString(PromptMode mode);
    static PromptMode detectPromptMode(const QJsonValue &value,
                                       QString *inlineText,
                                       QString *filePath);

    PromptMode currentPromptMode() const;
    QString currentInlinePromptText() const;
    QString currentFilePath() const;

    void onPromptModeChanged();
    void onBrowseFileReference();
    void onLoadPromptFromFile();
    void onInlinePromptTextChanged();
    void onFilePathChanged();

    // Permissions tab helpers
    void populatePermissionsTable(const QJsonObject &perms);
    void appendPermissionRow(const QString &key, const QString &value, const QString &description, bool canonical);
    QString valueAtPermissionRow(int row) const;
    void tintPermissionValueCombo(int row);
    void onResetPermissionsToDefaults();
    void onPermissionComboChanged(int row);

    QLabel *m_idLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_descriptionEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;

    QTabWidget *m_tabWidget = nullptr;

    QButtonGroup *m_promptModeGroup = nullptr;
    QRadioButton *m_promptInlineRadio = nullptr;
    QRadioButton *m_promptFileRadio = nullptr;
    QStackedWidget *m_promptModeStack = nullptr;

    QPlainTextEdit *m_systemPromptEdit = nullptr;
    QPushButton *m_loadFromFileButton = nullptr;

    QLineEdit *m_filePathEdit = nullptr;
    QPushButton *m_browseFileButton = nullptr;

    QLabel *m_promptPreviewHeader = nullptr;
    QPlainTextEdit *m_promptPreviewBody = nullptr;
    QLabel *m_promptPreviewTokenLabel = nullptr;

    QTableWidget *m_permissionsTable = nullptr;
    QPushButton *m_resetPermissionsButton = nullptr;

    QLineEdit *m_toolNameEdit = nullptr;
    QPushButton *m_addToolButton = nullptr;
    QPushButton *m_removeToolButton = nullptr;
    QListWidget *m_toolsList = nullptr;

    QTableWidget *m_metadataTable = nullptr;
    QPushButton *m_addMetadataRowButton = nullptr;
    QPushButton *m_removeMetadataRowButton = nullptr;

    QString m_cachedInlineText;
    QString m_cachedFilePath;

    Role m_initialRole;
};
