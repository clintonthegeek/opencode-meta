#include "ui/TeamsWidget.h"

#include <QAction>
#include <QDateTime>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/FilterBar.h"
#include "ui/TeamEditorWidget.h"

namespace {

void flashBlockedDeleteRow(QTableWidget *table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) {
        return;
    }

    const QColor tint(255, 232, 232);
    QList<QPair<QTableWidgetItem *, QBrush>> itemBrushes;
    QList<QPair<QWidget *, QString>> widgetStyles;

    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto *item = table->item(row, col)) {
            itemBrushes.append(qMakePair(item, item->background()));
            item->setBackground(tint);
        }
        if (auto *cellWidget = table->cellWidget(row, col)) {
            widgetStyles.append(qMakePair(cellWidget, cellWidget->styleSheet()));
            cellWidget->setStyleSheet(QStringLiteral("background-color: rgba(255, 232, 232, 0.85);"));
        }
    }

    table->viewport()->update();
    QTimer::singleShot(250, table, [itemBrushes, widgetStyles]() {
        for (const auto &entry : itemBrushes) {
            if (entry.first) {
                entry.first->setBackground(entry.second);
            }
        }
        for (const auto &entry : widgetStyles) {
            if (entry.first) {
                entry.first->setStyleSheet(entry.second);
            }
        }
    });
}

QLabel *makeStockBadge(const QString &text, QWidget *parent)
{
    auto *badge = new QLabel(text, parent);
    badge->setObjectName(QStringLiteral("stockBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setToolTip(QObject::tr("Stock seed item - cannot be modified or deleted. Created automatically for new users."));
    badge->setStyleSheet(QStringLiteral(
        "QLabel#stockBadge {"
        " background-color: #eef2ff;"
        " color: #4338ca;"
        " border: 1px solid #c7d2fe;"
        " border-radius: 8px;"
        " padding: 1px 6px;"
        " font-size: 10px;"
        " font-weight: 600;"
        " }"));
    return badge;
}

QToolButton *makeStockInfoButton(int row,
                                 QTableWidget *table,
                                 QAction *aboutAction,
                                 QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("stockInfoButton"));
    button->setAutoRaise(true);
    button->setToolTip(QObject::tr("About this stock item"));
    button->setIcon(parent->style()->standardIcon(QStyle::SP_MessageBoxInformation));
    button->setIconSize(QSize(12, 12));
    QObject::connect(button, &QToolButton::clicked, parent, [table, row, aboutAction]() {
        if (table) {
            table->setCurrentCell(row, 0);
        }
        if (aboutAction) {
            aboutAction->trigger();
        }
    });
    return button;
}

QWidget *makeStockNameCell(int row,
                           const QString &name,
                           QTableWidget *table,
                           QAction *aboutAction,
                           QWidget *parent)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *nameLabel = new QLabel(name, cell);
    layout->addWidget(nameLabel);
    layout->addWidget(makeStockBadge(QObject::tr("Stock"), cell));
    layout->addWidget(makeStockInfoButton(row, table, aboutAction, cell));
    layout->addStretch(1);
    return cell;
}

QString stockAboutText(const QString &itemKind)
{
    return QObject::tr("This %1 came from the stock seed data.\n\n"
                       "To opt out, turn off the hidden `settings/seed_stock_defaults` flag in your opencode settings.")
        .arg(itemKind);
}

bool loadShowStockTeamsSetting()
{
    return QSettings().value(QStringLiteral("settings/teams_show_stock"), false).toBool();
}

void saveShowStockTeamsSetting(bool showStock)
{
    QSettings().setValue(QStringLiteral("settings/teams_show_stock"), showStock);
}

// Produce a unique Team id under the given storage root, starting from
// the slugified base and appending "-N" as needed.
QString generateUniqueTeamId(StorageManager &storage, const QString &base)
{
    QString baseId;
    baseId.reserve(base.size());
    for (QChar c : base) {
        if (c.isLetterOrNumber()) {
            baseId.append(c.toLower());
        } else if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            if (!baseId.endsWith(QLatin1Char('-'))) {
                baseId.append(QLatin1Char('-'));
            }
        }
    }
    while (baseId.endsWith(QLatin1Char('-'))) {
        baseId.chop(1);
    }
    if (baseId.isEmpty()) {
        baseId = QStringLiteral("team");
    }

    QString candidate = baseId;
    int suffix = 1;
    while (true) {
        const Team existing = storage.loadTeam(candidate);
        if (existing.id.isEmpty()) {
            return candidate;
        }
        candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix++);
    }
}

} // namespace

