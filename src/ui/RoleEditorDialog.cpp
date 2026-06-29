#include "ui/RoleEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
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
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QVBoxLayout>

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
        // Use QString::number so 7 prints as "7" (not "7.0").
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isObject()) {
        const QJsonDocument doc(value.toObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    if (value.isArray()) {
        const QJsonDocument doc(value.toArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    // Null / undefined -> empty cell so the table still shows the row.
    return QString();
}

QJsonValue RoleEditorDialog::parseMetadataValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QJsonValue(QString());
    }

    // Booleans.
    if (trimmed == QLatin1String("true")) {
        return QJsonValue(true);
    }
    if (trimmed == QLatin1String("false")) {
        return QJsonValue(false);
    }

    // Numbers: integer or rational. Use std::isdigit via manual scan to
    // avoid pulling <cctype> for the sake of one check.
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

    // Try to parse as JSON object or array.
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

    // Default: treat as plain string.
    return QJsonValue(trimmed);
}

RoleEditorDialog::RoleEditorDialog(const Role &role, QWidget *parent)
    : QDialog(parent)
    , m_initialRole(role)
{
    setupUi();
    setupTabs();
    loadFromRole(role);
}

void RoleEditorDialog::setupUi()
{
    setWindowTitle(tr("Role Editor"));
    resize(720, 560);

    auto *mainLayout = new QVBoxLayout(this);

    // Header area: id (read-only label) + name + description + mode.
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
    auto *promptHint = new QLabel(tr("System prompt (string form)"), promptTab);
    m_systemPromptEdit = new QPlainTextEdit(promptTab);
    m_systemPromptEdit->setObjectName(QStringLiteral("roleEditor.systemPromptEdit"));
    m_systemPromptEdit->setPlaceholderText(tr("Enter system prompt for this role..."));
    m_systemPromptEdit->setToolTip(tr("Inline string system prompt for this agent."));
    m_systemPromptEdit->setWhatsThis(tr("Writes into the opencode.json `system_prompt` field as a plain string."));
    promptLayout->addWidget(promptHint);
    promptLayout->addWidget(m_systemPromptEdit, 1);
    m_tabWidget->addTab(promptTab, tr("Prompt"));
    m_tabWidget->setTabToolTip(0, tr("The body of the agent prompt itself."));

    // --- Permissions tab --------------------------------------------
    auto *permsTab = new QWidget(this);
    auto *permsLayout = new QVBoxLayout(permsTab);
    permsLayout->setContentsMargins(8, 8, 8, 8);
    auto *permsHint = new QLabel(
        tr("Permissions — one row per key (e.g. edit / bash / webfetch). Values stay as strings."),
        permsTab);
    permsHint->setWordWrap(true);
    m_permissionsTable = new QTableWidget(0, 2, permsTab);
    m_permissionsTable->setObjectName(QStringLiteral("roleEditor.permissionsTable"));
    m_permissionsTable->setHorizontalHeaderLabels(QStringList{ tr("Key"), tr("Value") });
    m_permissionsTable->horizontalHeader()->setStretchLastSection(true);
    m_permissionsTable->verticalHeader()->setVisible(false);
    m_permissionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_permissionsTable->setToolTip(tr("Edit per-tool permission profiles (ask / deny / allow)."));
    m_permissionsTable->setWhatsThis(tr("Key/value pairs written to opencode.json `permissions` on the agent entry."));
    permsLayout->addWidget(permsHint);
    permsLayout->addWidget(m_permissionsTable, 1);
    m_tabWidget->addTab(permsTab, tr("Permissions"));
    m_tabWidget->setTabToolTip(1, tr("Per-tool allow / deny / ask overlay."));

    // --- Tools tab ---------------------------------------------------
    auto *toolsTab = new QWidget(this);
    auto *toolsLayout = new QVBoxLayout(toolsTab);
    toolsLayout->setContentsMargins(8, 8, 8, 8);
    auto *toolsHint = new QLabel(tr("Tools exposed to this role."), toolsTab);
    m_toolNameEdit = new QLineEdit(toolsTab);
    m_toolNameEdit->setObjectName(QStringLiteral("roleEditor.toolNameEdit"));
    m_toolNameEdit->setPlaceholderText(tr("e.g. bash, read, edit"));
    m_toolNameEdit->setToolTip(tr("Type a tool name and press Add."));
    m_toolNameEdit->setWhatsThis(tr("Each entry maps to {\"<name>\": true} in the agent `tools` object."));
    m_addToolButton = new QPushButton(tr("Add"), toolsTab);
    m_addToolButton->setObjectName(QStringLiteral("roleEditor.addToolButton"));
    m_addToolButton->setToolTip(tr("Append the typed tool name to the list."));
    m_toolsList = new QListWidget(toolsTab);
    m_toolsList->setObjectName(QStringLiteral("roleEditor.toolsList"));
    m_toolsList->setToolTip(tr("Tools currently registered for this role."));

    auto *addRowLayout = new QHBoxLayout();
    addRowLayout->addWidget(m_toolNameEdit, 1);
    addRowLayout->addWidget(m_addToolButton);

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
        // Skip duplicates — keep the list tidy and avoid round-trip churn.
        for (int i = 0; i < m_toolsList->count(); ++i) {
            if (m_toolsList->item(i)->text() == name) {
                m_toolNameEdit->clear();
                return;
            }
        }
        appendListEntry(m_toolsList, name);
        m_toolNameEdit->clear();
    });

    m_tabWidget->addTab(toolsTab, tr("Tools"));
    m_tabWidget->setTabToolTip(2, tr("List the tools this agent is allowed to call."));

    // --- Metadata tab ------------------------------------------------
    auto *metaTab = new QWidget(this);
    auto *metaLayout = new QVBoxLayout(metaTab);
    metaLayout->setContentsMargins(8, 8, 8, 8);
    auto *metaHint = new QLabel(
        tr("Metadata — one row per key. Values can be plain strings, numbers (e.g. 7), "
           "booleans (true / false) or nested JSON ({...} / [...])."),
        metaTab);
    metaHint->setWordWrap(true);
    m_metadataTable = new QTableWidget(0, 2, metaTab);
    m_metadataTable->setObjectName(QStringLiteral("roleEditor.metadataTable"));
    m_metadataTable->setHorizontalHeaderLabels(QStringList{ tr("Key"), tr("Value") });
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->verticalHeader()->setVisible(false);
    m_metadataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_metadataTable->setToolTip(tr("Free-form metadata for this role (tags, notes, timestamps)."));
    m_metadataTable->setWhatsThis(tr("Key/value pairs written to opencode.json `metadata` on the agent entry."));
    metaLayout->addWidget(metaHint);
    metaLayout->addWidget(m_metadataTable, 1);
    m_tabWidget->addTab(metaTab, tr("Metadata"));
    m_tabWidget->setTabToolTip(3, tr("Free-form metadata attached to this agent."));
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

    if (m_systemPromptEdit) {
        m_systemPromptEdit->setPlainText(jsonValueToDisplayText(role.systemPrompt));
        m_systemPromptEdit->moveCursor(QTextCursor::Start);
    }

    if (m_permissionsTable) {
        m_permissionsTable->setRowCount(0);
        for (auto it = role.permissions.constBegin(); it != role.permissions.constEnd(); ++it) {
            appendTableRow(m_permissionsTable, it.key(), jsonValueToDisplayText(it.value()));
        }
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

    // Permissions table: key + string value per row.
    if (m_permissionsTable) {
        QJsonObject perms;
        for (int row = 0; row < m_permissionsTable->rowCount(); ++row) {
            const QTableWidgetItem *keyItem = m_permissionsTable->item(row, 0);
            if (!keyItem) {
                continue;
            }
            const QString key = keyItem->text().trimmed();
            if (key.isEmpty()) {
                continue;
            }
            const QTableWidgetItem *valItem = m_permissionsTable->item(row, 1);
            const QString valueText = valItem ? valItem->text().trimmed() : QString();
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

    // Preserve the original systemPrompt type unless the user actually
    // edited the prompt text — same behaviour as the pre-tabbed dialog.
    if (m_systemPromptEdit) {
        const QString newPromptText = m_systemPromptEdit->toPlainText();
        const QString originalPromptText = jsonValueToDisplayText(role.systemPrompt);
        if (newPromptText.trimmed() != originalPromptText.trimmed()) {
            role.systemPrompt = QJsonValue(newPromptText);
        }
    }
}

Role RoleEditorDialog::roleData() const
{
    Role result = m_initialRole;
    applyToRole(result);
    return result;
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
