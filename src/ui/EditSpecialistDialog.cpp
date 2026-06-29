#include "ui/EditSpecialistDialog.h"

#include <QAbstractItemModel>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include "models/Role.h"
#include "storage/StorageManager.h"
#include "ui/ModelsBrowserWidget.h"
#include "ui/PromptPreview.h"

EditSpecialistDialog::EditSpecialistDialog(const Specialist &initial,
                                           StorageManager &storageManager,
                                           QWidget *parent)
    : QDialog(parent)
    , m_initial(initial)
    , m_storageManager(storageManager)
    , m_modelsBrowser(new ModelsBrowserWidget(storageManager,
                                              this,
                                              /*pickerMode=*/true))
{
    setWindowTitle(tr("Edit Specialist"));
    resize(950, 720);

    auto *mainLayout = new QVBoxLayout(this);

    // 1. Specialist id (read-only reference). Confirms which binding the
    //    user is editing and is the contract the consumer (TeamEditorWidget)
    //    uses to write back to the underlying JSON.
    m_idLabel = new QLabel(this);
    m_idLabel->setObjectName(QStringLiteral("editSpecialist.idLabel"));
    QFont boldFont = m_idLabel->font();
    boldFont.setBold(true);
    m_idLabel->setFont(boldFont);
    m_idLabel->setText(tr("Specialist id: %1").arg(m_initial.id));
    m_idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_idLabel);

    auto *formLayout = new QFormLayout();

    // 2. Name field — independent of the Role name. Empty is allowed (the
    //    consumer falls back to displaying the id).
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("editSpecialist.nameEdit"));
    m_nameEdit->setPlaceholderText(
        tr("Optional human label for this Specialist (e.g. \"Reviewer\")."));
    m_nameEdit->setText(m_initial.name);
    m_nameEdit->setToolTip(tr(
        "Optional human label for this Specialist. Empty is allowed "
        "(the consumer falls back to displaying the Specialist id)."));
    formLayout->addRow(tr("Name:"), m_nameEdit);

    mainLayout->addLayout(formLayout);

    // 3. Model picker: reuses ModelsBrowserWidget in pickerMode so the
    //    same search/filters the Add Specialist flow uses are available.
    auto *modelsGroup = new QGroupBox(tr("Pick a Model"), this);
    auto *modelsLayout = new QVBoxLayout(modelsGroup);
    auto *hint = new QLabel(
        tr("The picker's current row is pre-selected to this Specialist's "
           "current model. Click \"OK\" below (or double-click a row) to "
           "change the model."),
        this);
    hint->setWordWrap(true);
    modelsLayout->addWidget(hint);
    m_modelsBrowser->setToolTip(tr(
        "Live provider catalog. Search by name, filter by capabilities "
        "(tool use, reasoning) and minimum context window. The row "
        "that is highlighted when you click OK becomes the new model "
        "binding for this Specialist."));
    modelsLayout->addWidget(m_modelsBrowser, 1);
    mainLayout->addWidget(modelsGroup, 1);

    // 4. Prompt override — same shape as AddSpecialistDialog: plain text
    //    or `{file: "./prompts/override.md"}` JSON entered verbatim.
    m_overrideEdit = new QPlainTextEdit(this);
    m_overrideEdit->setObjectName(QStringLiteral("editSpecialist.overrideEdit"));
    m_overrideEdit->setPlaceholderText(
        tr("Optional: prompt override applied on top of the Role's system "
           "prompt.\nPlain text or {file: \"./prompts/override.md\"}. "
           "Leave empty to remove an existing override."));
    m_overrideEdit->setToolTip(tr(
        "Appended on top of the Role's system prompt. Leave the field "
        "empty to remove an existing override entirely."));
    QString overrideSeed;
    if (!m_initial.promptOverride.isUndefined() && !m_initial.promptOverride.isNull()) {
        if (m_initial.promptOverride.isString()) {
            overrideSeed = m_initial.promptOverride.toString();
        } else if (m_initial.promptOverride.isObject()) {
            // Render the JSON shape verbatim so the user can edit it byte-for-byte.
            overrideSeed = QString::fromUtf8(
                QJsonDocument(m_initial.promptOverride.toObject())
                    .toJson(QJsonDocument::Compact));
        }
    }
    m_overrideEdit->setPlainText(overrideSeed);
    m_overrideEdit->setFixedHeight(80);

    auto *overrideLayout = new QFormLayout();
    overrideLayout->addRow(tr("Prompt Override:"), m_overrideEdit);
    mainLayout->addLayout(overrideLayout);

    // 5. Effective Prompt preview so the user can see what the merged
    //    prompt looks like (Role + override) before they accept.
    m_promptPreview = new PromptPreview(this);
    m_promptPreview->setObjectName(QStringLiteral("editSpecialist.promptPreview"));
    m_promptPreview->setMinimumHeight(160);
    m_promptPreview->setToolTip(tr(
        "What the model actually receives: Role system prompt plus "
        "the current prompt override, with an approximate token count."));
    mainLayout->addWidget(m_promptPreview, 1);

    // Drive accept/reject from the embedded picker.
    connect(m_modelsBrowser, &ModelsBrowserWidget::modelAccepted,
            this, &EditSpecialistDialog::onModelAccepted);
    connect(m_modelsBrowser, &ModelsBrowserWidget::selectionCanceled,
            this, &EditSpecialistDialog::onPickerCanceled);

    // Drive the preview from BOTH the name editor (so renames propagate
    // to the header line) and the override editor (so the merged prompt
    // stays in sync as the user types).
    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &EditSpecialistDialog::refreshPromptPreview);
    connect(m_overrideEdit, &QPlainTextEdit::textChanged,
            this, &EditSpecialistDialog::refreshPromptPreview);

    // Seed the preview immediately so the dialog is never visually empty
    // when it first opens.
    refreshPromptPreview();

    // Pre-select the current model in the picker so a "no-op edit" simply
    // re-confirms via the OK button without scrolling for the old model.
    setModelId(m_initial.modelId);
}

