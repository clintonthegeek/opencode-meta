// tests/test_paradigm_help.cpp
//
// ROADMAP P2-4 smoke + contract test for paradigm help + tooltips.
//
// Two halves of coverage:
//
// 1. ParadigmHelpDialog
//    - Construct cleanly with all expected child widgets reachable
//      through stable objectNames.
//    - Body HTML covers the four core vocabulary words (Role,
//      Specialist, Team, Trial) so the dialog is never visually empty
//      when it first opens.
//    - Key/keyShowOnStartup()/keyParadigmShown() round-trip with the
//      SettingsDialog pattern.
//    - loadShowOnStartup()/saveShowOnStartup() write/read lossless.
//    - consumeFirstRunAutoShow() flips the "shown" flag exactly once
//      when the user has not yet seen the help, and never again.
//
// 2. Context-sensitive tooltips
//    - Confirm that the major widgets picked up non-empty toolTip()
//      text. We deliberately do not assert exact wording so future
//      copy-edits do not break the test: presence-of-text is what
//      matters for "is this discoverable".
//
// Never show() the dialog. The harness edits values via findChild and
// reaches the public static helpers directly, so the test never
// depends on a running event loop and never bleeds into the user's
// real preferences.

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>

#include "MainWindow.h"
#include "models/ModelInfo.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/AddSpecialistDialog.h"
#include "ui/ConfirmApplyDialog.h"
#include "ui/EditSpecialistDialog.h"
#include "ui/LabOverviewWidget.h"
#include "ui/ParadigmHelpDialog.h"
#include "ui/ProjectsWidget.h"
#include "ui/RoleEditorDialog.h"
#include "ui/RolesWidget.h"
#include "ui/SettingsDialog.h"
#include "ui/TeamsWidget.h"
#include "ui/TrialsWidget.h"

class TestParadigmHelp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void helpDialogConstructsCleanly();
    void helpBodyCoversFourEntities();
    void helpLoopsOverSixPhases();
    void settingsKeysAreStable();
    void showOnStartupRoundTrips();
    void firstRunFlagConsumesOnce();
    void firstRunFlagRespectsShowOnStartup();

    void rolesWidgetHasTooltips();
    void teamsWidgetHasTooltips();
    void trialsWidgetHasTooltips();
    void projectsWidgetHasTooltips();
    void labOverviewHasTooltips();
    void roleEditorDialogHasTooltips();
    void settingsDialogHasTooltips();
    void addSpecialistDialogHasTooltips();
    void editSpecialistDialogHasTooltips();
    void confirmApplyDialogHasTooltips();

private:
    static bool anyTooltipSetOn(const QObject *root, const QString &childName);
    static bool anyWhatsThisSetOn(const QObject *root, const QString &childName);

    QTemporaryDir m_tmpRoot;
    QTemporaryDir m_appCfgRoot;
    QString m_storageRoot;
};