TeamsWidget::TeamsWidget(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    auto *layout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Teams: reusable AI coding lineups."), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *contentLayout = new QHBoxLayout();

    // ROADMAP P2-2: keep the table and editor in their existing
    // side-by-side layout, but group the table with a FilterBar in a
    // left column so the search box only affects the Team rows.
    auto *leftColumn = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *filterRow = new QWidget(leftColumn);
    auto *filterRowLayout = new QHBoxLayout(filterRow);
    filterRowLayout->setContentsMargins(0, 0, 0, 0);
    filterRowLayout->setSpacing(6);
    auto *filterBar = new FilterBar(tr("Filter teams..."), filterRow);
    m_filterEdit = filterBar->findChild<QLineEdit *>();
    filterRowLayout->addWidget(filterBar, 1);
    m_showStockCheck = new QCheckBox(tr("Show stock"), filterRow);
    m_showStockCheck->setObjectName(QStringLiteral("teamsWidget.showStock"));
    m_showStockCheck->setChecked(loadShowStockTeamsSetting());
    m_showStockCheck->setToolTip(tr("Show stock Teams in the list"));
    filterRowLayout->addWidget(m_showStockCheck);
    leftLayout->addWidget(filterRow);

    m_table = new QTableWidget(leftColumn);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << tr("ID")
            << tr("Name")
            << tr("Description")
            << tr("Primary Specialists");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    leftLayout->addWidget(m_table, 1);

    contentLayout->addWidget(leftColumn, 1);

    m_editor = new TeamEditorWidget(m_storageManager, this);
    contentLayout->addWidget(m_editor, 1);

    // Drive row visibility from a FilterProxyModel; the QTableWidget
    // stays the view so existing selection / double-click paths keep
    // using item()/currentRow().
    m_filterProxy = new FilterProxyModel(leftColumn);
    m_filterProxy->setSourceModel(m_table->model());
    m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filterProxy->setFilterKeyColumn(-1); // every column

    connect(filterBar, &FilterBar::filterChanged,
             this, &TeamsWidget::applyFilter);
    connect(m_showStockCheck, &QCheckBox::toggled, this, [this]() {
        saveShowStockTeamsSetting(m_showStockCheck && m_showStockCheck->isChecked());
        if (m_editor) {
            m_editor->setShowStock(m_showStockCheck && m_showStockCheck->isChecked());
        }
        applyFilter(m_filterEdit ? m_filterEdit->text() : QString());
    });

    connect(m_editor,
            &TeamEditorWidget::teamVariantCreated,
            this,
            [this](const QString &newTeamId) {
                Q_UNUSED(newTeamId);
                // Refresh the table so the user can see and click the
                // new variant, but DO NOT auto-select it: switching the
                // selection flips the editor's currently loaded Team
                // via the updateActionStates() cascade, which would
                // yank the user away from the Team they were just
                // working on (and previously caused the cross-view
                // smoke test to apply the Variant instead of the
                // parent Team). The user can click the variant row to
                // open it if they want to.
                refreshTeams();
            });
    // F2: forward the editor's createRoleRequested signal so MainWindow
    // can switch to the Roles tab and open Role authoring flow.
    connect(m_editor, &TeamEditorWidget::createRoleRequested,
            this, &TeamsWidget::createRoleRequested);

    // F1: forward the editor's applyTeamRequested signal so MainWindow
    // can open the existing apply dialog (TeamsDialog) with the current
    // Team pre-selected, mirroring the Team menu's "Apply Team..." path.
    connect(m_editor, &TeamEditorWidget::applyTeamRequested,
            this, &TeamsWidget::applyTeamRequested);

    layout->addLayout(contentLayout, 1);

    if (m_editor) {
        m_editor->setShowStock(m_showStockCheck && m_showStockCheck->isChecked());
    }

    auto *buttonRow = new QHBoxLayout();
    m_newButton = new QPushButton(tr("New Team"), this);
    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_cloneButton = new QPushButton(tr("Clone & edit"), this);
    m_newButton->setObjectName(QStringLiteral("teamsWidget.newButton"));
    m_deleteButton->setObjectName(QStringLiteral("teamsWidget.deleteButton"));
    m_cloneButton->setObjectName(QStringLiteral("teamsWidget.cloneButton"));
    buttonRow->addWidget(m_newButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addWidget(m_cloneButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(m_newButton, &QPushButton::clicked,
            this, &TeamsWidget::createTeam);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &TeamsWidget::deleteSelectedTeam);
    connect(m_cloneButton, &QPushButton::clicked,
            this, &TeamsWidget::cloneSelectedStockTeam);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &TeamsWidget::onSelectionChanged);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (!m_table) {
            return;
        }

        const int row = m_table->rowAt(pos.y());
        if (row < 0 || !rowIsStock(row)) {
            return;
        }

        m_table->setCurrentCell(row, 0);

        QMenu menu(m_table);
        if (m_aboutStockItemAction) {
            menu.addAction(m_aboutStockItemAction);
        }
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });

    m_aboutStockItemAction = new QAction(tr("About this stock item"), this);
    m_aboutStockItemAction->setObjectName(QStringLiteral("teamsWidget.aboutStockItemAction"));
    connect(m_aboutStockItemAction, &QAction::triggered, this, &TeamsWidget::showAboutThisStockItem);

    installShortcuts();

    refreshTeams();
    updateActionStates();
}

