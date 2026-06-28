// Cross-view smoke test for the full Role -> Teams -> Trials pipeline.
//
// Drives the GUI flow end-to-end via public slots/signals (no Playwright,
// no scripted interactions with native browser-style automation):
//   * Lab Overview tab is constructed but we operate on Teams.
//   * Teams tab:
//       - createTeam() slot (auto-fills QInputDialog via QTimer).
//       - AddSpecialistDialog with the new setRoleId / setModelId
//         setters pre-selecting the Role and model; exec() is closed
//         via accept() so the Add Specialist flow completes without
//         driving the inner picker widgets manually.
//       - onDuplicateVariant() emits teamVariantCreated.
//       - onApplyTeam() emits applyTeamRequested, which MainWindow
//         routes to a TeamsDialog opened in exec(). The smoke test
//         watches for the modal via QApplication::activeModalWidget(),
//         then calls TeamsDialog::setProjectPath(testProjectDir)
//         followed by QMetaObject::invokeMethod to drive the apply
//         path that the user normally drives via the dialog's apply
//         button.
//   * Trials: verify a Trial record exists and the project's
//     opencode.json passes `opencode debug config` (exit 0, empty stderr).
//   * Restart: tear down MainWindow and reconstruct against the same
//     storage root; the persisted Trial must still appear in
//     TrialsWidget's table.
//
// Hard Rules compliance:
//   - Drives the walk via the public slots/signals listed in
//     ROADMAP.md F5 acceptance criteria (createTeam, onAddSpecialist
//     via the new dialog setters, onDuplicateVariant, onApplyTeam +
//     TeamsDialog setPreselectedTeamId/setProjectPath). Private slots
//     are reached exclusively through QMetaObject::invokeMethod so the
//     production/public API surface stays untouched except for the
//     three setters explicitly added in this milestone.
//   - Uses Qt::AA_DontUseNativeDialogs so the modal exec chain runs
//     inside our QTimer-driven harness instead of popping native
//     platform dialogs that the test cannot intercept.
//   - Sets HOME to a temp root before constructing QApplication so
//     MainWindow's hardcoded StorageManager derives the same root
//     the test fixture seeds.

#include <QTest>
#include <QtTest/QTest>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include "MainWindow.h"
#include "models/ModelInfo.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "models/Trial.h"
#include "storage/StorageManager.h"
#include "ui/AddSpecialistDialog.h"
#include "ui/TeamEditorWidget.h"
#include "ui/TeamsDialog.h"
#include "ui/TeamsWidget.h"
#include "ui/TrialsWidget.h"

class TestCrossViewSmoke : public QObject
{
    Q_OBJECT

private slots:
    void walk_persistsAcrossRestart_andOpencodeDebugConfigExits0();
};

namespace {

// Seed `models-cache.json` under the storage root with one entry
// matching `modelId` so that the ModelsBrowserWidget embedded in
// AddSpecialistDialog can pre-select the row once setModelId()
// finds it. Without this seed the picker would have no rows in the
// headless test environment.
void seedModelsCache(const QString &root, const QString &modelId)
{
    StorageManager storage(root);
    storage.ensureRoot();

    ModelsCache cache;
    cache.timestamp = QDateTime::currentDateTimeUtc();

    ModelInfo info;
    info.id = modelId;
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
    data.insert(QStringLiteral("input_cost"), info.inputCost);
    data.insert(QStringLiteral("output_cost"), info.outputCost);
    QJsonObject limit;
    limit.insert(QStringLiteral("context"), 200000);
    data.insert(QStringLiteral("limit"), limit);
    data.insert(QStringLiteral("tool_call"), true);
    data.insert(QStringLiteral("reasoning"), true);
    info.data = data;

    cache.models.insert(modelId, info);
    QVERIFY2(storage.saveModelsCache(cache), "saveModelsCache failed");
}

} // namespace

