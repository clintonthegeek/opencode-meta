#include "ui/RoleEditorDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QVBoxLayout>

#include <QColor>
#include <QPalette>
#include <QStyle>

#include "models/Role.h"

namespace {

void appendTableRow(QTableWidget *table, const QString &key, const QString &valueText)
{
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(key));
    table->setItem(row, 1, new QTableWidgetItem(valueText));
}

void appendListEntry(QListWidget *list, const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    list->addItem(new QListWidgetItem(text));
}

int approxTokenCount(const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }
    return (text.size() + 3) / 4;
}

QString renderFileReferenceDisplay(const QString &filePath)
{
    return QStringLiteral("{\"file\": \"%1\"}").arg(filePath);
}

QString renderPlainObjectDisplay(const QJsonObject &obj)
{
    const QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QStringList canonicalPermissionKeysOrdered()
{
    // Order from PARADIGM.md §5.4 (also matches OPENCODE-CONFIG-INTROSPECTION §6.1).
    return QStringList{
        QStringLiteral("read"),
        QStringLiteral("edit"),
        QStringLiteral("glob"),
        QStringLiteral("grep"),
        QStringLiteral("list"),
        QStringLiteral("bash"),
        QStringLiteral("task"),
        QStringLiteral("external_directory"),
        QStringLiteral("lsp"),
        QStringLiteral("skill"),
        QStringLiteral("todowrite"),
        QStringLiteral("question"),
        QStringLiteral("webfetch"),
        QStringLiteral("websearch"),
        QStringLiteral("doom_loop")
    };
}

QSet<QString> readOnlyPermissionKeysSet()
{
    // Considered read-only / safe-by-default operations.
    return QSet<QString>{
        QStringLiteral("read"),
        QStringLiteral("glob"),
        QStringLiteral("grep"),
        QStringLiteral("list"),
        QStringLiteral("webfetch"),
        QStringLiteral("websearch"),
        QStringLiteral("question")
    };
}

QString valueToPermissionString(const QJsonValue &value)
{
    if (!value.isString()) {
        return QString();
    }
    const QString raw = value.toString().trimmed().toLower();
    if (raw == QLatin1String("ask") || raw == QLatin1String("allow") || raw == QLatin1String("deny")) {
        return raw;
    }
    return QString();
}

QString valueFromObjectPermission(const QJsonValue &value)
{
    // Permission object form: {"<pattern>": "ask"|"allow"|"deny"} — pick the first
    // recognisable action so the combo box still has something to display.
    if (!value.isObject()) {
        return QString();
    }
    const QJsonObject obj = value.toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString candidate = valueToPermissionString(it.value());
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    return QString();
}

} // namespace

QString RoleEditorDialog::jsonValueToDisplayText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonValue fileValue = obj.value(QStringLiteral("file"));
        if (obj.size() == 1 && fileValue.isString()) {
            return renderFileReferenceDisplay(fileValue.toString());
        }
        const QJsonDocument doc(obj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    if (value.isArray()) {
        const QJsonDocument doc(value.toArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    return QString();
}

QJsonValue RoleEditorDialog::parseMetadataValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QJsonValue(QString());
    }

    if (trimmed == QLatin1String("true")) {
        return QJsonValue(true);
    }
    if (trimmed == QLatin1String("false")) {
        return QJsonValue(false);
    }

    auto isAllDigitsWithOptionalSign = [](const QString &s) {
        int i = 0;
        if (s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('+'))) {
            i = 1;
            if (s.size() == 1) {
                return false;
            }
        }
        for (; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
                return false;
            }
        }
        return true;
    };

    if (isAllDigitsWithOptionalSign(trimmed)) {
        bool ok = false;
        const long long ll = trimmed.toLongLong(&ok);
        if (ok) {
            return QJsonValue(qint64(ll));
        }
    }

    if ((trimmed.startsWith(QLatin1Char('{')) && trimmed.endsWith(QLatin1Char('}')))
        || (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))) {
        QJsonParseError err;
        const QJsonDocument parsed = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && parsed.isObject()) {
            return QJsonValue(parsed.object());
        }
        if (err.error == QJsonParseError::NoError && parsed.isArray()) {
            return QJsonValue(parsed.array());
        }
    }

    return QJsonValue(trimmed);
}

RoleEditorDialog::RoleEditorDialog(const Role &role, QWidget *parent)
    : QDialog(parent)
    , m_initialRole(role)
{
    setupUi();
    setupTabs();
    loadFromRole(role);
    rebuildPromptPreview();
}