void TeamsWidget::installShortcuts()
{
    // Ctrl+N: create a new Team. Widget-scoped so it does not clash with
    // whatever global shortcuts MainWindow may add later.
    m_newAction = new QAction(tr("New Team"), this);
    m_newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    m_newAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_newAction->setStatusTip(tr("Create a new Team (Ctrl+N)"));
    addAction(m_newAction);
    connect(m_newAction, &QAction::triggered, this, &TeamsWidget::createTeam);

    // Delete key on the table also drives the delete path so users do
    // not need to mouse over to the Delete button.
    m_deleteAction = new QAction(tr("Delete Team"), this);
    m_deleteAction->setObjectName(QStringLiteral("teamsWidget.deleteAction"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_deleteAction->setStatusTip(tr("Delete the selected Team"));
    addAction(m_deleteAction);
    connect(m_deleteAction, &QAction::triggered,
            this, &TeamsWidget::onDeleteKeyPressedOnTable);
}

void TeamsWidget::refreshTeams()
{
    // Capture the currently selected team id BEFORE rebuilding the row
    // list. The storage layer returns Teams in directory-iteration
    // order which is filesystem-dependent; if a new Team is added
    // (e.g. via Duplicate Variant) the next refresh can shift the
    // previously-selected row index to point at a different Team. We
    // want the user's selection (and the editor's teamId) to stay on
    // the same Team across refreshes.
    QString previouslySelectedId;
    if (m_table && m_table->currentRow() >= 0) {
        previouslySelectedId = selectedTeamId();
    }

    const QList<Team> teams = m_storageManager.listTeams();

    m_table->setRowCount(teams.size());

    int desiredRow = -1;
    for (int row = 0; row < teams.size(); ++row) {
        const Team &team = teams.at(row);

        auto *idItem = new QTableWidgetItem(team.id);
        idItem->setData(Qt::UserRole, team.id);
        idItem->setData(Qt::UserRole + 1, m_storageManager.isStockTeam(team));
        m_table->setItem(row, 0, idItem);

        auto *nameItem = new QTableWidgetItem(team.name);
        if (m_storageManager.isStockTeam(team)) {
            const QString displayName = team.name.isEmpty() ? team.id : team.name;
            nameItem->setText(displayName);
            nameItem->setToolTip(tr("Stock team"));
            m_table->setCellWidget(row,
                                   1,
                                   makeStockNameCell(row,
                                                     displayName,
                                                     m_table,
                                                     m_aboutStockItemAction,
                                                     m_table));
        }
        m_table->setItem(row, 1, nameItem);

        auto *descItem = new QTableWidgetItem(team.description);
        m_table->setItem(row, 2, descItem);

        const int primaryCount = team.primarySpecialistIds.size();
        auto *primaryItem = new QTableWidgetItem(QString::number(primaryCount));
        m_table->setItem(row, 3, primaryItem);

        if (!previouslySelectedId.isEmpty() && team.id == previouslySelectedId) {
            desiredRow = row;
        }
    }

    m_table->resizeColumnsToContents();

    // Restore the user's prior selection by id BEFORE we re-apply the
    // active filter. applyFilter() reaches into updateActionStates()
    // and would otherwise shove the editor onto whatever Team now
    // happens to live at the old row index (the directory-iteration
    // order is filesystem-dependent and shifts when new Teams land on
    // disk). Blocking signals keeps the visual highlight in place
    // without triggering the editor-flip cascade.
    if (desiredRow >= 0 && !previouslySelectedId.isEmpty()) {
        m_table->blockSignals(true);
        m_table->setCurrentCell(desiredRow, 0);
        m_table->blockSignals(false);
    }

    // Re-apply the active filter so newly added/changed rows respect
    // the current search.
    applyFilter(m_filterEdit ? m_filterEdit->text() : QString());
}

QString TeamsWidget::selectedTeamId() const
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

bool TeamsWidget::selectedTeamIsStock() const
{
    return rowIsStock(m_table ? m_table->currentRow() : -1);
}

bool TeamsWidget::rowIsStock(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return false;
    }

    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem) {
        return false;
    }

    return idItem->data(Qt::UserRole + 1).toBool();
}

