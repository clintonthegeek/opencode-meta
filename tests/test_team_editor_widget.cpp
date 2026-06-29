// Tests for TeamEditorWidget's revert affordance.

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QFrame>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTest>

#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/TeamEditorWidget.h"

class TestTeamEditorWidget : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void revertButtonTracksDirtyStateAndEmitsSignal();
    void defaultAgentBadgeIsShownAndStockSpecialistsCanBeHidden();
    void makeDefaultActionUpdatesAgentAndEmitsStatusMessage();
    void resetToStockRestoresClonedTeamAndClearsParentLink();
    void compareWithStockButtonShowsCompactPopoverForClonedTeams();

private:
    static void seedTeam(StorageManager &storage, const QString &teamId);

    QTemporaryDir m_tmpRoot;
    QString m_storageRoot;
};

void TestTeamEditorWidget::seedTeam(StorageManager &storage, const QString &teamId)
{
    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    QVERIFY(storage.saveRole(role));

    Role stockRole;
    stockRole.id = QStringLiteral("stock-review");
    stockRole.name = QStringLiteral("Stock Review");
    stockRole.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY(storage.saveRole(stockRole));

    Role reviewRole;
    reviewRole.id = QStringLiteral("review");
    reviewRole.name = QStringLiteral("Review");
    QVERIFY(storage.saveRole(reviewRole));

    Specialist spec;
    spec.id = QStringLiteral("spec-build");
    spec.roleId = role.id;
    spec.modelId = QStringLiteral("model-a");
    spec.name = QStringLiteral("Build Specialist");
    QVERIFY(storage.saveSpecialist(spec));

    Specialist stockSpec;
    stockSpec.id = QStringLiteral("spec-stock");
    stockSpec.roleId = stockRole.id;
    stockSpec.modelId = QStringLiteral("model-b");
    stockSpec.name = QStringLiteral("Stock Specialist");
    stockSpec.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY(storage.saveSpecialist(stockSpec));

    Specialist reviewSpec;
    reviewSpec.id = QStringLiteral("spec-review");
    reviewSpec.roleId = reviewRole.id;
    reviewSpec.modelId = QStringLiteral("model-c");
    reviewSpec.name = QStringLiteral("Review Specialist");
    QVERIFY(storage.saveSpecialist(reviewSpec));

    Team team;
    team.id = teamId;
    team.name = QStringLiteral("Widget Team");
    team.version = QStringLiteral("1.0.0");
    team.description = QStringLiteral("initial");
    team.metadata.insert(QStringLiteral("default_agent"), spec.id);
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = role.id;
    binding.specialistId = spec.id;
    team.specialists.append(binding);

    Team::SpecialistBinding stockBinding;
    stockBinding.roleId = stockRole.id;
    stockBinding.specialistId = stockSpec.id;
    team.specialists.append(stockBinding);

    Team::SpecialistBinding reviewBinding;
    reviewBinding.roleId = reviewRole.id;
    reviewBinding.specialistId = reviewSpec.id;
    team.specialists.append(reviewBinding);
    QVERIFY(storage.saveTeam(team));
}

void TestTeamEditorWidget::initTestCase()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QVERIFY(m_tmpRoot.isValid());
    QVERIFY(QDir(m_tmpRoot.path()).mkpath(QStringLiteral(".")));
    m_storageRoot = QDir::cleanPath(m_tmpRoot.path());
}

void TestTeamEditorWidget::revertButtonTracksDirtyStateAndEmitsSignal()
{
    StorageManager storage(m_storageRoot);
    const QString teamId = QStringLiteral("team-editor-widget");
    seedTeam(storage, teamId);

    TeamEditorWidget widget(storage);
    widget.setTeamId(teamId);

    auto *table = widget.findChild<QTableWidget *>();
    QVERIFY(table);
    auto *revertButton = widget.findChild<QPushButton *>(QStringLiteral("teamEditor.revertButton"));
    QVERIFY(revertButton);
    auto *dirtyIndicator = widget.findChild<QLabel *>(QStringLiteral("teamEditor.dirtyIndicator"));
    QVERIFY(dirtyIndicator);

    QVERIFY(!revertButton->isEnabled());
    QVERIFY(dirtyIndicator->isHidden());

    Team changed = storage.loadTeam(teamId);
    QVERIFY(!changed.id.isEmpty());
    changed.description = QStringLiteral("changed on disk");
    QVERIFY(storage.saveTeam(changed));

    table->clearSelection();
    table->setCurrentCell(0, 0);
    QApplication::processEvents();

    QVERIFY(revertButton->isEnabled());
    QVERIFY(!dirtyIndicator->isHidden());

    QSignalSpy revertedSpy(&widget, &TeamEditorWidget::teamReverted);
    QVERIFY(revertedSpy.isValid());

    QVERIFY(QMetaObject::invokeMethod(&widget,
                                      "reloadTeamFromStorage",
                                      Qt::DirectConnection));
    QTRY_COMPARE(revertedSpy.count(), 1);

    const QList<QVariant> args = revertedSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), teamId);
    QCOMPARE(args.at(1).toString(), QStringLiteral("user-discard"));

    QVERIFY(!revertButton->isEnabled());
    QVERIFY(dirtyIndicator->isHidden());

    const Team reloaded = storage.loadTeam(teamId);
    QCOMPARE(reloaded.description, QStringLiteral("changed on disk"));
}

