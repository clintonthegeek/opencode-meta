#include "ui/ConfigInspector.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextStream>
#include <QVBoxLayout>

#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"

namespace {

// Minimal JSON syntax highlighter: keys (string before colon),
// string values, numeric literals, and `true`/`false`/`null`.
// Keys are matched first with a lookahead so the generic-string
// pattern no longer double-tags them.
class JsonHighlighter : public QSyntaxHighlighter
{
public:
    explicit JsonHighlighter(QTextDocument *parent)
        : QSyntaxHighlighter(parent)
    {
        QTextCharFormat keyFormat;
        keyFormat.setForeground(QColor(86, 156, 214));
        keyFormat.setFontWeight(QFont::Bold);
        HighlightingRule keyRule;
        keyRule.pattern = QRegularExpression(QStringLiteral("\"[^\"\\n]*\"(?=\\s*:)"));
        keyRule.format = keyFormat;
        rules.append(keyRule);

        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor(206, 145, 120));
        HighlightingRule stringRule;
        stringRule.pattern = QRegularExpression(QStringLiteral("\"[^\"\\n]*\""));
        stringRule.format = stringFormat;
        rules.append(stringRule);

        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor(181, 206, 168));
        HighlightingRule numberRule;
        numberRule.pattern = QRegularExpression(QStringLiteral("\\b-?\\d+(\\.\\d+)?\\b"));
        numberRule.format = numberFormat;
        rules.append(numberRule);

        QTextCharFormat literalFormat;
        literalFormat.setForeground(QColor(197, 134, 192));
        literalFormat.setFontWeight(QFont::Bold);
        const QStringList literals = {
            QStringLiteral("true"),
            QStringLiteral("false"),
            QStringLiteral("null"),
        };
        for (const QString &lit : literals) {
            HighlightingRule rule;
            rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(lit));
            rule.format = literalFormat;
            rules.append(rule);
        }
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const HighlightingRule &rule : rules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(),
                          match.capturedLength(),
                          rule.format);
            }
        }
    }

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> rules;
};

// Render a Team to its opencode.json QJsonObject shape. Mirrors the
// local helper at TeamEditorWidget.cpp:105 — duplicated here to keep
// the inspector decoupled from the editor's private helpers.
QJsonObject renderConfig(const Team &team, StorageManager &storage)
{
    QSet<QString> specialistIds;
    QSet<QString> roleIds;
    for (const auto &binding : team.specialists) {
        if (!binding.roleId.isEmpty()) {
            roleIds.insert(binding.roleId);
        }
        if (!binding.specialistId.isEmpty()) {
            specialistIds.insert(binding.specialistId);
        }
    }

    QMap<QString, Specialist> specialists;
    for (const QString &specId : std::as_const(specialistIds)) {
        const Specialist s = storage.loadSpecialist(specId);
        if (!s.id.isEmpty()) {
            specialists.insert(s.id, s);
        }
    }

    QMap<QString, Role> roles;
    for (const QString &roleId : std::as_const(roleIds)) {
        const Role r = storage.loadRole(roleId);
        if (!r.id.isEmpty()) {
            roles.insert(r.id, r);
        }
    }

    return TeamRenderer::render(team, specialists, roles);
}

} // namespace

ConfigInspector::ConfigInspector(StorageManager &storageManager, QWidget *parent)
    : QWidget(parent)
    , m_storageManager(storageManager)
{
    setObjectName(QStringLiteral("ConfigInspector"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setText(tr("<b>Rendered opencode.json</b> "
                              "<span style='color: gray;'>(live preview)</span>"));
    layout->addWidget(m_headerLabel);

    m_editor = new QPlainTextEdit(this);
    m_editor->setReadOnly(true);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(qMax(9, mono.pointSize()));
    m_editor->setFont(mono);
    m_editor->setPlaceholderText(tr(
        "Select a Team from the list above to see its rendered opencode.json here."));
    layout->addWidget(m_editor, 1);

    // Synced with the lifetime of m_editor so the highlighter follows
    // QPlainTextEdit::setDocument(...) reassignments safely.
    new JsonHighlighter(m_editor->document());

    auto *buttonRow = new QHBoxLayout();
    m_copyButton = new QPushButton(tr("Copy to Clipboard"), this);
    m_saveButton = new QPushButton(tr("Save as..."), this);
    buttonRow->addWidget(m_copyButton);
    buttonRow->addWidget(m_saveButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(m_copyButton, &QPushButton::clicked,
            this, &ConfigInspector::onCopyToClipboard);
    connect(m_saveButton, &QPushButton::clicked,
            this, &ConfigInspector::onSaveAs);

    // Initial state: no team loaded.
    m_copyButton->setEnabled(false);
    m_saveButton->setEnabled(false);
    setTeam(Team());
}

void ConfigInspector::setTeam(const Team &team)
{
    m_team = team;
    const QString text = renderCurrentText();

    m_editor->setPlainText(text);

    const bool canExport = !m_team.id.isEmpty() && !text.trimmed().isEmpty();
    m_copyButton->setEnabled(canExport);
    m_saveButton->setEnabled(canExport);

    if (m_headerLabel) {
        QString title = team.name.isEmpty() ? team.id : team.name;
        if (title.isEmpty()) {
            m_headerLabel->setText(tr("<b>Rendered opencode.json</b> "
                                      "<span style='color: gray;'>(live preview)</span>"));
        } else {
            m_headerLabel->setText(tr("<b>Rendered opencode.json</b> "
                                      "<span style='color: gray;'>(live preview &mdash; %1)</span>")
                                       .arg(title.toHtmlEscaped()));
        }
    }
}

QString ConfigInspector::renderCurrentText() const
{
    if (m_team.id.isEmpty()) {
        return QString();
    }
    const QJsonObject config = renderConfig(m_team, m_storageManager);
    if (config.isEmpty()) {
        return QString();
    }
    return QString::fromUtf8(
        QJsonDocument(config).toJson(QJsonDocument::Indented));
}

void ConfigInspector::onCopyToClipboard()
{
    const QString text = renderCurrentText();
    if (text.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Copy to Clipboard"),
                                 tr("There is no rendered config to copy yet. "
                                    "Select a Team with at least one Specialist "
                                    "and try again."));
        return;
    }
    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
    }
}

void ConfigInspector::onSaveAs()
{
    const QString text = renderCurrentText();
    if (text.isEmpty()) {
        QMessageBox::information(this,
                                 tr("Save as..."),
                                 tr("There is no rendered config to save yet. "
                                    "Select a Team with at least one Specialist "
                                    "and try again."));
        return;
    }

    QString suggested;
    if (!m_team.id.isEmpty()) {
        suggested = m_team.id + QStringLiteral(".opencode.json");
    } else {
        suggested = QStringLiteral("opencode.json");
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Rendered opencode.json"),
        suggested,
        tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this,
                             tr("Save as..."),
                             tr("Could not open '%1' for writing: %2")
                                 .arg(path, out.errorString()));
        return;
    }
    QTextStream stream(&out);
    stream << text;
    out.close();
}