void TeamsWidget::createTeam()
{
    bool ok = false;
    const QString rawName = QInputDialog::getText(this,
                                                 tr("New Team"),
                                                 tr("Name for the new Team:"),
                                                 QLineEdit::Normal,
                                                 QString(),
                                                 &ok);
    if (!ok) {
        return;
    }

    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this,
                                 tr("New Team"),
                                 tr("Team name cannot be empty."));
        return;
    }

    Team team;
    team.id = generateUniqueTeamId(m_storageManager, name);
    team.name = name;
    team.description = QStringLiteral("Created on %1")
                           .arg(QDateTime::currentDateTimeUtc()
                                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    team.version = QStringLiteral("0.1.0");

    if (!m_storageManager.saveTeam(team)) {
        QMessageBox::warning(this,
                             tr("New Team"),
                             tr("Failed to save the new Team to disk."));
        return;
    }

    refreshTeams();
    selectTeamById(team.id);
    if (m_editor) {
        m_editor->setTeamId(team.id);
    }

    emit teamCreated(team.id);
}

void TeamsWidget::deleteSelectedTeam()
{
    const QString teamId = selectedTeamId();
    if (teamId.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Delete Team"),
                                 tr("Select a Team first."));
        return;
    }

    const Team team = m_storageManager.loadTeam(teamId);
    if (team.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Delete Team"),
                             tr("Could not load the selected Team; nothing to delete."));
        return;
    }

    if (m_storageManager.isStockTeam(team)) {
        flashBlockedDeleteRow(m_table, m_table->currentRow());
        return;
    }

    QString summary = QStringLiteral("Delete Team '%1'?")
                          .arg(team.name.isEmpty() ? team.id : team.name);
    const int specialistCount = team.specialists.size();
    if (specialistCount > 0) {
        summary += QStringLiteral("\n\nThis Team has %1 Specialist binding(s). "
                                  "Deleting the Team does NOT remove the underlying "
                                  "Specialist records; they can still be reused by "
                                  "other Teams or by creating a new Team.")
                       .arg(specialistCount);
    } else {
        summary += QStringLiteral("\n\nThis Team has no Specialists yet.");
    }

    const auto reply = QMessageBox::question(
        this,
        tr("Delete Team"),
        summary,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!m_storageManager.deleteTeam(teamId)) {
        QMessageBox::warning(this,
                             tr("Delete Team"),
                             tr("Failed to remove the Team file from disk."));
        return;
    }

    if (m_editor) {
        m_editor->setTeamId(QString());
    }

    refreshTeams();
    updateActionStates();

    emit teamDeleted(teamId);
}

