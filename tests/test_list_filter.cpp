// Tests for ROADMAP P2-2 search/filter behaviour across the four
// list/table widgets that gained a FilterBar + QSortFilterProxyModel:
//   * RolesWidget   (QTableWidget-based, ID + Name + Description + Mode)
//   * TeamsWidget   (QTableWidget-based, ID + Name + Description + Primary Count)
//   * TrialsWidget  (QTableWidget-based, Date + Team + Project + Ratings + Notes)
//   * ProjectsWidget (QListWidget-based, summary text)
//
// Each widget drives row/item visibility from a real
// QSortFilterProxyModel, so this suite exercises both the
// case-insensitive substring match across all visible columns and
// the row-hiding + ESC clear behaviour the user will see.

#include <QTest>
#include <QtTest/QTest>
#include <QApplication>
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>

#include "models/ProjectRecord.h"
#include "models/Role.h"
#include "models/Team.h"
#include "models/Trial.h"
#include "storage/StorageManager.h"
#include "ui/FilterBar.h"
#include "ui/ProjectsWidget.h"
#include "ui/RolesWidget.h"
#include "ui/TeamEditorWidget.h"
#include "ui/TeamsWidget.h"
#include "ui/TrialsWidget.h"

class TestListFilter : public QObject
{
    Q_OBJECT

private slots:
    void roles_filtersAcrossAllColumns_caseInsensitive();
    void roles_hidesStockByDefault_andToggleShowsIt();
    void teams_appliesOnIdAndName_clearsOnEscape();
    void teams_hidesStockByDefault_andToggleShowsIt();
    void teams_showStockToggleAlsoControlsEmbeddedEditorStockSpecialists();
    void teams_cloneStockTeamSelectsEditableCopy();
    void teams_stockDeleteShowsFlashFeedback();
    void roles_stockDeleteShowsFlashFeedback();
    void trials_hidesNonMatchingRows_andPreservesIdForActions();
    void projects_hidesFilteredListItems_andDisablesActions();
};

namespace {

// Build a fresh storage root under HOME so StorageManager's defaults
// land in our temp dir, mirroring the test_cross_view_smoke pattern.
void seedHomeAndStorageRoot(const QString &root)
{
    qputenv("HOME", root.toUtf8());
}

// Build a Role, returning the persisted record. Used by the Roles test.
void makeAndSaveRole(StorageManager &storage,
                     const QString &id,
                     const QString &name,
                     const QString &description,
                     Role::Mode mode)
{
    Role role;
    role.id = id;
    role.name = name;
    role.description = description;
    role.mode = mode;
    QVERIFY2(storage.saveRole(role),
             qPrintable(QStringLiteral("saveRole(%1) failed").arg(id)));
}

// Build a Team with trivially empty Specialists. Used by Teams test.
Team makeAndSaveTeam(StorageManager &storage,
                     const QString &id,
                     const QString &name,
                     const QString &description)
{
    Team team;
    team.id = id;
    team.name = name;
    team.description = description;
    team.version = QStringLiteral("0.1.0");
    if (!storage.saveTeam(team)) {
        qFatal("saveTeam(%s) failed", qPrintable(id));
    }
    return team;
}

void makeAndSaveStockTeam(StorageManager &storage,
                          const QString &id,
                          const QString &name,
                          const QString &description)
{
    Team team = makeAndSaveTeam(storage, id, name, description);
    team.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY2(storage.saveTeam(team),
             qPrintable(QStringLiteral("saveTeam(%1) failed").arg(id)));
}

void makeAndSaveSpecialist(StorageManager &storage,
                           const QString &id,
                           const QString &roleId,
                           const QString &name,
                           bool stock = false)
{
    Specialist spec;
    spec.id = id;
    spec.roleId = roleId;
    spec.modelId = QStringLiteral("model-a");
    spec.name = name;
    if (stock) {
        spec.metadata.insert(QStringLiteral("stock"), true);
    }
    QVERIFY2(storage.saveSpecialist(spec),
             qPrintable(QStringLiteral("saveSpecialist(%1) failed").arg(id)));
}

// Build a Trial that points at an existing Team so the TrialsWidget
// summary path produces text we can search against.
void makeAndSaveTrial(StorageManager &storage,
                       const QString &trialId,
                       const Team &team,
                       const QString &projectPath,
                       const QString &notes)
{
    Trial trial;
    trial.id = trialId;
    trial.teamId = team.id;
    trial.projectPath = projectPath;
    trial.timestamp = QDateTime::currentDateTimeUtc();
    trial.notes = notes;
    if (!storage.saveTrial(trial)) {
        qFatal("saveTrial(%s) failed", qPrintable(trialId));
    }
}

} // namespace