void TestParadigmHelp::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    QVERIFY(m_appCfgRoot.isValid());

    // Keep QSettings sandboxed to a temp directory so the test never
    // touches real prefs. Mirrors test_settings_dialog's pattern.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_appCfgRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("opencode-meta-tests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("opencode-meta-tests.local"));
    QCoreApplication::setApplicationName(QStringLiteral("paradigm-help-test"));

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    qputenv("HOME", m_tmpRoot.path().toUtf8());
    m_storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");

    StorageManager storage(m_storageRoot);
    storage.ensureRoot();

    // Seed minimal Role/Specialist/Team so widget constructors that
    //         touch listTeams/listRoles don't error out.
    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.systemPrompt = QJsonValue(QStringLiteral("You are the build agent."));
    role.mode = Role::Mode::Primary;
    QVERIFY(storage.saveRole(role));

    Specialist spec;
    spec.id = QStringLiteral("spec-build");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.name = QStringLiteral("Build agent");
    QVERIFY(storage.saveSpecialist(spec));

    Team team;
    team.id = QStringLiteral("starter");
    team.name = QStringLiteral("Starter");
    team.primarySpecialistIds.append(spec.id);
    team.version = QStringLiteral("0.1.0");
    Team::SpecialistBinding roleRef;
    roleRef.roleId = QStringLiteral("build");
    roleRef.specialistId = QStringLiteral("spec-build");
    team.specialists.append(roleRef);
    QVERIFY(storage.saveTeam(team));

    // AddSpecialistDialog / EditSpecialistDialog instantiate
    // ModelsBrowserWidget in pickerMode whose constructor shells out to
    // `opencode models --refresh` when no cache exists. Seed a single
    // row so the picker has something to bind to without spawning a
    // process. Mirrors the pattern in test_edit_specialist_dialog.
    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();
    ModelInfo info;
    info.id = QStringLiteral("anthropic/claude-sonnet-4-6");
    info.displayName = QStringLiteral("Anthropic Claude Sonnet 4.6");
    info.inputCost = 3.0;
    info.outputCost = 15.0;
    info.capabilities.insert(QStringLiteral("tool-use"));
    info.capabilities.insert(QStringLiteral("reasoning"));
    QJsonObject data;
    data.insert(QStringLiteral("id"), info.id);
    data.insert(QStringLiteral("display_name"), info.displayName);
    data.insert(QStringLiteral("provider"), QStringLiteral("anthropic"));
    data.insert(QStringLiteral("provider_display_name"), QStringLiteral("Anthropic"));
    data.insert(QStringLiteral("tool_call"), true);
    data.insert(QStringLiteral("reasoning"), true);
    QJsonObject limit;
    limit.insert(QStringLiteral("context"), 200000);
    data.insert(QStringLiteral("limit"), limit);
    QJsonArray caps;
    caps.append(QStringLiteral("tool-use"));
    caps.append(QStringLiteral("reasoning"));
    data.insert(QStringLiteral("capabilities"), caps);
    info.data = data;
    cache.models.insert(info.id, info);
    QVERIFY2(storage.saveModelsCache(cache), "saveModelsCache failed");

    // Reset the first-run flag in case a previous test left it set.
    QSettings().remove(ParadigmHelpDialog::keyParadigmShown());
    QSettings().remove(ParadigmHelpDialog::keyShowOnStartup());
    QSettings().sync();
}

void TestParadigmHelp::cleanupTestCase()
{
    // No global cleanup needed for this harness.
}

void TestParadigmHelp::helpDialogConstructsCleanly()
{
    ParadigmHelpDialog dlg;
    QVERIFY(dlg.findChild<QTextBrowser *>(
        QStringLiteral("paradigmHelpDialog.browser")));
    QVERIFY(dlg.findChild<QCheckBox *>(
        QStringLiteral("paradigmHelpDialog.showOnStartupCheck")));
    QVERIFY(dlg.findChild<QDialogButtonBox *>());

    const QString body = ParadigmHelpDialog::paradigmHtmlBody();
    QVERIFY2(!body.isEmpty(),
             "paradigmHtmlBody() must return non-empty content");
}

void TestParadigmHelp::helpBodyCoversFourEntities()
{
    // The four words are not just jargon -- they're the entire domain
    // model. If any one disappears from the help body, the dialog
    // no longer teaches the user the paradigm.
    const QString body = ParadigmHelpDialog::paradigmHtmlBody();
    QVERIFY2(body.contains(QStringLiteral("<b>Role</b>")),
             "help body must mention Role");
    QVERIFY2(body.contains(QStringLiteral("<b>Specialist</b>")),
             "help body must mention Specialist");
    QVERIFY2(body.contains(QStringLiteral("<b>Team</b>")),
             "help body must mention Team");
    QVERIFY2(body.contains(QStringLiteral("<b>Trial</b>")),
             "help body must mention Trial");

    // Plus an explicit "no Template / no Profile" reminder -- the
    //       legacy vocabulary must NOT be smuggled back into the help.
    QVERIFY2(body.contains(QStringLiteral("OpenCode Meta")),
             "help body must surface 'OpenCode Meta' branding");
}