void TeamsWidget::cloneSelectedStockTeam()
{
    const QString teamId = selectedTeamId();
    if (teamId.isEmpty()) {
        return;
    }

    const Team selected = m_storageManager.loadTeam(teamId);
    if (selected.id.isEmpty() || !m_storageManager.isStockTeam(selected)) {
        return;
    }

    const Team cloned = m_storageManager.cloneTeam(teamId);
    if (cloned.id.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Clone Team"),
                             tr("Failed to clone the selected stock Team."));
        return;
    }

    refreshTeams();
    selectTeamById(cloned.id);
    if (m_editor) {
        m_editor->setTeamId(cloned.id);
    }

    emit teamCreated(cloned.id);
}

void TeamsWidget::onDeleteKeyPressedOnTable()
{
    // Only act on Delete when the table actually holds a selection, so
    // navigation keys are not consumed while focus is on the empty area.
    if (m_table && m_table->currentRow() >= 0) {
        deleteSelectedTeam();
    }
}

void TeamsWidget::showAboutThisStockItem()
{
    if (!m_table || !rowIsStock(m_table->currentRow())) {
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("About this stock item"));
    box.setIcon(QMessageBox::Information);
    box.setText(stockAboutText(tr("Team")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void TeamsWidget::selectTeamById(const QString &teamId)
{
    if (!m_table || teamId.isEmpty()) {
        return;
    }

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (!idItem) {
            continue;
        }

        const QVariant userData = idItem->data(Qt::UserRole);
        const QString id = userData.isValid() ? userData.toString() : idItem->text();
        if (id == teamId) {
            // skip rows the active filter has hidden — they are not a
            // valid target for the editor surface.
            if (m_table->isRowHidden(row)) {
                continue;
            }
            m_table->setCurrentCell(row, 0);
            return;
        }
    }
}

void TeamsWidget::onSelectionChanged()
{
    updateActionStates();
}

void TeamsWidget::applyFilter(const QString &text)
{
    if (!m_table || !m_filterProxy) {
        return;
    }

    const QString needle = text.trimmed();
    m_filterProxy->setFilterFixedString(needle);
    const bool showStock = m_showStockCheck && m_showStockCheck->isChecked();

    const QModelIndex parent;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool match = needle.isEmpty() || m_filterProxy->acceptsRow(row, parent);
        const bool stockHidden = !showStock && rowIsStock(row);
        m_table->setRowHidden(row, !match || stockHidden);
    }

    if (m_table->currentRow() >= 0 && m_table->isRowHidden(m_table->currentRow())) {
        int firstVisibleRow = -1;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (!m_table->isRowHidden(row)) {
                firstVisibleRow = row;
                break;
            }
        }
        if (firstVisibleRow >= 0) {
            m_table->setCurrentCell(firstVisibleRow, 0);
        } else {
            m_table->setCurrentItem(nullptr);
        }
    }

    // Visibility just changed; make sure the action buttons (Delete,
    // Editor wiring) reflect the new visible selection rather than
    // whatever row happens to be currentCell().
    updateActionStates();
}

void TeamsWidget::updateActionStates()
{
    const bool hasSelection = (m_table && m_table->currentRow() >= 0);
    const bool stockSelection = hasSelection && selectedTeamIsStock();
    const bool deleteEnabled = hasSelection;
    const bool cloneEnabled = stockSelection;
    const QString deleteTooltip = stockSelection
        ? tr("Stock items cannot be deleted")
        : tr("Delete the selected Team");
    if (m_deleteButton) {
        m_deleteButton->setEnabled(deleteEnabled);
        m_deleteButton->setToolTip(deleteTooltip);
    }
    if (m_deleteAction) {
        m_deleteAction->setEnabled(deleteEnabled);
        m_deleteAction->setToolTip(deleteTooltip);
        m_deleteAction->setStatusTip(deleteTooltip);
    }
    if (m_cloneButton) {
        m_cloneButton->setVisible(cloneEnabled);
        m_cloneButton->setEnabled(cloneEnabled);
        m_cloneButton->setToolTip(tr("Clone this stock Team into an editable copy"));
    }

    if (m_editor) {
        const QString id = hasSelection ? selectedTeamId() : QString();
        m_editor->setTeamId(id);
    }
}