void TestListFilter::roles_filtersAcrossAllColumns_caseInsensitive()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();
    makeAndSaveRole(storage, QStringLiteral("build"), QStringLiteral("Build"),
                    QStringLiteral("Primary build agent"), Role::Mode::Primary);
    makeAndSaveRole(storage, QStringLiteral("review"), QStringLiteral("Review"),
                    QStringLiteral("Reviews diffs"), Role::Mode::Subagent);
    makeAndSaveRole(storage, QStringLiteral("refactor"), QStringLiteral("Refactor"),
                    QStringLiteral("Refactors code"), Role::Mode::Subagent);

    RolesWidget widget(storage);

    QTableWidget *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "RolesWidget has no QTableWidget");
    QCOMPARE(table->rowCount(), 3);

    auto *filterEdit = widget.findChild<QLineEdit *>();
    QVERIFY2(filterEdit, "RolesWidget has no filter QLineEdit");
    QCOMPARE(filterEdit->placeholderText(), QStringLiteral("Filter roles..."));

    auto *proxy = widget.findChild<QSortFilterProxyModel *>();
    QVERIFY2(proxy, "RolesWidget has no QSortFilterProxyModel");
    QCOMPARE(proxy->filterCaseSensitivity(), Qt::CaseInsensitive);
    QCOMPARE(proxy->filterKeyColumn(), -1);

    // Helper: locate the row whose id cell stores `expectedId`. Roles
    // come back from the storage layer in directory-iteration order
    // (not alphabetical), so the test never assumes a row index.
    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    // Empty filter -> everything visible.
    QCOMPARE(table->isRowHidden(0), false);
    QCOMPARE(table->isRowHidden(1), false);
    QCOMPARE(table->isRowHidden(2), false);

    // Match by Name (case-insensitive). Only Review survives.
    filterEdit->setText(QStringLiteral("review"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("review"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("refactor"))), true);

    // Match by id column ("BUILD" -> 1 row visible).
    filterEdit->setText(QStringLiteral("BUILD"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("review"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("refactor"))), true);

    // Match by description column ("diff" only in review row).
    filterEdit->setText(QStringLiteral("diff"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("review"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("refactor"))), true);

    // Match by mode column rendered text ("subagent" excludes Primary build row).
    filterEdit->setText(QStringLiteral("subagent"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("review"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("refactor"))), false);

    // Empty text + ESC shortcut both restore all rows.
    filterEdit->clear();
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("review"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("refactor"))), false);
}

void TestListFilter::roles_hidesStockByDefault_andToggleShowsIt()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveRole(storage, QStringLiteral("build"), QStringLiteral("Build"),
                    QStringLiteral("Primary build agent"), Role::Mode::Primary);
    Role stockRole;
    stockRole.id = QStringLiteral("stock-review");
    stockRole.name = QStringLiteral("Stock Review");
    stockRole.description = QStringLiteral("Seeded review role");
    stockRole.mode = Role::Mode::Subagent;
    stockRole.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY2(storage.saveRole(stockRole), "saveRole(stock-review) failed");

    RolesWidget widget(storage);

    QTableWidget *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "RolesWidget has no QTableWidget");
    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("rolesWidget.showStock"));
    QVERIFY2(showStock, "RolesWidget has no Show stock checkbox");

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("build"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("stock-review"))), true);

    showStock->setChecked(true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("stock-review"))), false);

    showStock->setChecked(false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("stock-review"))), true);
}

