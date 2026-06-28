#include "ui/TrialCompareDialog.h"

#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVector>

#include "generation/TeamRenderer.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "models/Trial.h"
#include "storage/StorageManager.h"

namespace {

// Render the opencode.json that the given Team would produce given
// the current Roles + Specialists on disk. Mirrors the renderer in
// ConfirmApplyDialog.cpp so the diff panes look identical whether
// the user is comparing Trials or previewing an Apply.
//
// Returns an empty QJsonObject if the Team can't be loaded — callers
// detect that and render a placeholder instead of crashing.
QJsonObject renderTeamConfig(const Team &team, StorageManager &storage)
{
    if (team.id.isEmpty()) {
        return QJsonObject();
    }

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

// Pick the JSON to show for one side. Snapshot wins because it is
// the only representation that survives Roles / Specialists being
// drifted after the Trial was recorded.
QString renderedTextForTrial(const Trial &trial, StorageManager &storage)
{
    if (!trial.renderedConfigSnapshot.isEmpty()) {
        return QString::fromUtf8(
            QJsonDocument(trial.renderedConfigSnapshot)
                .toJson(QJsonDocument::Indented));
    }

    if (!trial.teamId.isEmpty()) {
        const Team team = storage.loadTeam(trial.teamId);
        const QJsonObject reRendered = renderTeamConfig(team, storage);
        if (!reRendered.isEmpty()) {
            return QString::fromUtf8(
                QJsonDocument(reRendered).toJson(QJsonDocument::Indented));
        }
    }

    return QStringLiteral("(no rendered config available for this trial)");
}

// Compact "key=value, key=value" preview of the ratings object —
// the same shape TrialsWidget already uses in its table cell so users
// recognize it immediately.
QString summarizeRatings(const QJsonObject &ratings)
{
    if (ratings.isEmpty()) {
        return QStringLiteral("n/a");
    }

    QStringList parts;
    const auto keys = ratings.keys();
    for (int i = 0; i < keys.size() && i < 4; ++i) {
        const QString &key = keys.at(i);
        const QJsonValue value = ratings.value(key);
        if (value.isDouble()) {
            parts.append(QStringLiteral("%1=%2").arg(key).arg(value.toDouble()));
        } else if (value.isString()) {
            parts.append(QStringLiteral("%1=%2").arg(key, value.toString()));
        }
    }

    if (parts.isEmpty()) {
        return QStringLiteral("n/a");
    }

    if (keys.size() > parts.size()) {
        parts.append(QStringLiteral("…"));
    }

    return parts.join(QStringLiteral(", "));
}

// Resolve the human-friendly Team label for the trial's metadata
// header. Mirrors TrialsWidget::refreshTrials() so the dialog never
// disagrees with the row the user just clicked.
QString teamLabelForTrial(const Trial &trial, StorageManager &storage)
{
    if (trial.teamId.isEmpty()) {
        return QStringLiteral("(none)");
    }
    const Team team = storage.loadTeam(trial.teamId);
    if (!team.id.isEmpty()) {
        if (!team.name.isEmpty()) {
            return QStringLiteral("%1 (%2)").arg(team.name, team.id);
        }
        return team.id;
    }
    return trial.teamId;
}

// One-line metadata strip rendered above each diff pane. Built from
// the same fields TrialsWidget already shows in its row plus
// duration when available, so users see the same data twice — once
// compact in the table, once in detail at the top of the dialog.
QString buildMetadataHeader(const Trial &trial, StorageManager &storage)
{
    const QString ts = trial.timestamp.isValid()
                          ? trial.timestamp.toString(Qt::ISODate)
                          : QStringLiteral("(unknown)");

    const QString project = trial.projectPath.isEmpty()
                                ? QStringLiteral("(none)")
                                : trial.projectPath;

    const QString duration = trial.durationMinutes >= 0
                                 ? QStringLiteral("%1 min").arg(trial.durationMinutes)
                                 : QStringLiteral("n/a");

    const QString notes = trial.notes.isEmpty()
                              ? QStringLiteral("(none)")
                              : trial.notes;

    QString html;
    html += QStringLiteral("<b>Trial ID:</b> %1<br/>")
                .arg(trial.id.isEmpty() ? QStringLiteral("(unknown)")
                                        : trial.id.toHtmlEscaped());
    html += QStringLiteral("<b>Team:</b> %1<br/>")
                .arg(teamLabelForTrial(trial, storage).toHtmlEscaped());
    html += QStringLiteral("<b>Project:</b> %1<br/>")
                .arg(project.toHtmlEscaped());
    html += QStringLiteral("<b>Timestamp:</b> %1<br/>")
                .arg(ts.toHtmlEscaped());
    html += QStringLiteral("<b>Duration:</b> %1<br/>")
                .arg(duration.toHtmlEscaped());
    html += QStringLiteral("<b>Ratings:</b> %1<br/>")
                .arg(summarizeRatings(trial.ratings).toHtmlEscaped());
    html += QStringLiteral("<b>Notes:</b> %1")
                .arg(notes.toHtmlEscaped());

    return html;
}

// Populate one side of the diff. Mirrors
// ConfirmApplyDialog::populateDiffEditor() line-for-line so the visual
// vocabulary (white = unchanged, tinted = changed) is identical.
void populateDiffEditor(QTextEdit *edit,
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

} // namespace

TrialCompareDialog::TrialCompareDialog(const QString &leftId,
                                       const QString &rightId,
                                       StorageManager &storage,
                                       QWidget *parent)
    : QDialog(parent)
    , m_leftId(leftId)
    , m_rightId(rightId)
{
    setWindowTitle(tr("Compare Trials"));
    resize(1100, 640);

    auto *mainLayout = new QVBoxLayout(this);

    Trial leftTrial = storage.loadTrial(leftId);
    if (leftTrial.id.isEmpty()) {
        // Fall back to a placeholder record so the metadata header
        // still renders something coherent instead of crashing.
        leftTrial.id = leftId;
    }
    Trial rightTrial = storage.loadTrial(rightId);
    if (rightTrial.id.isEmpty()) {
        rightTrial.id = rightId;
    }

    m_leftRenderedText = renderedTextForTrial(leftTrial, storage);
    m_rightRenderedText = renderedTextForTrial(rightTrial, storage);

    // Banner — short context line at the top so the user never has
    // to guess which side is which.
    auto *banner = new QLabel(
        tr("<b>Comparing Trials:</b> %1 &harr; %2<br/>"
           "Lines highlighted in red (left) and green (right) differ between the two rendered configs.")
            .arg(leftTrial.id.toHtmlEscaped(), rightTrial.id.toHtmlEscaped()),
        this);
    banner->setTextFormat(Qt::RichText);
    banner->setWordWrap(true);
    mainLayout->addWidget(banner);

    auto *diffGroup = new QGroupBox(tr("Diff"), this);
    auto *diffLayout = new QHBoxLayout(diffGroup);

    auto *leftColumn = new QVBoxLayout();
    auto *rightColumn = new QVBoxLayout();

    auto *leftHeader = new QLabel(this);
    leftHeader->setTextFormat(Qt::RichText);
    leftHeader->setText(buildMetadataHeader(leftTrial, storage));
    leftHeader->setWordWrap(true);
    leftColumn->addWidget(leftHeader);

    auto *rightHeader = new QLabel(this);
    rightHeader->setTextFormat(Qt::RichText);
    rightHeader->setText(buildMetadataHeader(rightTrial, storage));
    rightHeader->setWordWrap(true);
    rightColumn->addWidget(rightHeader);

    auto *leftEdit = new QTextEdit(diffGroup);
    leftEdit->setObjectName(QStringLiteral("trialCompare.leftEdit"));
    auto *rightEdit = new QTextEdit(diffGroup);
    rightEdit->setObjectName(QStringLiteral("trialCompare.rightEdit"));

    const QStringList leftLines = m_leftRenderedText.split(QLatin1Char('\n'));
    const QStringList rightLines = m_rightRenderedText.split(QLatin1Char('\n'));

    const int maxLines = qMax(leftLines.size(), rightLines.size());
    QVector<bool> differentFlags(maxLines, false);
    for (int i = 0; i < maxLines; ++i) {
        const QString left = (i < leftLines.size()) ? leftLines.at(i) : QString();
        const QString right = (i < rightLines.size()) ? rightLines.at(i) : QString();
        if (left != right) {
            differentFlags[i] = true;
        }
    }

    populateDiffEditor(leftEdit, leftLines, differentFlags, QColor(255, 210, 210));
    populateDiffEditor(rightEdit, rightLines, differentFlags, QColor(210, 255, 210));

    leftColumn->addWidget(leftEdit, 1);
    rightColumn->addWidget(rightEdit, 1);

    diffLayout->addLayout(leftColumn);
    diffLayout->addLayout(rightColumn);

    mainLayout->addWidget(diffGroup, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &TrialCompareDialog::reject);
    mainLayout->addWidget(buttonBox);
}
