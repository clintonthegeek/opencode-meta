#include "ui/ConfirmApplyDialog.h"

#include <QColor>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "storage/StorageManager.h"

namespace {

// Render `team` against the Specialists and Roles referenced by its
// bindings. Mirrors ProjectsWidget::renderTeamConfig() so callers see
// the exact same JSON StorageManager::applyTeamToProject() would write.
QJsonObject renderTeamConfig(const Team &team, StorageManager &storage)
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

ConfirmApplyDialog::ConfirmApplyDialog(const QString &projectPath,
                                       const Team &team,
                                       StorageManager &storage,
                                       const QString &currentText,
                                       bool currentIsJson,
                                       QWidget *parent)
    : QDialog(parent)
    , m_projectPath(projectPath)
    , m_teamId(team.id)
    , m_teamName(team.name.isEmpty() ? team.id : team.name)
{
    const QString windowSuffix = m_teamName.isEmpty()
                                     ? QString()
                                     : QStringLiteral(": ") + m_teamName;
    setWindowTitle(tr("Confirm Apply%1").arg(windowSuffix));
    resize(1050, 620);

    auto *mainLayout = new QVBoxLayout(this);

    // Header — repeats what was clicked, so the user is never in doubt
    // about what they are confirming.
    auto *header = new QLabel(
        tr("<b>Applying Team:</b> %1<br/><b>Project:</b> %2")
            .arg(m_teamName.toHtmlEscaped(),
                 m_projectPath.toHtmlEscaped()),
        this);
    header->setTextFormat(Qt::RichText);
    header->setWordWrap(true);
    mainLayout->addWidget(header);

    // Render the post-apply JSON so the right pane and the file write
    // are guaranteed to use the exact same bytes.
    const QJsonObject config = renderTeamConfig(team, storage);
    m_renderedText = QString::fromUtf8(
        QJsonDocument(config).toJson(QJsonDocument::Indented));

    // Banner — explains exactly what Accept will do to disk.
    if (!currentText.isEmpty() && !currentIsJson) {
        m_summaryText = tr(
            "The existing opencode.json is not valid JSON. A timestamped "
            ".bak will still be written before the new config replaces it.");
    } else if (!currentText.isEmpty()) {
        m_summaryText = tr(
            "An opencode.json already exists in this project. Accept will "
            "create a timestamped .bak backup and then write the Team's "
            "rendered JSON in its place.");
    } else {
        m_summaryText = tr(
            "No opencode.json was found in this project. Accept will create "
            "a new one from the Team's rendered JSON.");
    }

    auto *summary = new QLabel(m_summaryText, this);
    summary->setWordWrap(true);
    summary->setObjectName(QStringLiteral("confirmApply.summary"));
    QPalette summaryPalette = summary->palette();
    if (!currentText.isEmpty() && !currentIsJson) {
        summaryPalette.setColor(QPalette::WindowText, QColor(180, 80, 0));
    } else if (!currentText.isEmpty()) {
        summaryPalette.setColor(QPalette::WindowText, QColor(170, 30, 30));
    } else {
        summaryPalette.setColor(QPalette::WindowText, QColor(30, 90, 170));
    }
    summary->setPalette(summaryPalette);
    mainLayout->addWidget(summary);

    // Side-by-side diff. Left = current file (or placeholder); right =
    // rendered Team JSON. Thin lines tinted red on the left and green
    // on the right mark rows that differ.
    auto *diffGroup = new QGroupBox(tr("Diff (current &rarr; rendered)"), this);
    auto *diffLayout = new QHBoxLayout(diffGroup);

    auto *leftEdit = new QTextEdit(diffGroup);
    leftEdit->setObjectName(QStringLiteral("confirmApply.leftEdit"));
    leftEdit->setToolTip(tr(
        "Left pane: the project's on-disk opencode.json as of the "
        "current moment. Read-only."));
    auto *rightEdit = new QTextEdit(diffGroup);
    rightEdit->setObjectName(QStringLiteral("confirmApply.rightEdit"));
    rightEdit->setToolTip(tr(
        "Right pane: the JSON the Team will write if you click Apply. "
        "Read-only. Identical to what StorageManager::applyTeamToProject "
        "writes."));

    const QStringList currentLines = currentText.isEmpty()
                                         ? QStringList{ QStringLiteral("(no existing opencode.json)") }
                                         : currentText.split(QLatin1Char('\n'));
    const QStringList renderedLines = m_renderedText.split(QLatin1Char('\n'));

    const int maxLines = qMax(currentLines.size(), renderedLines.size());
    QVector<bool> differentFlags(maxLines, false);
    for (int i = 0; i < maxLines; ++i) {
        const QString left = (i < currentLines.size()) ? currentLines.at(i) : QString();
        const QString right = (i < renderedLines.size()) ? renderedLines.at(i) : QString();
        if (left != right) {
            differentFlags[i] = true;
        }
    }

    populateDiffEditor(leftEdit, currentLines, differentFlags, QColor(255, 210, 210));
    populateDiffEditor(rightEdit, renderedLines, differentFlags, QColor(210, 255, 210));

    diffLayout->addWidget(leftEdit);
    diffLayout->addWidget(rightEdit);
    mainLayout->addWidget(diffGroup, 1);
    diffGroup->setToolTip(tr(
        "Lines highlighted in red (left) / green (right) are the "
        "ones that will change on disk if you click Apply."));

    // Standard OK / Cancel — Accept closes with QDialog::Accepted so
    // the caller can branch on the result of exec().
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                           Qt::Horizontal, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    buttonBox->button(QDialogButtonBox::Ok)->setToolTip(tr(
        "Write the rendered JSON to the project's opencode.json. A "
        "timestamped .bak file is created first if a current file "
        "exists."));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr(
        "Dismiss without writing anything to disk. No file IO, no "
        "Trial recorded."));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void ConfirmApplyDialog::populateDiffEditor(QTextEdit *edit,
                                            const QStringList &lines,
                                            const QVector<bool> &isDifferent,
                                            const QColor &diffColor)
{
    if (!edit) {
        return;
    }

    edit->clear();
    edit->setReadOnly(true);

    QTextCharFormat normalFormat;
    QTextCharFormat diffFormat;
    diffFormat.setBackground(diffColor);

    QTextDocument *doc = edit->document();
    QTextCursor cursor(doc);

    for (int i = 0; i < lines.size(); ++i) {
        const bool different = (i < isDifferent.size()) ? isDifferent[i] : false;
        cursor.setCharFormat(different ? diffFormat : normalFormat);
        cursor.insertText(lines.at(i));
        if (i + 1 < lines.size()) {
            cursor.insertBlock();
        }
    }
}