void RoleEditorDialog::setupUi()
{
    setWindowTitle(tr("Role Editor"));
    resize(720, 560);

    auto *mainLayout = new QVBoxLayout(this);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignRight);

    m_idLabel = new QLabel(this);
    m_idLabel->setObjectName(QStringLiteral("roleEditor.idLabel"));
    m_idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_idLabel->setToolTip(tr("Stable id assigned when this role was first saved."));
    m_idLabel->setWhatsThis(tr("Stable identifier for this role in opencode.json — read-only here."));
    formLayout->addRow(tr("ID"), m_idLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("roleEditor.nameEdit"));
    m_nameEdit->setToolTip(tr("Human-readable name for this role (e.g. \"Coder\")."));
    m_nameEdit->setWhatsThis(tr("The display name shown in the Roles table and the opencode.json agent entry."));
    formLayout->addRow(tr("Name"), m_nameEdit);

    m_descriptionEdit = new QLineEdit(this);
    m_descriptionEdit->setObjectName(QStringLiteral("roleEditor.descriptionEdit"));
    m_descriptionEdit->setToolTip(tr("Long-form description that explains the role's purpose."));
    m_descriptionEdit->setWhatsThis(tr("Optional. Surfaces inside the agent tooltip in the Teams editor."));
    formLayout->addRow(tr("Description"), m_descriptionEdit);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("roleEditor.modeCombo"));
    m_modeCombo->addItem(tr("Primary"), QVariant(static_cast<int>(Role::Mode::Primary)));
    m_modeCombo->addItem(tr("Subagent"), QVariant(static_cast<int>(Role::Mode::Subagent)));
    m_modeCombo->addItem(tr("All"), QVariant(static_cast<int>(Role::Mode::All)));
    m_modeCombo->setToolTip(tr("Where this role is valid: primary entry point, subagent only, or both."));
    m_modeCombo->setWhatsThis(tr("Mirrors the opencode.json `mode` field on the agent entry."));
    formLayout->addRow(tr("Mode"), m_modeCombo);

    mainLayout->addLayout(formLayout);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &RoleEditorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &RoleEditorDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void RoleEditorDialog::setupTabs()
{
    // --- Prompt tab --------------------------------------------------
    auto *promptTab = new QWidget(this);
    auto *promptLayout = new QVBoxLayout(promptTab);
    promptLayout->setContentsMargins(8, 8, 8, 8);
    promptLayout->setSpacing(8);

    auto *modeHint = new QLabel(tr("Prompt form"), promptTab);
    promptLayout->addWidget(modeHint);

    auto *modeRow = new QHBoxLayout();
    modeRow->setContentsMargins(0, 0, 0, 0);
    m_promptInlineRadio = new QRadioButton(tr("Inline text"), promptTab);
    m_promptInlineRadio->setObjectName(QStringLiteral("roleEditor.promptInlineRadio"));
    m_promptInlineRadio->setToolTip(tr("Write the system prompt as a plain string in opencode.json."));
    m_promptFileRadio = new QRadioButton(tr("Reference file"), promptTab);
    m_promptFileRadio->setObjectName(QStringLiteral("roleEditor.promptFileRadio"));
    m_promptFileRadio->setToolTip(tr("Store a relative path; opencode will read the prompt from that file."));
    m_promptModeGroup = new QButtonGroup(this);
    m_promptModeGroup->setExclusive(true);
    m_promptModeGroup->addButton(m_promptInlineRadio, static_cast<int>(PromptModeInlineText));
    m_promptModeGroup->addButton(m_promptFileRadio, static_cast<int>(PromptModeReferenceFile));
    modeRow->addWidget(m_promptInlineRadio);
    modeRow->addWidget(m_promptFileRadio);
    modeRow->addStretch(1);
    promptLayout->addLayout(modeRow);

    m_promptModeStack = new QStackedWidget(promptTab);
    m_promptModeStack->setObjectName(QStringLiteral("roleEditor.promptModeStack"));

    auto *inlinePanel = new QWidget(promptTab);
    auto *inlineLayout = new QVBoxLayout(inlinePanel);
    inlineLayout->setContentsMargins(0, 0, 0, 0);
    inlineLayout->setSpacing(6);
    m_systemPromptEdit = new QPlainTextEdit(promptTab);
    m_systemPromptEdit->setObjectName(QStringLiteral("roleEditor.systemPromptEdit"));
    m_systemPromptEdit->setPlaceholderText(tr("Enter system prompt for this role..."));
    m_systemPromptEdit->setToolTip(tr("Inline string system prompt for this agent."));
    m_systemPromptEdit->setWhatsThis(tr("Writes into the opencode.json `system_prompt` field as a plain string."));
    inlineLayout->addWidget(m_systemPromptEdit, 1);

    m_loadFromFileButton = new QPushButton(tr("Load from file..."), inlinePanel);
    m_loadFromFileButton->setObjectName(QStringLiteral("roleEditor.loadFromFileButton"));
    m_loadFromFileButton->setToolTip(tr("Pick a prompt file and switch this role into Reference-file mode."));
    m_loadFromFileButton->setWhatsThis(tr("Opens a file picker, switches the mode, and stores the relative path as {\"file\": \"...\"}."));
    auto *inlineButtonRow = new QHBoxLayout();
    inlineButtonRow->setContentsMargins(0, 0, 0, 0);
    inlineButtonRow->addWidget(m_loadFromFileButton);
    inlineButtonRow->addStretch(1);
    inlineLayout->addLayout(inlineButtonRow);

    m_promptModeStack->addWidget(inlinePanel);

    auto *filePanel = new QWidget(promptTab);
    auto *fileLayout = new QFormLayout(filePanel);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setLabelAlignment(Qt::AlignRight);

    auto *filePathRow = new QHBoxLayout();
    filePathRow->setContentsMargins(0, 0, 0, 0);
    m_filePathEdit = new QLineEdit(filePanel);
    m_filePathEdit->setObjectName(QStringLiteral("roleEditor.filePathEdit"));
    m_filePathEdit->setPlaceholderText(tr("./prompts/role.md"));
    m_filePathEdit->setToolTip(tr("Relative or absolute path written into {\"file\": \"...\"}."));
    m_filePathEdit->setWhatsThis(tr("The path is persisted verbatim into the opencode.json `system_prompt` object."));
    m_browseFileButton = new QPushButton(tr("Browse..."), filePanel);
    m_browseFileButton->setObjectName(QStringLiteral("roleEditor.browseFileButton"));
    m_browseFileButton->setToolTip(tr("Pick a prompt file from disk."));
    m_browseFileButton->setWhatsThis(tr("Opens a file picker and copies the chosen path into the File path field."));
    filePathRow->addWidget(m_filePathEdit, 1);
    filePathRow->addWidget(m_browseFileButton);
    auto *filePathHolder = new QWidget(filePanel);
    filePathHolder->setLayout(filePathRow);
    fileLayout->addRow(tr("File path"), filePathHolder);

    auto *fileHint = new QLabel(
        tr("Written as  {\"file\": \"<path>\"}  into the opencode.json `system_prompt` field."),
        filePanel);
    fileHint->setWordWrap(true);
    fileHint->setObjectName(QStringLiteral("roleEditor.filePathHint"));
    fileLayout->addRow(QString(), fileHint);

    m_promptModeStack->addWidget(filePanel);

    promptLayout->addWidget(m_promptModeStack, 1);

    auto *previewSeparator = new QLabel(tr("Preview"), promptTab);
    previewSeparator->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    promptLayout->addWidget(previewSeparator);

    m_promptPreviewHeader = new QLabel(promptTab);
    m_promptPreviewHeader->setObjectName(QStringLiteral("roleEditor.promptPreviewHeader"));
    m_promptPreviewHeader->setTextFormat(Qt::RichText);
    m_promptPreviewHeader->setText(tr("<i>The serialized prompt appears here as the opencode.json writer will see it.</i>"));
    promptLayout->addWidget(m_promptPreviewHeader);

    m_promptPreviewBody = new QPlainTextEdit(promptTab);
    m_promptPreviewBody->setObjectName(QStringLiteral("roleEditor.promptPreviewBody"));
    m_promptPreviewBody->setReadOnly(true);
    m_promptPreviewBody->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_promptPreviewBody->setPlaceholderText(tr(
        "Pick a prompt mode above and edit the body. This preview updates live to show "
        "what will be saved into opencode.json."));
    promptLayout->addWidget(m_promptPreviewBody, 1);

    m_promptPreviewTokenLabel = new QLabel(promptTab);
    m_promptPreviewTokenLabel->setObjectName(QStringLiteral("roleEditor.promptPreviewTokenLabel"));
    m_promptPreviewTokenLabel->setTextFormat(Qt::RichText);
    m_promptPreviewTokenLabel->setText(QStringLiteral("<span style='color: gray;'>"
                                                     "approx. &mdash; tokens</span>"));
    promptLayout->addWidget(m_promptPreviewTokenLabel);

    m_tabWidget->addTab(promptTab, tr("Prompt"));
    m_tabWidget->setTabToolTip(0, tr("System prompt body: pick the form and edit the body. Live preview mirrors the opencode.json output."));

    connect(m_promptInlineRadio, &QRadioButton::toggled,
            this, &RoleEditorDialog::onPromptModeChanged);
    connect(m_promptFileRadio, &QRadioButton::toggled,
            this, &RoleEditorDialog::onPromptModeChanged);
    connect(m_systemPromptEdit, &QPlainTextEdit::textChanged,
            this, &RoleEditorDialog::onInlinePromptTextChanged);
    connect(m_filePathEdit, &QLineEdit::textChanged,
            this, &RoleEditorDialog::onFilePathChanged);
    connect(m_browseFileButton, &QPushButton::clicked,
            this, &RoleEditorDialog::onBrowseFileReference);
    connect(m_loadFromFileButton, &QPushButton::clicked,
            this, &RoleEditorDialog::onLoadPromptFromFile);

    // --- Permissions tab --------------------------------------------
    auto *permsTab = new QWidget(this);
    auto *permsLayout = new QVBoxLayout(permsTab);
    permsLayout->setContentsMargins(8, 8, 8, 8);

    auto *permsHint = new QLabel(
        tr("Permissions — %1 canonical keys from PARADIGM.md §5.4. "
           "Pick ask / allow / deny per row. Custom keys are preserved at the bottom.")
            .arg(canonicalPermissionKeysOrdered().size()),
        permsTab);
    permsHint->setWordWrap(true);
    permsHint->setObjectName(QStringLiteral("roleEditor.permissionsHint"));
    permsLayout->addWidget(permsHint);

    // Tint the value column's combo box using a stylesheet that keys off
    // the dynamic "permissionValue" property we flip in tintPermissionValueCombo.
    m_permissionsTable = new QTableWidget(0, 3, permsTab);
    m_permissionsTable->setObjectName(QStringLiteral("roleEditor.permissionsTable"));
    m_permissionsTable->setHorizontalHeaderLabels(
        QStringList{ tr("Key"), tr("Value"), tr("Description") });
    m_permissionsTable->verticalHeader()->setVisible(false);
    m_permissionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_permissionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if (auto *hh = m_permissionsTable->horizontalHeader()) {
        hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        hh->setSectionResizeMode(2, QHeaderView::Stretch);
    }
    m_permissionsTable->setColumnWidth(0, 140);
    m_permissionsTable->setColumnWidth(1, 110);
    m_permissionsTable->setStyleSheet(
        QStringLiteral(
            "QComboBox[permissionValue=\"allow\"] { background-color: #c8e6c9; }"
            "QComboBox[permissionValue=\"ask\"]   { background-color: #fff59d; }"
            "QComboBox[permissionValue=\"deny\"]  { background-color: #ffcdd2; }"
        )
    );
    m_permissionsTable->setToolTip(tr(
        "Canonical opencode permission keys. Use the Reset button to restore defaults."));
    m_permissionsTable->setWhatsThis(tr(
        "Each row maps to one entry in the opencode.json `permissions` object on the agent. "
        "Green = allow, yellow = ask, red = deny, applied via the value combo."));
    permsLayout->addWidget(m_permissionsTable, 1);

    auto *permsButtonRow = new QHBoxLayout();
    permsButtonRow->setContentsMargins(0, 0, 0, 0);
    m_resetPermissionsButton = new QPushButton(tr("Reset to defaults"), permsTab);
    m_resetPermissionsButton->setObjectName(QStringLiteral("roleEditor.resetPermissionsButton"));
    m_resetPermissionsButton->setToolTip(tr(
        "Restore all 15 canonical permission rows to ask / allow defaults."));
    m_resetPermissionsButton->setWhatsThis(tr(
        "Clears any custom rows and rewrites the value column to ask (write-ish keys) "
        "or allow (read-only keys such as read, glob, grep, list, webfetch, websearch, question)."));
    permsButtonRow->addWidget(m_resetPermissionsButton);
    permsButtonRow->addStretch(1);
    permsLayout->addLayout(permsButtonRow);

    connect(m_resetPermissionsButton, &QPushButton::clicked,
            this, &RoleEditorDialog::onResetPermissionsToDefaults);
    connect(m_permissionsTable, &QTableWidget::currentCellChanged,
            this, [](int, int, int, int) { /* placeholder: future row actions */ });

    m_tabWidget->addTab(permsTab, tr("Permissions"));
    m_tabWidget->setTabToolTip(1, tr(
        "Per-tool allow / ask / deny overlay — 15 canonical keys with sensible defaults."));

    // --- Tools tab ---------------------------------------------------
    auto *toolsTab = new QWidget(this);
    auto *toolsLayout = new QVBoxLayout(toolsTab);
    toolsLayout->setContentsMargins(8, 8, 8, 8);

    // Deprecation banner — the opencode runtime still folds `tools` into
    // `permission` at config.ts:552-563 but the load path is fragile, so
    // the Renderer emits the modern `permission` form on save. We keep
    // the editor surface here so existing `tools` blocks can be migrated
    // to the Permissions tab at a single, visible choke-point rather than
    // being silently dropped on reload.
    auto *toolsBanner = new QLabel(toolsTab);
    toolsBanner->setObjectName(QStringLiteral("roleEditor.toolsDeprecationBanner"));
    toolsBanner->setTextFormat(Qt::RichText);
    toolsBanner->setWordWrap(true);
    toolsBanner->setText(tr(
        "<b>Deprecated</b> &mdash; the <code>tools</code> map is folded into "
        "<code>permissions</code> at runtime; we keep this surface only so you "
        "can migrate legacy <code>{name: true}</code> blocks. Move these to the "
        "Permissions tab above whenever possible."));
    toolsBanner->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #fff3cd;"
        "         color: #5a4500;"
        "         border: 1px solid #ffe69c;"
        "         border-radius: 4px;"
        "         padding: 6px 8px; }"));
    toolsBanner->setToolTip(tr(
        "opencode-meta retains the legacy `tools` editor for migration only. "
        "Saving emits the modern `permissions` form."));

    auto *toolsHint = new QLabel(tr("Tools currently registered for this role."), toolsTab);
    m_toolNameEdit = new QLineEdit(toolsTab);
    m_toolNameEdit->setObjectName(QStringLiteral("roleEditor.toolNameEdit"));
    m_toolNameEdit->setPlaceholderText(tr("e.g. bash, read, edit"));
    m_toolNameEdit->setToolTip(tr("Type a tool name and press Add."));
    m_toolNameEdit->setWhatsThis(tr("Each entry maps to {\"<name>\": true} in the agent `tools` object."));
    m_addToolButton = new QPushButton(tr("Add"), toolsTab);
    m_addToolButton->setObjectName(QStringLiteral("roleEditor.addToolButton"));
    m_addToolButton->setToolTip(tr("Append the typed tool name to the list."));
    m_removeToolButton = new QPushButton(tr("Remove"), toolsTab);
    m_removeToolButton->setObjectName(QStringLiteral("roleEditor.removeToolButton"));
    m_removeToolButton->setToolTip(tr("Remove the selected tool from the list."));
    m_removeToolButton->setWhatsThis(tr("Deletes the highlighted list entry; no-op if nothing selected."));
    m_toolsList = new QListWidget(toolsTab);
    m_toolsList->setObjectName(QStringLiteral("roleEditor.toolsList"));
    m_toolsList->setToolTip(tr("Tools currently registered for this role."));

    auto *addRowLayout = new QHBoxLayout();
    addRowLayout->addWidget(m_toolNameEdit, 1);
    addRowLayout->addWidget(m_addToolButton);
    addRowLayout->addWidget(m_removeToolButton);

    toolsLayout->addWidget(toolsBanner);
    toolsLayout->addWidget(toolsHint);
    toolsLayout->addLayout(addRowLayout);
    toolsLayout->addWidget(m_toolsList, 1);

    connect(m_addToolButton, &QPushButton::clicked, this, [this]() {
        if (!m_toolNameEdit || !m_toolsList) {
            return;
        }
        const QString name = m_toolNameEdit->text().trimmed();
        if (name.isEmpty()) {
            return;
        }
        for (int i = 0; i < m_toolsList->count(); ++i) {
            if (m_toolsList->item(i)->text() == name) {
                m_toolNameEdit->clear();
                return;
            }
        }
        appendListEntry(m_toolsList, name);
        m_toolNameEdit->clear();
    });

    connect(m_removeToolButton, &QPushButton::clicked, this, [this]() {
        if (!m_toolsList) {
            return;
        }
        const int row = m_toolsList->currentRow();
        if (row < 0 || row >= m_toolsList->count()) {
            return;
        }
        delete m_toolsList->takeItem(row);
    });

    m_tabWidget->addTab(toolsTab, tr("Tools"));
    m_tabWidget->setTabToolTip(2, tr(
        "Legacy `tools` map — deprecated in favour of Permissions. "
        "Surface kept for migration only."));

    // --- Metadata tab ------------------------------------------------
    auto *metaTab = new QWidget(this);
    auto *metaLayout = new QVBoxLayout(metaTab);
    metaLayout->setContentsMargins(8, 8, 8, 8);
    auto *metaHint = new QLabel(
        tr("Metadata — one row per key. Values can be plain strings, numbers (e.g. 7), "
           "booleans (true / false) or nested JSON ({...} / [...]). Empty rows are "
           "ignored on save."),
        metaTab);
    metaHint->setWordWrap(true);
    m_metadataTable = new QTableWidget(0, 2, metaTab);
    m_metadataTable->setObjectName(QStringLiteral("roleEditor.metadataTable"));
    m_metadataTable->setHorizontalHeaderLabels(QStringList{ tr("Key"), tr("Value") });
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->verticalHeader()->setVisible(false);
    m_metadataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_metadataTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                     | QAbstractItemView::EditKeyPressed
                                     | QAbstractItemView::AnyKeyPressed);
    m_metadataTable->setToolTip(tr("Free-form metadata for this role (tags, notes, timestamps). "
                                   "Double-click a cell to edit."));
    m_metadataTable->setWhatsThis(tr("Key/value pairs written to opencode.json `metadata` on the agent entry."));

    auto *metaButtonRow = new QHBoxLayout();
    metaButtonRow->setContentsMargins(0, 0, 0, 0);
    m_addMetadataRowButton = new QPushButton(tr("Add Row"), metaTab);
    m_addMetadataRowButton->setObjectName(QStringLiteral("roleEditor.addMetadataRowButton"));
    m_addMetadataRowButton->setToolTip(tr("Append a new empty key/value row to the metadata table."));
    m_addMetadataRowButton->setWhatsThis(tr("Adds a row at the bottom so you can fill in a new key/value pair."));
    m_removeMetadataRowButton = new QPushButton(tr("Remove Row"), metaTab);
    m_removeMetadataRowButton->setObjectName(QStringLiteral("roleEditor.removeMetadataRowButton"));
    m_removeMetadataRowButton->setToolTip(tr("Remove the selected row from the metadata table."));
    m_removeMetadataRowButton->setWhatsThis(tr("No-op if no row is selected."));
    metaButtonRow->addWidget(m_addMetadataRowButton);
    metaButtonRow->addWidget(m_removeMetadataRowButton);
    metaButtonRow->addStretch(1);

    metaLayout->addWidget(metaHint);
    metaLayout->addWidget(m_metadataTable, 1);
    metaLayout->addLayout(metaButtonRow);

    connect(m_addMetadataRowButton, &QPushButton::clicked, this, [this]() {
        if (!m_metadataTable) {
            return;
        }
        const int row = m_metadataTable->rowCount();
        m_metadataTable->insertRow(row);
        m_metadataTable->setItem(row, 0, new QTableWidgetItem(QString()));
        m_metadataTable->setItem(row, 1, new QTableWidgetItem(QString()));
        m_metadataTable->setCurrentCell(row, 0);
        m_metadataTable->editItem(m_metadataTable->item(row, 0));
    });

    connect(m_removeMetadataRowButton, &QPushButton::clicked, this, [this]() {
        if (!m_metadataTable) {
            return;
        }
        const int row = m_metadataTable->currentRow();
        if (row < 0 || row >= m_metadataTable->rowCount()) {
            return;
        }
        m_metadataTable->removeRow(row);
    });

    m_tabWidget->addTab(metaTab, tr("Metadata"));
    m_tabWidget->setTabToolTip(3, tr("Free-form metadata attached to this agent."));
}