void EditSpecialistDialog::setModelId(const QString &modelId)
{
    if (modelId.isEmpty() || !m_modelsBrowser) {
        return;
    }

    QTableView *tableView = nullptr;
    for (QTableView *candidate : m_modelsBrowser->findChildren<QTableView *>()) {
        if (candidate->parent() == m_modelsBrowser) {
            tableView = candidate;
            break;
        }
    }
    QStandardItemModel *sourceModel = nullptr;
    for (QStandardItemModel *candidate : m_modelsBrowser->findChildren<QStandardItemModel *>()) {
        if (candidate->parent() == m_modelsBrowser) {
            sourceModel = candidate;
            break;
        }
    }
    QSortFilterProxyModel *proxyModel = nullptr;
    for (QSortFilterProxyModel *candidate : m_modelsBrowser->findChildren<QSortFilterProxyModel *>()) {
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

    // Column 0 in ModelsBrowserWidget's source model always holds the id
    // (see ModelsBrowserWidget::addModelRow). Walk the rows, find the
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

QString EditSpecialistDialog::selectedName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString EditSpecialistDialog::selectedModelId() const
{
    return m_modelsBrowser ? m_modelsBrowser->selectedModelId() : QString();
}

QString EditSpecialistDialog::promptOverrideText() const
{
    return m_overrideEdit ? m_overrideEdit->toPlainText() : QString();
}

Specialist EditSpecialistDialog::editedSpecialist() const
{
    Specialist out = m_initial;
    out.name = selectedName();
    out.modelId = selectedModelId().isEmpty() ? m_initial.modelId
                                              : selectedModelId();
    const QString trimmedOverride = promptOverrideText().trimmed();
    if (trimmedOverride.isEmpty()) {
        // Empty == "remove the override" — collapse to undefined so the
        // JSON writer drops the field rather than serializing "".
        out.promptOverride = QJsonValue(QJsonValue::Undefined);
    } else {
        out.promptOverride = QJsonValue(trimmedOverride);
    }
    return out;
}

void EditSpecialistDialog::onModelAccepted(const QString &modelId)
{
    Q_UNUSED(modelId);

    // The picker already validated selectedModelId() is non-empty before
    // emitting modelAccepted (see ModelsBrowserWidget::onAcceptButtonClicked
    // -- empty selection just writes a status hint and refuses to emit).
    // Validate that name picked something sensible: an empty model is the
    // only blocking condition (the name is allowed to be empty: the
    // consumer falls back to the id).
    if (selectedModelId().isEmpty()) {
        QMessageBox::warning(this,
                             tr("Edit Specialist"),
                             tr("Please choose a model before accepting."));
        return;
    }

    accept();
}

void EditSpecialistDialog::onPickerCanceled()
{
    reject();
}

void EditSpecialistDialog::refreshPromptPreview()
{
    if (!m_promptPreview) {
        return;
    }

    // Role context is fixed (binding.roleId is immutable in this dialog),
    // so we resolve it once per refresh.
    const Role role = m_storageManager.loadRole(m_initial.roleId);
    QString basePrompt;
    if (role.systemPrompt.isString()) {
        basePrompt = role.systemPrompt.toString();
    } else if (role.systemPrompt.isObject()) {
        // PARADIGM §2.1: render the {file: ...} shape verbatim so the
        // user sees exactly what was stored; we never crack the file at
        // preview time.
        basePrompt = QStringLiteral("(file reference) ") +
            QString::fromUtf8(QJsonDocument(role.systemPrompt.toObject())
                                  .toJson(QJsonDocument::Compact));
    }

    m_promptPreview->setPreview(role.name,
                                role.id,
                                selectedName().isEmpty() ? m_initial.id
                                                         : selectedName(),
                                selectedModelId(),
                                basePrompt,
                                promptOverrideText());
}
