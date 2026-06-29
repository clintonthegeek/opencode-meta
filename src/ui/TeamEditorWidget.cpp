// TeamEditorWidget: full Specialist editing UX for Teams (Phase E, Stage 5).
//
// Provides:
//   * Specialists table with Primary checkbox, name, Role, model display,
//     and cost/context badge.
//   * Add Specialist flow (Role picker -> ModelsBrowserWidget pickerMode ->
//     optional prompt override) via AddSpecialistDialog.
//   * Reorder (move up / move down), Remove, Duplicate as Variant, Compare.
//   * Widget-scoped keyboard shortcuts for the most common actions.
// All edits persist immediately through StorageManager.

#include "ui/TeamEditorWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QColor>
#include <QDebug>
#include <QDialog>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QFont>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include "generation/ProviderCatalog.h"
#include "generation/TeamRenderer.h"
#include "models/ModelInfo.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/AddSpecialistDialog.h"

namespace {

QString generateUniqueSpecialistId(StorageManager &storage, const QString &base)
{
    QString baseId = base.trimmed();
    if (baseId.isEmpty()) {
        baseId = QStringLiteral("spec");
    }

    QString candidate = baseId;
    int suffix = 1;
    while (true) {
        const Specialist existing = storage.loadSpecialist(candidate);
        if (existing.id.isEmpty()) {
            break;
        }
        candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix++);
    }
    return candidate;
}

QString generateUniqueTeamId(StorageManager &storage, const QString &base)
{
    QString baseId = base.trimmed();
    if (baseId.isEmpty()) {
        baseId = QStringLiteral("team");
    }

    QString candidate = baseId;
    int suffix = 1;
    while (true) {
        const Team existing = storage.loadTeam(candidate);
        if (existing.id.isEmpty()) {
            break;
        }
        candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix++);
    }
    return candidate;
}

QWidget *makeDefaultAgentCell(const QString &name, QWidget *parent)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *nameLabel = new QLabel(name, cell);
    layout->addWidget(nameLabel);

    auto *badge = new QLabel(QStringLiteral("★"), cell);
    badge->setObjectName(QStringLiteral("defaultAgentBadge"));
    badge->setToolTip(QObject::tr("Default agent"));
    badge->setStyleSheet(QStringLiteral(
        "QLabel#defaultAgentBadge { color: #d97706; font-weight: 700; }"));
    layout->addWidget(badge);
    layout->addStretch(1);
    return cell;
}

QToolButton *makeMakeDefaultButton(QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("teamEditor.makeDefaultButton"));
    button->setAutoRaise(true);
    button->setText(QObject::tr("★ Make default"));
    button->setToolTip(QObject::tr("Set this Specialist as the team's default agent"));
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

bool specialistIsStock(const Specialist &spec)
{
    return spec.metadata.value(QStringLiteral("stock")).toBool(false)
        || spec.metadata.value(QStringLiteral("native")).toBool(false);
}

QString contextWindowString(int tokens)
{
    if (tokens <= 0) {
        return QString();
    }
    if (tokens >= 1'000'000) {
        return QStringLiteral("%1M ctx").arg(QString::number(tokens / 1'000'000.0, 'f', 1));
    }
    if (tokens >= 1'000) {
        return QStringLiteral("%1k ctx").arg(tokens / 1'000);
    }
    return QStringLiteral("%1 ctx").arg(tokens);
}

// F3: render a Team to its opencode.json QJsonObject shape so we can
// pretty-print both sides of a Team-vs-Team diff. Mirrors the helper
// used in ProjectsWidget::viewTeamDiffsForProject; kept local so the
// editor never grows an extra public API surface on TeamRenderer.
QJsonObject renderTeamConfig(const Team &team, StorageManager &storage)
{
    QSet<QString> specialistIds;
    QSet<QString> roleIds;
    for (const auto &binding : team.specialists) {
        if (!binding.roleId.isEmpty()) {
            roleIds.insert(binding.roleId);
        }
        if (!binding.specialistId.isEmpty()) {
            specialistIds.insert(binding.specialistId);
        }
    }

    QMap<QString, Specialist> specialists;
    for (const QString &specId : std::as_const(specialistIds)) {
        const Specialist s = storage.loadSpecialist(specId);
        if (!s.id.isEmpty()) {
            specialists.insert(s.id, s);
        }
    }

    QMap<QString, Role> roles;
    for (const QString &roleId : std::as_const(roleIds)) {
        const Role r = storage.loadRole(roleId);
        if (!r.id.isEmpty()) {
            roles.insert(r.id, r);
        }
    }

    return TeamRenderer::render(team, specialists, roles);
}

bool teamsEqual(const Team &lhs, const Team &rhs)
{
    if (lhs.id != rhs.id
        || lhs.name != rhs.name
        || lhs.description != rhs.description
        || lhs.primarySpecialistIds != rhs.primarySpecialistIds
        || lhs.version != rhs.version
        || lhs.parentTeamId != rhs.parentTeamId
        || lhs.metadata != rhs.metadata
        || lhs.specialists.size() != rhs.specialists.size()) {
        return false;
    }

    for (int i = 0; i < lhs.specialists.size(); ++i) {
        const Team::SpecialistBinding &left = lhs.specialists.at(i);
        const Team::SpecialistBinding &right = rhs.specialists.at(i);
        if (left.roleId != right.roleId || left.specialistId != right.specialistId) {
            return false;
        }
    }

    return true;
}

bool isResettableStockClone(const Team &team, StorageManager &storage)
{
    if (team.id.isEmpty() || team.parentTeamId.isEmpty()) {
        return false;
    }

    const Team parent = storage.loadTeam(team.parentTeamId);
    return !parent.id.isEmpty() && storage.isStockTeam(parent);
}

// F3: side-by-side diff highlighter. Identical shape to
// ProjectsWidget::populateDiffEditor -- kept local and minimal so the
// editor can launch a lightweight inline diff dialog without dragging
// ProjectsWidget's dialog design into the editor surface.
void populateDiffEditor(QTextEdit *edit,
                        const QStringList &lines,
                        const QVector<bool> &isDifferent,
                        const QColor &diffColor)
{
    edit->clear();
    edit->setReadOnly(true);

    QTextCharFormat normalFormat;
    QTextCharFormat diffFormat;
    diffFormat.setBackground(diffColor);

    QTextDocument *doc = edit->document();
    QTextCursor cursor(doc);

    for (int i = 0; i < lines.size(); ++i) {
        const bool different = (i < isDifferent.size()) ? isDifferent[i] : false;
        cursor.setCharFormat(different ? diffFormat : normalFormat);
        cursor.insertText(lines.at(i));
        if (i + 1 < lines.size()) {
            cursor.insertBlock();
        }
    }
}

} // namespace

