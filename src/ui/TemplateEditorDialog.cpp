#include "TemplateEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "adapter/OpencodeSchemaAdapter.h"
#include "models/Template.h"

enum Columns {
    ColName = 0,
    ColMode,
    ColDescription,
    ColPrompt,
    ColEdit,
    ColBash,
    ColTask,
    ColRead,
    ColGrep,
    ColGlob,
    ColTools,
    ColCount
};

TemplateEditorDialog::TemplateEditorDialog(const Template &t, QWidget *parent)
    : QDialog(parent)
    , m_initialTemplate(new Template(t))
{
    setupUi();
    loadFromTemplate(t);
}

void TemplateEditorDialog::setupUi()
{
    setWindowTitle(tr("Template Editor"));

    auto *layout = new QVBoxLayout(this);

    // Top-level template metadata
    auto *formLayout = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_versionEdit = new QLineEdit(this);
    m_defaultAgentCombo = new QComboBox(this);
    m_defaultAgentCombo->setEditable(false);
    formLayout->addRow(tr("Name"), m_nameEdit);
    formLayout->addRow(tr("Version"), m_versionEdit);
    formLayout->addRow(tr("Default agent"), m_defaultAgentCombo);
    layout->addLayout(formLayout);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    QStringList headers;
    headers << tr("Name")
            << tr("Mode")
            << tr("Description")
            << tr("Prompt")
            << tr("Edit")
            << tr("Bash")
            << tr("Task")
            << tr("Read")
            << tr("Grep")
            << tr("Glob")
            << tr("Tools JSON");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table, 1);

    auto *buttonsRow = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add Agent"), this);
    m_removeButton = new QPushButton(tr("Remove Agent"), this);
    buttonsRow->addWidget(m_addButton);
    buttonsRow->addWidget(m_removeButton);
    buttonsRow->addStretch(1);
    layout->addLayout(buttonsRow);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TemplateEditorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &TemplateEditorDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_addButton, &QPushButton::clicked, this, &TemplateEditorDialog::addAgentRow);
    connect(m_removeButton, &QPushButton::clicked, this, &TemplateEditorDialog::removeSelectedRow);

    connect(m_table, &QTableWidget::itemChanged,
            this, &TemplateEditorDialog::onTableItemChanged);
}

void TemplateEditorDialog::loadFromTemplate(const Template &t)
{
    m_nameEdit->setText(t.name);
    m_versionEdit->setText(t.version);

    m_table->setRowCount(0);

    int row = 0;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(it.key());
        m_table->setItem(row, ColName, nameItem);

        const AgentDef &def = it.value();

        QString modeStr = AgentDef::modeToString(def.mode);
        auto *modeItem = new QTableWidgetItem(modeStr);
        m_table->setItem(row, ColMode, modeItem);

        auto *descItem = new QTableWidgetItem(def.description);
        m_table->setItem(row, ColDescription, descItem);

        QString promptText;
        if (def.prompt.isString()) {
            promptText = def.prompt.toString();
        }
        auto *promptItem = new QTableWidgetItem(promptText);
        m_table->setItem(row, ColPrompt, promptItem);

        auto *editItem = new QTableWidgetItem(AgentDef::permissionToString(def.edit));
        m_table->setItem(row, ColEdit, editItem);

        auto *bashItem = new QTableWidgetItem(AgentDef::permissionToString(def.bash));
        m_table->setItem(row, ColBash, bashItem);

        auto *taskItem = new QTableWidgetItem(AgentDef::permissionToString(def.task));
        m_table->setItem(row, ColTask, taskItem);

        auto *readItem = new QTableWidgetItem(AgentDef::permissionToString(def.read));
        m_table->setItem(row, ColRead, readItem);

        auto *grepItem = new QTableWidgetItem(AgentDef::permissionToString(def.grep));
        m_table->setItem(row, ColGrep, grepItem);

        auto *globItem = new QTableWidgetItem(AgentDef::permissionToString(def.glob));
        m_table->setItem(row, ColGlob, globItem);

        QString toolsText;
        if (!def.tools.isEmpty()) {
            toolsText = QString::fromUtf8(QJsonDocument(def.tools).toJson(QJsonDocument::Compact));
        }
        auto *toolsItem = new QTableWidgetItem(toolsText);
        m_table->setItem(row, ColTools, toolsItem);

        ++row;
    }

    rebuildDefaultAgentCombo(t.defaultAgent);
}