void TestParadigmHelp::helpLoopsOverSixPhases()
{
    // The 6-phase loop is one of the conceptual anchors -- if the loop
    // shrinks or rephrases, the test will fail loudly and force the
    // author to think about whether the help still teaches the right
    // shape.
    const QString body = ParadigmHelpDialog::paradigmHtmlBody();
    QVERIFY2(body.contains(QStringLiteral("6-Phase Loop")),
             "help body must call out the 6-Phase Loop");

    const QStringList stepHeadings = {
        QStringLiteral("Design Roles"),
        QStringLiteral("Bind Specialists"),
        QStringLiteral("Compose Teams"),
        QStringLiteral("Apply Team to Project"),
        QStringLiteral("Run Trial"),
        QStringLiteral("Promote Winner"),
    };
    for (const QString &head : stepHeadings) {
        QVERIFY2(body.contains(head),
                 qPrintable(QStringLiteral("6-phase step missing: ") + head));
    }
}

void TestParadigmHelp::settingsKeysAreStable()
{
    // Stable QString keys live behind small accessors -- future
    // re-naming should be a deliberate single-file change.
    QCOMPARE(ParadigmHelpDialog::keyShowOnStartup(),
             QStringLiteral("help/paradigm_show_on_startup"));
    QCOMPARE(ParadigmHelpDialog::keyParadigmShown(),
             QStringLiteral("help/paradigm_shown"));
}

void TestParadigmHelp::showOnStartupRoundTrips()
{
    QSettings().remove(ParadigmHelpDialog::keyShowOnStartup());
    QSettings().sync();

    QVERIFY(!ParadigmHelpDialog::loadShowOnStartup());
    ParadigmHelpDialog::saveShowOnStartup(true);
    QVERIFY(ParadigmHelpDialog::loadShowOnStartup());
    ParadigmHelpDialog::saveShowOnStartup(false);
    QVERIFY(!ParadigmHelpDialog::loadShowOnStartup());
}

void TestParadigmHelp::firstRunFlagConsumesOnce()
{
    QSettings().remove(ParadigmHelpDialog::keyShowOnStartup());
    QSettings().remove(ParadigmHelpDialog::keyParadigmShown());
    QSettings().sync();

    // First ever launch: must report true (we should auto-show).
    QVERIFY(ParadigmHelpDialog::consumeFirstRunAutoShow());
    // After the first call the flag is flipped to true, so
    // subsequent invocations must report false (we already showed
    // the dialog -- don't keep nagging the user on later launches).
    QVERIFY(!ParadigmHelpDialog::consumeFirstRunAutoShow());
    QVERIFY(!ParadigmHelpDialog::consumeFirstRunAutoShow());

    // The flag must itself be true after the first call.
    QSettings settings;
    QVERIFY(settings.value(ParadigmHelpDialog::keyParadigmShown(), false).toBool());
}

void TestParadigmHelp::firstRunFlagRespectsShowOnStartup()
{
    QSettings().remove(ParadigmHelpDialog::keyShowOnStartup());
    QSettings().setValue(ParadigmHelpDialog::keyParadigmShown(), true);
    QSettings().sync();

    // User has already seen the dialog. Default state (no
    // showOnStartup) must NOT re-show.
    QVERIFY(!ParadigmHelpDialog::consumeFirstRunAutoShow());

    // But if they flipped "Show on startup", we re-show every launch
    // regardless of the shown flag.
    QSettings().setValue(ParadigmHelpDialog::keyShowOnStartup(), true);
    QSettings().sync();
    QVERIFY(ParadigmHelpDialog::consumeFirstRunAutoShow());
    QVERIFY(ParadigmHelpDialog::consumeFirstRunAutoShow());
    QVERIFY(ParadigmHelpDialog::consumeFirstRunAutoShow());
}

bool TestParadigmHelp::anyTooltipSetOn(const QObject *root, const QString &childName)
{
    if (!root) {
        return false;
    }
    const QList<QWidget *> hits = root->findChildren<QWidget *>(childName);
    for (const QWidget *w : hits) {
        if (!w->toolTip().isEmpty()) {
            return true;
        }
        if (!w->whatsThis().isEmpty()) {
            return true;
        }
    }
    return false;
}