QString RoleEditorDialog::promptModeToString(PromptMode mode)
{
    switch (mode) {
    case PromptModeInlineText:
        return QStringLiteral("inline");
    case PromptModeReferenceFile:
        return QStringLiteral("file");
    }
    return QStringLiteral("inline");
}

RoleEditorDialog::PromptMode RoleEditorDialog::detectPromptMode(const QJsonValue &value,
                                                                 QString *inlineText,
                                                                 QString *filePath)
{
    if (inlineText) {
        *inlineText = QString();
    }
    if (filePath) {
        *filePath = QString();
    }

    if (value.isString()) {
        if (inlineText) {
            *inlineText = value.toString();
        }
        return PromptModeInlineText;
    }

    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonValue fileValue = obj.value(QStringLiteral("file"));
        if (fileValue.isString()) {
            if (filePath) {
                *filePath = fileValue.toString();
            }
            return PromptModeReferenceFile;
        }

        if (inlineText) {
            *inlineText = renderPlainObjectDisplay(obj);
        }
        return PromptModeInlineText;
    }

    if (!value.isNull() && !value.isUndefined()) {
        if (inlineText) {
            *inlineText = jsonValueToDisplayText(value);
        }
    }

    return PromptModeInlineText;
}

RoleEditorDialog::PromptMode RoleEditorDialog::currentPromptMode() const
{
    if (m_promptFileRadio && m_promptFileRadio->isChecked()) {
        return PromptModeReferenceFile;
    }
    return PromptModeInlineText;
}

