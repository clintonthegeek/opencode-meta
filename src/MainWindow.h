#pragma once

#include <QMainWindow>

#include "storage/StorageManager.h"

class KeyboardShortcutsDialog;
class ParadigmHelpDialog;
class QAction;
class QMenu;
class QTabWidget;

// MainWindow: top-level shell of the opencode-meta-qt app.
//
// Five-view navigation (Lab Overview, Roles, Teams, Trials, Projects)
// plus File / Edit / Teams / Help menus, a paradigm help dialog
// surfaced from Help -> "Show Paradigm Help..." (ROADMAP P2-4), and
// a keyboard shortcut reference overlay reachable via Help ->
// "Keyboard Shortcuts..." and F1 (ROADMAP P2-5).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    void createMenus();
    void installContextHelp();
    void maybeShowParadigmHelpOnFirstRun();
    void showParadigmHelp();
    void m_paradigmHelpAction_setText(const QString &text);

    // P2-5: lazy show the KeyboardShortcutsDialog so Help -> "Keyboard
    // Shortcuts..." and F1 share one allocation & dialog lifetime.
    void showKeyboardShortcuts();

    StorageManager m_storageManager;
    QTabWidget *m_tabWidget = nullptr;
    QMenu *m_teamsMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_helpMenu = nullptr;

    // P2-4: track the paradigm help dialog so the toggle action stays
    // in sync with the window's visibility, and the dialog is deleted
    // automatically when the QDialog closes (WA_DeleteOnClose set in
    // showParadigmHelp()).
    ParadigmHelpDialog *m_paradigmHelp = nullptr;
    QAction *m_showParadigmHelpAction = nullptr;

    // P2-5: same lazy-alloc + WA_DeleteOnClose pattern as the
    // paradigm help dialog.
    KeyboardShortcutsDialog *m_keyboardShortcuts = nullptr;
    QAction *m_showShortcutsAction = nullptr;
};
