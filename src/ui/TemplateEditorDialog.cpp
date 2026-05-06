#include "TemplateEditorDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "models/Template.h"

enum Columns {
    ColName = 0,
    ColMode,
    ColDescription,
    ColPrompt,
    ColPermission,
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

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    QStringList headers;
    headers << tr("Name") << tr("Mode") << tr("Description") << tr("Prompt") << tr("Permissions");
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
}

void TemplateEditorDialog::loadFromTemplate(const Template &t)
{
    m_table->setRowCount(0);

    int row = 0;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(it.key());
        m_table->setItem(row, ColName, nameItem);

        QString modeStr = AgentDef::modeToString(it.value().mode);
        auto *modeItem = new QTableWidgetItem(modeStr);
        m_table->setItem(row, ColMode, modeItem);

        auto *descItem = new QTableWidgetItem(it.value().description);
        m_table->setItem(row, ColDescription, descItem);

        QString promptText;
        if (it.value().prompt.isString()) {
            promptText = it.value().prompt.toString();
        }
        auto *promptItem = new QTableWidgetItem(promptText);
        m_table->setItem(row, ColPrompt, promptItem);

        // Simplified permissions: store the main edit permission as string
        const QString permStr = AgentDef::permissionToString(it.value().edit);
        auto *permItem = new QTableWidgetItem(permStr);
        m_table->setItem(row, ColPermission, permItem);

        ++row;
    }
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

        const QString permStr = m_table->item(row, ColPermission) ? m_table->item(row, ColPermission)->text() : QStringLiteral("ask");
        def.edit = AgentDef::permissionFromString(permStr);
        // Keep other permissions default/ask for now to keep UI simple

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

    auto *permItem = new QTableWidgetItem(QStringLiteral("ask"));
    m_table->setItem(row, ColPermission, permItem);
}

void TemplateEditorDialog::removeSelectedRow()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    m_table->removeRow(row);
}