QString RoleEditorDialog::currentInlinePromptText() const
{
    return m_systemPromptEdit ? m_systemPromptEdit->toPlainText() : QString();
}

QString RoleEditorDialog::currentFilePath() const
{
    return m_filePathEdit ? m_filePathEdit->text() : QString();
}

void RoleEditorDialog::onPromptModeChanged()
{
    if (!m_promptModeStack) {
        return;
    }
    m_promptModeStack->setCurrentIndex(static_cast<int>(currentPromptMode()));
    rebuildPromptPreview();
}

void RoleEditorDialog::onBrowseFileReference()
{
    const QString startDir = m_filePathEdit && !m_filePathEdit->text().isEmpty()
                                 ? QFileInfo(m_filePathEdit->text()).absolutePath()
                                 : QString();
    const QString chosen = QFileDialog::getOpenFileName(
        this,
        tr("Choose prompt file"),
        startDir,
        tr("Prompt files (*.md *.txt *.prompt);;All files (*)"));

    if (chosen.isEmpty()) {
        return;
    }

    if (!m_filePathEdit) {
        return;
    }
    m_filePathEdit->setText(chosen);
    if (m_filePathEdit->text() != chosen) {
        m_filePathEdit->setText(chosen);
    }
}

void RoleEditorDialog::onLoadPromptFromFile()
{
    const QString startDir = QString();
    const QString chosen = QFileDialog::getOpenFileName(
        this,
        tr("Choose prompt file"),
        startDir,
        tr("Prompt files (*.md *.txt *.prompt);;All files (*)"));

    if (chosen.isEmpty()) {
        return;
    }

    if (m_promptFileRadio && !m_promptFileRadio->isChecked()) {
        if (m_promptModeGroup) {
            m_promptModeGroup->blockSignals(true);
        }
        m_promptFileRadio->setChecked(true);
        if (m_promptModeGroup) {
            m_promptModeGroup->blockSignals(false);
        }
        if (m_promptModeStack) {
            m_promptModeStack->setCurrentIndex(static_cast<int>(PromptModeReferenceFile));
        }
    }

    if (m_filePathEdit) {
        m_filePathEdit->setText(chosen);
    }
    if (m_promptModeStack) {
        m_promptModeStack->setCurrentIndex(static_cast<int>(currentPromptMode()));
    }
    rebuildPromptPreview();
}

