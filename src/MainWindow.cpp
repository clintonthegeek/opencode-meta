#include "MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>
#include <QWhatsThis>

#include "ui/KeyboardShortcutsDialog.h"
#include "ui/LabOverviewWidget.h"
#include "ui/ParadigmHelpDialog.h"
#include "ui/ProjectsWidget.h"
#include "ui/RolesWidget.h"
#include "ui/SettingsDialog.h"
#include "ui/TeamsDialog.h"
#include "ui/TeamsWidget.h"
#include "ui/TrialsWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenCode Meta Qt"));
    resize(800, 600);

    m_tabWidget = new QTabWidget(this);

    // Ensure storage root and default PARADIGM entities exist before
    // constructing view widgets so initial data (including the Starter
    // Team) is visible as soon as the tabs render.
    m_storageManager.ensureRoot();
    m_storageManager.seedDefaultsIfNeeded();

    // New five-view navigation: Lab Overview, Roles, Teams, Trials, Projects.
    auto *labOverview = new LabOverviewWidget(m_storageManager, this);
    auto *rolesWidget = new RolesWidget(m_storageManager, this);
    auto *teamsWidget = new TeamsWidget(m_storageManager, this);
    auto *trialsWidget = new TrialsWidget(m_storageManager, this);
    auto *projectsWidget = new ProjectsWidget(m_storageManager, this);

    m_tabWidget->addTab(labOverview, tr("Lab Overview"));
    m_tabWidget->addTab(rolesWidget, tr("Roles"));
    m_tabWidget->addTab(teamsWidget, tr("Teams"));
    m_tabWidget->addTab(trialsWidget, tr("Trials"));
    m_tabWidget->addTab(projectsWidget, tr("Projects"));

    // Lab Overview quick actions: route into Roles / Teams views.
    connect(labOverview, &LabOverviewWidget::newTeamRequested,
            this, [this, teamsWidget]() {
                m_tabWidget->setCurrentWidget(teamsWidget);
                teamsWidget->createTeam();
            });

    connect(labOverview, &LabOverviewWidget::browseRolesRequested,
            this, [this, rolesWidget]() {
                m_tabWidget->setCurrentWidget(rolesWidget);
            });

    connect(labOverview, &LabOverviewWidget::openTeamRequested,
            this, [this, teamsWidget](const QString &teamId) {
                m_tabWidget->setCurrentWidget(teamsWidget);
                teamsWidget->selectTeamById(teamId);
            });

    // P0-1: Start-Trial quick action placeholder. The full Trial-creation
    // flow lives in the Projects view (apply Team -> project) which records a
    // Trial entry; here we surface the request so the user sees an
    // observable response rather than a silent no-op.
    connect(labOverview, &LabOverviewWidget::startTrialRequested,
            this, [this, teamsWidget](const QString &projectPath,
                                     const QString &teamId) {
                qDebug() << "MainWindow: startTrialRequested"
                         << "project=" << projectPath
                         << "team=" << teamId;

                QString message;
                if (projectPath.isEmpty()) {
                    message = tr("Start Trial was requested, but no "
                                 "project was selected.");
                } else if (teamId.isEmpty()) {
                    message = tr("Start Trial requested for project:\n"
                                 "%1\n\n"
                                 "No Team was selected; switch to the Teams "
                                 "tab to pick one, then use Projects to apply "
                                 "it and record a Trial.")
                                 .arg(projectPath);
                } else {
                    message = tr("Start Trial requested:\n"
                                 "  Project: %1\n"
                                 "  Team: %2\n\n"
                                 "Switch to the Projects tab to apply this "
                                 "Team and record the Trial.")
                                 .arg(projectPath, teamId);
                }

                QMessageBox::information(this, tr("Start Trial"), message);
                m_tabWidget->setCurrentWidget(teamsWidget);
            });

    // F2 cross-view wiring: whenever a Team-side flow asks to author a
    // new Role inline (Add Specialist with no Role available, or
    // TeamEditorWidget direct), switch to the Roles tab and seed the
    // editor with the proposed name (empty is acceptable -- the Role
    // editor exposes a name field).
    connect(teamsWidget, &TeamsWidget::createRoleRequested,
            this, [this, rolesWidget](const QString &proposedName) {
                m_tabWidget->setCurrentWidget(rolesWidget);
                rolesWidget->createRole(proposedName);
            });

    // F1 cross-view wiring: when TeamEditorWidget's footer "Apply Team..."
    // button is clicked, open the same TeamsDialog the Teams menu uses,
    // but route the currently loaded Team id through the new
    // setPreselectedTeamId() so the matching row is selected on open.
    // Matches the existing apply path exactly except for the pre-selection.
    connect(teamsWidget, &TeamsWidget::applyTeamRequested,
            this, [this](const QString &teamId) {
                TeamsDialog dlg(m_storageManager, this);
                if (!teamId.isEmpty()) {
                    dlg.setPreselectedTeamId(teamId);
                }
                dlg.exec();
            });

    // P0-1: Promote-winning-Team placeholder. The full duplication /
    // promotion flow belongs to the Teams view; here we log + surface the
    // request so the Trials tab's Promote button produces a visible effect
    // instead of firing an unhandled signal.
    connect(trialsWidget, &TrialsWidget::promoteTeamRequested,
            this, [this, teamsWidget](const QString &teamId) {
                qDebug() << "MainWindow: promoteTeamRequested" << "team=" << teamId;

                const QString message = teamId.isEmpty()
                    ? tr("Promote Team was requested, but no Team was "
                         "associated with the selected Trial.")
                    : tr("Promote Team requested:\n"
                         "  Team: %1\n\n"
                         "Switching to the Teams tab; full promote "
                         "workflow will be wired up in a follow-up task.")
                          .arg(teamId);

                QMessageBox::information(this, tr("Promote Team"), message);
                m_tabWidget->setCurrentWidget(teamsWidget);
            });

    setCentralWidget(m_tabWidget);

    // P2-4: install per-widget whats-this text BEFORE the menu is
    // built so Help -> "What's This?" (Shift+F1) and the F1 shortcut
    // can find non-empty whatsThis strings on the major UI elements.
    // The paradigm help itself is a separate, explicit action.
    installContextHelp();

    createMenus();

    // P2-4: surface the paradigm help the first time the user runs
    // the app, or every launch if they checked "Show on startup" in
    // a previous session. The flag is flipped inside
    // ParadigmHelpDialog::consumeFirstRunAutoShow() so we never
    // auto-show twice in a row even if the user closes the dialog
    // with the window manager close button instead of "OK".
    // Defer until the window is shown so the dialog lands on top of
    // the main window rather than being orphaned.
    QTimer::singleShot(0, this, &MainWindow::maybeShowParadigmHelpOnFirstRun);
}

