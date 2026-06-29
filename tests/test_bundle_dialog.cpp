// Tests for ImportExportDialog (ROADMAP P3-2).
//
// The dialog is a QDialog wrapper over QListWidget / QLineEdit that
// collects user choices. We exercise:
//   * Constructor smoke (object names, label text, button labels).
//   * ExportMode populates the role + team lists from the seed
//     storage and toggles correctly: a fresh dialog has no checked
//     entries, every item carries the correct id in UserRole, and
//     OK is disabled until the user picks something.
//   * Result() in ExportMode returns the checked ids verbatim.
//   * ImportMode shows the role/team lists as disabled (read-only)
//     and disables the OK button until a manifest-readable path is
//     entered.

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/ImportExportManager.h"
#include "storage/StorageManager.h"
#include "ui/ImportExportDialog.h"

namespace {

QList<Role> seedRoles()
{
    QList<Role> roles;
    {
        Role r;
        r.id = QStringLiteral("build");
        r.name = QStringLiteral("Build");
        r.description = QStringLiteral("primary dev");
        r.mode = Role::Mode::Primary;
        roles.append(r);
    }
    {
        Role r;
        r.id = QStringLiteral("plan");
        r.name = QStringLiteral("Plan");
        r.description = QStringLiteral("planners");
        r.mode = Role::Mode::Subagent;
        roles.append(r);
    }
    return roles;
}

QList<Team> seedTeams()
{
    QList<Team> teams;
    {
        Team t;
        t.id = QStringLiteral("starter-team");
        t.name = QStringLiteral("Starter Team");
        t.description = QStringLiteral("default");
        teams.append(t);
    }
    {
        Team t;
        t.id = QStringLiteral("qa-team");
        t.name = QStringLiteral("QA Team");
        t.description = QStringLiteral("review");
        teams.append(t);
    }
    return teams;
}

} // namespace

class TestBundleDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void constructsWithStableObjectNames();
    void exportModePopulatesListsAndDisablesOkUntilSelection();
    void exportModeResultReflectsCheckedEntries();
    void exportModeEmptySelectionKeepsOkDisabled();
    void importModeListsAreReadOnly();
    void suggestExportFilenameIsStable();

private:
    QTemporaryDir m_storageRoot;
};

void TestBundleDialog::initTestCase()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QVERIFY(m_storageRoot.isValid());
}

