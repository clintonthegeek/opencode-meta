#pragma once

#include <QDialog>

#include <QJsonObject>
#include <QJsonValue>

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
class RoleEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RoleEditorDialog(const Role &role, QWidget *parent = nullptr);

    Role roleData() const;

    void accept() override;

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

    QLineEdit *m_toolNameEdit = nullptr;
    QPushButton *m_addToolButton = nullptr;
    QListWidget *m_toolsList = nullptr;

    QTableWidget *m_metadataTable = nullptr;

    QString m_cachedInlineText;
    QString m_cachedFilePath;

    Role m_initialRole;
};
