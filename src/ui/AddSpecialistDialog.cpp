#include "ui/AddSpecialistDialog.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include "models/Role.h"
#include "storage/StorageManager.h"
#include "ui/ModelsBrowserWidget.h"

AddSpecialistDialog::AddSpecialistDialog(StorageManager &storageManager,
                                         QWidget *parent)
    : QDialog(parent)
    , m_modelsBrowser(new ModelsBrowserWidget(storageManager, this, /*pickerMode=*/true))
    , m_storageManager(storageManager)
{
    setWindowTitle(tr("Add Specialist"));
    resize(950, 720);

    auto *mainLayout = new QVBoxLayout(this);

    auto *formLayout = new QFormLayout();

    m_roleCombo = new QComboBox(this);
    int defaultIndex = -1;
    const QList<Role> roles = storageManager.listRoles();
    if (roles.isEmpty()) {
        m_roleCombo->addItem(tr("(no Roles available - create one first)"), QString());
        m_roleCombo->setEnabled(false);
    } else {
        for (int i = 0; i < roles.size(); ++i) {
            const Role &role = roles.at(i);
            const QString labelText = role.name.isEmpty()
                                          ? role.id
                                          : QStringLiteral("%1 (%2)").arg(role.name, role.id);
            m_roleCombo->addItem(labelText, role.id);
            if (defaultIndex < 0 && role.id == QLatin1String("build")) {
                defaultIndex = i;
            }
        }
        if (defaultIndex < 0) {
            defaultIndex = 0;
        }
        m_roleCombo->setCurrentIndex(defaultIndex);
    }

    formLayout->addRow(tr("Role:"), m_roleCombo);
    mainLayout->addLayout(formLayout);

    auto *modelsGroup = new QGroupBox(tr("Pick a Model"), this);
    auto *modelsLayout = new QVBoxLayout(modelsGroup);
    auto *hint = new QLabel(
        tr("Use the search and filters above to narrow the list.\n"
           "When ready, click \"OK\" below (or double-click a row) to accept the highlighted model."),
        this);
    hint->setWordWrap(true);
    modelsLayout->addWidget(hint);
    modelsLayout->addWidget(m_modelsBrowser, 1);
    mainLayout->addWidget(modelsGroup, 1);

    m_overrideEdit = new QPlainTextEdit(this);
    m_overrideEdit->setPlaceholderText(
        tr("Optional: prompt override applied on top of the Role's system prompt.\n"
           "Plain text or {file: \"./prompts/override.md\"}."));
    m_overrideEdit->setFixedHeight(80);

    auto *overrideLayout = new QFormLayout();
    overrideLayout->addRow(tr("Prompt Override:"), m_overrideEdit);
    mainLayout->addLayout(overrideLayout);

    connect(m_modelsBrowser, &ModelsBrowserWidget::modelAccepted,
            this, &AddSpecialistDialog::onModelAccepted);
    connect(m_modelsBrowser, &ModelsBrowserWidget::selectionCanceled,
            this, &AddSpecialistDialog::onPickerCanceled);
}

QString AddSpecialistDialog::selectedRoleId() const
{
    return m_roleCombo ? m_roleCombo->currentData().toString() : QString();
}

void AddSpecialistDialog::setRoleId(const QString &roleId)
{
    // F5: pre-select the named Role BEFORE exec(). Walk the combo box
    // and look for the entry whose UserData matches `roleId`. Empty id
    // (or unset combo) is a no-op so the existing user-driven path is
    // bit-for-bit unchanged when callers do not pre-set.
    if (roleId.isEmpty() || !m_roleCombo) {
        return;
    }
    for (int i = 0; i < m_roleCombo->count(); ++i) {
        if (m_roleCombo->itemData(i).toString() == roleId) {
            m_roleCombo->setCurrentIndex(i);
            return;
        }
    }
}

void AddSpecialistDialog::setModelId(const QString &modelId)
{
    // F5: pre-select the model row whose id matches `modelId` BEFORE
    // exec(). We reach into the embedded ModelsBrowserWidget's private
    // QTableView / QStandardItemModel / QSortFilterProxyModel via
    // findChild on direct-clock children only — this lets us drive the
    // picker programmatically without expanding ModelsBrowserWidget's
    // public API.
    if (modelId.isEmpty() || !m_modelsBrowser) {
        return;
    }

    QTableView *tableView = nullptr;
    for (QTableView *candidate : m_modelsBrowser->findChildren<QTableView*>()) {
        if (candidate->parent() == m_modelsBrowser) {
            tableView = candidate;
            break;
        }
    }
    QStandardItemModel *sourceModel = nullptr;
    for (QStandardItemModel *candidate : m_modelsBrowser->findChildren<QStandardItemModel*>()) {
        if (candidate->parent() == m_modelsBrowser) {
            sourceModel = candidate;
            break;
        }
    }
    QSortFilterProxyModel *proxyModel = nullptr;
    for (QSortFilterProxyModel *candidate : m_modelsBrowser->findChildren<QSortFilterProxyModel*>()) {
        if (candidate->parent() == m_modelsBrowser) {
            proxyModel = candidate;
            break;
        }
    }

    if (!tableView || !sourceModel || !proxyModel) {
        return;
    }
    if (!tableView->selectionModel()) {
        return;
    }

    // Source-model column 0 holds the model id (see
    // ModelsBrowserWidget::addModelRow). Walk the rows, find the
    // matching id, map to proxy index, and select the row.
    for (int row = 0; row < sourceModel->rowCount(); ++row) {
        QStandardItem *idItem = sourceModel->item(row, 0);
        if (!idItem) {
            continue;
        }
        if (idItem->text() != modelId) {
            continue;
        }
        const QModelIndex sourceIndex = sourceModel->index(row, 0);
        const QModelIndex proxyIndex = proxyModel->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid()) {
            return;
        }
        tableView->selectionModel()->select(
            QItemSelection(proxyIndex, proxyIndex),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        tableView->setCurrentIndex(proxyIndex);
        return;
    }
}

QString AddSpecialistDialog::selectedModelId() const
{
    return m_modelsBrowser ? m_modelsBrowser->selectedModelId() : QString();
}

QString AddSpecialistDialog::promptOverrideText() const
{
    return m_overrideEdit ? m_overrideEdit->toPlainText() : QString();
}

void AddSpecialistDialog::onModelAccepted(const QString &modelId)
{
    Q_UNUSED(modelId);

    if (m_roleCombo && !m_roleCombo->isEnabled()) {
        // No Roles are available -- the previously dead-end path. Per F2,
        // hand off authoring to the Roles view via the createRoleRequested
        // signal and close this dialog. Pass an empty proposed name as a
        // sensible default; RoleEditorDialog exposes a name field where
        // the user will type the final Role name.
        emit createRoleRequested(QString());
        accept();
        return;
    }

    if (selectedRoleId().isEmpty()) {
        QMessageBox::warning(this,
                             tr("Add Specialist"),
                             tr("Please choose a Role for the new Specialist."));
        return;
    }

    accept();
}

void AddSpecialistDialog::onPickerCanceled()
{
    reject();
}