void TestBundleDialog::constructsWithStableObjectNames()
{
    ImportExportDialog dlg(ImportExportDialog::Mode::Export,
                           seedRoles(),
                           seedTeams());
    QVERIFY(dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.roleList")));
    QVERIFY(dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.teamList")));
    auto *path = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.pathEdit"));
    QVERIFY(path);
    auto *notes = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.notesEdit"));
    QVERIFY(notes);
    auto *browse = dlg.findChild<QPushButton *>(QStringLiteral("bundleDialog.browseButton"));
    QVERIFY(browse);
    auto *summary = dlg.findChild<QLabel *>(QStringLiteral("bundleDialog.summaryLabel"));
    QVERIFY(summary);

    auto *buttonBox = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(buttonBox);
    QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(ok);
    QCOMPARE(ok->text(), QStringLiteral("Export"));
}

void TestBundleDialog::exportModePopulatesListsAndDisablesOkUntilSelection()
{
    ImportExportDialog dlg(ImportExportDialog::Mode::Export,
                           seedRoles(),
                           seedTeams());

    auto *roleList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.roleList"));
    auto *teamList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.teamList"));
    QVERIFY(roleList);
    QVERIFY(teamList);

    QCOMPARE(roleList->count(), seedRoles().size());
    QCOMPARE(teamList->count(), seedTeams().size());

    // Each row must carry its real id in UserRole.
    QSet<QString> seenRoleIds;
    for (int i = 0; i < roleList->count(); ++i) {
        seenRoleIds.insert(roleList->item(i)->data(Qt::UserRole).toString());
    }
    QCOMPARE(seenRoleIds.size(), seedRoles().size());
    QVERIFY(seenRoleIds.contains(QStringLiteral("build")));
    QVERIFY(seenRoleIds.contains(QStringLiteral("plan")));

    QSet<QString> seenTeamIds;
    for (int i = 0; i < teamList->count(); ++i) {
        seenTeamIds.insert(teamList->item(i)->data(Qt::UserRole).toString());
    }
    QVERIFY(seenTeamIds.contains(QStringLiteral("starter-team")));
    QVERIFY(seenTeamIds.contains(QStringLiteral("qa-team")));

    // OK should start disabled because nothing is checked AND path
    // is empty.
    auto *buttonBox = dlg.findChild<QDialogButtonBox *>();
    QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(ok);
    QVERIFY(!ok->isEnabled());

    // Tick one Role -> OK should still be disabled (path empty).
    roleList->item(0)->setCheckState(Qt::Checked);
    QVERIFY(!ok->isEnabled());

    // Provide a path -> OK should now be enabled.
    auto *path = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.pathEdit"));
    path->setText(m_storageRoot.filePath(QStringLiteral("ok-enabled.zip")));
    QVERIFY(ok->isEnabled());
}

void TestBundleDialog::exportModeResultReflectsCheckedEntries()
{
    ImportExportDialog dlg(ImportExportDialog::Mode::Export,
                           seedRoles(),
                           seedTeams());

    auto *roleList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.roleList"));
    auto *teamList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.teamList"));
    auto *path    = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.pathEdit"));
    auto *notes   = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.notesEdit"));

    // Tick two roles + one team.
    for (int i = 0; i < roleList->count(); ++i) {
        const QString id = roleList->item(i)->data(Qt::UserRole).toString();
        if (id == QStringLiteral("build") || id == QStringLiteral("plan")) {
            roleList->item(i)->setCheckState(Qt::Checked);
        }
    }
    for (int i = 0; i < teamList->count(); ++i) {
        const QString id = teamList->item(i)->data(Qt::UserRole).toString();
        if (id == QStringLiteral("starter-team")) {
            teamList->item(i)->setCheckState(Qt::Checked);
        }
    }
    path->setText(m_storageRoot.filePath(QStringLiteral("result.zip")));
    notes->setText(QStringLiteral("note shape"));

    const ImportExportDialog::Result r = dlg.result();
    QCOMPARE(r.mode, ImportExportDialog::Mode::Export);
    QVERIFY(r.roleIds.contains(QStringLiteral("build")));
    QVERIFY(r.roleIds.contains(QStringLiteral("plan")));
    QVERIFY(r.teamIds.contains(QStringLiteral("starter-team")));
    QCOMPARE(r.roleIds.size(), 2);
    QCOMPARE(r.teamIds.size(), 1);
    QCOMPARE(r.notes, QStringLiteral("note shape"));
    QVERIFY(r.zipPath.endsWith(QStringLiteral("result.zip")));
}

void TestBundleDialog::exportModeEmptySelectionKeepsOkDisabled()
{
    ImportExportDialog dlg(ImportExportDialog::Mode::Export,
                           seedRoles(),
                           seedTeams());

    auto *path = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.pathEdit"));
    path->setText(m_storageRoot.filePath(QStringLiteral("xx.zip")));

    auto *buttonBox = dlg.findChild<QDialogButtonBox *>();
    QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(ok);
    QVERIFY(!ok->isEnabled());

    auto *summary = dlg.findChild<QLabel *>(QStringLiteral("bundleDialog.summaryLabel"));
    QVERIFY(summary);
    QVERIFY(summary->text().contains(QStringLiteral("No Roles or Teams selected")));
}

void TestBundleDialog::importModeListsAreReadOnly()
{
    ImportExportDialog dlg(ImportExportDialog::Mode::Import,
                           seedRoles(),
                           seedTeams());

    auto *roleList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.roleList"));
    auto *teamList = dlg.findChild<QListWidget *>(QStringLiteral("bundleDialog.teamList"));
    QVERIFY(roleList);
    QVERIFY(teamList);
    QVERIFY(!roleList->isEnabled());
    QVERIFY(!teamList->isEnabled());

    auto *path = dlg.findChild<QLineEdit *>(QStringLiteral("bundleDialog.pathEdit"));
    QVERIFY(path);
    // OK should start disabled because no path is set.
    auto *buttonBox = dlg.findChild<QDialogButtonBox *>();
    QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(ok);
    QVERIFY(!ok->isEnabled());
    QCOMPARE(ok->text(), QStringLiteral("Import"));

    auto *preview = dlg.findChild<QTextEdit *>(QStringLiteral("bundleDialog.importPreview"));
    QVERIFY(preview);
    QVERIFY(preview->isReadOnly());

    // Now point to a real bundle written through ImportExportManager:
    // the preview should populate and OK should enable.
    StorageManager sourceStorage(m_storageRoot.path());
    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.description = QStringLiteral("primary");
    role.systemPrompt = QJsonValue(QStringLiteral("You are the Build agent."));
    role.mode = Role::Mode::Primary;
    QVERIFY(sourceStorage.saveRole(role));

    Specialist spec;
    spec.id = QStringLiteral("starter-build");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.name = QStringLiteral("Starter Build");
    QVERIFY(sourceStorage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("starter-team");
    team.name = QStringLiteral("Starter Team");
    team.description = QStringLiteral("default");
    {
        Team::SpecialistBinding b;
        b.roleId = QStringLiteral("build");
        b.specialistId = QStringLiteral("starter-build");
        team.specialists.append(b);
    }
    QVERIFY(sourceStorage.saveTeam(team));

    const QString zipPath = m_storageRoot.filePath(QStringLiteral("for-preview.zip"));
    ImportExportManager exporter(sourceStorage);
    ImportExportManager::ExportRequest req;
    req.roleIds = { QStringLiteral("build") };
    req.teamIds = { QStringLiteral("starter-team") };
    QVERIFY(exporter.exportBundle(zipPath, req).success);

    path->setText(zipPath);
    QVERIFY(ok->isEnabled());

    // The preview text reflects the bundle's manifest.
    const QString text = preview->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("build"))
              && text.contains(QStringLiteral("starter-team"))
              && text.contains(QStringLiteral("starter-build")),
             qPrintable(QStringLiteral("preview text missing ids: ") + text));
}

void TestBundleDialog::suggestExportFilenameIsStable()
{
    QCOMPARE(ImportExportDialog::suggestExportFilename({ QStringLiteral("starter-team") }),
             QStringLiteral("bundle-starter-team.zip"));
    QCOMPARE(ImportExportDialog::suggestExportFilename({ QStringLiteral("a-team"),
                                                        QStringLiteral("b-team") }),
             QStringLiteral("bundle-a-team.zip"));
    QCOMPARE(ImportExportDialog::suggestExportFilename({}),
             QStringLiteral("bundle.zip"));
}

QTEST_MAIN(TestBundleDialog)
#include "test_bundle_dialog.moc"
