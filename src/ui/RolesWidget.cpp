#include "ui/RolesWidget.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "models/Role.h"
#include "storage/StorageManager.h"
#include "ui/RoleEditorDialog.h"

namespace {

QString generateUniqueRoleId(StorageManager &storage, const QString &base)
{
    QString baseId = base.trimmed();
    if (baseId.isEmpty()) {
        baseId = QStringLiteral("role");
    }

    QString candidate = baseId;
    int suffix = 1;
    while (true) {
        const Role existing = storage.loadRole(candidate);
        if (existing.id.isEmpty()) {
            break;
        }
        candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix++);
    }

    return candidate;
}

} // namespace

RolesWidget::RolesWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    auto *layout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Roles library: job descriptions and system prompts."), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << tr("ID")
            << tr("Name")
            << tr("Description")
            << tr("Mode");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table, 1);

    auto *buttonRow = new QHBoxLayout();
    m_createButton = new QPushButton(tr("Create New"), this);
    m_editButton = new QPushButton(tr("Edit"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    buttonRow->addWidget(m_createButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_duplicateButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        createRole(QString());
    });
    connect(m_editButton, &QPushButton::clicked, this, &RolesWidget::editSelectedRole);
    connect(m_duplicateButton, &QPushButton::clicked, this, &RolesWidget::duplicateSelectedRole);
    connect(m_deleteButton, &QPushButton::clicked, this, &RolesWidget::deleteSelectedRole);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &RolesWidget::onSelectionChanged);
    connect(m_table, &QTableWidget::itemDoubleClicked,
            this, &RolesWidget::onItemDoubleClicked);

    refreshRoles();
    onSelectionChanged();
}

void RolesWidget::refreshRoles()
{
    const QList<Role> roles = m_storageManager.listRoles();

    m_table->setRowCount(roles.size());

    for (int row = 0; row < roles.size(); ++row) {
        const Role &role = roles.at(row);

        auto *idItem = new QTableWidgetItem(role.id);
        idItem->setData(Qt::UserRole, role.id);
        m_table->setItem(row, 0, idItem);

        auto *nameItem = new QTableWidgetItem(role.name);
        m_table->setItem(row, 1, nameItem);

        auto *descItem = new QTableWidgetItem(role.description);
        m_table->setItem(row, 2, descItem);

        auto *modeItem = new QTableWidgetItem(Role::modeToString(role.mode));
        m_table->setItem(row, 3, modeItem);
    }

    m_table->resizeColumnsToContents();
}

QString RolesWidget::selectedRoleId() const
{
    if (!m_table) {
        return QString();
    }

    const int row = m_table->currentRow();
    if (row < 0) {
        return QString();
    }

    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem) {
        return QString();
    }

    const QVariant userData = idItem->data(Qt::UserRole);
    const QString id = userData.isValid() ? userData.toString() : idItem->text();
    return id.trimmed();
}

void RolesWidget::createRole()
{
    createRole(QString());
}

void RolesWidget::createRole(const QString &proposedName)
{
    Role role;
    role.name = proposedName.trimmed();

    RoleEditorDialog dlg(role, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Role updated = dlg.roleData();

    if (updated.id.isEmpty()) {
        updated.id = generateUniqueRoleId(m_storageManager, updated.name);
    }

    if (!m_storageManager.saveRole(updated)) {
        QMessageBox::warning(this, tr("Save Role"), tr("Failed to save role."));
        return;
    }

    refreshRoles();
}

void RolesWidget::editSelectedRole()
{
    const QString id = selectedRoleId();
    if (id.isEmpty()) {
        return;
    }

    Role role = m_storageManager.loadRole(id);
    if (role.id.isEmpty()) {
        QMessageBox::warning(this, tr("Edit Role"), tr("Failed to load role."));
        return;
    }

    RoleEditorDialog dlg(role, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Role updated = dlg.roleData();
    if (updated.id.isEmpty()) {
        updated.id = role.id; // keep original id stable during edit
    }

    if (!m_storageManager.saveRole(updated)) {
        QMessageBox::warning(this, tr("Save Role"), tr("Failed to save role."));
        return;
    }

    refreshRoles();
}

void RolesWidget::duplicateSelectedRole()
{
    const QString id = selectedRoleId();
    if (id.isEmpty()) {
        return;
    }

    Role role = m_storageManager.loadRole(id);
    if (role.id.isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate Role"), tr("Failed to load role."));
        return;
    }

    role.id.clear();
    role.name += QStringLiteral(" (copy)");

    RoleEditorDialog dlg(role, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    Role updated = dlg.roleData();
    if (updated.id.isEmpty()) {
        updated.id = generateUniqueRoleId(m_storageManager, updated.name.isEmpty() ? id : updated.name);
    }

    if (!m_storageManager.saveRole(updated)) {
        QMessageBox::warning(this, tr("Save Role"), tr("Failed to save duplicated role."));
        return;
    }

    refreshRoles();
}

void RolesWidget::deleteSelectedRole()
{
    const QString id = selectedRoleId();
    if (id.isEmpty()) {
        return;
    }

    const auto reply = QMessageBox::question(this,
                                             tr("Delete Role"),
                                             tr("Delete selected role?"));
    if (reply != QMessageBox::Yes) {
        return;
    }

    // Minimal deletion: remove the role JSON file under the default
    // ~/.opencode-meta/roles/ location, mirroring TemplatesWidget.
    const QString path = QDir::homePath() + QStringLiteral("/.opencode-meta/roles/%1.json").arg(id);
    QFile file(path);
    if (file.exists() && !file.remove()) {
        QMessageBox::warning(this,
                             tr("Delete Role"),
                             tr("Failed to delete role file:\n%1")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }

    refreshRoles();
}

void RolesWidget::onSelectionChanged()
{
    const bool hasSelection = (m_table && m_table->currentRow() >= 0);

    if (m_editButton) {
        m_editButton->setEnabled(hasSelection);
    }
    if (m_duplicateButton) {
        m_duplicateButton->setEnabled(hasSelection);
    }
    if (m_deleteButton) {
        m_deleteButton->setEnabled(hasSelection);
    }
}

void RolesWidget::onItemDoubleClicked(QTableWidgetItem *item)
{
    Q_UNUSED(item);
    editSelectedRole();
}