void RoleEditorDialog::onInlinePromptTextChanged()
{
    m_cachedInlineText = currentInlinePromptText();
    rebuildPromptPreview();
}

void RoleEditorDialog::onFilePathChanged()
{
    m_cachedFilePath = currentFilePath();
    rebuildPromptPreview();
}

void RoleEditorDialog::rebuildPromptPreview()
{
    if (!m_promptPreviewBody || !m_promptPreviewHeader) {
        return;
    }

    const PromptMode mode = currentPromptMode();
    const QString roleName = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
    const QString roleId = m_idLabel ? m_idLabel->text().trimmed() : QString();

    QString title = roleName.isEmpty() ? QStringLiteral("(no name)") : roleName;
    if (!roleId.isEmpty() && roleId != roleName) {
        title += QStringLiteral(" [") + roleId + QStringLiteral("]");
    }
    const QString modeLabel = (mode == PromptModeReferenceFile)
                                  ? QStringLiteral("Reference file")
                                  : QStringLiteral("Inline text");
    const QString header = QStringLiteral("<b>Effective Prompt</b> &mdash; %1<br>"
                                          "<span style='color: gray;'>form: %2</span>")
                               .arg(title.toHtmlEscaped(), modeLabel.toHtmlEscaped());
    m_promptPreviewHeader->setText(header);

    QString body;
    QString tokenSource;

    if (mode == PromptModeReferenceFile) {
        const QString path = currentFilePath().trimmed();
        if (path.isEmpty()) {
            body = QStringLiteral("(no file path set — {\"file\": \"...\"} will be empty if you save now)");
            tokenSource = body;
        } else {
            body = renderFileReferenceDisplay(path);
            tokenSource = path;
        }
    } else {
        const QString t = currentInlinePromptText();
        if (t.trimmed().isEmpty()) {
            body = QStringLiteral("(no inline prompt set — the system_prompt field will be omitted if you save now)");
            tokenSource = t;
        } else {
            body = t;
            tokenSource = t;
        }
    }

    m_promptPreviewBody->setPlainText(body);

    if (m_promptPreviewTokenLabel) {
        const int tokens = approxTokenCount(tokenSource);
        m_promptPreviewTokenLabel->setText(tr("<span style='color: gray;'>"
                                               "approx. %1 tokens (%2 chars)</span>")
                                               .arg(tokens)
                                               .arg(tokenSource.size()));
    }
}