void MainWindow::installContextHelp()
{
    // Walk the tab widget and stamp a per-view whatsThis describing
    // the role of that view in the paradigm. The text is the same way
    // users will discover it: each view explains itself when the user
    // presses Shift+F1 anywhere on it.
    if (!m_tabWidget) {
        return;
    }

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        QWidget *page = m_tabWidget->widget(i);
        if (!page) {
            continue;
        }
        const QString tabLabel = m_tabWidget->tabText(i);
        if (tabLabel == tr("Lab Overview")) {
            page->setWhatsThis(tr(
                "<b>Lab Overview</b> &mdash; the home dashboard.<br/>"
                "Use the quick actions to create a Team, browse Roles, "
                "or start a Trial on a project. Double-click a Team row "
                "to jump straight to the Teams editor."));
        } else if (tabLabel == tr("Roles")) {
            page->setWhatsThis(tr(
                "<b>Roles</b> &mdash; the job descriptions and system "
                "prompts for every kind of agent you design.<br/>"
                "Roles are the only place where system prompts live. "
                "Specialists only carry a small override."));
        } else if (tabLabel == tr("Teams")) {
            page->setWhatsThis(tr(
                "<b>Teams</b> &mdash; reusable lineups of Specialists "
                "bound to models, with one or more primaries.<br/>"
                "Use this view to compare variants and diff rendered "
                "<code>opencode.json</code> outputs before applying."));
        } else if (tabLabel == tr("Trials")) {
            page->setWhatsThis(tr(
                "<b>Trials</b> &mdash; the recorded history of Teams "
                "applied to projects, with ratings and notes.<br/>"
                "Select two Trials and click <i>Compare Two Trials</i> "
                "to see them side-by-side, then promote the winner "
                "back to the Teams view."));
        } else if (tabLabel == tr("Projects")) {
            page->setWhatsThis(tr(
                "<b>Projects</b> &mdash; real directories containing "
                "or expecting an <code>opencode.json</code>.<br/>"
                "Scan for projects, then <i>Switch Team</i> to apply "
                "the rendered config (a Trial is recorded on every "
                "switch)."));
        }
    }
}

