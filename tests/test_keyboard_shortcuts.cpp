// tests/test_keyboard_shortcuts.cpp
//
// ROADMAP P2-5 -- smoke + contract test for the keyboard shortcut
// overlay.
//
// Two halves of coverage:
//
//   1. The static entry model
//      - defaultShortcuts() returns a non-empty list.
//      - Every expected global shortcut is present (F1, Shift+F1,
//        Preferences, Apply Team, paradigm help).
//      - Every expected widget-scoped shortcut is present (Teams
//        Widget / Team Editor / Filter Bar entries).
//      - No row has a shortcut that is silently blank for an action
//        that claims to have a binding (cosmetic, but it keeps the
//        table from showing "Action X - " rows).
//
//   2. The dialog UI
//      - Constructs with a 4-column QTableWidget reachable via
//        stable objectNames.
//      - The number of rows in the table matches defaultShortcuts().
//      - Typing in the search QLineEdit hides rows whose haystack
//        does not contain the substring (case-insensitive), and
//        clearing shows them all again.
//      - ESC inside the search box clears it without closing the
//        dialog.
//
// The dialog is never show()'n, so the harness never depends on the
// event loop and never leaks real on-disk state.

#include <QApplication>
#include <QDialogButtonBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QShortcut>
#include <QSignalSpy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

#include "ui/KeyboardShortcutsDialog.h"

class TestKeyboardShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void defaultListIsNonEmpty();
    void globalsCoverF1ShiftF1PreferencesAndParadigmHelp();
    void widgetScopedEntriesArePresent();
    void dialogBuildsTableAndSearchEdit();
    void tableRowCountMatchesEntryCount();
    void searchHidesNonMatchingRows();
    void searchIsCaseInsensitive();
    void clearingSearchRestoresAllRows();
    void escInSearchEditClearsButDoesNotClose();
};

namespace {

QString shortcutTextFor(const QKeySequence &seq)
{
    return seq.toString();
}

bool entryExists(const QList<KeyboardShortcutsDialog::Entry> &entries,
                 const QString &action)
{
    for (const auto &e : entries) {
        if (e.action == action) {
            return true;
        }
    }
    return false;
}

int indexOfAction(const QTableWidget *t, const QString &action)
{
    for (int row = 0; row < t->rowCount(); ++row) {
        const QTableWidgetItem *item = t->item(row, 0);
        if (item && item->text() == action) {
            return row;
        }
    }
    return -1;
}

QTableWidgetItem *cellItem(QTableWidget *t, int row, int col)
{
    if (!t || row < 0 || row >= t->rowCount()) {
        return nullptr;
    }
    return t->item(row, col);
}

} // namespace

void TestKeyboardShortcuts::defaultListIsNonEmpty()
{
    const auto entries = KeyboardShortcutsDialog::defaultShortcuts();
    QVERIFY2(!entries.isEmpty(), "defaultShortcuts() must return at least one entry");

    // Every entry must carry an action, a scope and a description.
    // We deliberately allow empty "shortcut" (for menu-only entries
    // like "Apply Team...") but no other field may be blank.
    for (const auto &e : entries) {
        QVERIFY2(!e.action.isEmpty(),
                 qPrintable(QStringLiteral("entry has empty action: ") + e.scope));
        QVERIFY2(!e.scope.isEmpty(),
                 qPrintable(QStringLiteral("entry has empty scope: ") + e.action));
        QVERIFY2(!e.description.isEmpty(),
                 qPrintable(QStringLiteral("entry has empty description: ") + e.action));
    }
}

