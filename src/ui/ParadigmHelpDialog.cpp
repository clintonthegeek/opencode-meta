#include "ui/ParadigmHelpDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QLabel>

namespace {

constexpr const char *kKeyShowOnStartup   = "help/paradigm_show_on_startup";
constexpr const char *kKeyParadigmShown   = "help/paradigm_shown";

} // namespace

ParadigmHelpDialog::ParadigmHelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("paradigmHelpDialog"));
    setWindowTitle(tr("OpenCode Meta — Paradigm Help"));
    resize(720, 640);

    buildUi();
}

void ParadigmHelpDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("OpenCode Meta treats your AI lineups as a small laboratory "
           "notebook. This page explains the vocabulary the app speaks "
           "and the loop it expects you to run."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("paradigmHelpDialog.browser"));
    m_browser->setOpenExternalLinks(false);
    m_browser->setHtml(paradigmHtmlBody());
    layout->addWidget(m_browser, 1);

    m_showOnStartupCheck = new QCheckBox(
        tr("Show this dialog automatically on launch."),
        this);
    m_showOnStartupCheck->setObjectName(
        QStringLiteral("paradigmHelpDialog.showOnStartupCheck"));
    m_showOnStartupCheck->setChecked(loadShowOnStartup());
    layout->addWidget(m_showOnStartupCheck);
    connect(m_showOnStartupCheck, &QCheckBox::toggled,
            this, &ParadigmHelpDialog::onShowOnStartupToggled);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok, Qt::Horizontal, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Got it"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        onAccepted();
        accept();
    });
    layout->addWidget(buttonBox);
}

QString ParadigmHelpDialog::keyShowOnStartup()
{
    return QString::fromLatin1(kKeyShowOnStartup);
}

QString ParadigmHelpDialog::keyParadigmShown()
{
    return QString::fromLatin1(kKeyParadigmShown);
}

bool ParadigmHelpDialog::loadShowOnStartup()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(kKeyShowOnStartup), false).toBool();
}

void ParadigmHelpDialog::saveShowOnStartup(bool value)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kKeyShowOnStartup), value);
    settings.sync();
}

bool ParadigmHelpDialog::consumeFirstRunAutoShow()
{
    QSettings settings;

    // Honor an explicit "show on startup" preference first -- if the
    // user toggled it on, we always surface the dialog regardless of
    // whether they have seen it before.
    const bool showOnStartup = settings
        .value(QString::fromLatin1(kKeyShowOnStartup), false)
        .toBool();
    const bool alreadyShown = settings
        .value(QString::fromLatin1(kKeyParadigmShown), false)
        .toBool();

    if (!showOnStartup && alreadyShown) {
        return false;
    }

    settings.setValue(QString::fromLatin1(kKeyParadigmShown), true);
    settings.sync();
    return true;
}

void ParadigmHelpDialog::onShowOnStartupToggled(bool checked)
{
    saveShowOnStartup(checked);
}

void ParadigmHelpDialog::onAccepted()
{
    // Persist the user's checkbox pick even when accepting via
    // "Got it" / Enter so a single click re-opens or hides the
    // dialog exactly as the user expects next time.
    if (m_showOnStartupCheck) {
        saveShowOnStartup(m_showOnStartupCheck->isChecked());
    }
}

QString ParadigmHelpDialog::paradigmHtmlBody()
{
    // Compact HTML derived from docs/PARADIGM.md §1-§2. Kept inline
    // (not loaded from disk) so tests can assert on the body verbatim
    // and so the dialog never breaks when the docs file is missing
    // from a packaged build.
    return QStringLiteral(
        "<html><body>"
        "<h2>The Core Idea</h2>"
        "<p><b>OpenCode Meta is a laboratory notebook for AI coding "
        "teams.</b> You compose named lineups of agents, apply them "
        "to real projects, and record what worked.</p>"

        "<h2>The Vocabulary (Four Words Only)</h2>"
        "<ul>"
        "<li><b>Role</b> &mdash; a job with a system prompt and a "
        "default permission profile. Edited only in the <i>Roles</i> "
        "view. Identified by a stable <code>id</code>; becomes "
        "<code>agent.&lt;id&gt;</code> in <code>opencode.json</code>.</li>"
        "<li><b>Specialist</b> &mdash; a concrete filling of a Role. "
        "Binds one Role to one model, with an optional tiny prompt "
        "override. Specialists are the atomic model-swap unit.</li>"
        "<li><b>Team</b> &mdash; a reusable lineup of Specialists "
        "with one or more primaries. The first primary is the "
        "<code>default_agent</code>.</li>"
        "<li><b>Trial</b> &mdash; a record of applying one Team to one "
        "project, capturing ratings, notes and the exact "
        "<code>opencode.json</code> snapshot that was written.</li>"
        "</ul>"

        "<h2>The 6-Phase Loop</h2>"
        "<ol>"
        "<li><b>Design Roles.</b> In <i>Roles</i>, name the kinds of "
        "agents you need and write their system prompts. Use the "
        "built-in <code>build</code>, <code>plan</code>, "
        "<code>general</code> as starting points.</li>"
        "<li><b>Bind Specialists.</b> In <i>Teams</i>, open a Team "
        "and click <i>Add Specialist&hellip;</i>. Pick a Role and a "
        "model from the live provider catalog.</li>"
        "<li><b>Compose Teams.</b> Group Specialists, mark one or "
        "more as primary, and use <i>Duplicate as Variant</i> to keep "
        "an A/B test alongside the original.</li>"
        "<li><b>Apply Team to Project.</b> In <i>Projects</i>, scan "
        "for a project, then <i>Switch Team</i>. A side-by-side diff "
        "gates the write so you see what changes before "
        "<code>opencode.json</code> is overwritten.</li>"
        "<li><b>Run Trial.</b> Switching the Team records a Trial "
        "automatically. Use the project to do real work; come back "
        "to fill in ratings and notes.</li>"
        "<li><b>Promote Winner / Repeat.</b> In <i>Trials</i>, "
        "compare two Trials side-by-side. The better one becomes the "
        "new active Team for the project, and you go back to step 1 "
        "or 3 with new evidence.</li>"
        "</ol>"

        "<h2>Why Each Piece Exists</h2>"
        "<p>The four-way split keeps the four questions separate:</p>"
        "<ul>"
        "<li>What should the agent <i>think</i>? &rarr; Role.</li>"
        "<li>Which <i>model</i> is thinking it? &rarr; Specialist.</li>"
        "<li>Which <i>combination</i> are we trying? &rarr; Team.</li>"
        "<li>How well did it actually <i>perform</i>? &rarr; Trial.</li>"
        "</ul>"

        "<p>This doc is a summary. The authoritative source is "
        "<code>docs/PARADIGM.md</code> in the project root.</p>"
        "</body></html>"
    );
}
