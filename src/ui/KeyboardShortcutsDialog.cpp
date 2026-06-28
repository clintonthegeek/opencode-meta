#include "ui/KeyboardShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

constexpr const char *kObjSearchEdit       = "keyboardShortcuts.searchEdit";
constexpr const char *kObjTable            = "keyboardShortcuts.table";

// Display strings for QKeySequence::StandardKey values. Each helper
// keeps the shortcut printable in the dialog without losing the
// platform-portable semantic the QKeySequence enum carries.
QString preferencesShortcut()
{
    return QKeySequence(QKeySequence::Preferences).toString();
}

QString helpContentsShortcut()
{
    return QKeySequence(QKeySequence::HelpContents).toString();
}

} // namespace

KeyboardShortcutsDialog::KeyboardShortcutsDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("keyboardShortcutsDialog"));
    setWindowTitle(tr("Keyboard Shortcuts"));
    // Non-modal by design: MainWindow pops the dialog with show() so
    // the user can still interact with the rest of the app.
    setModal(false);
    resize(820, 540);

    m_entries = defaultShortcuts();

    buildUi();
    populateTable();
}

void KeyboardShortcutsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Every shortcut wired up in OpenCode Meta. Search filters "
           "rows by action, keys, scope, or description."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *searchRow = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);

    auto *searchLabel = new QLabel(tr("&Search:"), searchRow);
    searchLayout->addWidget(searchLabel);

    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setObjectName(QString::fromLatin1(kObjSearchEdit));
    m_searchEdit->setPlaceholderText(tr("Filter by action, shortcut, scope, or description"));
    m_searchEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(m_searchEdit, 1);
    searchLabel->setBuddy(m_searchEdit);

    // ESC inside the search box clears the filter; it does NOT close
    // the dialog because the user is still mid-edit. Closing the
    // dialog via ESC happens at the dialog level (standard QDialog
    // reject path).
    auto *clearShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchEdit);
    clearShortcut->setContext(Qt::WidgetShortcut);
    connect(clearShortcut, &QShortcut::activated, m_searchEdit, &QLineEdit::clear);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &KeyboardShortcutsDialog::applyFilter);

    layout->addWidget(searchRow);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QString::fromLatin1(kObjTable));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({
        tr("Action"),
        tr("Shortcut"),
        tr("Scope"),
        tr("Description"),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Stretch the description column so multi-line explanations are
    // legible; the other three stay size-hint-sized.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    // Standard QDialogButtonBox with only Close — the dialog is
    // read-only so we deliberately avoid OK/Cancel that would imply
    // a write side-effect.
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

void KeyboardShortcutsDialog::populateTable()
{
    if (!m_table) {
        return;
    }

    m_table->setRowCount(0);
    m_table->setRowCount(m_entries.size());

    for (int row = 0; row < m_entries.size(); ++row) {
        const Entry &e = m_entries.at(row);

        auto *actionItem = new QTableWidgetItem(e.action);
        auto *keyItem    = new QTableWidgetItem(e.shortcut);
        auto *scopeItem  = new QTableWidgetItem(e.scope);
        auto *descItem   = new QTableWidgetItem(e.description);

        // Prevent users from accidentally editing cells in the table.
        for (QTableWidgetItem *item : { actionItem, keyItem, scopeItem, descItem }) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }

        m_table->setItem(row, 0, actionItem);
        m_table->setItem(row, 1, keyItem);
        m_table->setItem(row, 2, scopeItem);
        m_table->setItem(row, 3, descItem);
    }
}

QString KeyboardShortcutsDialog::entryToSearchHaystack(const Entry &e)
{
    // One flattened, lowercased string drives the case-insensitive
    // substring match. We deliberately paste all four fields into a
    // single string so users can type "ctrl" and still find rows
    // whose description just mentions "Ctrl" (no per-column picker).
    return (e.action + QLatin1Char(' ')
            + e.shortcut + QLatin1Char(' ')
            + e.scope + QLatin1Char(' ')
            + e.description).toLower();
}

void KeyboardShortcutsDialog::applyFilter(const QString &needle)
{
    if (!m_table) {
        return;
    }

    const QString needleLower = needle.trimmed().toLower();

    for (int row = 0; row < m_table->rowCount(); ++row) {
        const Entry &e = m_entries.at(row);
        const bool matches = needleLower.isEmpty()
                             || entryToSearchHaystack(e).contains(needleLower);
        m_table->setRowHidden(row, !matches);
    }
}

QList<KeyboardShortcutsDialog::Entry>
KeyboardShortcutsDialog::defaultShortcuts()
{
    QList<Entry> out;

    // ---- Global / MainWindow scope ----
    out.append({
        tr("Show Keyboard Shortcuts"),
        QKeySequence(Qt::Key_F1).toString(),
        tr("Global"),
        tr("Open this overlay (F1 from anywhere in the app)."),
    });
    out.append({
        tr("What's This?"),
        QKeySequence(Qt::SHIFT | Qt::Key_F1).toString(),
        tr("Global"),
        tr("Click-to-discover: pick this then click any control to "
           "see its description."),
    });
    out.append({
        tr("Preferences..."),
        preferencesShortcut(),
        tr("Global"),
        tr("Open the application Preferences dialog (paths and theme)."),
    });
    out.append({
        tr("Show Paradigm Help..."),
        helpContentsShortcut() + QLatin1String("  (Help menu)"),
        tr("Global"),
        tr("Open the paradigm help window explaining Roles, "
           "Specialists, Teams and Trials."),
    });
    out.append({
        tr("Apply Team..."),
        QString(), // No shortcut today — kept as a row so users can
                   // confirm the menu entry they have been clicking.
        tr("Global"),
        tr("Open the Teams picker to apply a Team to a project."),
    });

    // ---- Teams widget (Teams view list of Teams) ----
    out.append({
        tr("New Team"),
        QKeySequence(QStringLiteral("Ctrl+N")).toString(),
        tr("Teams Widget"),
        tr("Create a new Team (widget-scoped -- safe to use while the "
           "Teams tab is focused)."),
    });
    out.append({
        tr("Delete Team"),
        QKeySequence(QKeySequence::Delete).toString(),
        tr("Teams Widget"),
        tr("Delete the currently selected Team (Delete key on the "
           "Teams table)."),
    });

    // ---- Team editor (Teams view editor of one Team) ----
    out.append({
        tr("Add Specialist"),
        QKeySequence(QStringLiteral("Ctrl+N")).toString(),
        tr("Team Editor"),
        tr("Add a new Specialist to the Team currently being edited "
           "(widget-scoped)."),
    });
    out.append({
        tr("Remove Specialist"),
        QKeySequence(QKeySequence::Delete).toString(),
        tr("Team Editor"),
        tr("Remove the selected Specialist from the Team currently "
           "being edited."),
    });
    out.append({
        tr("Move Up"),
        QKeySequence(QStringLiteral("Ctrl+Up")).toString(),
        tr("Team Editor"),
        tr("Move the selected Specialist up in the lineup."),
    });
    out.append({
        tr("Move Down"),
        QKeySequence(QStringLiteral("Ctrl+Down")).toString(),
        tr("Team Editor"),
        tr("Move the selected Specialist down in the lineup."),
    });
    out.append({
        tr("Duplicate as Variant"),
        QKeySequence(QStringLiteral("Ctrl+D")).toString(),
        tr("Team Editor"),
        tr("Duplicate the current Team as a new variant for A/B "
           "comparison."),
    });

    // ---- Filter bars (Roles / Teams / Trials / Projects) ----
    out.append({
        tr("Clear filter"),
        QKeySequence(Qt::Key_Escape).toString(),
        tr("Filter Bar"),
        tr("Clear the active filter in any list's search box "
           "(Escape while focused in the filter input)."),
    });

    return out;
}
