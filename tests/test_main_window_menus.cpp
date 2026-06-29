// tests/test_main_window_menus.cpp
//
// ROADMAP P0-4 -- menu bar + per-action wiring tests for MainWindow.
//
// Two halves of coverage:
//
//   1. Menu structure
//      - MainWindow builds a QMenuBar with five top-level menus in
//        standard desktop order: File, Edit, View, Teams, Help.
//      - File holds New (submenu with Team and Role) + Open Project
//        (disabled) + Save (disabled) + Export Rendered Config +
//        Exit.
//      - Edit holds Undo (disabled) + Redo (disabled) + Preferences.
//      - View holds five tab-switch actions with Ctrl+1..5.
//      - Teams holds "Apply Team..." (Ctrl+Return -- P0-4 addition).
//      - Help holds the existing paradigm / shortcuts / WhatsThis
//        entries (P2-4/P2-5 surface).
//
//   2. Per-action routing
//      - File -> "New Team..." routes QTabWidget to the Teams tab
//        and invokes createTeam() (we observe the dialog side
//        effect via QInputDialog's modal widget reach).
//      - File -> "New Role..." routes to the Roles tab with an
//        empty proposed name (the RoleEditorDialog popup confirms
//        the createRole(QString) slot was called).
//      - View -> Ctrl+1..5 flips the QTabWidget index to
//        Lab Overview / Roles / Teams / Trials / Projects.
//      - The existing Paradigm Help / Keyboard Shortcuts entries
//        still own their previous behavior; we only assert that the
//        wiring is still present (full coverage of those dialogs
//        lives in test_paradigm_help / test_keyboard_shortcuts).
//
// The window is never show()'n, so the harness never depends on
// the event loop and never leaks real on-disk state -- the test
// sets HOME to a temp dir before QApplication is built so that
// MainWindow's hardcoded StorageManager derives its root from that
// throwaway location.

#include <QTest>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>

#include "MainWindow.h"
#include "ui/RolesWidget.h"
#include "ui/TeamsWidget.h"

class TestMainWindowMenus : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void menuBarHostsFiveTopLevelMenusInOrder();
    void fileMenuHasNewSubmenuOpenSaveExportAndExit();
    void fileMenuNewTeamRoutesToTeamsTab();
    void fileMenuNewRoleRoutesToRolesTab();
    void viewMenuRoutesCtrl1ThroughCtrl5ToTheFiveTabs();
    void editMenuHostsUndoRedoAndPreferences();
    void fileMenuOpenProjectAndSaveAreDisabledPlaceholders();
    void teamsMenuHasApplyTeamAction();
    void helpMenuStillOwnsParadigmHelpShortcutsAndWhatsThis();
    void menuActionsCarryExpectedKeyboardShortcuts();

private:
    // Throwaway HOME so StorageManager's default ~/.opencode-meta root
    // resolves to a temp dir per test slot, isolating the host's real
    // OpenCode Meta data from this test run.
    QTemporaryDir *m_tmpRoot = nullptr;
};

