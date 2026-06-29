#pragma once

#include <QDialog>

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

#include <optional>

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
//
// Phase C1-4 / D-4: Tools tab carries the §6.3 deprecation banner plus
// a Migrate-now button, and `accept()` runs the auto-migration path
// before returning. `committedRoleData()` exposes the post-migration
// Role back to production callers (MainWindow / TeamEditorWidget).
//
// Phase C3 / D-5: a single QCheckBox in the Permissions tab marks the
// role "read-only"; the renderer honors it by omitting `edit`/`bash`
// keys on Primary mode.
//
// Phase C4 / D-6: a yellow inline banner in the Permissions tab
// surfaces whenever the loaded Role carries any non-canonical
// permission key. The renderer still strips before write per D-6, so
// the warning is purely advisory.
class RoleEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RoleEditorDialog(const Role &role, QWidget *parent = nullptr);

    Role roleData() const;

    // Returns the role after `accept()` has run, with the deprecated
    // `tools` map migrated into `permissions` per OPENCODE-CONFIG-INTROSPECTION
    // §6.3 / C1-4 D-4. Empty `std::optional` until `accept()` succeeds;
    // after reject the previously-committed migration (if any) is
    // preserved so callers can re-discard.
    std::optional<Role> committedRoleData() const;

    // Migration helper exposed for tests + the in-dialog "Migrate now"
    // button. Given the in-memory `tools` map, returns the list of keys
    // that were merged into the Permissions table (existing permissions
    // are NOT overwritten) and emits a one-shot qWarning matching the
    // §6.3 migration rule. Side-effect: leaves the QListWidget cleared
    // when invoking from a button slot.
    QStringList applyMigrationToPermissions(const QJsonObject &tools);

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

    // Reads the current Tools QListWidget into a {"<name>": true} JSON
    // object to feed the migration helper. Pure UI->model adapter.
    QJsonObject currentToolsObjectFromUi() const;

    // Phase C4-1 / D-6: refresh the inline warning label's visibility
    // based on whether the supplied `perms` object contains any
    // non-canonical permission keys. Called from `loadFromRole` and
    // from `onResetPermissionsToDefaults` (whose action clears the
    // warning).
    void updateCustomKeyWarning(const QJsonObject &perms);

    // Helper: scans the permissions table for an existing row keyed by
    // `key` and returns the row index, or -1 if absent. Used by the
    // migration path to detect "user already set this in Permissions"
    // so the migration never clobbers it.
    int indexOfRowWithKey(QTableWidget *table, const QString &key) const;

    QLabel *m_idLabel = nullptr;
    QLabel *m_nativeBadge = nullptr; // Phase D3-1 / D-10: stock-defined indicator
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_descriptionEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QPushButton *m_okButton = nullptr;

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
    QCheckBox *m_readOnlyCheckBox = nullptr;
    QLabel *m_customPermissionWarningLabel = nullptr;

    QLineEdit *m_toolNameEdit = nullptr;
    QPushButton *m_addToolButton = nullptr;
    QPushButton *m_removeToolButton = nullptr;
    QListWidget *m_toolsList = nullptr;
    QLabel *m_toolsPendingMigrationLabel = nullptr;
    QPushButton *m_migrateToolsNowButton = nullptr;

    QTableWidget *m_metadataTable = nullptr;
    QPushButton *m_addMetadataRowButton = nullptr;
    QPushButton *m_removeMetadataRowButton = nullptr;

    // Phase C6-2: per-row QLineEdit pairs on the Workspace tab. Each
    // row maps to a single `Role::metadata.<sub>Entries` sub-key
    // whose inner object is the row's JSON value. The dialog's
    // loadFromRole / applyToRole routes the contents directly into
    // the Role model so the renderer lift in TeamRenderer.cpp picks
    // them up at apply time.
    QLineEdit *m_workspaceMcpIdEdit = nullptr;
    QLineEdit *m_workspaceMcpJsonEdit = nullptr;
    QLineEdit *m_workspaceLspIdEdit = nullptr;
    QLineEdit *m_workspaceLspJsonEdit = nullptr;
    QLineEdit *m_workspaceFormatterIdEdit = nullptr;
    QLineEdit *m_workspaceFormatterJsonEdit = nullptr;
    QLineEdit *m_workspaceReferencesIdEdit = nullptr;
    QLineEdit *m_workspaceReferencesJsonEdit = nullptr;

    QString m_cachedInlineText;
    QString m_cachedFilePath;

    Role m_initialRole;

    // Holds the post-migration Role the dialog committed via OK. Empty
    // until `accept()` succeeds; reset on `reject()` so the next time
    // the dialog opens for the same Role the pending-migration path is
    // re-armed.
    std::optional<Role> m_committedRole;
};