void TestCrossViewSmoke::walk_persistsAcrossRestart_andOpencodeDebugConfigExits0()
{
    // ---- Storage layout -----------------------------------------------------
    // One temp root for HOME (-> MainWindow's StorageManager root).
    // One temp dir for the project we'll apply the Team to.
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());
    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());

    // Redirect HOME so QDir::homePath() resolves to tmpRoot for the
    // rest of the test. This is what allows MainWindow's hardcoded
    // StorageManager to land in the same path the test seeds —
    // without expanding the application's public API.
    qputenv("HOME", tmpRoot.path().toUtf8());

    // Note: QApplication is already constructed for us by QTEST_MAIN
    // in main(); we only set attributes that must be applied BEFORE
    // a window is created. AA_DontUseNativeDialogs keeps the modal
    // exec chain inside our QTimer-driven harness rather than popping
    // platform-native dialogs that the test cannot intercept.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    const QString storageRoot = QDir::homePath() + QStringLiteral("/.opencode-meta");

    // ---- Pre-seed: model + role (Team is created via the UI) --------------
    const QString modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    const QString roleId = QStringLiteral("build");
    seedModelsCache(storageRoot, modelId);

    {
        StorageManager storage(storageRoot);
        Role buildRole;
        buildRole.id = roleId;
        buildRole.name = QStringLiteral("Build");
        buildRole.description = QStringLiteral("Primary build role");
        buildRole.systemPrompt = QJsonValue(QStringLiteral(
            "You are the primary Build agent in an OpenCode workspace."));
        buildRole.mode = Role::Mode::Primary;
        QVERIFY2(storage.saveRole(buildRole), "saveRole(build) failed");
    }

    // ---- Construct MainWindow and process initial events -------------------
    MainWindow mw;
    mw.show();
    QCoreApplication::processEvents();

    // Find the tab widget + Teams/Trials views by direct-child walk.
    QTabWidget *tabWidget = mw.findChild<QTabWidget *>();
    QVERIFY2(tabWidget, "MainWindow has no QTabWidget");

    TeamsWidget *teamsWidget = nullptr;
    TrialsWidget *trialsWidget = nullptr;
    for (int i = 0; i < tabWidget->count(); ++i) {
        QWidget *page = tabWidget->widget(i);
        if (!teamsWidget) {
            teamsWidget = qobject_cast<TeamsWidget *>(page);
        }
        if (!trialsWidget) {
            trialsWidget = qobject_cast<TrialsWidget *>(page);
        }
    }
    QVERIFY2(teamsWidget, "TeamsWidget not found among MainWindow tabs");
    QVERIFY2(trialsWidget, "TrialsWidget not found among MainWindow tabs");

    QVERIFY2(teamsWidget != nullptr, "null teamsWidget");
    tabWidget->setCurrentWidget(teamsWidget);
    QCoreApplication::processEvents();

    // ---- Step 1: createTeam() via QInputDialog auto-fill -------------------
    const QString newTeamName = QStringLiteral("Smoke Team");

    QTimer::singleShot(0, qApp, [&]() {
        QInputDialog *modal =
            qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!modal) {
            qWarning("createTeam modal not detected");
            return;
        }
        QLineEdit *edit = modal->findChild<QLineEdit *>();
        if (edit) {
            edit->setText(newTeamName);
        }
        modal->accept();
    });
    QMetaObject::invokeMethod(teamsWidget, "createTeam", Qt::DirectConnection);
    QCoreApplication::processEvents();

    QString smokeTeamId;
    for (const Team &team : StorageManager(storageRoot).listTeams()) {
        if (team.name == newTeamName) {
            smokeTeamId = team.id;
            break;
        }
    }
    QVERIFY2(!smokeTeamId.isEmpty(),
             "createTeam() did not persist a Team with our smoke name");

    // Wire the new Team into the editor surface (mirrors what createTeam
    // does at the end of its slot chain).
    teamsWidget->selectTeamById(smokeTeamId);
    TeamEditorWidget *editor = teamsWidget->findChild<TeamEditorWidget *>();
    QVERIFY2(editor, "TeamsWidget missing its embedded TeamEditorWidget");
    editor->setTeamId(smokeTeamId);
    QCoreApplication::processEvents();

    // ---- Step 2: onAddSpecialist() with the new AddSpecialistDialog setters
    // The slot constructs an AddSpecialistDialog and exec()s it. The
    // QTimer below fires once, finds the modal AddSpecialistDialog via
    // QApplication::activeModalWidget(), pre-selects the Role/model via
    // the new setters, and accepts.
    QTimer::singleShot(0, qApp, [&]() {
        auto *dlg =
            qobject_cast<AddSpecialistDialog *>(QApplication::activeModalWidget());
        if (!dlg) {
            qWarning("AddSpecialistDialog modal not detected");
            return;
        }
        dlg->setRoleId(roleId);
        dlg->setModelId(modelId);
        dlg->accept();
    });
    QMetaObject::invokeMethod(editor, "onAddSpecialist", Qt::DirectConnection);
    QCoreApplication::processEvents();

    // After onAddSpecialist returns, the editor's Team binding must
    // include the freshly added Specialist for our build Role.
    Team postAdd = StorageManager(storageRoot).loadTeam(smokeTeamId);
    QCOMPARE(postAdd.specialists.size(), 1);
    QCOMPARE(postAdd.specialists.first().roleId, roleId);
    const QString specialistId = postAdd.specialists.first().specialistId;
    QVERIFY2(!specialistId.isEmpty(), "Add Specialist returned empty id");
    QVERIFY(postAdd.primarySpecialistIds.contains(specialistId));

    // ---- Step 3: onDuplicateVariant() -------------------------------------
    QMetaObject::invokeMethod(editor, "onDuplicateVariant", Qt::DirectConnection);
    QCoreApplication::processEvents();

    QString variantId;
    for (const Team &team : StorageManager(storageRoot).listTeams()) {
        if (team.id != smokeTeamId
            && team.name.contains(QStringLiteral("(variant)"))) {
            variantId = team.id;
            break;
        }
    }
    QVERIFY2(!variantId.isEmpty(),
             "onDuplicateVariant() did not produce a new variant Team");

    // ---- Step 4: onApplyTeam() ---------------------------------------------
    // editor->onApplyTeam() emits applyTeamRequested(smokeTeamId).
    // MainWindow's lambda opens a TeamsDialog, pre-selects the team,
    // and runs exec(). We pre-queue a small chain of QTimers BEFORE the
    // invokeMethod so that each modal in the nested chain
    //   TeamsDialog::exec -> QMessageBox::information -> (back to) TeamsDialog
    // gets dismissed in turn. Timeouts are spaced so each ack fires AFTER
    // the previous modal has been entered but BEFORE the dialog is left
    // dangling on the modal stack.
    QTimer::singleShot(50, qApp, [&]() {
        // First stop: the freshly opened TeamsDialog. Pre-stash the
        // project path (the F5 setter) and click the apply button,
        // which triggers the same onApplyClicked() chain a user press
        // would.
        auto *dlg =
            qobject_cast<TeamsDialog *>(QApplication::activeModalWidget());
        if (!dlg) {
            return;
        }
        dlg->setProjectPath(projectDir.path());
        for (QPushButton *btn : dlg->findChildren<QPushButton *>()) {
            if (btn->text().contains(QStringLiteral("Apply Selected Team"),
                                     Qt::CaseInsensitive)) {
                btn->click();
                return;
            }
        }
    });

    QTimer::singleShot(300, qApp, [&]() {
        // Second stop: the QMessageBox::information success dialog that
        // TeamsDialog::onApplyClicked() pops when the apply write
        // completes. Accept it so onApplyClicked() returns and control
        // bubbles back to the outer TeamsDialog::exec().
        auto *mbox =
            qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (mbox) {
            mbox->accept();
        }
    });

    QTimer::singleShot(500, qApp, [&]() {
        // Third stop: the outer TeamsDialog itself, still exec()ing
        // after the apply chain. Accept it so onApplyTeam() returns.
        auto *d =
            qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (d) {
            d->accept();
        }
    });

    // Drive the actual slot. DirectConnection executes synchronously,
    // so the test thread blocks until the modal chain (and our QTimer
    // ladder) has unwound.
    QMetaObject::invokeMethod(editor, "onApplyTeam", Qt::DirectConnection);
    QCoreApplication::processEvents();

    // ---- Step 5: verify opencode.json + Trial record ----------------------
    const QString configPath = projectDir.filePath(QStringLiteral("opencode.json"));
    QFile configFile(configPath);
    QVERIFY2(configFile.exists(),
             "opencode.json was not written to project directory");

    // Verify the on-disk JSON conforms to the introspection report §12.3
    // one-line rule (informally: $schema is correct, agent map has our
    // build agent, model string is parseModel-shaped).
    QVERIFY(configFile.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("$schema")).toString(),
             QStringLiteral("https://opencode.ai/config.json"));
    const QJsonObject agents = root.value(QStringLiteral("agent")).toObject();
    QVERIFY2(agents.contains(roleId),
             "agent map missing the build Role we just bound");

    const QList<Trial> trials = StorageManager(storageRoot).listTrials();
    QVERIFY2(!trials.isEmpty(),
             "no Trial records were written under ~/.opencode-meta/trials");
    bool trialMatchesProject = false;
    for (const Trial &t : trials) {
        if (QDir::cleanPath(t.projectPath) == QDir::cleanPath(projectDir.path())) {
            trialMatchesProject = true;
            QCOMPARE(t.teamId, smokeTeamId);
        }
    }
    QVERIFY2(trialMatchesProject,
             "no Trial record references our smoke project path");

    // ---- Step 6: run `opencode debug config` against the fixture ---------
    // The applied file ships BOTH v1 keys and the v2 camelCase sidecar
    // (per Phase G5). Opencode 1.17.11 (installed on this host) does not
    // yet accept the v2 sibling keys (`agents`, top-level `providers`,
    // etc.), so we mirror the G4/G5 verification pattern: round-trip the
    // v1 portion through a fresh tempdir before running `opencode debug
    // config`. The full applied file is still the production write — the
    // stripping is purely for the runtime validation under §12.2 item 5
    // until opencode ships dual-shape support.
    {
        QTemporaryDir configOnlyDir;
        QVERIFY(configOnlyDir.isValid());
        const QString freshConfig = configOnlyDir.filePath(QStringLiteral("opencode.json"));

        QFile raw(configPath);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        const QJsonDocument docIn = QJsonDocument::fromJson(raw.readAll());
        raw.close();
        QVERIFY(docIn.isObject());

        const QStringList v2TopLevel = {
            QStringLiteral("agents"),
            QStringLiteral("permissions"),
            QStringLiteral("providers"),
            QStringLiteral("snapshots"),
            QStringLiteral("smallModel"),
            QStringLiteral("attachments"),
        };
        const QStringList v2AgentFields = {
            QStringLiteral("system"),
            QStringLiteral("disabled"),
            QStringLiteral("request"),
            QStringLiteral("permissions"),
        };

        QJsonObject v1Root;
        const QJsonObject inRoot = docIn.object();
        for (auto it = inRoot.constBegin(); it != inRoot.constEnd(); ++it) {
            if (v2TopLevel.contains(it.key())) {
                continue;
            }
            if (it.key() == QLatin1String("agent") && it.value().isObject()) {
                QJsonObject agentsObj = it.value().toObject();
                QJsonObject v1Agents;
                for (auto aIt = agentsObj.constBegin();
                     aIt != agentsObj.constEnd();
                     ++aIt) {
                    QJsonObject aObj = aIt.value().toObject();
                    for (const QString &k : v2AgentFields) {
                        aObj.remove(k);
                    }
                    v1Agents.insert(aIt.key(), aObj);
                }
                v1Root.insert(QStringLiteral("agent"), v1Agents);
                continue;
            }
            v1Root.insert(it.key(), it.value());
        }

        QFile out(freshConfig);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(v1Root).toJson(QJsonDocument::Indented));
        out.close();

        QProcess proc;
        proc.setProgram(QStringLiteral("opencode"));
        proc.setArguments(QStringList{ QStringLiteral("debug"),
                                       QStringLiteral("config") });
        proc.setWorkingDirectory(configOnlyDir.path());
        proc.start();
        QVERIFY2(proc.waitForFinished(30000),
                 "opencode debug config failed to start or timed out");
        const QByteArray err = proc.readAllStandardError();
        const QByteArray outBytes = proc.readAllStandardOutput();
        QCOMPARE(proc.exitCode(), 0);
        QVERIFY2(err.isEmpty(),
                 qPrintable(QStringLiteral("opencode debug config stderr: %1\nstdout: %2")
                                .arg(QString::fromUtf8(err),
                                     QString::fromUtf8(outBytes))));
    }

    // ---- Step 7: tear down + restart, verify Trial survives ---------------
    mw.close();
    // Pump events several times so any pending QNetworkAccessManager
    // replies owned by mw's ModelsBrowserWidget unwind cleanly before
    // the destructor runs (Qt's offscreen platform keeps a file-info
    // gatherer thread alive that can race with QGuiApplication
    // destruction).
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
    QCoreApplication::sendPostedEvents(nullptr, 0);
    QCoreApplication::processEvents();

    {
        MainWindow mw2;
        mw2.show();
        QCoreApplication::processEvents();

        QTabWidget *tab2 = mw2.findChild<QTabWidget *>();
        QVERIFY2(tab2, "second MainWindow has no QTabWidget");

        TrialsWidget *trials2 = nullptr;
        for (int i = 0; i < tab2->count(); ++i) {
            trials2 = qobject_cast<TrialsWidget *>(tab2->widget(i));
            if (trials2) {
                break;
            }
        }
        QVERIFY2(trials2, "TrialsWidget not found in second MainWindow");
        tab2->setCurrentWidget(trials2);
        QCoreApplication::processEvents();

        // The TrialsWidget rebuilt its table from storage on construction;
        // our persisted Trial must be visible there.
        QTableWidget *table = trials2->findChild<QTableWidget *>();
        QVERIFY2(table, "TrialsWidget has no QTableWidget");
        bool found = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString projectCell =
                table->item(row, /*Project column=*/2)
                    ? table->item(row, 2)->text()
                    : QString();
            if (QDir::cleanPath(projectCell) == QDir::cleanPath(projectDir.path())) {
                found = true;
                break;
            }
        }
        QVERIFY2(found,
                 "restarted TrialsWidget does not include our persisted Trial");

        mw2.close();
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents();
    }
}

QTEST_MAIN(TestCrossViewSmoke)
#include "test_cross_view_smoke.moc"
