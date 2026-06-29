// Tests for TeamEditorWidget's revert affordance.

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
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

    Specialist spec;
    spec.id = QStringLiteral("spec-build");
    spec.roleId = role.id;
    spec.modelId = QStringLiteral("model-a");
    spec.name = QStringLiteral("Build Specialist");
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = teamId;
    team.name = QStringLiteral("Widget Team");
    team.version = QStringLiteral("1.0.0");
    team.description = QStringLiteral("initial");
    team.primarySpecialistIds.append(spec.id);
    Team::SpecialistBinding binding;
    binding.roleId = role.id;
    binding.specialistId = spec.id;
    team.specialists.append(binding);
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

QTEST_MAIN(TestTeamEditorWidget)
#include "test_team_editor_widget.moc"