void MainWindow::createMenus()
{
    QMenuBar *bar = menuBar();
    if (!bar) {
        return;
    }

    // "Edit" menu hosts global preferences. ROADMAP P2-3 added a
    // SettingsDialog; placing it under Edit follows the platform
    // convention (macOS/Windows ship "Edit → Preferences..." /
    // "Tools → Options...").
    m_editMenu = bar->addMenu(tr("Edit"));
    auto *preferencesAction = new QAction(tr("Preferences..."), this);
    preferencesAction->setShortcut(QKeySequence::Preferences);
    m_editMenu->addAction(preferencesAction);
    connect(preferencesAction, &QAction::triggered, this, [this]() {
        // Modal, parented — Accept persists via QSettings in SettingsDialog.
        SettingsDialog dlg(this);
        dlg.exec();
    });

    m_teamsMenu = bar->addMenu(tr("Teams"));

    auto *applyTeamAction = new QAction(tr("Apply Team..."), this);
    m_teamsMenu->addAction(applyTeamAction);

    connect(applyTeamAction, &QAction::triggered, this, [this]() {
        TeamsDialog dlg(m_storageManager, this);
        dlg.exec();
    });

    // P2-4: Help menu. Hosts the paradigm help dialog plus the standard
    // "What's This?" cursor so the per-widget whatsThis text installed
    // by installContextHelp() is actually reachable.
    //
    // We do NOT put Show/Hide paradigm help under View; the canonical
    // place for a help-of-this-app surface is Help, and we keep the
    // toggle model (Show / Hide) so the same action can dismiss the
    // dialog if the user found it via the menu by accident.
    m_helpMenu = bar->addMenu(tr("&Help"));

    m_showParadigmHelpAction = new QAction(tr("Show Paradigm Help..."), this);
    m_showParadigmHelpAction->setShortcut(QKeySequence::HelpContents);
    m_showParadigmHelpAction->setStatusTip(tr(
        "Open the paradigm help window explaining Roles, Specialists, "
        "Teams and Trials."));
    m_helpMenu->addAction(m_showParadigmHelpAction);
    connect(m_showParadigmHelpAction, &QAction::triggered, this, [this]() {
        if (!m_paradigmHelp) {
            showParadigmHelp();
            return;
        }
        if (m_paradigmHelp->isVisible()) {
            m_paradigmHelp->hide();
            m_paradigmHelpAction_setText(tr("Show Paradigm Help..."));
        } else {
            showParadigmHelp();
        }
    });

    // Standard "What's This?" cursor. Picking the menu entry from the
    // Help menu arms the cursor so the next click on any widget in
    // the app shows that widget's per-element whatsThis text (set in
    // installContextHelp + set with individual setWhatsThis calls on
    // buttons and editors across the project).
    auto *whatsThisAction = new QAction(tr("What's This?"), this);
    whatsThisAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
    whatsThisAction->setStatusTip(tr(
        "Click a control to see its description."));
    m_helpMenu->addAction(whatsThisAction);
    connect(whatsThisAction, &QAction::triggered, this, [this]() {
        QWhatsThis::enterWhatsThisMode();
    });

    // P2-5: keyboard shortcut overlay. Help -> "Keyboard Shortcuts..."
    // mirrors the F1 convenience binding so the dialog is reachable
    // from both the menu bar (macOS convention) and the keyboard (PC
    // convention). We deliberately leave the action itself unbound
    // from QKeySequence::HelpContents because F1 already controls
    // the global f1Action below.
    m_showShortcutsAction = new QAction(tr("Keyboard Shortcuts..."), this);
    m_showShortcutsAction->setStatusTip(tr(
        "Show every keyboard shortcut wired up in the app."));
    m_helpMenu->addAction(m_showShortcutsAction);
    connect(m_showShortcutsAction, &QAction::triggered,
            this, &MainWindow::showKeyboardShortcuts);

    // P2-5: F1 application-wide shortcut. Hooked as a top-level
    // action so it works from anywhere in MainWindow, including from
    // widgets that do not have a focused shortcut scope. Re-purposed
    // from the previous WhatsThis binding -- if the user wants to
    // click a control for in-place help, Shift+F1 still does that.
    auto *f1Action = new QAction(this);
    f1Action->setShortcut(QKeySequence(Qt::Key_F1));
    f1Action->setShortcutContext(Qt::ApplicationShortcut);
    addAction(f1Action);
    connect(f1Action, &QAction::triggered,
            this, &MainWindow::showKeyboardShortcuts);
}

