#include "ProfileEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "models/Profile.h"
#include "models/Template.h"

enum Columns {
    ColAgentName = 0,
    ColModel,
    ColCount
};

ProfileEditorDialog::ProfileEditorDialog(const QList<Template> &templates,
                                         const Profile &profile,
                                         const QString &defaultModelId,
                                         QWidget *parent)
    : QDialog(parent)
    , m_templates(templates)
    , m_initialProfile(new Profile(profile))
    , m_defaultModelId(defaultModelId)
{
    setupUi();
    loadFromProfile(profile);
}

void ProfileEditorDialog::setupUi()
{
    setWindowTitle(tr("Profile Editor"));

    auto *layout = new QVBoxLayout(this);

    auto *templateRow = new QHBoxLayout();
    auto *templateLabel = new QLabel(tr("Base Template:"), this);
    m_templateCombo = new QComboBox(this);
    templateRow->addWidget(templateLabel);
    templateRow->addWidget(m_templateCombo, 1);
    layout->addLayout(templateRow);

    for (const Template &t : m_templates) {
        m_templateCombo->addItem(t.name, t.id);
    }

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    QStringList headers;
    headers << tr("Agent") << tr("Model ID");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table, 1);

    auto *overridesRow = new QHBoxLayout();
    auto *smallModelLabel = new QLabel(tr("Global small_model:"), this);
    m_smallModelEdit = new QLineEdit(this);
    overridesRow->addWidget(smallModelLabel);
    overridesRow->addWidget(m_smallModelEdit, 1);
    layout->addLayout(overridesRow);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ProfileEditorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ProfileEditorDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProfileEditorDialog::onTemplateChanged);
}

void ProfileEditorDialog::loadFromProfile(const Profile &profile)
{
    // Select template based on profile.templateId, default to first.
    int indexToSelect = 0;
    if (!profile.templateId.isEmpty()) {
        const int count = m_templateCombo->count();
        for (int i = 0; i < count; ++i) {
            if (m_templateCombo->itemData(i).toString() == profile.templateId) {
                indexToSelect = i;
                break;
            }
        }
    }
    m_templateCombo->setCurrentIndex(indexToSelect);

    // Global overrides (small_model only for now).
    const QString smallModel = profile.globalOverrides.value(QStringLiteral("small_model")).toString();
    m_smallModelEdit->setText(smallModel);

    const Template *t = currentTemplate();
    if (t) {
        rebuildAgentRowsForTemplate(*t, profile);
    }
}

const Template *ProfileEditorDialog::currentTemplate() const
{
    const int index = m_templateCombo->currentIndex();
    if (index < 0 || index >= m_templates.size()) {
        return nullptr;
    }
    return &m_templates.at(index);
}

void ProfileEditorDialog::rebuildAgentRowsForTemplate(const Template &t, const Profile &profile)
{
    m_table->setRowCount(0);

    int row = 0;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        const QString agentName = it.key();
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(agentName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColAgentName, nameItem);

        const QString modelId = profile.modelAssignments.value(agentName, m_defaultModelId);
        auto *modelItem = new QTableWidgetItem(modelId);
        m_table->setItem(row, ColModel, modelItem);

        ++row;
    }
}

void ProfileEditorDialog::onTemplateChanged(int)
{
    const Template *t = currentTemplate();
    if (!t || !m_initialProfile) {
        return;
    }
    rebuildAgentRowsForTemplate(*t, *m_initialProfile);
}

Profile ProfileEditorDialog::profileData() const
{
    Profile result;
    if (m_initialProfile) {
        result = *m_initialProfile;
    }

    const Template *t = currentTemplate();
    if (t) {
        result.templateId = t->id;
    }

    // Collect model assignments from the table.
    result.modelAssignments.clear();
    const int rows = m_table->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QTableWidgetItem *nameItem = m_table->item(row, ColAgentName);
        const QTableWidgetItem *modelItem = m_table->item(row, ColModel);
        const QString agentName = nameItem ? nameItem->text() : QString();
        const QString modelId = modelItem ? modelItem->text() : QString();
        if (agentName.isEmpty() || modelId.isEmpty()) {
            continue;
        }
        result.modelAssignments.insert(agentName, modelId);
    }

    // Global overrides: keep existing keys but update small_model.
    QJsonObject overrides = result.globalOverrides;
    const QString smallModel = m_smallModelEdit->text().trimmed();
    if (!smallModel.isEmpty()) {
        overrides.insert(QStringLiteral("small_model"), smallModel);
    } else {
        overrides.remove(QStringLiteral("small_model"));
    }
    result.globalOverrides = overrides;

    return result;
}