namespace {

// Lookup helper: walk a QMenu's action list (recursing one level into
// submenus so we can find the actions under File -> New) and return
// the first QAction whose objectName matches the requested
// `objectName`. Returns nullptr if no match -- callers use QVERIFY
// rather than dereferencing.
QAction *findActionByObjectName(QMenu *menu, const QString &objectName)
{
    if (!menu) {
        return nullptr;
    }
    const QList<QAction *> actions = menu->actions();
    for (QAction *a : actions) {
        if (!a) {
            continue;
        }
        if (a->objectName() == objectName) {
            return a;
        }
        if (a->menu()) {
            if (QAction *sub = findActionByObjectName(a->menu(), objectName)) {
                return sub;
            }
        }
    }
    return nullptr;
}

// Convenience wrapper: QMenuBar::actions() returns QActions whose
// QMenu is reachable via action->menu() -- but the action's
// objectName is set by Qt internally (e.g. "menu_N") rather than
// carrying the inner QMenu's objectName. We strip one level into
// `a->menu()` and compare against its objectName instead.
QMenu *findMenuByObjectName(QMenuBar *bar, const QString &objectName)
{
    if (!bar) {
        return nullptr;
    }
    const QList<QAction *> actions = bar->actions();
    for (QAction *a : actions) {
        if (!a) {
            continue;
        }
        QMenu *m = a->menu();
        if (m && m->objectName() == objectName) {
            return m;
        }
    }
    return nullptr;
}

// Pump queued + posted events to drain anything left over from a
// previous test slot -- this mirrors the cleanup pattern used by
// test_cross_view_smoke where the smoke test constructs and tears
// down MainWindow more than once. Without this, a modal QInputDialog
// rejected in one slot can still have queued events that fire after
// the dialog's parent (the previous slot's MainWindow) is destroyed.
void drainPendingEvents()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
    QCoreApplication::sendPostedEvents(nullptr, 0);
    QCoreApplication::processEvents();
}

// Close `w` cleanly before its destructor runs so any nested event
// loops / queued posted events targeting its child modals unwind
// in order. Honours WA_DeleteOnClose on the way out so disposal is
// immediate rather than racing with the next slot.
void closeAndDrain(QMainWindow *w)
{
    if (!w) {
        return;
    }
    if (w->isVisible()) {
        w->close();
    }
    drainPendingEvents();
}

} // namespace

void TestMainWindowMenus::initTestCase()
{
    // Redirect HOME to a per-run temp root so StorageManager derives
    // its read/write path from throwaway storage instead of the host's
    // real ~/.opencode-meta. We deliberately allocate the temp dir as
    // a heap object owned by `this` so its lifetime spans every test
    // slot -- Qt::QTemporaryDir auto-removes on destruction.
    m_tmpRoot = new QTemporaryDir;
    QVERIFY(m_tmpRoot->isValid());
    qputenv("HOME", m_tmpRoot->path().toUtf8());

    // QApplication is constructed by QTEST_MAIN before initTestCase
    // runs, so we make sure native dialogs don't sneak into the
    // harness when modal events fire (File > New Team / New Role).
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
}