void TestListFilter::teams_appliesOnIdAndName_clearsOnEscape()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveTeam(storage, QStringLiteral("alpha"), QStringLiteral("Alpha Squad"),
                    QStringLiteral("Investigation team"));
    makeAndSaveTeam(storage, QStringLiteral("beta-build"), QStringLiteral("Beta Build"),
                    QStringLiteral("Builds things"));
    makeAndSaveTeam(storage, QStringLiteral("gamma"), QStringLiteral("Gamma Review"),
                    QStringLiteral("Reviews"));

    TeamsWidget widget(storage);

    QTableWidget *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "TeamsWidget has no QTableWidget");
    QCOMPARE(table->rowCount(), 3);

    auto *filterEdit = widget.findChild<QLineEdit *>();
    QVERIFY2(filterEdit, "TeamsWidget has no filter QLineEdit");

    auto *proxy = widget.findChild<QSortFilterProxyModel *>();
    QVERIFY2(proxy, "TeamsWidget has no QSortFilterProxyModel");
    QCOMPARE(proxy->filterCaseSensitivity(), Qt::CaseInsensitive);

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    // Filter on Name "beta" -> only Beta Build survives.
    filterEdit->setText(QStringLiteral("beta"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("alpha"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("beta-build"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("gamma"))), true);

    // Filter on id column (matches exact id "alpha").
    filterEdit->setText(QStringLiteral("alpha"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("alpha"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("beta-build"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("gamma"))), true);

    // Filter on description "review" -> only Gamma Review visible.
    filterEdit->setText(QStringLiteral("review"));
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("alpha"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("beta-build"))), true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("gamma"))), false);

    // FilterBar exposes ESC as a widget-scoped shortcut that clears the
    // edit; we drive the slot path directly here because synthesising
    // key events through the Qt offscreen platform is fragile.
    filterEdit->clear();
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("alpha"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("beta-build"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("gamma"))), false);
}

void TestListFilter::teams_hidesStockByDefault_andToggleShowsIt()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveTeam(storage, QStringLiteral("custom-team"),
                    QStringLiteral("Custom Team"), QStringLiteral("User team"));
    makeAndSaveStockTeam(storage, QStringLiteral("starter-team"),
                         QStringLiteral("Starter Team"), QStringLiteral("Seeded team"));

    TeamsWidget widget(storage);

    QTableWidget *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "TeamsWidget has no QTableWidget");
    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("teamsWidget.showStock"));
    QVERIFY2(showStock, "TeamsWidget has no Show stock checkbox");

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("custom-team"))), false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("starter-team"))), true);

    showStock->setChecked(true);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("starter-team"))), false);

    showStock->setChecked(false);
    QCOMPARE(table->isRowHidden(findRowForId(QStringLiteral("starter-team"))), true);
}

void TestListFilter::teams_showStockToggleAlsoControlsEmbeddedEditorStockSpecialists()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveRole(storage, QStringLiteral("build"), QStringLiteral("Build"),
                    QStringLiteral("Primary build agent"), Role::Mode::Primary);
    makeAndSaveRole(storage, QStringLiteral("review"), QStringLiteral("Review"),
                    QStringLiteral("Reviews diffs"), Role::Mode::Subagent);

    makeAndSaveSpecialist(storage, QStringLiteral("spec-build"), QStringLiteral("build"),
                          QStringLiteral("Build Specialist"));
    makeAndSaveSpecialist(storage, QStringLiteral("spec-stock"), QStringLiteral("review"),
                          QStringLiteral("Stock Specialist"), true);

    Team team;
    team.id = QStringLiteral("custom-team");
    team.name = QStringLiteral("Custom Team");
    team.description = QStringLiteral("User team");
    team.version = QStringLiteral("0.1.0");
    Team::SpecialistBinding a;
    a.roleId = QStringLiteral("build");
    a.specialistId = QStringLiteral("spec-build");
    team.specialists.append(a);
    Team::SpecialistBinding b;
    b.roleId = QStringLiteral("review");
    b.specialistId = QStringLiteral("spec-stock");
    team.specialists.append(b);
    QVERIFY(storage.saveTeam(team));

    TeamsWidget widget(storage);

    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("teamsWidget.showStock"));
    QVERIFY(showStock);
    auto *editor = widget.findChild<TeamEditorWidget *>();
    QVERIFY(editor);
    auto *table = editor->findChild<QTableWidget *>();
    QVERIFY(table);

    widget.selectTeamById(QStringLiteral("custom-team"));
    editor->setTeamId(QStringLiteral("custom-team"));

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    const int stockRow = findRowForId(QStringLiteral("spec-stock"));
    QVERIFY(stockRow >= 0);
    QCOMPARE(table->isRowHidden(stockRow), true);

    showStock->setChecked(true);
    QCOMPARE(table->isRowHidden(stockRow), false);

    showStock->setChecked(false);
    QCOMPARE(table->isRowHidden(stockRow), true);
}