TeamEditorWidget::TeamEditorWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    setObjectName(QStringLiteral("TeamEditorWidget"));

    m_modelsCache = m_storageManager.loadModelsCache();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel(tr("Specialists in this Team:"), this);
    layout->addWidget(titleLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        tr("Primary"),
        tr("Specialist"),
        tr("Role"),
        tr("Model"),
        tr("Cost / Tokens"),
        tr("Default"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    layout->addWidget(m_emptyLabel);

    auto *buttonRow = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add Specialist..."), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_moveUpButton = new QPushButton(tr("Move Up"), this);
    m_moveDownButton = new QPushButton(tr("Move Down"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate as Variant"), this);
    m_resetButton = new QPushButton(tr("Reset to stock"), this);
    m_compareButton = new QPushButton(tr("Compare..."), this);
    m_revertButton = new QPushButton(tr("Revert changes"), this);
    m_applyButton = new QPushButton(tr("Apply Team..."), this);

    m_revertButton->setObjectName(QStringLiteral("teamEditor.revertButton"));
    m_revertButton->setToolTip(tr("Discard in-memory changes and reload the Team from storage."));
    m_revertButton->setStatusTip(tr("Reload the Team from storage and discard local edits"));
    m_resetButton->setObjectName(QStringLiteral("teamEditor.resetToStockButton"));
    m_resetButton->setToolTip(tr("Reset this cloned Team back to its original stock source"));
    m_resetButton->setStatusTip(tr("Reset this cloned Team back to its original stock source"));

    m_dirtyIndicator = new QLabel(tr("Unsaved changes"), this);
    m_dirtyIndicator->setObjectName(QStringLiteral("teamEditor.dirtyIndicator"));
    QFont dirtyFont = m_dirtyIndicator->font();
    dirtyFont.setItalic(true);
    m_dirtyIndicator->setFont(dirtyFont);
    m_dirtyIndicator->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_dirtyIndicator->setVisible(false);

    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addWidget(m_moveUpButton);
    buttonRow->addWidget(m_moveDownButton);
    buttonRow->addWidget(m_duplicateButton);
    buttonRow->addWidget(m_resetButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_compareButton);
    buttonRow->addWidget(m_dirtyIndicator);
    buttonRow->addWidget(m_revertButton);
    buttonRow->addWidget(m_applyButton);
    layout->addLayout(buttonRow);

    connect(m_addButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onAddSpecialist);
    connect(m_removeButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onRemoveSpecialist);
    connect(m_moveUpButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onMoveUp);
    connect(m_moveDownButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onMoveDown);
    connect(m_duplicateButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onDuplicateVariant);
    connect(m_resetButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onResetToStock);
    connect(m_compareButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onCompare);
    connect(m_revertButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onRevertChanges);
    // F1: footer "Apply Team..." button simply forwards the current
    // Team id upward; the host (TeamsWidget -> MainWindow) decides
    // how to present the apply dialog.
    connect(m_applyButton, &QPushButton::clicked,
            this, &TeamEditorWidget::onApplyTeam);

    // Re-render button-enabled state when the selection changes and when the
    // table contents are updated programmatically (we connect via the helper
    // refresh plus the dedicated selection signal).
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TeamEditorWidget::updateActionButtons);
    connect(m_table, &QTableWidget::itemChanged,
            this, &TeamEditorWidget::onPrimaryItemChanged);

    // Widget-scoped keyboard shortcuts (effectively enabled only while this
    // editor or any of its child widgets has focus, so they don't clash with
    // MainWindow menu shortcuts).
    auto *addAct = new QAction(tr("Add Specialist"), this);
    addAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    addAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAct->setStatusTip(tr("Add a new Specialist to this Team"));
    addAction(addAct);
    connect(addAct, &QAction::triggered, this, &TeamEditorWidget::onAddSpecialist);

    auto *removeAct = new QAction(tr("Remove Specialist"), this);
    removeAct->setShortcut(QKeySequence::Delete);
    removeAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    removeAct->setStatusTip(tr("Remove the selected Specialist from this Team"));
    addAction(removeAct);
    connect(removeAct, &QAction::triggered, this, &TeamEditorWidget::onRemoveSpecialist);

    auto *upAct = new QAction(tr("Move Up"), this);
    upAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Up")));
    upAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(upAct);
    connect(upAct, &QAction::triggered, this, &TeamEditorWidget::onMoveUp);

    auto *downAct = new QAction(tr("Move Down"), this);
    downAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Down")));
    downAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(downAct);
    connect(downAct, &QAction::triggered, this, &TeamEditorWidget::onMoveDown);

    auto *dupAct = new QAction(tr("Duplicate as Variant"), this);
    dupAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    dupAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(dupAct);
    connect(dupAct, &QAction::triggered, this, &TeamEditorWidget::onDuplicateVariant);

    auto *resetAct = new QAction(tr("Reset to stock"), this);
    resetAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    resetAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(resetAct);
    connect(resetAct, &QAction::triggered, this, &TeamEditorWidget::onResetToStock);

    refreshSpecialistsTable();
    updateActionButtons();
}

void TeamEditorWidget::setShowStock(bool showStock)
{
    if (m_showStockSpecialists == showStock) {
        return;
    }

    m_showStockSpecialists = showStock;
    refreshSpecialistsTable();
    updateActionButtons();
}

void TeamEditorWidget::refreshSpecialistsTable()
{
    if (!m_table) {
        return;
    }

    const QString previouslySelectedId = specialistIdAtRow(m_table->currentRow());
    m_updatingTable = true;

    if (m_team.id.isEmpty()) {
        m_table->setRowCount(0);
        if (m_emptyLabel) {
            m_emptyLabel->setText(tr("Select a Team from the list at left to edit its Specialists."));
            m_emptyLabel->setVisible(true);
        }
        m_table->setVisible(false);
        m_updatingTable = false;
        return;
    }

    if (m_team.specialists.isEmpty()) {
        m_table->setRowCount(0);
        if (m_emptyLabel) {
            m_emptyLabel->setText(
                tr("This Team has no Specialists yet.\n"
                   "Click \"Add Specialist...\" to bind a Role to a model."));
            m_emptyLabel->setVisible(true);
        }
        m_table->setVisible(false);
        m_updatingTable = false;
        return;
    }

    if (m_emptyLabel) {
        m_emptyLabel->setVisible(false);
    }
    m_table->setVisible(true);

    m_table->setRowCount(static_cast<int>(m_team.specialists.size()));

    // Cache Roles + Specialists per refresh to avoid repeated disk hits.
    QHash<QString, Role> roleCache;
    QHash<QString, Specialist> specialistCache;

    for (int row = 0; row < m_team.specialists.size(); ++row) {
        const Team::SpecialistBinding &binding = m_team.specialists.at(row);

        if (!specialistCache.contains(binding.specialistId)) {
            specialistCache.insert(binding.specialistId, m_storageManager.loadSpecialist(binding.specialistId));
        }
        if (!roleCache.contains(binding.roleId)) {
            roleCache.insert(binding.roleId, m_storageManager.loadRole(binding.roleId));
        }

        const Specialist spec = specialistCache.value(binding.specialistId);
        const Role role = roleCache.value(binding.roleId);
        const bool stockSpecialist = m_storageManager.isStockTeam(m_team) || specialistIsStock(spec);

        // Column 0: primary checkbox. The specialist id is stored in the
        // UserRole so onPrimaryItemChanged() can locate the binding.
        auto *primaryItem = new QTableWidgetItem();
        primaryItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        primaryItem->setData(Qt::UserRole, binding.specialistId);
        primaryItem->setData(Qt::UserRole + 1, stockSpecialist);
        primaryItem->setCheckState(
            m_team.primarySpecialistIds.contains(binding.specialistId)
                ? Qt::Checked
                : Qt::Unchecked);
        primaryItem->setToolTip(tr("Mark this Specialist as a primary (orchestrator). "
                                   "Multiple primaries are allowed for OpenCode Tab-switching."));
        m_table->setItem(row, 0, primaryItem);

        // Column 1: Specialist name (with id and prompt override in tooltip).
        QString nameText = spec.name.isEmpty() ? binding.specialistId : spec.name;
        auto *nameItem = new QTableWidgetItem(nameText);
        QString nameToolTip = QStringLiteral("Specialist id: %1").arg(binding.specialistId);
        if (!spec.promptOverride.isUndefined() && !spec.promptOverride.isNull()) {
            if (spec.promptOverride.isString()) {
                nameToolTip += QStringLiteral("\nOverride prompt:\n%1").arg(spec.promptOverride.toString());
            } else if (spec.promptOverride.isObject()) {
                nameToolTip += QStringLiteral("\nOverride prompt:\n%1")
                                   .arg(QString::fromUtf8(QJsonDocument(spec.promptOverride.toObject())
                                                              .toJson(QJsonDocument::Compact)));
            }
        }
        nameItem->setToolTip(nameToolTip);
        m_table->setCellWidget(row, 1, nullptr);
        if (binding.specialistId == m_team.metadata.value(QStringLiteral("default_agent")).toString()) {
            m_table->setCellWidget(row, 1, makeDefaultAgentCell(nameText, m_table));
        }
        m_table->setItem(row, 1, nameItem);

        m_table->setCellWidget(row, 5, nullptr);
        if (!stockSpecialist) {
            auto *defaultButton = makeMakeDefaultButton(m_table);
            connect(defaultButton, &QToolButton::clicked, this, [this, row, nameText]() {
                if (!m_table || row < 0 || row >= m_team.specialists.size()) {
                    return;
                }

                const QString specialistId = m_team.specialists.at(row).specialistId;
                if (m_table) {
                    m_table->setCurrentCell(row, 0);
                }

                m_team.metadata.insert(QStringLiteral("default_agent"), specialistId);
                if (!m_storageManager.saveTeam(m_team)) {
                    QMessageBox::warning(this,
                                         tr("Team Editor"),
                                         tr("Failed to save Team changes."));
                    return;
                }

                emit teamUpdated(m_team.id);
                emit statusMessageRequested(tr("Default agent set to %1").arg(nameText));
                refreshSpecialistsTable();
                updateActionButtons();
            });
            m_table->setCellWidget(row, 5, defaultButton);
        }

        // Column 2: Role (display name + id).
        QString roleText = binding.roleId;
        if (!role.id.isEmpty() && !role.name.isEmpty()) {
            roleText = QStringLiteral("%1 (%2)").arg(role.name, binding.roleId);
        }
        auto *roleItem = new QTableWidgetItem(roleText);
        roleItem->setToolTip(QStringLiteral("Role id: %1").arg(binding.roleId));
        m_table->setItem(row, 2, roleItem);

        // Column 3: Model display name + id.
        QString modelText = spec.modelId.isEmpty() ? QStringLiteral("(no model)") : spec.modelId;
        if (!spec.modelId.isEmpty() && m_modelsCache.models.contains(spec.modelId)) {
            const ModelInfo info = m_modelsCache.models.value(spec.modelId);
            const QString display = info.displayName.isEmpty() ? spec.modelId : info.displayName;
            modelText = QStringLiteral("%1 (%2)").arg(display, spec.modelId);
        }
        auto *modelItem = new QTableWidgetItem(modelText);
        modelItem->setToolTip(QStringLiteral("Model id: %1").arg(spec.modelId));
        m_table->setItem(row, 3, modelItem);

        // Column 4: Cost / Tokens badge.
        const QString costText = formatCostBadge(spec.modelId);
        auto *costItem = new QTableWidgetItem(costText);
        if (!spec.modelId.isEmpty()) {
            costItem->setToolTip(QStringLiteral("Model: %1\nCost badge: %2").arg(spec.modelId, costText));
        }
        m_table->setItem(row, 4, costItem);

        m_table->setRowHidden(row, !m_showStockSpecialists && rowIsStock(row));
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);

    if (!previouslySelectedId.isEmpty()) {
        int desiredRow = -1;
        int firstVisibleRow = -1;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (firstVisibleRow < 0 && !m_table->isRowHidden(row)) {
                firstVisibleRow = row;
            }
            if (!m_table->isRowHidden(row) && specialistIdAtRow(row) == previouslySelectedId) {
                desiredRow = row;
                break;
            }
        }
        if (desiredRow >= 0) {
            m_table->setCurrentCell(desiredRow, 0);
        } else if (firstVisibleRow >= 0) {
            m_table->setCurrentCell(firstVisibleRow, 0);
        } else {
            m_table->setCurrentItem(nullptr);
        }
    }

    m_updatingTable = false;
}

QString TeamEditorWidget::formatModelDisplay(const QString &modelId) const
{
    if (modelId.isEmpty()) {
        return QStringLiteral("(no model)");
    }
    if (!m_modelsCache.models.contains(modelId)) {
        return modelId;
    }
    const ModelInfo info = m_modelsCache.models.value(modelId);
    return info.displayName.isEmpty() ? modelId : info.displayName;
}

QString TeamEditorWidget::formatCostBadge(const QString &modelId) const
{
    if (modelId.isEmpty() || !m_modelsCache.models.contains(modelId)) {
        return QStringLiteral("-");
    }
    const ModelInfo info = m_modelsCache.models.value(modelId);

    int contextTokens = 0;
    const QJsonObject limitObj = info.data.value(QLatin1String("limit")).toObject();
    if (!limitObj.isEmpty()) {
        contextTokens = limitObj.value(QLatin1String("context")).toInt();
    } else {
        contextTokens = info.data.value(QLatin1String("context_window")).toInt();
    }

    QStringList parts;
    parts << QStringLiteral("in $%1 / out $%2")
                 .arg(QString::number(info.inputCost, 'f', 3))
                 .arg(QString::number(info.outputCost, 'f', 3));

    const QString ctxStr = contextWindowString(contextTokens);
    if (!ctxStr.isEmpty()) {
        parts << ctxStr;
    }

    return parts.join(QStringLiteral(" - "));
}

void TeamEditorWidget::updateActionButtons()
{
    const bool hasTeam = !m_team.id.isEmpty();
    const bool dirty = hasDirtyChanges();
    const int row = currentSpecialistRow();
    const bool hasSelection = (row >= 0);
    const int rowCount = m_table ? m_table->rowCount() : 0;

    if (m_addButton) {
        m_addButton->setEnabled(hasTeam);
    }
    if (m_removeButton) {
        m_removeButton->setEnabled(hasSelection);
    }
    if (m_moveUpButton) {
        m_moveUpButton->setEnabled(hasSelection && row > 0);
    }
    if (m_moveDownButton) {
        m_moveDownButton->setEnabled(hasSelection && row >= 0 && row < rowCount - 1);
    }
    if (m_duplicateButton) {
        m_duplicateButton->setEnabled(hasTeam && rowCount > 0);
    }
    const bool canReset = isResettableStockClone(m_team, m_storageManager);
    if (m_resetButton) {
        m_resetButton->setVisible(canReset);
        m_resetButton->setEnabled(canReset);
        m_resetButton->setToolTip(tr("Reset this cloned Team back to its original stock source"));
    }
    if (m_compareButton) {
        m_compareButton->setEnabled(hasTeam);
    }
    if (m_dirtyIndicator) {
        m_dirtyIndicator->setVisible(hasTeam && dirty);
    }
    if (m_revertButton) {
        m_revertButton->setEnabled(hasTeam && dirty);
    }
    // F1: the Apply Team... footer button only makes sense when a Team
    // is actually loaded; no Team = no pre-selection to send upstream.
    if (m_applyButton) {
        m_applyButton->setEnabled(hasTeam);
    }
}

bool TeamEditorWidget::hasDirtyChanges() const
{
    if (m_team.id.isEmpty()) {
        return false;
    }

    const Team stored = m_storageManager.loadTeam(m_team.id);
    if (stored.id.isEmpty()) {
        return false;
    }

    return !teamsEqual(m_team, stored);
}

QString TeamEditorWidget::teamId() const
{
    return m_team.id;
}

int TeamEditorWidget::currentSpecialistRow() const
{
    if (!m_table) {
        return -1;
    }
    return m_table->currentRow();
}

QString TeamEditorWidget::specialistIdAtRow(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return QString();
    }
    QTableWidgetItem *item = m_table->item(row, 0);
    if (!item) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

bool TeamEditorWidget::rowIsStock(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return false;
    }

    QTableWidgetItem *item = m_table->item(row, 0);
    if (!item) {
        return false;
    }

    return item->data(Qt::UserRole + 1).toBool();
}

