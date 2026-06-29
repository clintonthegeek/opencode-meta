// StatusBar.h
//
// ROADMAP P0-3 -- persistent status bar for opencode-meta-qt.
//
// Provides three labelled indicators that always stay visible across
// every view, plus a transient message slot so the host can announce
// successes without popping a modal QMessageBox.
//
// Layout (left-to-right inside the QStatusBar's permanent-widget slot):
//
//   [ last action ]    [ ⚠ / ✕  indicator ]    [ R: N  T: N  Tr: N  P: N ]
//
// * lastActionLabel: tiny one-liner summarizing the most recent user
//   action ("Created Role 'build'", "Deleted Team 'foo'", "Loaded
//   Specialists in 12 ms"). Cleared via clearLastAction() or replaced
//   on every subsequent action. Replaces writing to the temporary
//   QStatusBar message area so the user always has a visible record
//   of the last thing they did.
//
// * indicatorLabel: a fixed-width coloured label that shows ⚠ for
//   warnings, ✕ for errors, and stays empty when there is nothing to
//   flag. Warnings paint yellow, errors paint red. Every call into
//   setWarning / setError / clearIndicator replaces the prior state
//   (errors take priority over warnings).
//
// * countsLabel: a permanent compact summary of model size, updated
//   by refreshCountsFromStorage(). Format is platform-portable so a
//   user on any Qt6 platform sees the same string.
//
// Tests reach every visible child through stable objectNames; the
// widget itself is never shown() so the harness can inspect labels
// directly without spinning up a window.

#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class StorageManager;

class StatusBar : public QWidget
{
    Q_OBJECT

public:
    // Severity for the indicator label. Errors take priority over
    // warnings; setting Warning after Error demotes the indicator
    // back to "warn" mode. Clear resets to no indicator at all.
    enum class Severity {
        None    = 0,
        Warning = 1,
        Error   = 2,
    };

    explicit StatusBar(QWidget *parent = nullptr);
    ~StatusBar() override = default;

    // Re-read roles/teams/trials/projects from the given storage
    // manager and refresh the counts label. Safe to call from a
    // signal handler in the GUI thread.
    void refreshCountsFromStorage(StorageManager &storageManager);

    // Update the "last action" persistent label. An empty string
    // clears it.
    void setLastAction(const QString &text);
    void clearLastAction();

    // Show/hide the coloured indicator. The label keeps its slot in
    // the layout when cleared so the bar does not visually jump as
    // indicators come and go (matching the ROADMAP "smooth feedback"
    // intent).
    void setWarning(const QString &message);
    void setError(const QString &message);
    void clearIndicator();

    Severity severity() const { return m_severity; }

    // Read-only accessors for tests. Returns nullptr only if a child
    // widget failed to build -- callers must QVERIFY rather than
    // dereference.
    QLabel *countsLabel()     const { return m_countsLabel; }
    QLabel *lastActionLabel() const { return m_lastActionLabel; }
    QLabel *indicatorLabel()  const { return m_indicatorLabel; }

private:
    void renderIndicator();
    static QString severityPrefix(Severity s);

    QLabel *m_lastActionLabel = nullptr;
    QLabel *m_indicatorLabel  = nullptr;
    QLabel *m_countsLabel     = nullptr;

    QString m_lastAction;
    QString m_indicatorText;
    Severity m_severity = Severity::None;
};