void TestTeamEditorWidget::defaultAgentBadgeIsShownAndStockSpecialistsCanBeHidden()
{
    StorageManager storage(m_storageRoot);
    const QString teamId = QStringLiteral("team-editor-widget-badge");
    seedTeam(storage, teamId);

    TeamEditorWidget widget(storage);
    widget.setTeamId(teamId);

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

    const int defaultRow = findRowForId(QStringLiteral("spec-build"));
    const int stockRow = findRowForId(QStringLiteral("spec-stock"));
    QVERIFY(defaultRow >= 0);
    QVERIFY(stockRow >= 0);

    auto *nameCell = table->cellWidget(defaultRow, 1);
    QVERIFY(nameCell);
    const auto badges = nameCell->findChildren<QLabel *>(QStringLiteral("defaultAgentBadge"));
    QVERIFY(!badges.isEmpty());
    QCOMPARE(badges.first()->text(), QStringLiteral("★"));

    QCOMPARE(table->isRowHidden(defaultRow), false);
    QCOMPARE(table->isRowHidden(stockRow), true);

    widget.setShowStock(true);
    QCOMPARE(table->isRowHidden(stockRow), false);

    widget.setShowStock(false);
    QCOMPARE(table->isRowHidden(stockRow), true);
}

void TestTeamEditorWidget::makeDefaultActionUpdatesAgentAndEmitsStatusMessage()
{
    StorageManager storage(m_storageRoot);
    const QString teamId = QStringLiteral("team-editor-widget-make-default");
    seedTeam(storage, teamId);

    TeamEditorWidget widget(storage);
    widget.setTeamId(teamId);

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

    const int currentDefaultRow = findRowForId(QStringLiteral("spec-build"));
    const int targetRow = findRowForId(QStringLiteral("spec-review"));
    const int stockRow = findRowForId(QStringLiteral("spec-stock"));
    QVERIFY(currentDefaultRow >= 0);
    QVERIFY(targetRow >= 0);
    QVERIFY(stockRow >= 0);

    auto *button = qobject_cast<QToolButton *>(table->cellWidget(targetRow, 5));
    QVERIFY(button);
    QVERIFY(table->cellWidget(stockRow, 5) == nullptr);

    QSignalSpy updatedSpy(&widget, &TeamEditorWidget::teamUpdated);
    QSignalSpy statusSpy(&widget, &TeamEditorWidget::statusMessageRequested);
    QVERIFY(updatedSpy.isValid());
    QVERIFY(statusSpy.isValid());

    QTest::mouseClick(button, Qt::LeftButton);

    QTRY_COMPARE(updatedSpy.count(), 1);
    QTRY_COMPARE(statusSpy.count(), 1);

    const QList<QVariant> statusArgs = statusSpy.takeFirst();
    QCOMPARE(statusArgs.at(0).toString(), QStringLiteral("Default agent set to Review Specialist"));

    const Team saved = storage.loadTeam(teamId);
    QCOMPARE(saved.metadata.value(QStringLiteral("default_agent")).toString(),
             QStringLiteral("spec-review"));

    auto *newDefaultCell = table->cellWidget(targetRow, 1);
    QVERIFY(newDefaultCell);
    const auto newBadges = newDefaultCell->findChildren<QLabel *>(QStringLiteral("defaultAgentBadge"));
    QVERIFY(!newBadges.isEmpty());

    QCOMPARE(table->cellWidget(currentDefaultRow, 1), nullptr);
}

void TestTeamEditorWidget::resetToStockRestoresClonedTeamAndClearsParentLink()
{
    StorageManager storage(m_storageRoot);
    const QString stockTeamId = QStringLiteral("starter-team");
    seedTeam(storage, stockTeamId);

    Team stock = storage.loadTeam(stockTeamId);
    QVERIFY(!stock.id.isEmpty());
    stock.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY(storage.saveTeam(stock));

    const Team cloned = storage.cloneTeam(stockTeamId);
    QVERIFY(!cloned.id.isEmpty());

    Team editedClone = storage.loadTeam(cloned.id);
    QVERIFY(!editedClone.id.isEmpty());
    editedClone.description = QStringLiteral("custom clone notes");
    editedClone.metadata.insert(QStringLiteral("note"), QStringLiteral("user edit"));
    QVERIFY(storage.saveTeam(editedClone));

    TeamEditorWidget widget(storage);
    widget.setTeamId(cloned.id);

    auto *resetButton = widget.findChild<QPushButton *>(QStringLiteral("teamEditor.resetToStockButton"));
    QVERIFY(resetButton);
    QVERIFY(!resetButton->isHidden());

    QSignalSpy updatedSpy(&widget, &TeamEditorWidget::teamUpdated);
    QVERIFY(updatedSpy.isValid());

    QTimer::singleShot(0, [&]() {
        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(topLevel);
            if (!box) {
                continue;
            }
            for (QAbstractButton *button : box->buttons()) {
                if (button && button->text() == QStringLiteral("Keep current name")) {
                    QTest::mouseClick(button, Qt::LeftButton);
                    return;
                }
            }
        }
    });

    QTest::mouseClick(resetButton, Qt::LeftButton);
    QTRY_COMPARE(updatedSpy.count(), 1);

    const Team reset = storage.loadTeam(cloned.id);
    QCOMPARE(reset.id, cloned.id);
    QCOMPARE(reset.name, editedClone.name);
    QCOMPARE(reset.description, stock.description);
    QCOMPARE(reset.parentTeamId, QString());
    QVERIFY(!reset.metadata.contains(QStringLiteral("cloned_from")));
    QVERIFY(!reset.metadata.contains(QStringLiteral("cloned_from_team_id")));
    QVERIFY(!reset.metadata.contains(QStringLiteral("stock")));
    QTRY_VERIFY(resetButton->isHidden());
}