void MainWindow::m_paradigmHelpAction_setText(const QString &text)
{
    if (m_showParadigmHelpAction) {
        m_showParadigmHelpAction->setText(text);
    }
}

void MainWindow::maybeShowParadigmHelpOnFirstRun()
{
    // P2-4: first-launch affordance. Only auto-shows the paradigm
    // help the very first time the app runs (or every launch when
    // the user toggled "Show on startup") -- never after the user
    // has explicitly seen the dialog. The flip happens inside
    // ParadigmHelpDialog::consumeFirstRunAutoShow() so the flag
    // flips even if the user closes via the window-close button.
    if (ParadigmHelpDialog::consumeFirstRunAutoShow()) {
        showParadigmHelp();
    }
}

void MainWindow::showParadigmHelp()
{
    // Lazily alloc the dialog as a top-level window owned by MainWindow
    // so it survives tab switches and does not nest inside the tab
    // widget. WA_DeleteOnClose + storing the pointer locally lets us
    // re-show cleanly without leaking; if the user closed it we just
    // allocate a new one on the next show.
    if (m_paradigmHelp) {
        if (!m_paradigmHelp->isVisible()) {
            m_paradigmHelp->show();
            m_paradigmHelp->raise();
            m_paradigmHelp->activateWindow();
        }
        m_paradigmHelpAction_setText(tr("Hide Paradigm Help..."));
        return;
    }

    auto *dlg = new ParadigmHelpDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dlg, &ParadigmHelpDialog::destroyed, this, [this]() {
        m_paradigmHelp = nullptr;
        m_paradigmHelpAction_setText(tr("Show Paradigm Help..."));
    });
    m_paradigmHelp = dlg;
    m_paradigmHelpAction_setText(tr("Hide Paradigm Help..."));
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::showKeyboardShortcuts()
{
    // P2-5: lazy-alloc + WA_DeleteOnClose mirrors the paradigm help
    // dialog handling. The dialog is non-modal by design, so users
    // can keep clicking around in MainWindow while it is open. If
    // the user pressed F1 by accident and the dialog is already up,
    // we re-raise it instead of stacking a second copy -- Qt would
    // happily allow two open, but a single source of truth keeps
    // context consistent with the Help menu's verbs.
    if (m_keyboardShortcuts) {
        if (!m_keyboardShortcuts->isVisible()) {
            m_keyboardShortcuts->show();
            m_keyboardShortcuts->raise();
            m_keyboardShortcuts->activateWindow();
        } else {
            m_keyboardShortcuts->raise();
            m_keyboardShortcuts->activateWindow();
        }
        return;
    }

    auto *dlg = new KeyboardShortcutsDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dlg, &KeyboardShortcutsDialog::destroyed, this, [this]() {
        m_keyboardShortcuts = nullptr;
    });
    m_keyboardShortcuts = dlg;
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}