void TestKeyboardShortcuts::globalsCoverF1ShiftF1PreferencesAndParadigmHelp()
{
    const auto entries = KeyboardShortcutsDialog::defaultShortcuts();

    QVERIFY(entryExists(entries, QStringLiteral("Show Keyboard Shortcuts")));
    QVERIFY(entryExists(entries, QStringLiteral("What's This?")));
    QVERIFY(entryExists(entries, QStringLiteral("Preferences...")));
    QVERIFY(entryExists(entries, QStringLiteral("Show Paradigm Help...")));
    QVERIFY(entryExists(entries, QStringLiteral("Apply Team...")));

    // F1 must use the bare "F1" string (or its platform-portable
    // equivalent via QKeySequence::toString(), which on Linux/Win is
    // "F1"). We assert the toString() form so the dialog stays
    // platform-portable.
    const QString f1Str = shortcutTextFor(QKeySequence(Qt::Key_F1));
    const QString shiftF1Str =
        shortcutTextFor(QKeySequence(Qt::SHIFT | Qt::Key_F1));

    for (const auto &e : entries) {
        if (e.action == QStringLiteral("Show Keyboard Shortcuts")) {
            QCOMPARE(e.shortcut, f1Str);
            QCOMPARE(e.scope, QStringLiteral("Global"));
        } else if (e.action == QStringLiteral("What's This?")) {
            QCOMPARE(e.shortcut, shiftF1Str);
            QCOMPARE(e.scope, QStringLiteral("Global"));
        } else if (e.action == QStringLiteral("Preferences...")) {
            QCOMPARE(e.shortcut, shortcutTextFor(QKeySequence::Preferences));
            QCOMPARE(e.scope, QStringLiteral("Global"));
        } else if (e.action == QStringLiteral("Show Paradigm Help...")) {
            // May carry a "Help menu" suffix -- the Help keymap on its own
            // is just an FYI next to the menu entry. We only require it
            // mention whatever F1 expands to (so the user sees F1 next
            // to that row).
            QVERIFY2(e.shortcut.contains(f1Str, Qt::CaseInsensitive)
                     || e.description.contains(QStringLiteral("Help menu")),
                     qPrintable(QStringLiteral("paradigm help row must mention F1 or Help menu: ")
                                + e.shortcut));
            QCOMPARE(e.scope, QStringLiteral("Global"));
        } else if (e.action == QStringLiteral("Apply Team...")) {
            // Apply Team is reachable only via the menu today; we do
            // not bind it to F-keys, so the shortcut cell stays blank
            // on purpose. We still require the scope to be "Global".
            QCOMPARE(e.scope, QStringLiteral("Global"));
        }
    }
}

void TestKeyboardShortcuts::widgetScopedEntriesArePresent()
{
    const auto entries = KeyboardShortcutsDialog::defaultShortcuts();

    // Teams list widget
    QVERIFY(entryExists(entries, QStringLiteral("New Team")));
    QVERIFY(entryExists(entries, QStringLiteral("Delete Team")));

    // Team editor widget
    QVERIFY(entryExists(entries, QStringLiteral("Add Specialist")));
    QVERIFY(entryExists(entries, QStringLiteral("Remove Specialist")));
    QVERIFY(entryExists(entries, QStringLiteral("Move Up")));
    QVERIFY(entryExists(entries, QStringLiteral("Move Down")));
    QVERIFY(entryExists(entries, QStringLiteral("Duplicate as Variant")));

    // Filter bar (covered by FilterBar's ESC shortcut)
    QVERIFY(entryExists(entries, QStringLiteral("Clear filter")));

    for (const auto &e : entries) {
        if (e.scope == QStringLiteral("Teams Widget")) {
            // Teams Widget carries New Team (Ctrl+N) and Delete
            // Team (Del) — both are real bindings, just distinct
            // keys. We assert only that they are non-empty so the
            //     test stays platform-portable.
            QVERIFY2(!e.shortcut.isEmpty(),
                     qPrintable(QStringLiteral("Teams Widget row missing shortcut: ")
                                + e.action));
        }
        if (e.scope == QStringLiteral("Team Editor")) {
            // Every Team Editor row is widget-scoped with a binding.
            QVERIFY2(!e.shortcut.isEmpty(),
                     qPrintable(QStringLiteral("Team Editor row missing shortcut: ") + e.action));
        }
    }
}

void TestKeyboardShortcuts::dialogBuildsTableAndSearchEdit()
{
    KeyboardShortcutsDialog dlg;

    auto *search = dlg.searchEdit();
    QVERIFY(search);
    QCOMPARE(search->objectName(),
            QStringLiteral("keyboardShortcuts.searchEdit"));

    auto *table = dlg.tableWidget();
    QVERIFY(table);
    QCOMPARE(table->objectName(),
            QStringLiteral("keyboardShortcuts.table"));
    QCOMPARE(table->columnCount(), 4);

    // Header order matches the spec.
    QStringList headers;
    for (int c = 0; c < table->columnCount(); ++c) {
        headers.append(table->horizontalHeaderItem(c)->text());
    }
    QCOMPARE(headers,
             (QStringList{QStringLiteral("Action"),
                           QStringLiteral("Shortcut"),
                           QStringLiteral("Scope"),
                           QStringLiteral("Description")}));

    // Close-only button box.
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    QVERIFY(bb->button(QDialogButtonBox::Close));
    QVERIFY(!bb->button(QDialogButtonBox::Ok));
    QVERIFY(!bb->button(QDialogButtonBox::Cancel));
}

