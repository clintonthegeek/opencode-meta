// Tests for TeamEditorWidget's revert affordance.

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
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
    void resetToStockRestoresClonedTeamAndClearsParentLink();

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

QTEST_MAIN(TestTeamEditorWidget)
#include "test_team_editor_widget.moc"