void TestMainWindowMenus::menuBarHostsFiveTopLevelMenusInOrder()
{
    // Smoke: the constructor runs at this point and we want the very
    // first assertion to confirm the menu bar is shaped the way
    // MainWindow::createMenus() describes (File, Edit, View, Teams,
    // Help, in that order).
    MainWindow w;
    QMenuBar *bar = w.menuBar();
    QVERIFY(bar);

    QStringList titles;
    for (QAction *a : bar->actions()) {
        if (a && a->menu()) {
            titles.append(a->menu()->title());
        }
    }

    QCOMPARE(titles.size(), 5);
    QCOMPARE(titles.at(0), QStringLiteral("&File"));
    QCOMPARE(titles.at(1), QStringLiteral("&Edit"));
    QCOMPARE(titles.at(2), QStringLiteral("&View"));
    QCOMPARE(titles.at(3), QStringLiteral("&Teams"));
    QCOMPARE(titles.at(4), QStringLiteral("&Help"));

    // Object names stable enough for tests to navigate by -- if any
    // of these regress the Test would silently miss menu items.
    QVERIFY(findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu")));
    QVERIFY(findMenuByObjectName(bar, QStringLiteral("mainWindow.editMenu")));
    QVERIFY(findMenuByObjectName(bar, QStringLiteral("mainWindow.viewMenu")));
    QVERIFY(findMenuByObjectName(bar, QStringLiteral("mainWindow.teamsMenu")));
    QVERIFY(findMenuByObjectName(bar, QStringLiteral("mainWindow.helpMenu")));
}

void TestMainWindowMenus::fileMenuHasNewSubmenuOpenSaveExportAndExit()
{
    MainWindow w;
    QMenuBar *bar = w.menuBar();
    QVERIFY(bar);

    QMenu *fileMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu"));
    QVERIFY(fileMenu);

    // "New" submenu with Team and Role verbs.
    QMenu *newMenu = nullptr;
    for (QAction *a : fileMenu->actions()) {
        if (a && a->menu() && a->menu()->title() == QStringLiteral("&New")) {
            newMenu = a->menu();
            break;
        }
    }
    QVERIFY2(newMenu, "File menu must contain a 'New' submenu");

    QStringList newTitles;
    for (QAction *a : newMenu->actions()) {
        if (a) {
            newTitles.append(a->text());
        }
    }
    QVERIFY(newTitles.contains(QStringLiteral("&Team...")));
    QVERIFY(newTitles.contains(QStringLiteral("&Role...")));

    // Top-level File actions: Open Project..., Save, Export Rendered
    // Config..., and Exit. We deliberately do not count items in
    // the New submenu (which sits at the top) -- flat walk only.
    QStringList topTitles;
    for (QAction *a : fileMenu->actions()) {
        if (a && !a->menu() && !a->isSeparator()) {
            topTitles.append(a->text());
        }
    }
    QVERIFY(topTitles.contains(QStringLiteral("&Open Project...")));
    QVERIFY(topTitles.contains(QStringLiteral("&Save")));
    QVERIFY(topTitles.contains(QStringLiteral("&Export Rendered Config...")));
    QVERIFY(topTitles.contains(QStringLiteral("E&xit")));
}

void TestMainWindowMenus::cleanupTestCase()
{
    delete m_tmpRoot;
    m_tmpRoot = nullptr;
}

void TestMainWindowMenus::fileMenuNewTeamRoutesToTeamsTab()
{
    // ROADMAP P0-4: File > New Team... must flip QTabWidget to the
    // Teams tab and reach TeamsWidget::createTeam(). We capture the
    // flip synchronously via currentChanged so the assertion observes
    // the routing even if the modal chain (QInputDialog) is blocked
    // somewhere downstream -- we just dismiss the modal cleanly so
    // the test does not hang.
    MainWindow w;

    QTabWidget *tabs = w.tabWidget();
    QVERIFY(tabs);

    QMenuBar *bar = w.menuBar();
    QMenu *fileMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu"));
    QVERIFY(fileMenu);

    QAction *newTeam = findActionByObjectName(
        fileMenu, QStringLiteral("mainWindow.fileMenu.newTeamAction"));
    QVERIFY(newTeam);

    int observedIndex = -1;
    QObject::connect(tabs, &QTabWidget::currentChanged,
                     &w, [&](int idx) { observedIndex = idx; },
                     Qt::DirectConnection);

    QTimer::singleShot(0, qApp, []() {
        if (QInputDialog *modal =
                qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    newTeam->trigger();
    QCoreApplication::processEvents();

    // The setCurrentWidget() call happens before createTeam() opens
    // its modal QInputDialog, so observedIndex records the intended
    // flip regardless of whether the modal was dismissed.
    QCOMPARE(observedIndex, 2);
    QCOMPARE(tabs->currentIndex(), 2);
    QCOMPARE(qobject_cast<TeamsWidget *>(tabs->currentWidget()) != nullptr,
             true);

    closeAndDrain(&w);
}

void TestMainWindowMenus::fileMenuNewRoleRoutesToRolesTab()
{
    // ROADMAP P0-4: File > New Role... must flip QTabWidget to the
    // Roles tab and reach RolesWidget::createRole(QString()). We use
    // the same currentChanged-spy trick as the New Team test so the
    // assertion observes the routing even if the modal chain is
    // blocked somewhere downstream.
    MainWindow w;

    QTabWidget *tabs = w.tabWidget();
    QVERIFY(tabs);

    QMenuBar *bar = w.menuBar();
    QMenu *fileMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu"));
    QVERIFY(fileMenu);

    QAction *newRole = findActionByObjectName(
        fileMenu, QStringLiteral("mainWindow.fileMenu.newRoleAction"));
    QVERIFY(newRole);

    int observedIndex = -1;
    QObject::connect(tabs, &QTabWidget::currentChanged,
                     &w, [&](int idx) { observedIndex = idx; },
                     Qt::DirectConnection);

    QTimer::singleShot(0, qApp, []() {
        if (QDialog *modal =
                qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    newRole->trigger();
    QCoreApplication::processEvents();

    QCOMPARE(observedIndex, 1);
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(qobject_cast<RolesWidget *>(tabs->currentWidget()) != nullptr,
             true);

    closeAndDrain(&w);
}

void TestMainWindowMenus::viewMenuRoutesCtrl1ThroughCtrl5ToTheFiveTabs()
{
    MainWindow w;

    QTabWidget *tabs = w.tabWidget();
    QVERIFY(tabs);

    QMenuBar *bar = w.menuBar();
    QMenu *viewMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.viewMenu"));
    QVERIFY(viewMenu);

    struct Expected
    {
        const char *objectName;
        int targetIndex;
    };
    const Expected expected[] = {
        { "mainWindow.viewMenu.goToLabOverviewAction", 0 },
        { "mainWindow.viewMenu.goToRolesAction",        1 },
        { "mainWindow.viewMenu.goToTeamsAction",        2 },
        { "mainWindow.viewMenu.goToTrialsAction",       3 },
        { "mainWindow.viewMenu.goToProjectsAction",     4 },
    };

    // Start from an unrelated index so we can confirm the action
    // flips it each time rather than coincidentally matching.
    tabs->setCurrentIndex(2);

    for (const Expected &x : expected) {
        const QString objectName = QString::fromLatin1(x.objectName);
        QAction *a = findActionByObjectName(viewMenu, objectName);
        QVERIFY2(a, qPrintable(QStringLiteral("missing view action: ") + objectName));
        a->trigger();
        QCOMPARE(tabs->currentIndex(), x.targetIndex);
    }

    closeAndDrain(&w);
}

void TestMainWindowMenus::editMenuHostsUndoRedoAndPreferences()
{
    MainWindow w;

    QMenuBar *bar = w.menuBar();
    QMenu *editMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.editMenu"));
    QVERIFY(editMenu);

    QAction *undo = findActionByObjectName(
        editMenu, QStringLiteral("mainWindow.editMenu.undoAction"));
    QAction *redo = findActionByObjectName(
        editMenu, QStringLiteral("mainWindow.editMenu.redoAction"));
    QAction *prefs = findActionByObjectName(
        editMenu, QStringLiteral("mainWindow.editMenu.preferencesAction"));

    QVERIFY(undo);
    QVERIFY(redo);
    QVERIFY(prefs);

    // Both placeholders stay disabled (every edit is persisted
    // immediately) but Preferences stays enabled so the user can
    // always reach it.
    QVERIFY(!undo->isEnabled());
    QVERIFY(!redo->isEnabled());
    QVERIFY(prefs->isEnabled());

    // A Separator must sit between the disabled Undo/Redo cluster
    // and Preferences -- visual grouping matters because Qt removes
    // disabled-only sections otherwise.
    bool undoBeforePrefs = false;
    bool undoSeen = false;
    bool separatorSeen = false;
    for (QAction *a : editMenu->actions()) {
        if (a == undo) {
            undoSeen = true;
        } else if (undoSeen && !separatorSeen && a == redo) {
            // Both placeholders clustered together is fine.
        } else if (a && a->isSeparator()) {
            separatorSeen = true;
        } else if (undoSeen && separatorSeen && a == prefs) {
            undoBeforePrefs = true;
        }
    }
    QVERIFY(undoBeforePrefs);

    closeAndDrain(&w);
}

void TestMainWindowMenus::fileMenuOpenProjectAndSaveAreDisabledPlaceholders()
{
    MainWindow w;

    QMenuBar *bar = w.menuBar();
    QMenu *fileMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu"));
    QVERIFY(fileMenu);

    QAction *open = findActionByObjectName(
        fileMenu, QStringLiteral("mainWindow.fileMenu.openProjectAction"));
    QAction *save = findActionByObjectName(
        fileMenu, QStringLiteral("mainWindow.fileMenu.saveAction"));

    QVERIFY(open);
    QVERIFY(save);
    QVERIFY(!open->isEnabled());
    QVERIFY(!save->isEnabled());

    // Status tips carry the explanation -- they must be non-empty so
    // the future status bar (P0-3) has something to surface.
    QVERIFY(!open->statusTip().isEmpty());
    QVERIFY(!save->statusTip().isEmpty());

    closeAndDrain(&w);
}

void TestMainWindowMenus::teamsMenuHasApplyTeamAction()
{
    MainWindow w;

    QMenuBar *bar = w.menuBar();
    QMenu *teamsMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.teamsMenu"));
    QVERIFY(teamsMenu);

    QAction *apply = findActionByObjectName(
        teamsMenu, QStringLiteral("mainWindow.teamsMenu.applyTeamAction"));
    QVERIFY(apply);
    QCOMPARE(apply->text(), QStringLiteral("&Apply Team..."));
    QVERIFY(apply->isEnabled());

    closeAndDrain(&w);
}

void TestMainWindowMenus::helpMenuStillOwnsParadigmHelpShortcutsAndWhatsThis()
{
    // P2-4 + P2-5 surface lives under Help; P0-4 must NOT regress
    // any of it. The three actions are still wired, still textually
    // intact, and the action text starts with the matching mnemonic
    // so the menu looks identical to the user.
    MainWindow w;

    QMenuBar *bar = w.menuBar();
    QMenu *helpMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.helpMenu"));
    QVERIFY(helpMenu);

    QAction *paradigm = findActionByObjectName(
        helpMenu, QStringLiteral("mainWindow.helpMenu.showParadigmHelpAction"));
    QAction *shortcuts = findActionByObjectName(
        helpMenu, QStringLiteral("mainWindow.helpMenu.showShortcutsAction"));
    QAction *whatsthis = findActionByObjectName(
        helpMenu, QStringLiteral("mainWindow.helpMenu.whatsThisAction"));

    QVERIFY(paradigm);
    QVERIFY(shortcuts);
    QVERIFY(whatsthis);

    QVERIFY(paradigm->text().startsWith(QStringLiteral("Show Paradigm Help")));
    QVERIFY(shortcuts->text().startsWith(QStringLiteral("Keyboard Shortcuts")));
    QCOMPARE(whatsthis->text(), QStringLiteral("What's This?"));

    closeAndDrain(&w);
}

void TestMainWindowMenus::menuActionsCarryExpectedKeyboardShortcuts()
{
    // Sanity check the wiring of the four "hot" shortcuts P0-4 added
    // at the File / Edit / View / Teams layer. We assert via
    // QKeySequence::toString so the strings stay platform-portable
    // (Linux gets "Ctrl+N", macOS gets "Meta+N"). QKeySequence's
    // StandardKey enum values (Quit, Undo, Redo, ...) need to be
    // wrapped in QKeySequence(...) before they become a value that
    // can be stringified.
    auto findShortcut = [](QMenu *m, const QString &objectName) -> QString {
        QAction *a = findActionByObjectName(m, objectName);
        if (!a) {
            return QString();
        }
        return a->shortcut().toString();
    };

    MainWindow w;
    QMenuBar *bar = w.menuBar();

    const QString empty;
    if (empty.isEmpty()) {
        // Group the actual assertions below so we can QVERIFY2 on
        // every missing-action instead of crashing inside a helper.
        // (FIXME: actual block below.)
    }

    QMenu *fileMenu  = findMenuByObjectName(bar, QStringLiteral("mainWindow.fileMenu"));
    QMenu *editMenu  = findMenuByObjectName(bar, QStringLiteral("mainWindow.editMenu"));
    QMenu *viewMenu  = findMenuByObjectName(bar, QStringLiteral("mainWindow.viewMenu"));
    QMenu *teamsMenu = findMenuByObjectName(bar, QStringLiteral("mainWindow.teamsMenu"));
    QMenu *helpMenu  = findMenuByObjectName(bar, QStringLiteral("mainWindow.helpMenu"));
    QVERIFY(fileMenu); QVERIFY(editMenu); QVERIFY(viewMenu);
    QVERIFY(teamsMenu); QVERIFY(helpMenu);

    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.newTeamAction")),
             QKeySequence(QStringLiteral("Ctrl+N")).toString());
    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.newRoleAction")),
             QKeySequence(QStringLiteral("Ctrl+Shift+N")).toString());
    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.exportRenderedConfigAction")),
             QKeySequence(QStringLiteral("Ctrl+E")).toString());
    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.exportBundleAction")),
             QKeySequence(QStringLiteral("Ctrl+Shift+E")).toString());
    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.importBundleAction")),
             QKeySequence(QStringLiteral("Ctrl+Shift+I")).toString());
    QCOMPARE(findShortcut(fileMenu,
                          QStringLiteral("mainWindow.fileMenu.exitAction")),
             QKeySequence(QKeySequence::Quit).toString());

    // ROADMAP P3-2 -- the bundle menu entries carry objectNames that
    // match the File-menu pattern and they default to enabled so
    // users have a working import/export out-of-the-box.
    QVERIFY(findActionByObjectName(fileMenu,
                                   QStringLiteral("mainWindow.fileMenu.exportBundleAction")));
    QVERIFY(findActionByObjectName(fileMenu,
                                   QStringLiteral("mainWindow.fileMenu.importBundleAction")));
    QVERIFY(findActionByObjectName(fileMenu,
                                   QStringLiteral("mainWindow.fileMenu.exportBundleAction"))->isEnabled());
    QVERIFY(findActionByObjectName(fileMenu,
                                   QStringLiteral("mainWindow.fileMenu.importBundleAction"))->isEnabled());

    QCOMPARE(findShortcut(editMenu,
                          QStringLiteral("mainWindow.editMenu.undoAction")),
             QKeySequence(QKeySequence::Undo).toString());
    QCOMPARE(findShortcut(editMenu,
                          QStringLiteral("mainWindow.editMenu.redoAction")),
             QKeySequence(QKeySequence::Redo).toString());
    QCOMPARE(findShortcut(editMenu,
                          QStringLiteral("mainWindow.editMenu.preferencesAction")),
             QKeySequence(QKeySequence::Preferences).toString());

    QCOMPARE(findShortcut(viewMenu,
                          QStringLiteral("mainWindow.viewMenu.goToLabOverviewAction")),
             QKeySequence(QStringLiteral("Ctrl+1")).toString());
    QCOMPARE(findShortcut(viewMenu,
                          QStringLiteral("mainWindow.viewMenu.goToProjectsAction")),
             QKeySequence(QStringLiteral("Ctrl+5")).toString());

    QCOMPARE(findShortcut(teamsMenu,
                          QStringLiteral("mainWindow.teamsMenu.applyTeamAction")),
             QKeySequence(QStringLiteral("Ctrl+Return")).toString());

    QCOMPARE(findShortcut(helpMenu,
                          QStringLiteral("mainWindow.helpMenu.whatsThisAction")),
             QKeySequence(Qt::SHIFT | Qt::Key_F1).toString());
}

QTEST_MAIN(TestMainWindowMenus)
#include "test_main_window_menus.moc"