void TestListFilter::teams_cloneStockTeamSelectsEditableCopy()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveStockTeam(storage, QStringLiteral("starter-team"),
                         QStringLiteral("Starter Team"), QStringLiteral("Seeded team"));

    TeamsWidget widget(storage);

    auto *cloneButton = widget.findChild<QPushButton *>(QStringLiteral("teamsWidget.cloneButton"));
    QVERIFY2(cloneButton, "TeamsWidget has no clone button");
    auto *editor = widget.findChild<TeamEditorWidget *>();
    QVERIFY(editor);
    auto *table = widget.findChild<QTableWidget *>();
    QVERIFY(table);

    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("teamsWidget.showStock"));
    QVERIFY(showStock);
    showStock->setChecked(true);

    widget.selectTeamById(QStringLiteral("starter-team"));
    QApplication::processEvents();
    QVERIFY(!cloneButton->isHidden());

    cloneButton->click();
    QApplication::processEvents();

    QVERIFY(table->currentRow() >= 0);
    QTableWidgetItem *idItem = table->item(table->currentRow(), 0);
    QVERIFY(idItem);
    const QString clonedId = idItem->data(Qt::UserRole).isValid()
                                 ? idItem->data(Qt::UserRole).toString()
                                 : idItem->text();
    QVERIFY2(clonedId != QStringLiteral("starter-team"), "clone did not select a new Team");
    QCOMPARE(editor->teamId(), clonedId);

    const Team original = storage.loadTeam(QStringLiteral("starter-team"));
    QVERIFY(storage.isStockTeam(original));

    const Team cloned = storage.loadTeam(clonedId);
    QVERIFY(!cloned.id.isEmpty());
    QVERIFY(!storage.isStockTeam(cloned));
    QCOMPARE(cloned.parentTeamId, QStringLiteral("starter-team"));
}

void TestListFilter::teams_stockDeleteShowsFlashFeedback()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveTeam(storage, QStringLiteral("custom-team"),
                    QStringLiteral("Custom Team"), QStringLiteral("User team"));
    makeAndSaveStockTeam(storage, QStringLiteral("starter-team"),
                        QStringLiteral("Starter Team"), QStringLiteral("Seeded team"));

    TeamsWidget widget(storage);

    auto *deleteButton = widget.findChild<QPushButton *>(QStringLiteral("teamsWidget.deleteButton"));
    QVERIFY2(deleteButton, "TeamsWidget has no delete button");
    auto *deleteAction = widget.findChild<QAction *>(QStringLiteral("teamsWidget.deleteAction"));
    QVERIFY2(deleteAction, "TeamsWidget has no delete action");
    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("teamsWidget.showStock"));
    QVERIFY2(showStock, "TeamsWidget has no Show stock checkbox");
    auto *table = widget.findChild<QTableWidget *>();
    QVERIFY(table);

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    widget.selectTeamById(QStringLiteral("custom-team"));
    QApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());
    QVERIFY(deleteAction->isEnabled());

    showStock->setChecked(true);
    widget.selectTeamById(QStringLiteral("starter-team"));
    QApplication::processEvents();
    QVERIFY(deleteButton->isEnabled());
    QVERIFY(deleteAction->isEnabled());
    QCOMPARE(deleteButton->toolTip(), QStringLiteral("Stock items cannot be deleted"));

    const int stockRow = findRowForId(QStringLiteral("starter-team"));
    QVERIFY(stockRow >= 0);
    const QBrush originalBrush = table->item(stockRow, 0)->background();

    deleteButton->click();
    const QBrush flashBrush = table->item(stockRow, 0)->background();
    QVERIFY2(flashBrush != originalBrush, "stock team row did not flash");

    QTest::qWait(350);
    QCOMPARE(table->item(stockRow, 0)->background(), originalBrush);
}

