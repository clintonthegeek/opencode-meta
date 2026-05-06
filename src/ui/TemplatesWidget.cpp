#include "TemplatesWidget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "models/Template.h"
#include "storage/StorageManager.h"
#include "ui/TemplateEditorDialog.h"

namespace {

QString formatTemplateSummary(const Template &t)
{
    int primaryCount = 0;
    int subagentCount = 0;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        if (it.value().mode == AgentDef::Mode::Primary) {
            ++primaryCount;
        } else {
            ++subagentCount;
        }
    }

    const int totalAgents = primaryCount + subagentCount;
    const QString modified = t.metadata.value(QStringLiteral("modified"));

    QString text = t.name;
    text += QStringLiteral("  (");
    text += QString::number(totalAgents);
    text += QStringLiteral(" agents: ");
    text += QString::number(primaryCount);
    text += QStringLiteral(" primary, ");
    text += QString::number(subagentCount);
    text += QStringLiteral(" subagents");
    if (!modified.isEmpty()) {
        text += QStringLiteral(", modified: ");
        text += modified;
    }
    text += QLatin1Char(')');

    return text;
}

} // namespace

TemplatesWidget::TemplatesWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    setupUi();
    refreshTemplates();
}

void TemplatesWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget, 1);

    auto *buttonRow = new QHBoxLayout();

    m_createButton = new QPushButton(tr("Create New"), this);
    m_editButton = new QPushButton(tr("Edit"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_exportButton = new QPushButton(tr("Export"), this);

    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_duplicateButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_exportButton);

    layout->addLayout(buttonRow);

    connect(m_createButton, &QPushButton::clicked, this, &TemplatesWidget::createTemplate);
    connect(m_editButton, &QPushButton::clicked, this, &TemplatesWidget::editSelectedTemplate);
    connect(m_duplicateButton, &QPushButton::clicked, this, &TemplatesWidget::duplicateSelectedTemplate);
    connect(m_deleteButton, &QPushButton::clicked, this, &TemplatesWidget::deleteSelectedTemplate);
    connect(m_exportButton, &QPushButton::clicked, this, &TemplatesWidget::exportSelectedTemplate);
}

void TemplatesWidget::refreshTemplates()
{
    m_listWidget->clear();

    const QList<Template> templates = m_storageManager.listTemplates();
    for (const Template &t : templates) {
        auto *item = new QListWidgetItem(formatTemplateSummary(t), m_listWidget);
        item->setData(Qt::UserRole, t.id);
        item->setToolTip(t.name);
    }
}

QString TemplatesWidget::selectedTemplateId() const
{
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

void TemplatesWidget::createTemplate()
{
    Template t;

    TemplateEditorDialog dlg(t, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Template updated = dlg.templateData();
    if (updated.id.isEmpty()) {
        updated.id = updated.name;
    }

    if (!m_storageManager.saveTemplate(updated)) {
        QMessageBox::warning(this, tr("Save Template"), tr("Failed to save template."));
        return;
    }

    refreshTemplates();
}

void TemplatesWidget::editSelectedTemplate()
{
    const QString id = selectedTemplateId();
    if (id.isEmpty()) {
        return;
    }

    Template t = m_storageManager.loadTemplate(id);
    if (t.id.isEmpty()) {
        QMessageBox::warning(this, tr("Edit Template"), tr("Failed to load template."));
        return;
    }

    TemplateEditorDialog dlg(t, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Template updated = dlg.templateData();
    if (updated.id.isEmpty()) {
        updated.id = t.id;
    }

    if (!m_storageManager.saveTemplate(updated)) {
        QMessageBox::warning(this, tr("Save Template"), tr("Failed to save template."));
        return;
    }

    refreshTemplates();
}

void TemplatesWidget::duplicateSelectedTemplate()
{
    const QString id = selectedTemplateId();
    if (id.isEmpty()) {
        return;
    }

    Template t = m_storageManager.loadTemplate(id);
    if (t.id.isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate Template"), tr("Failed to load template."));
        return;
    }

    t.id.clear();
    t.name += QStringLiteral(" (copy)");

    TemplateEditorDialog dlg(t, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Template updated = dlg.templateData();
    if (updated.id.isEmpty()) {
        updated.id = updated.name;
    }

    if (!m_storageManager.saveTemplate(updated)) {
        QMessageBox::warning(this, tr("Save Template"), tr("Failed to save duplicated template."));
        return;
    }

    refreshTemplates();
}

void TemplatesWidget::deleteSelectedTemplate()
{
    const QString id = selectedTemplateId();
    if (id.isEmpty()) {
        return;
    }

    const auto reply = QMessageBox::question(this, tr("Delete Template"), tr("Delete selected template?"));
    if (reply != QMessageBox::Yes) {
        return;
    }

    // For now, deleting means removing the template directory on disk.
    // StorageManager doesn't expose this directly, keep it minimal.
    const QString path = QDir::homePath() + QStringLiteral("/.opencode-meta/templates/%1").arg(id);
    QDir dir(path);
    if (!dir.removeRecursively()) {
        QMessageBox::warning(this, tr("Delete Template"), tr("Failed to delete template directory."));
    }

    refreshTemplates();
}

void TemplatesWidget::exportSelectedTemplate()
{
    const QString id = selectedTemplateId();
    if (id.isEmpty()) {
        return;
    }

    Template t = m_storageManager.loadTemplate(id);
    if (t.id.isEmpty()) {
        QMessageBox::warning(this, tr("Export Template"), tr("Failed to load template."));
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Template"), t.name + QStringLiteral(".json"), tr("JSON Files (*.json)"));
    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Template"), tr("Failed to open file for writing."));
        return;
    }

    const QJsonDocument doc(t.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
}
