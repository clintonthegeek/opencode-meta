#include "ui/RoleEditorDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

#include "models/Role.h"

namespace {

// Produce a human-editable text representation of the systemPrompt field.
QString systemPromptToDisplayText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }

    if (value.isObject()) {
        // For object prompts (e.g. {"file": "..."}) show JSON so the user
        // can intentionally replace it with an inline string if desired.
        const QJsonObject obj = value.toObject();
        const QJsonDocument doc(obj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    return QString();
}

} // namespace

RoleEditorDialog::RoleEditorDialog(const Role &role, QWidget *parent)
    : QDialog(parent)
    , m_initialRole(role)
{
    setupUi();
    loadFromRole(role);
}

void RoleEditorDialog::setupUi()
{
    setWindowTitle(tr("Role Editor"));
    resize(700, 500);

    auto *mainLayout = new QVBoxLayout(this);

    auto *formLayout = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    formLayout->addRow(tr("Name"), m_nameEdit);
    mainLayout->addLayout(formLayout);

    auto *promptLabel = new QLabel(tr("System prompt"), this);
    m_systemPromptEdit = new QPlainTextEdit(this);
    m_systemPromptEdit->setPlaceholderText(tr("Enter system prompt for this role..."));

    mainLayout->addWidget(promptLabel);
    mainLayout->addWidget(m_systemPromptEdit, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &RoleEditorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &RoleEditorDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void RoleEditorDialog::loadFromRole(const Role &role)
{
    if (m_nameEdit) {
        m_nameEdit->setText(role.name);
    }

    if (m_systemPromptEdit) {
        const QString promptText = systemPromptToDisplayText(role.systemPrompt);
        m_systemPromptEdit->setPlainText(promptText);
        m_systemPromptEdit->moveCursor(QTextCursor::Start);
    }
}

void RoleEditorDialog::applyToRole(Role &role) const
{
    if (m_nameEdit) {
        role.name = m_nameEdit->text().trimmed();
    }

    if (!m_systemPromptEdit) {
        return;
    }

    const QString newPromptText = m_systemPromptEdit->toPlainText();
    const QString originalPromptText = systemPromptToDisplayText(role.systemPrompt);

    // Only overwrite systemPrompt when the user actually changes it.
    if (newPromptText.trimmed() != originalPromptText.trimmed()) {
        role.systemPrompt = QJsonValue(newPromptText);
    }
}

Role RoleEditorDialog::roleData() const
{
    Role result = m_initialRole; // start from initial data and apply edits
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
