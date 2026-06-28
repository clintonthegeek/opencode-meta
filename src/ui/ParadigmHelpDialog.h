// ParadigmHelpDialog.h
//
// ROADMAP P2-4 – context-sensitive paradigm help.
//
// Non-modal help window driven by a QTextBrowser. Shows the core
// opencode paradigm (Labs / Roles / Specialists / Teams / Trials /
// Apply) so first-time users can understand the vocabulary and the
// workflow before they start clicking around. Reachable two ways:
//   1. Help → "Show Paradigm Help..." in the menu bar.
//   2. Asynchronously on first launch (read via QSettings key
//      "help/paradigm_shown" – flipped to true the first time the user
//      dismisses either form of the dialog).
//
// Construction is cheap (no IO): the help body is a small bit of HTML
// derived from docs/PARADIGM.md so it never goes stale against the
// docs in a way the test surface cannot catch. Tooltips on widgets
// across the app reference this dialog via the standard "?" whats-this
// flow (MainWindow installs a QWhatsThis action that opens it).
//
// Settings keys (QSettings, top-level — no group, matches the pattern
// used elsewhere for first-run flags):
//   help/paradigm_shown              bool  – "first run done"
//   help/paradigm_show_on_startup    bool  – user preference

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QTextBrowser;

class ParadigmHelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ParadigmHelpDialog(QWidget *parent = nullptr);
    ~ParadigmHelpDialog() override = default;

    // Pure HTML body the dialog renders. Public so tests can assert
    // that the help text covers the four core entities and the loop.
    static QString paradigmHtmlBody();

    // QSettings helpers -- treat the flag as a single-shot.
    static QString keyShowOnStartup();
    static QString keyParadigmShown();

    static bool   loadShowOnStartup();
    static void   saveShowOnStartup(bool value);

    // First-run: returns true if the help dialog should auto-appear
    // when MainWindow is constructed. Bumps the "shown" flag to true
    // whenever it returns true so we never auto-show twice in a row
    // even if the user closes the dialog via "X" instead of "OK".
    static bool consumeFirstRunAutoShow();

private slots:
    void onShowOnStartupToggled(bool checked);
    void onAccepted();

private:
    void buildUi();

    QTextBrowser *m_browser = nullptr;
    QCheckBox    *m_showOnStartupCheck = nullptr;
};