void TeamEditorWidget::setTeamId(const QString &teamId)
{
    if (teamId.isEmpty()) {
        m_team = Team();
    } else {
        const Team loaded = m_storageManager.loadTeam(teamId);
        m_team = loaded.id.isEmpty() ? Team() : loaded;
    }

    refreshSpecialistsTable();
    updateActionButtons();
}

void TeamEditorWidget::onPrimaryItemChanged(QTableWidgetItem *item)
{
    if (!item || m_updatingTable) {
        return;
    }
    if (item->column() != 0) {
        return;
    }

    const QString specialistId = item->data(Qt::UserRole).toString();
    if (specialistId.isEmpty()) {
        return;
    }

    const bool checked = (item->checkState() == Qt::Checked);
    const bool already = m_team.primarySpecialistIds.contains(specialistId);

    if (checked == already) {
        return;
    }

    if (checked) {
        m_team.primarySpecialistIds.append(specialistId);
    } else {
        m_team.primarySpecialistIds.removeAll(specialistId);
    }

    if (!m_storageManager.saveTeam(m_team)) {
        QMessageBox::warning(this,
                             tr("Team Editor"),
                             tr("Failed to save Team changes."));
        // Revert checkbox to the authoritative in-memory value.
        m_updatingTable = true;
        item->setCheckState(already ? Qt::Checked : Qt::Unchecked);
        m_updatingTable = false;
        updateActionButtons();
        return;
    }

    emit teamUpdated(m_team.id);
    emit specialistUpdated(specialistId);
}