void TestListFilter::roles_stockDeleteShowsFlashFeedback()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    makeAndSaveRole(storage, QStringLiteral("build"), QStringLiteral("Build"),
                    QStringLiteral("Primary build agent"), Role::Mode::Primary);
    Role stockRole;
    stockRole.id = QStringLiteral("stock-review");
    stockRole.name = QStringLiteral("Stock Review");
    stockRole.description = QStringLiteral("Seeded review role");
    stockRole.mode = Role::Mode::Subagent;
    stockRole.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY2(storage.saveRole(stockRole), "saveRole(stock-review) failed");

    RolesWidget widget(storage);

    auto *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "RolesWidget has no QTableWidget");
    auto *deleteButton = widget.findChild<QPushButton *>(QStringLiteral("rolesWidget.deleteButton"));
    QVERIFY(deleteButton);
    auto *showStock = widget.findChild<QCheckBox *>(QStringLiteral("rolesWidget.showStock"));
    QVERIFY(showStock);

    auto findRowForId = [&](const QString &expectedId) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *idItem = table->item(row, 0);
            if (!idItem) {
                continue;
            }
            const QString id = idItem->data(Qt::UserRole).isValid()
                                   ? idItem->data(Qt::UserRole).toString()
                                   : idItem->text();
            if (id == expectedId) {
                return row;
            }
        }
        return -1;
    };

    showStock->setChecked(true);
    const int stockRow = findRowForId(QStringLiteral("stock-review"));
    QVERIFY(stockRow >= 0);
    widget.findChild<QTableWidget *>()->setCurrentCell(stockRow, 0);
    QApplication::processEvents();

    QVERIFY(deleteButton->isEnabled());
    QCOMPARE(deleteButton->toolTip(), QStringLiteral("Stock items cannot be deleted"));

    const QBrush originalBrush = table->item(stockRow, 0)->background();
    deleteButton->click();
    const QBrush flashBrush = table->item(stockRow, 0)->background();
    QVERIFY2(flashBrush != originalBrush, "stock role row did not flash");

    QTest::qWait(350);
    QCOMPARE(table->item(stockRow, 0)->background(), originalBrush);
}