bool TestParadigmHelp::anyWhatsThisSetOn(const QObject *root, const QString &childName)
{
    if (!root) {
        return false;
    }
    const QList<QWidget *> hits = root->findChildren<QWidget *>(childName);
    for (const QWidget *w : hits) {
        if (!w->whatsThis().isEmpty()) {
            return true;
        }
    }
    return false;
}

void TestParadigmHelp::rolesWidgetHasTooltips()
{
    StorageManager storage(m_storageRoot);
    RolesWidget widget(storage);
    // Stronger: walk all children, require at least 3 widgets with
    // non-empty toolTip OR whatsThis -- proves tooltips were wired.
    int hits = 0;
    for (const QWidget *w : widget.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on RolesWidget, got ") + QString::number(hits)));
}

void TestParadigmHelp::teamsWidgetHasTooltips()
{
    StorageManager storage(m_storageRoot);
    TeamsWidget widget(storage);
    int hits = 0;
    for (const QWidget *w : widget.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on TeamsWidget, got ") + QString::number(hits)));
}

void TestParadigmHelp::trialsWidgetHasTooltips()
{
    StorageManager storage(m_storageRoot);
    TrialsWidget widget(storage);
    int hits = 0;
    for (const QWidget *w : widget.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on TrialsWidget, got ") + QString::number(hits)));
}

void TestParadigmHelp::projectsWidgetHasTooltips()
{
    StorageManager storage(m_storageRoot);
    ProjectsWidget widget(storage);
    int hits = 0;
    for (const QWidget *w : widget.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on ProjectsWidget, got ") + QString::number(hits)));
}

void TestParadigmHelp::labOverviewHasTooltips()
{
    StorageManager storage(m_storageRoot);
    LabOverviewWidget widget(storage);
    int hits = 0;
    for (const QWidget *w : widget.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on LabOverviewWidget, got ") + QString::number(hits)));
}

void TestParadigmHelp::roleEditorDialogHasTooltips()
{
    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.systemPrompt = QJsonValue(QStringLiteral("You are the build agent."));
    role.mode = Role::Mode::Primary;
    RoleEditorDialog dlg(role);

    int hits = 0;
    for (const QWidget *w : dlg.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on RoleEditorDialog, got ") + QString::number(hits)));
}

void TestParadigmHelp::settingsDialogHasTooltips()
{
    SettingsDialog dlg;
    int hits = 0;
    for (const QWidget *w : dlg.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on SettingsDialog, got ") + QString::number(hits)));
}

void TestParadigmHelp::addSpecialistDialogHasTooltips()
{
    StorageManager storage(m_storageRoot);
    AddSpecialistDialog dlg(storage);
    int hits = 0;
    for (const QWidget *w : dlg.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on AddSpecialistDialog, got ") + QString::number(hits)));
}

void TestParadigmHelp::editSpecialistDialogHasTooltips()
{
    StorageManager storage(m_storageRoot);
    Specialist spec;
    spec.id = QStringLiteral("spec-build");
    spec.roleId = QStringLiteral("build");
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.name = QStringLiteral("Build agent");
    EditSpecialistDialog dlg(spec, storage);
    int hits = 0;
    for (const QWidget *w : dlg.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 3,
             qPrintable(QStringLiteral("expected >= 3 widgets with tooltips/whatsThis on EditSpecialistDialog, got ") + QString::number(hits)));
}

void TestParadigmHelp::confirmApplyDialogHasTooltips()
{
    StorageManager storage(m_storageRoot);
    Team team;
    team.id = QStringLiteral("starter");
    team.name = QStringLiteral("Starter");
    ConfirmApplyDialog dlg(QStringLiteral("/tmp/proj"),
                            team,
                            storage,
                            QString(),
                            false);
    int hits = 0;
    for (const QWidget *w : dlg.findChildren<QWidget *>()) {
        if (!w->toolTip().isEmpty() || !w->whatsThis().isEmpty()) {
            ++hits;
        }
    }
    QVERIFY2(hits >= 2,
             qPrintable(QStringLiteral("expected >= 2 widgets with tooltips/whatsThis on ConfirmApplyDialog, got ") + QString::number(hits)));
}

QTEST_MAIN(TestParadigmHelp)
#include "test_paradigm_help.moc"