void TeamEditorWidget::onAddSpecialist()
{
    if (m_team.id.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Add Specialist"),
                                 tr("Select a Team first."));
        return;
    }

    AddSpecialistDialog dlg(m_storageManager, this);

    // F2: forward the dialog's createRoleRequested signal through this
    // widget so hosts (TeamsWidget -> MainWindow) can switch to the Roles
    // view and pre-seed the proposed Role name. The connection is in-scope
    // only for this exec() call.
    connect(&dlg, &AddSpecialistDialog::createRoleRequested,
            this, &TeamEditorWidget::createRoleRequested);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString roleId = dlg.selectedRoleId();
    const QString modelId = dlg.selectedModelId();
    const QString overrideText = dlg.promptOverrideText().trimmed();

    if (roleId.isEmpty() || modelId.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Add Specialist"),
                             tr("Both a Role and a model must be selected."));
        return;
    }

    // G4 hot-swap capability gate: warn the user when the chosen model
    // is not flagged tool-call-capable in the live catalog AND the
    // Role it will be bound to allows editing (`Primary` / `All` modes
    // — `Subagent` stays silent because seeded subagent Roles are
    // read-only by design). A missing catalog OR an unknown model id
    // yields a default `ModelCapabilities` with `toolcall=false`, and
    // we treat that case as "unknown" — silently skip the warning
    // rather than blocking on catalog absence, since the apply-path
    // ContractChecker still catches structurally-bad model strings
    // before any write reaches the filesystem.
    const Role role = m_storageManager.loadRole(roleId);
    const bool roleAllowsEditing = (role.mode == Role::Mode::Primary
                                    || role.mode == Role::Mode::All);
    const ProviderCatalog::ModelCapabilities caps =
        ProviderCatalog::instance().capabilitiesForModel(modelId);
    const bool catalogKnowsModel =
        ProviderCatalog::instance().isValidModel(modelId);
    if (roleAllowsEditing
        && catalogKnowsModel
        && !caps.toolcall) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Add Specialist"));
        box.setText(tr("Model %1 does not support tool calls.")
                        .arg(modelId));
        box.setInformativeText(tr(
            "Edit-mode Specialists require tool-call capability to apply "
            "edits and run file operations. Continue anyway, or pick a "
            "tool-call-capable model?"));
        QPushButton *continueBtn = box.addButton(tr("Continue anyway"),
                                                 QMessageBox::AcceptRole);
        QPushButton *cancelBtn = box.addButton(tr("Cancel"),
                                               QMessageBox::RejectRole);
        box.setDefaultButton(cancelBtn);
        box.exec();
        if (box.clickedButton() != continueBtn) {
            return;
        }
    }

    // PARADIGM §2.3 invariant: a Team must reference distinct Roles.
    for (const auto &binding : std::as_const(m_team.specialists)) {
        if (binding.roleId == roleId) {
            QMessageBox::warning(
                this,
                tr("Add Specialist"),
                tr("This Team already has a Specialist bound to Role '%1'.\n"
                   "Use Duplicate as Variant to create a copy of this Team and "
                   "swap the model there.")
                    .arg(roleId));
            return;
        }
    }

    Specialist spec;
    const QString roleBase = role.name.isEmpty() ? roleId : role.name;
    spec.id = generateUniqueSpecialistId(m_storageManager,
                                         QStringLiteral("spec-%1").arg(roleBase));
    spec.roleId = roleId;
    spec.modelId = modelId;
    if (!role.name.isEmpty()) {
        spec.name = role.name;
    }
    if (!overrideText.isEmpty()) {
        spec.promptOverride = QJsonValue(overrideText);
    }

    if (!m_storageManager.saveSpecialist(spec)) {
        QMessageBox::warning(this,
                             tr("Add Specialist"),
                             tr("Failed to save the new Specialist record."));
        return;
    }

    Team::SpecialistBinding binding;
    binding.roleId = roleId;
    binding.specialistId = spec.id;
    m_team.specialists.append(binding);

    // The first Specialist in a Team defaults to primary; also fall back to
    // marking primary when no primaries remain after edits.
    if (m_team.specialists.size() == 1 || m_team.primarySpecialistIds.isEmpty()) {
        if (!m_team.primarySpecialistIds.contains(spec.id)) {
            m_team.primarySpecialistIds.append(spec.id);
        }
    }

    if (!m_storageManager.saveTeam(m_team)) {
        QMessageBox::warning(this,
                             tr("Team Editor"),
                             tr("Specialist saved but Team update failed."));
        updateActionButtons();
        return;
    }

    emit teamUpdated(m_team.id);
    emit specialistUpdated(spec.id);

    refreshSpecialistsTable();
    updateActionButtons();
    if (m_table) {
        m_table->setCurrentCell(static_cast<int>(m_team.specialists.size()) - 1, 0);
    }
}