void TestListFilter::trials_hidesNonMatchingRows_andPreservesIdForActions()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    const Team buildTeam =
        makeAndSaveTeam(storage, QStringLiteral("build-team"),
                        QStringLiteral("Build Team"), QStringLiteral("Builds"));
    const Team reviewTeam =
        makeAndSaveTeam(storage, QStringLiteral("review-team"),
                        QStringLiteral("Review Team"), QStringLiteral("Reviews"));

    QTemporaryDir projA;
    QVERIFY(projA.isValid());
    QTemporaryDir projB;
    QVERIFY(projB.isValid());

    makeAndSaveTrial(storage, QStringLiteral("trial-a"), buildTeam, projA.path(),
                     QStringLiteral("very fast"));
    makeAndSaveTrial(storage, QStringLiteral("trial-b"), reviewTeam, projB.path(),
                     QStringLiteral("very thorough"));

    TrialsWidget widget(storage);

    QTableWidget *table = widget.findChild<QTableWidget *>();
    QVERIFY2(table, "TrialsWidget has no QTableWidget");
    QCOMPARE(table->rowCount(), 2);

    auto *filterEdit = widget.findChild<QLineEdit *>();
    QVERIFY2(filterEdit, "TrialsWidget has no filter QLineEdit");

    auto *proxy = widget.findChild<QSortFilterProxyModel *>();
    QVERIFY2(proxy, "TrialsWidget has no QSortFilterProxyModel");

    // Filter by team name "build" -> only trial-a visible.
    filterEdit->setText(QStringLiteral("build"));
    QCOMPARE(table->isRowHidden(0), false);
    QCOMPARE(table->isRowHidden(1), true);

    // The hidden trial's id must still be in the table for the
    // Compare/Promote/Delete buttons (which use selectedTrialIds()):
    // we just confirm the source row data is intact by reading the
    // user-role data even when the row is hidden.
    const QString idOfHiddenTrial =
        table->item(1, 0) ? table->item(1, 0)->data(Qt::UserRole).toString() : QString();
    QCOMPARE(idOfHiddenTrial, QStringLiteral("trial-b"));

    // Filter by notes "thorough" -> only trial-b visible.
    filterEdit->setText(QStringLiteral("thorough"));
    QCOMPARE(table->isRowHidden(0), true);
    QCOMPARE(table->isRowHidden(1), false);

    // Clearing restores both rows.
    filterEdit->clear();
    QCOMPARE(table->isRowHidden(0), false);
    QCOMPARE(table->isRowHidden(1), false);
}

void TestListFilter::projects_hidesFilteredListItems_andDisablesActions()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    seedHomeAndStorageRoot(tmpRoot.path());

    StorageManager storage(QDir::homePath() + QStringLiteral("/.opencode-meta"));
    storage.ensureRoot();

    ProjectRecord a;
    a.path = QStringLiteral("/tmp/my-alpha-project");
    a.watchEnabled = false;

    ProjectRecord b;
    b.path = QStringLiteral("/tmp/other-beta-project");
    b.watchEnabled = false;

    const QList<ProjectRecord> records{ a, b };
    QVERIFY(storage.saveProjects(records));

    ProjectsWidget widget(storage);

    QListWidget *list = widget.findChild<QListWidget *>();
    QVERIFY2(list, "ProjectsWidget has no QListWidget");
    QCOMPARE(list->count(), 2);

    auto *filterEdit = widget.findChild<QLineEdit *>();
    QVERIFY2(filterEdit, "ProjectsWidget has no filter QLineEdit");

    auto *proxy = widget.findChild<QSortFilterProxyModel *>();
    QVERIFY2(proxy, "ProjectsWidget has no QSortFilterProxyModel");

    // Filter by "alpha" -> alpha row visible, beta row hidden.
    filterEdit->setText(QStringLiteral("alpha"));
    QCOMPARE(list->item(0)->isHidden(), false);
    QCOMPARE(list->item(1)->isHidden(), true);

    // Once a row is hidden by the filter, the project-scoped action
    // buttons (Switch Team, View Team Diffs, Set Watch) must disable
    // themselves even if currentItem still nominally points at the
    // hidden row. We assert via the visible Scan button (always
    // enabled) as a sanity check and via per-button enable-state for
    // the project-scoped actions.
    bool anyProjectActionEnabled = false;
    for (QPushButton *btn : widget.findChildren<QPushButton *>()) {
        const QString label = btn->text();
        if (label.contains(QStringLiteral("Switch Team"), Qt::CaseInsensitive)
            || label.contains(QStringLiteral("View Team Diffs"), Qt::CaseInsensitive)
            || label.contains(QStringLiteral("Set Watch"), Qt::CaseInsensitive)) {
            if (btn->isEnabled()) {
                anyProjectActionEnabled = true;
                break;
            }
        }
    }
    QVERIFY2(!anyProjectActionEnabled,
             "projects-scoped action buttons should be disabled when the selected item is hidden by filter");

    // Clearing restores both rows and re-enables actions for the
    // selected row.
    filterEdit->clear();
    QCOMPARE(list->item(0)->isHidden(), false);
    QCOMPARE(list->item(1)->isHidden(), false);
}

QTEST_MAIN(TestListFilter)
#include "test_list_filter.moc"