void RoleEditorDialog::loadFromRole(const Role &role)
{
    if (m_idLabel) {
        m_idLabel->setText(role.id);
    }
    if (m_nameEdit) {
        m_nameEdit->setText(role.name);
    }
    if (m_descriptionEdit) {
        m_descriptionEdit->setText(role.description);
    }
    if (m_modeCombo) {
        const int idx = m_modeCombo->findData(QVariant(static_cast<int>(role.mode)));
        m_modeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    QString initialInline;
    QString initialFilePath;
    const PromptMode detected = detectPromptMode(role.systemPrompt,
                                                 &initialInline,
                                                 &initialFilePath);

    m_cachedInlineText = initialInline;
    m_cachedFilePath = initialFilePath;

    if (m_promptModeGroup) {
        m_promptModeGroup->blockSignals(true);
    }
    if (m_promptInlineRadio) {
        m_promptInlineRadio->setChecked(detected == PromptModeInlineText);
    }
    if (m_promptFileRadio) {
        m_promptFileRadio->setChecked(detected == PromptModeReferenceFile);
    }
    if (m_promptModeStack) {
        m_promptModeStack->setCurrentIndex(static_cast<int>(detected));
    }
    if (m_promptModeGroup) {
        m_promptModeGroup->blockSignals(false);
    }

    if (m_systemPromptEdit) {
        m_systemPromptEdit->blockSignals(true);
        m_systemPromptEdit->setPlainText(initialInline);
        m_systemPromptEdit->moveCursor(QTextCursor::Start);
        m_systemPromptEdit->blockSignals(false);
    }

    if (m_filePathEdit) {
        m_filePathEdit->blockSignals(true);
        m_filePathEdit->setText(initialFilePath);
        m_filePathEdit->blockSignals(false);
    }

    if (m_permissionsTable) {
        populatePermissionsTable(role.permissions);
    }

    if (m_toolsList) {
        m_toolsList->clear();
        for (auto it = role.tools.constBegin(); it != role.tools.constEnd(); ++it) {
            appendListEntry(m_toolsList, it.key());
        }
    }

    if (m_metadataTable) {
        m_metadataTable->setRowCount(0);
        for (auto it = role.metadata.constBegin(); it != role.metadata.constEnd(); ++it) {
            appendTableRow(m_metadataTable, it.key(), jsonValueToDisplayText(it.value()));
        }
    }
}

void RoleEditorDialog::applyToRole(Role &role) const
{
    if (m_nameEdit) {
        role.name = m_nameEdit->text().trimmed();
    }
    if (m_descriptionEdit) {
        role.description = m_descriptionEdit->text().trimmed();
    }
    if (m_modeCombo) {
        const int idx = m_modeCombo->currentIndex();
        const int raw = m_modeCombo->itemData(idx).toInt();
        role.mode = static_cast<Role::Mode>(raw);
    }

    // Permissions table: key + QComboBox value per row.
    if (m_permissionsTable) {
        QJsonObject perms;
        auto *table = const_cast<QTableWidget *>(m_permissionsTable);
        for (int row = 0; row < table->rowCount(); ++row) {
            const QTableWidgetItem *keyItem = table->item(row, 0);
            if (!keyItem) {
                continue;
            }
            const QString key = keyItem->text().trimmed();
            if (key.isEmpty()) {
                continue;
            }
            const QString valueText = valueAtPermissionRow(row);
            perms.insert(key, QJsonValue(valueText));
        }
        role.permissions = perms;
    }

    // Tools list: each entry becomes a {"<name>": true} entry.
    if (m_toolsList) {
        QJsonObject tools;
        for (int i = 0; i < m_toolsList->count(); ++i) {
            const QString name = m_toolsList->item(i)->text().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            tools.insert(name, QJsonValue(true));
        }
        role.tools = tools;
    }

    // Metadata table: parse the value cell back to a QJsonValue.
    if (m_metadataTable) {
        QJsonObject metadata;
        for (int row = 0; row < m_metadataTable->rowCount(); ++row) {
            const QTableWidgetItem *keyItem = m_metadataTable->item(row, 0);
            if (!keyItem) {
                continue;
            }
            const QString key = keyItem->text().trimmed();
            if (key.isEmpty()) {
                continue;
            }
            const QTableWidgetItem *valItem = m_metadataTable->item(row, 1);
            const QString valueText = valItem ? valItem->text().trimmed() : QString();
            metadata.insert(key, parseMetadataValue(valueText));
        }
        role.metadata = metadata;
    }

    // systemPrompt: type-preserving round-trip.
    //
    // Goal: untouched fields must come back with the SAME QJsonValue
    // (string stays string, {"file": ...} stays {"file": ...}). When the
    // user actually changes the body we emit the form implied by the
    // current UI mode.
    const PromptMode mode = const_cast<RoleEditorDialog *>(this)->currentPromptMode();
    const QJsonValue original = role.systemPrompt;

    auto unchanged = [&](const QJsonValue &candidate) -> bool {
        if (original.type() != candidate.type()) {
            return false;
        }
        if (original.isString() && candidate.isString()) {
            return original.toString() == candidate.toString();
        }
        if (original.isObject() && candidate.isObject()) {
            return original.toObject() == candidate.toObject();
        }
        return original == candidate;
    };

    if (mode == PromptModeReferenceFile) {
        const QString path = currentFilePath().trimmed();
        if (path.isEmpty()) {
            // Empty path under reference-file mode is treated as "drop the
            // field" so we don't burn the original by writing {"file": ""}.
            if (!original.isNull() && !original.isUndefined()) {
                role.systemPrompt = QJsonValue();
            }
        } else {
            QJsonObject obj;
            obj.insert(QStringLiteral("file"), path);
            const QJsonValue candidate(obj);
            if (!unchanged(candidate)) {
                role.systemPrompt = candidate;
            }
        }
    } else {
        const QString text = currentInlinePromptText();
        if (text.isEmpty()) {
            if (!original.isNull() && !original.isUndefined()) {
                role.systemPrompt = QJsonValue();
            }
        } else {
            const QJsonValue candidate(text);
            if (!unchanged(candidate)) {
                role.systemPrompt = candidate;
            }
        }
    }
}

Role RoleEditorDialog::roleData() const
{
    Role result = m_initialRole;
    applyToRole(result);
    return result;
}

void RoleEditorDialog::populatePermissionsTable(const QJsonObject &perms)
{
    if (!m_permissionsTable) {
        return;
    }

    m_permissionsTable->blockSignals(true);
    m_permissionsTable->setRowCount(0);

    const QStringList canonical = canonicalPermissionKeysOrdered();
    const QSet<QString> canonicalSet(canonical.begin(), canonical.end());

    // 1) Lay down the 15 canonical rows with sensible defaults; override
    //    the value cell from role.permissions when present.
    for (const QString &key : canonical) {
        QString value = defaultPermissionValueFor(key);
        if (perms.contains(key)) {
            const QString fromString = valueToPermissionString(perms.value(key));
            const QString fromObject = valueFromObjectPermission(perms.value(key));
            const QString candidate = !fromString.isEmpty() ? fromString : fromObject;
            if (!candidate.isEmpty()) {
                value = candidate;
            }
        }
        appendPermissionRow(key, value, permissionDescriptionFor(key), true);
    }

    // 2) Append any extra / custom keys at the bottom, sorted for stability.
    QStringList extraKeys;
    for (auto it = perms.constBegin(); it != perms.constEnd(); ++it) {
        if (!canonicalSet.contains(it.key())) {
            extraKeys.append(it.key());
        }
    }
    std::sort(extraKeys.begin(), extraKeys.end());

    QStringList canonicalDesc;
    if (!extraKeys.isEmpty()) {
        canonicalDesc.append(tr("(custom)"));
    }

    QJsonObject customSectionHeader;
    Q_UNUSED(customSectionHeader);

    for (const QString &key : extraKeys) {
        QString value = valueToPermissionString(perms.value(key));
        if (value.isEmpty()) {
            value = valueFromObjectPermission(perms.value(key));
        }
        if (value.isEmpty()) {
            value = QStringLiteral("ask");
        }
        if (canonicalSet.contains(key)) {
            continue; // safety: skip if somehow seen twice
        }
        appendPermissionRow(key,
                            value,
                            permissionDescriptionFor(key),
                            false);
    }

    m_permissionsTable->blockSignals(false);
}

void RoleEditorDialog::appendPermissionRow(const QString &key,
                                           const QString &value,
                                           const QString &description,
                                           bool canonical)
{
    if (!m_permissionsTable) {
        return;
    }
    QTableWidget *table = m_permissionsTable;
    const int row = table->rowCount();
    table->insertRow(row);

    auto *keyItem = new QTableWidgetItem(key);
    keyItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!canonical) {
        QFont f = keyItem->font();
        f.setItalic(true);
        keyItem->setFont(f);
    }
    table->setItem(row, 0, keyItem);

    auto *combo = new QComboBox(table);
    combo->setObjectName(QStringLiteral("roleEditor.permissionValueCombo"));
    combo->addItem(QStringLiteral("ask"));
    combo->addItem(QStringLiteral("allow"));
    combo->addItem(QStringLiteral("deny"));
    const int idx = combo->findText(value);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->setToolTip(tr("Action: ask (confirm every time), allow (silently execute), deny (block)."));
    combo->setWhatsThis(tr("Writes into opencode.json as the value for this permission key."));
    table->setCellWidget(row, 1, combo);

    auto *descItem = new QTableWidgetItem(description);
    descItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    descItem->setToolTip(description);
    table->setItem(row, 2, descItem);

    tintPermissionValueCombo(row);

    connect(combo, qOverload<const QString &>(&QComboBox::currentTextChanged),
            table, [this, row](const QString &) {
                tintPermissionValueCombo(row);
            });
}

