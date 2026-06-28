// KeyboardShortcutsDialog.h
//
// ROADMAP P2-5 -- keyboard shortcut reference overlay.
//
// Pure-viewer non-modal QDialog that lists every shortcut wired up in
// the application so the user can discover and learn them in one
// place. Reachable two ways:
//
//   1. Help -> "Keyboard Shortcuts..." in the menu bar
//   2. F1 application-wide shortcut from MainWindow
//
// The dialog intentionally is INPUT-ONLY (no actions, no signals
// other than those Qt owns). It does not mutate any model or any
// on-disk file. Closing it (Close button, ESC, window close) just
// dismisses.
//
// What you see
// ------------
// A search QLineEdit at the top followed by a 4-column QTableWidget:
//   * Action     -- human-readable action name (e.g. "New Team")
//   * Shortcut   -- the keys (e.g. "Ctrl+N", "F1", "Shift+F1")
//   * Scope      -- where it applies (Global / Teams Widget / Filter Bar)
//   * Description -- one-line explainer of what it does
//
// Search semantics
// ----------------
// As the user types in the search box, rows are hidden whenever none
// of the four cells contain the typed substring (case-insensitive).
// Clearing the field restores every row. ESC inside the search box
// clears it (not the dialog) -- ESC pressed anywhere else closes the
// dialog via the standard QDialog reject() path.
//
// Test surface
// ------------
// Tests reach the table, search box, and the static entry model via
// stable objectNames and the public static defaultShortcuts() helper.
// The dialog is never shown() during testing -- the harness edits
// state through the widget tree directly and asserts on row counts
// and contents.

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

class KeyboardShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    // One row in the shortcut reference table. Kept as a plain POD so
    // tests can call defaultShortcuts() and compare without spinning
    // up the dialog UI.
    struct Entry {
        QString action;       // e.g. "New Team"
        QString shortcut;     // e.g. "Ctrl+N"  (display form)
        QString scope;        // e.g. "Teams Widget"
        QString description;  // e.g. "Create a new Team"
    };

    explicit KeyboardShortcutsDialog(QWidget *parent = nullptr);
    ~KeyboardShortcutsDialog() override = default;

    // Canonical, hard-coded shortcut list. Public + static so tests
    // can drive the same source of truth without going through the
    // UI. Adding a new shortcut in the app means adding it here too.
    static QList<Entry> defaultShortcuts();

    // Read-only accessors for tests (mirrors the role_editor_dialog
    // pattern). Returns nullptr if a widget failed to build -- callers
    // should handle that with QVERIFY rather than dereferencing.
    QLineEdit    *searchEdit() const   { return m_searchEdit; }
    QTableWidget *tableWidget() const  { return m_table; }

private slots:
    void applyFilter(const QString &needle);

private:
    void buildUi();
    void populateTable();
    static QString entryToSearchHaystack(const Entry &e);

    QLineEdit    *m_searchEdit = nullptr;
    QTableWidget *m_table      = nullptr;

    QList<Entry>  m_entries;
};