void TestTeamEditorWidget::compareWithStockButtonShowsCompactPopoverForClonedTeams()
{
    StorageManager storage(m_storageRoot);
    const QString stockTeamId = QStringLiteral("team-editor-widget-stock-compare");
    seedTeam(storage, stockTeamId);

    Team stock = storage.loadTeam(stockTeamId);
    QVERIFY(!stock.id.isEmpty());
    stock.metadata.insert(QStringLiteral("stock"), true);
    QVERIFY(storage.saveTeam(stock));

    const Team cloned = storage.cloneTeam(stockTeamId);
    QVERIFY(!cloned.id.isEmpty());

    Team editedClone = storage.loadTeam(cloned.id);
    QVERIFY(!editedClone.id.isEmpty());
    editedClone.primarySpecialistIds = QStringList{QStringLiteral("spec-review")};
    editedClone.metadata.insert(QStringLiteral("default_agent"), QStringLiteral("spec-review"));

    Team::SpecialistBinding buildBinding;
    buildBinding.roleId = QStringLiteral("build");
    buildBinding.specialistId = QStringLiteral("spec-build");
    Team::SpecialistBinding reviewBinding;
    reviewBinding.roleId = QStringLiteral("review");
    reviewBinding.specialistId = QStringLiteral("spec-review");

    Role qaRole;
    qaRole.id = QStringLiteral("qa");
    qaRole.name = QStringLiteral("QA");
    QVERIFY(storage.saveRole(qaRole));

    Specialist qaSpec;
    qaSpec.id = QStringLiteral("spec-qa");
    qaSpec.roleId = qaRole.id;
    qaSpec.modelId = QStringLiteral("model-d");
    qaSpec.name = QStringLiteral("QA Specialist");
    QVERIFY(storage.saveSpecialist(qaSpec));

    Team::SpecialistBinding qaBinding;
    qaBinding.roleId = qaRole.id;
    qaBinding.specialistId = qaSpec.id;

    editedClone.specialists.clear();
    editedClone.specialists.append(buildBinding);
    editedClone.specialists.append(reviewBinding);
    editedClone.specialists.append(qaBinding);
    QVERIFY(storage.saveTeam(editedClone));

    TeamEditorWidget widget(storage);
    widget.setTeamId(cloned.id);

    auto *compareButton = widget.findChild<QToolButton *>(QStringLiteral("teamEditor.compareStockButton"));
    QVERIFY(compareButton);
    QVERIFY(!compareButton->isHidden());
    QCOMPARE(compareButton->toolTip(), QStringLiteral("4 changes from stock"));

    QTest::mouseClick(compareButton, Qt::LeftButton);

    QFrame *popover = nullptr;
    QTRY_VERIFY([&]() -> bool {
        popover = nullptr;
        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            auto *frame = qobject_cast<QFrame *>(topLevel);
            if (frame && frame->objectName() == QStringLiteral("teamEditor.stockComparePopover")) {
                popover = frame;
                break;
            }
        }
        return popover != nullptr;
    }());

    QVERIFY(popover);
    auto *summary = popover->findChild<QLabel *>(QStringLiteral("teamEditor.stockCompareSummary"));
    auto *details = popover->findChild<QLabel *>(QStringLiteral("teamEditor.stockCompareDetails"));
    QVERIFY(summary);
    QVERIFY(details);
    QCOMPARE(summary->text(), QStringLiteral("4 changes from stock"));
    QVERIFY(details->text().contains(QStringLiteral("Added specialists")));
    QVERIFY(details->text().contains(QStringLiteral("Removed specialists")));
    QVERIFY(details->text().contains(QStringLiteral("Reordered specialists")));
    QVERIFY(details->text().contains(QStringLiteral("default_agent")));
    QVERIFY(details->text().contains(QStringLiteral("QA Specialist")));
    QVERIFY(details->text().contains(QStringLiteral("Stock Specialist")));
}

QTEST_MAIN(TestTeamEditorWidget)
#include "test_team_editor_widget.moc"