QString RoleEditorDialog::valueAtPermissionRow(int row) const
{
    if (!m_permissionsTable) {
        return QString();
    }
    if (row < 0 || row >= m_permissionsTable->rowCount()) {
        return QString();
    }
    auto *cell = qobject_cast<QComboBox *>(m_permissionsTable->cellWidget(row, 1));
    if (!cell) {
        // Fallback: look for a plain text item (rare — only when the
        // table is in legacy 2-column mode). Returns trimmed value.
        const QTableWidgetItem *it = m_permissionsTable->item(row, 1);
        return it ? it->text().trimmed() : QString();
    }
    return cell->currentText().trimmed();
}

void RoleEditorDialog::tintPermissionValueCombo(int row)
{
    if (!m_permissionsTable) {
        return;
    }
    if (row < 0 || row >= m_permissionsTable->rowCount()) {
        return;
    }
    auto *cell = qobject_cast<QComboBox *>(m_permissionsTable->cellWidget(row, 1));
    if (!cell) {
        return;
    }
    const QString value = cell->currentText();
    cell->setProperty("permissionValue", value);
    if (cell->style()) {
        cell->style()->unpolish(cell);
        cell->style()->polish(cell);
        cell->update();
    }
}

void RoleEditorDialog::onPermissionComboChanged(int row)
{
    tintPermissionValueCombo(row);
}