void TeamEditorWidget::onRemoveSpecialist()
{
    if (m_team.id.isEmpty()) {
        return;
    }
    const int row = currentSpecialistRow();
    if (row < 0 || row >= m_team.specialists.size()) {
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        tr("Remove Specialist"),
        tr("Remove the selected Specialist from this Team?\n"
           "The underlying Specialist record is kept; it can be reused by "
           "other Teams or by creating a new Team variant."),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    const QString specialistId = m_team.specialists.at(row).specialistId;
    m_team.specialists.removeAt(row);
    m_team.primarySpecialistIds.removeAll(specialistId);

    if (!m_storageManager.saveTeam(m_team)) {
        QMessageBox::warning(this,
                             tr("Team Editor"),
                             tr("Failed to save Team changes."));
        updateActionButtons();
        return;
    }

    emit teamUpdated(m_team.id);
    emit specialistUpdated(specialistId);

    refreshSpecialistsTable();
    if (m_table && m_table->rowCount() > 0) {
        const int newRow = qMin(row, m_table->rowCount() - 1);
        m_table->setCurrentCell(newRow, 0);
    }
    updateActionButtons();
}

void TeamEditorWidget::onMoveUp()
{
    if (m_team.id.isEmpty()) {
        return;
    }
    const int row = currentSpecialistRow();
    if (row <= 0 || row >= m_team.specialists.size()) {
        return;
    }

    const QString specialistId = m_team.specialists.at(row).specialistId;
    m_team.specialists.move(row, row - 1);
    if (!m_storageManager.saveTeam(m_team)) {
        QMessageBox::warning(this,
                             tr("Team Editor"),
                             tr("Failed to save Team changes."));
        updateActionButtons();
        return;
    }

    emit teamUpdated(m_team.id);
    emit specialistUpdated(specialistId);

    refreshSpecialistsTable();
    if (m_table) {
        m_table->setCurrentCell(row - 1, 0);
    }
    updateActionButtons();
}

void TeamEditorWidget::onMoveDown()
{
    if (m_team.id.isEmpty()) {
        return;
    }
    const int row = currentSpecialistRow();
    if (row < 0 || row >= m_team.specialists.size() - 1) {
        return;
    }

    const QString specialistId = m_team.specialists.at(row).specialistId;
    m_team.specialists.move(row, row + 1);
    if (!m_storageManager.saveTeam(m_team)) {
        QMessageBox::warning(this,
                             tr("Team Editor"),
                             tr("Failed to save Team changes."));
        updateActionButtons();
        return;
    }

    emit teamUpdated(m_team.id);
    emit specialistUpdated(specialistId);

    refreshSpecialistsTable();
    if (m_table) {
        m_table->setCurrentCell(row + 1, 0);
    }
    updateActionButtons();
}

void TeamEditorWidget::onDuplicateVariant()
{
    if (m_team.id.isEmpty()) {
        return;
    }
    if (m_team.specialists.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Duplicate as Variant"),
                                 tr("This Team has no Specialists to duplicate. "
                                    "Add at least one Specialist first."));
        return;
    }

    Team copy = m_team;
    const QString originName = copy.name.isEmpty() ? copy.id : copy.name;
    copy.id = generateUniqueTeamId(m_storageManager, copy.id + QStringLiteral("-variant"));
    copy.name = originName + QStringLiteral(" (variant)");
    copy.description = QStringLiteral("Variant of %1").arg(originName);
    // Bump version to mark the variant as a fresh artifact.
    if (copy.version.isEmpty()) {
        copy.version = QStringLiteral("0.1.0");
    } else {
        copy.version = copy.version + QStringLiteral("-variant");
    }

    if (!m_storageManager.saveTeam(copy)) {
        QMessageBox::warning(this,
                             tr("Duplicate as Variant"),
                             tr("Failed to save the new Team variant."));
        return;
    }

    emit teamUpdated(copy.id);

    emit teamVariantCreated(copy.id);
}