void TemplateEditorDialog::applyToTemplate(Template &t) const
{
    t.agents.clear();

    const int rows = m_table->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString name = m_table->item(row, ColName) ? m_table->item(row, ColName)->text() : QString();
        if (name.isEmpty()) {
            continue;
        }

        AgentDef def;
        const QString modeStr = m_table->item(row, ColMode) ? m_table->item(row, ColMode)->text() : QStringLiteral("primary");
        def.mode = AgentDef::modeFromString(modeStr);

        def.description = m_table->item(row, ColDescription) ? m_table->item(row, ColDescription)->text() : QString();

        const QString promptText = m_table->item(row, ColPrompt) ? m_table->item(row, ColPrompt)->text() : QString();
        def.prompt = QJsonValue(promptText);

        auto permissionFromCell = [this, row](int col) {
            const QTableWidgetItem *item = m_table->item(row, col);
            const QString value = item ? item->text() : QStringLiteral("ask");
            return AgentDef::permissionFromString(value);
        };

        def.edit = permissionFromCell(ColEdit);
        def.bash = permissionFromCell(ColBash);
        def.task = permissionFromCell(ColTask);
        def.read = permissionFromCell(ColRead);
        def.grep = permissionFromCell(ColGrep);
        def.glob = permissionFromCell(ColGlob);

        // Tools JSON is treated as an optional advanced field. Invalid JSON
        // is silently ignored to avoid surprising hard failures in the UI.
        def.tools = QJsonObject();
        const QTableWidgetItem *toolsItem = m_table->item(row, ColTools);
        if (toolsItem) {
            const QString toolsText = toolsItem->text().trimmed();
            if (!toolsText.isEmpty()) {
                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(toolsText.toUtf8(), &parseError);
                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    def.tools = doc.object();
                }
            }
        }

        t.agents.insert(name, def);
    }
}

Template TemplateEditorDialog::templateData() const
{
    Template result;
    if (m_initialTemplate) {
        result = *m_initialTemplate;
    }

    applyToTemplate(result);

    result.name = m_nameEdit ? m_nameEdit->text().trimmed() : result.name;
    result.version = m_versionEdit ? m_versionEdit->text().trimmed() : result.version;
    if (m_defaultAgentCombo) {
        result.defaultAgent = m_defaultAgentCombo->currentText().trimmed();
    }
    return result;
}

void TemplateEditorDialog::addAgentRow()
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *nameItem = new QTableWidgetItem(QStringLiteral("agent"));
    m_table->setItem(row, ColName, nameItem);

    auto *modeItem = new QTableWidgetItem(QStringLiteral("primary"));
    m_table->setItem(row, ColMode, modeItem);

    auto *editItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColEdit, editItem);

    auto *bashItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColBash, bashItem);

    auto *taskItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColTask, taskItem);

    auto *readItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColRead, readItem);

    auto *grepItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColGrep, grepItem);

    auto *globItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColGlob, globItem);

    rebuildDefaultAgentCombo();
}

void TemplateEditorDialog::removeSelectedRow()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    m_table->removeRow(row);

    rebuildDefaultAgentCombo();
}

void TemplateEditorDialog::onTableItemChanged(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }

    if (item->column() == ColName) {
        rebuildDefaultAgentCombo();
    }
}

void TemplateEditorDialog::rebuildDefaultAgentCombo(const QString &preferredSelection)
{
    if (!m_defaultAgentCombo) {
        return;
    }

    const QString current = preferredSelection.isEmpty() ? m_defaultAgentCombo->currentText() : preferredSelection;

    m_defaultAgentCombo->clear();

    QStringList agentNames;
    const int rows = m_table ? m_table->rowCount() : 0;
    for (int row = 0; row < rows; ++row) {
        const QTableWidgetItem *nameItem = m_table->item(row, ColName);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        if (!name.isEmpty()) {
            agentNames.append(name);
        }
    }

    for (const QString &name : agentNames) {
        m_defaultAgentCombo->addItem(name);
    }

    int index = -1;
    if (!current.isEmpty()) {
        index = m_defaultAgentCombo->findText(current);
    }
    if (index < 0 && !agentNames.isEmpty()) {
        index = 0;
    }
    if (index >= 0) {
        m_defaultAgentCombo->setCurrentIndex(index);
    }
}

void TemplateEditorDialog::accept()
{
    Template result = templateData();

    if (result.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Template"), tr("Template must have a name."));
        return;
    }

    const auto validationError = OpencodeSchemaAdapter::validate(result);
    if (validationError.has_value()) {
        QMessageBox::warning(this, tr("Invalid Template"), *validationError);
        return;
    }

    if (m_initialTemplate) {
        *m_initialTemplate = result;
    }

    QDialog::accept();
}