void RoleEditorDialog::onResetPermissionsToDefaults()
{
    if (!m_permissionsTable) {
        return;
    }
    populatePermissionsTable({});
}

QStringList RoleEditorDialog::canonicalPermissionKeys()
{
    return canonicalPermissionKeysOrdered();
}

QSet<QString> RoleEditorDialog::readOnlyPermissionKeys()
{
    return readOnlyPermissionKeysSet();
}

QString RoleEditorDialog::defaultPermissionValueFor(const QString &key)
{
    return readOnlyPermissionKeysSet().contains(key)
               ? QStringLiteral("allow")
               : QStringLiteral("ask");
}

QString RoleEditorDialog::permissionDescriptionFor(const QString &key)
{
    if (key == QLatin1String("read")) {
        return tr("Read file contents (read-only).");
    }
    if (key == QLatin1String("edit")) {
        return tr("Modify files in the working directory.");
    }
    if (key == QLatin1String("glob")) {
        return tr("Match files by glob patterns.");
    }
    if (key == QLatin1String("grep")) {
        return tr("Search file contents with grep/regex.");
    }
    if (key == QLatin1String("list")) {
        return tr("List directory entries.");
    }
    if (key == QLatin1String("bash")) {
        return tr("Execute shell commands.");
    }
    if (key == QLatin1String("task")) {
        return tr("Delegate work to sub-agents (mandatory `allow` for subagent-mode roles).");
    }
    if (key == QLatin1String("external_directory")) {
        return tr("Access files outside the working directory.");
    }
    if (key == QLatin1String("lsp")) {
        return tr("Query the language server protocol (refactors, go-to-def, etc).");
    }
    if (key == QLatin1String("skill")) {
        return tr("Invoke registered skills.");
    }
    if (key == QLatin1String("todowrite")) {
        return tr("Write and edit the todo list.");
    }
    if (key == QLatin1String("question")) {
        return tr("Present a clarifying question to the user.");
    }
    if (key == QLatin1String("webfetch")) {
        return tr("Fetch content from a URL.");
    }
    if (key == QLatin1String("websearch")) {
        return tr("Search the public web.");
    }
    if (key == QLatin1String("doom_loop")) {
        return tr("Guard against repeated identical tool calls.");
    }
    return tr("Custom permission key — values round-trip through opencode.json.");
}

void RoleEditorDialog::accept()
{
    const Role updated = roleData();

    if (updated.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Role"), tr("Role must have a name."));
        return;
    }

    QDialog::accept();
}