void TeamEditorWidget::onResetToStock()
{
    if (m_team.id.isEmpty() || !isResettableStockClone(m_team, m_storageManager)) {
        return;
    }

    const Team stock = m_storageManager.loadTeam(m_team.parentTeamId);
    if (stock.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Reset to stock"),
                             tr("Could not load the original stock Team."));
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Reset to stock"));
    box.setText(tr("Reset this Team to a fresh copy of '%1'?")
                    .arg(stock.name.isEmpty() ? stock.id : stock.name));
    box.setInformativeText(tr("Keep the current Team name, or replace it with the stock version. The Team id stays the same either way."));
    QPushButton *keepButton = box.addButton(tr("Keep current name"), QMessageBox::AcceptRole);
    QPushButton *replaceButton = box.addButton(tr("Replace name with stock"), QMessageBox::DestructiveRole);
    QAbstractButton *cancelButton = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keepButton);
    box.exec();

    const QAbstractButton *clicked = box.clickedButton();
    if (clicked == nullptr || clicked == cancelButton) {
        return;
    }

    Team reset = stock;
    reset.id = m_team.id;
    if (clicked == keepButton) {
        reset.name = m_team.name;
    } else if (clicked != replaceButton) {
        return;
    }
    reset.parentTeamId.clear();
    reset.metadata.remove(QStringLiteral("cloned_from"));
    reset.metadata.remove(QStringLiteral("cloned_from_team_id"));
    reset.metadata.remove(QStringLiteral("stock"));

    if (!m_storageManager.saveTeam(reset)) {
        QMessageBox::warning(this,
                             tr("Reset to stock"),
                             tr("Failed to save the reset Team."));
        return;
    }

    m_team = reset;
    refreshSpecialistsTable();
    updateActionButtons();

    emit teamUpdated(m_team.id);
}