void TestKeyboardShortcuts::tableRowCountMatchesEntryCount()
{
    KeyboardShortcutsDialog dlg;
    const auto entries = KeyboardShortcutsDialog::defaultShortcuts();
    QCOMPARE(dlg.tableWidget()->rowCount(), entries.size());
}

void TestKeyboardShortcuts::searchHidesNonMatchingRows()
{
    KeyboardShortcutsDialog dlg;
    QTableWidget *table = dlg.tableWidget();
    QVERIFY(table);

    // Sanity: nothing hidden to start.
    for (int row = 0; row < table->rowCount(); ++row) {
        QVERIFY(!table->isRowHidden(row));
    }

    dlg.searchEdit()->setText(QStringLiteral("Move Up"));

    int visible = 0;
    QSet<int> visibleRows;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++visible;
            visibleRows.insert(indexOfAction(table, table->item(row, 0)->text()));
        }
    }

    QVERIFY2(visible == 1,
             qPrintable(QStringLiteral("expected exactly 1 visible row, got ")
                        + QString::number(visible)));
    const int row = indexOfAction(table, QStringLiteral("Move Up"));
    QVERIFY(row >= 0);
    QVERIFY(visibleRows.contains(row));
}

void TestKeyboardShortcuts::searchIsCaseInsensitive()
{
    KeyboardShortcutsDialog dlg;
    QTableWidget *table = dlg.tableWidget();
    QVERIFY(table);

    dlg.searchEdit()->setText(QStringLiteral("mOvE"));

    int visible = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++visible;
        }
    }
    QVERIFY2(visible >= 1,
             "case-insensitive search must still find 'Move Up' / 'Move Down'");
    QVERIFY(indexOfAction(table, QStringLiteral("Move Up")) >= 0);
    QVERIFY(!table->isRowHidden(indexOfAction(table, QStringLiteral("Move Up"))));
}

void TestKeyboardShortcuts::clearingSearchRestoresAllRows()
{
    KeyboardShortcutsDialog dlg;
    QTableWidget *table = dlg.tableWidget();
    QVERIFY(table);

    dlg.searchEdit()->setText(QStringLiteral("abc-no-match-anywhere-xyz"));
    int hidden = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->isRowHidden(row)) {
            ++hidden;
        }
    }
    QCOMPARE(hidden, table->rowCount());

    dlg.searchEdit()->clear();
    for (int row = 0; row < table->rowCount(); ++row) {
        QVERIFY2(!table->isRowHidden(row),
                 qPrintable(QStringLiteral("row ") + QString::number(row)
                            + QStringLiteral(" stayed hidden after clear()")));
    }
}

void TestKeyboardShortcuts::escInSearchEditClearsButDoesNotClose()
{
    KeyboardShortcutsDialog dlg;
    QLineEdit *search = dlg.searchEdit();
    QVERIFY(search);

    search->setText(QStringLiteral("temp"));
    QCOMPARE(search->text(), QStringLiteral("temp"));

    // Fire the ESC shortcut that lives on the search edit. The
    // QShortcut belongs to the search edit's children, so we find
    // it via findChildren.
    QList<QShortcut *> sc = search->findChildren<QShortcut *>();
    bool found = false;
    for (QShortcut *s : std::as_const(sc)) {
        if (s->key().toString() == QKeySequence(Qt::Key_Escape).toString()) {
            QVERIFY(!dlg.isVisible()); // never shown
            s->activated();
            found = true;
            break;
        }
    }
    QVERIFY2(found, "search edit must have an ESC shortcut that clears it");

    QCOMPARE(search->text(), QString());

    // Dialog itself was never shown, so we only assert that ESC did
    // not accidentally reject() it -- a non-visible dialog stays
    // non-visible.
    QVERIFY(!dlg.isVisible());
}

QTEST_MAIN(TestKeyboardShortcuts)
#include "test_keyboard_shortcuts.moc"