void TeamEditorWidget::onCompare()
{
    if (m_team.id.isEmpty()) {
        return;
    }

    // F3: real Team-vs-Team rendered-config diff (chosen over a Trial-vs-Trial
    // view for v1, per ROADMAP.md F3). Trial-vs-Trial stays deferred.
    const QList<Team> teams = m_storageManager.listTeams();
    if (teams.size() < 2) {
        QMessageBox::information(
            this,
            tr("Compare"),
            tr("At least two Teams are required to run a diff.\n"
               "Create another Team first (Ctrl+N from the Teams menu or the "
               "New Team button) and try again."));
        return;
    }

    // Build the picker list, excluding the current Team so the user never
    // accidentally diffs a Team against itself.
    QStringList items;
    QStringList ids;
    for (int i = 0; i < teams.size(); ++i) {
        const Team &team = teams.at(i);
        if (team.id == m_team.id) {
            continue;
        }
        items << QStringLiteral("%1 (%2)").arg(team.name.isEmpty() ? team.id : team.name, team.id);
        ids << team.id;
    }

    if (items.isEmpty()) {
        // Defensive: the early size<2 check covers this, but guard anyway.
        return;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this,
        tr("Compare Team"),
        tr("Select another Team to diff against '%1':")
            .arg(m_team.name.isEmpty() ? m_team.id : m_team.name),
        items,
        0,
        false,
        &ok);

    if (!ok || chosen.isEmpty()) {
        return;
    }

    const int idx = items.indexOf(chosen);
    if (idx < 0 || idx >= ids.size()) {
        return;
    }

    const QString otherTeamId = ids.at(idx);
    const Team otherTeam = m_storageManager.loadTeam(otherTeamId);
    if (otherTeam.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Compare"),
                             tr("Failed to load the selected Team."));
        return;
    }

    const QJsonObject leftConfig = renderTeamConfig(m_team, m_storageManager);
    const QJsonObject rightConfig = renderTeamConfig(otherTeam, m_storageManager);

    const QString leftText = QString::fromUtf8(
        QJsonDocument(leftConfig).toJson(QJsonDocument::Indented));
    const QString rightText = QString::fromUtf8(
        QJsonDocument(rightConfig).toJson(QJsonDocument::Indented));

    const QStringList leftLines = leftText.split(QLatin1Char('\n'));
    const QStringList rightLines = rightText.split(QLatin1Char('\n'));
    const int maxLines = qMax(leftLines.size(), rightLines.size());

    QVector<bool> leftDiff(maxLines, false);
    QVector<bool> rightDiff(maxLines, false);
    for (int i = 0; i < maxLines; ++i) {
        const QString left = (i < leftLines.size()) ? leftLines.at(i) : QString();
        const QString right = (i < rightLines.size()) ? rightLines.at(i) : QString();
        const bool differ = (left != right);
        leftDiff[i] = differ;
        rightDiff[i] = differ;
    }

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Team Diff: %1 vs %2")
                            .arg(m_team.name.isEmpty() ? m_team.id : m_team.name,
                                 otherTeam.name.isEmpty() ? otherTeam.id : otherTeam.name));

    auto *layout = new QHBoxLayout(dlg);
    auto *leftEdit = new QTextEdit(dlg);
    auto *rightEdit = new QTextEdit(dlg);
    leftEdit->setLineWrapMode(QTextEdit::NoWrap);
    rightEdit->setLineWrapMode(QTextEdit::NoWrap);

    populateDiffEditor(leftEdit, leftLines, leftDiff, QColor(255, 200, 200));
    populateDiffEditor(rightEdit, rightLines, rightDiff, QColor(200, 255, 200));

    layout->addWidget(leftEdit);
    layout->addWidget(rightEdit);
    dlg->resize(1000, 600);
    dlg->exec();
}

void TeamEditorWidget::onApplyTeam()
{
    // F1: re-use the public teamId() getter (never read m_team.id
    // directly from a handler -- the getter is the contract).
    const QString id = teamId();
    if (id.isEmpty()) {
        // updateActionButtons() already disables the button in this state,
        // but defend at handler depth in case the button is triggered via
        // a future shortcut.
        return;
    }
    emit applyTeamRequested(id);
}

void TeamEditorWidget::onRevertChanges()
{
    if (!hasDirtyChanges()) {
        updateActionButtons();
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        tr("Revert changes"),
        tr("Reload this Team from storage and discard in-memory edits?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    reloadTeamFromStorage();
}

void TeamEditorWidget::reloadTeamFromStorage()
{
    const QString id = teamId();
    if (id.isEmpty()) {
        return;
    }

    const Team reloaded = m_storageManager.loadTeam(id);
    if (reloaded.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Revert changes"),
                             tr("Failed to reload the Team from storage."));
        return;
    }

    m_team = reloaded;
    refreshSpecialistsTable();
    updateActionButtons();
    emit teamReverted(id, QStringLiteral("user-discard"));
}
