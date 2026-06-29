#include "ui/StatusBar.h"

#include <QHBoxLayout>
#include <QLabel>

#include "storage/StorageManager.h"

namespace {

constexpr const char *kObjLastActionLabel = "statusBar.lastActionLabel";
constexpr const char *kObjIndicatorLabel  = "statusBar.indicatorLabel";
constexpr const char *kObjCountsLabel     = "statusBar.countsLabel";

// Soft palette tints for the indicator. Picked so a system-following
// theme still renders readably and dark mode shows legible contrast
// without forcing stylesheets through the rest of the app.
const QColor kWarnColor(196, 152, 0);
const QColor kErrorColor(178, 34, 34);

} // namespace

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("statusBar"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(12);

    // ---- left side: last action ----
    m_lastActionLabel = new QLabel(this);
    m_lastActionLabel->setObjectName(QString::fromLatin1(kObjLastActionLabel));
    m_lastActionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lastActionLabel->setToolTip(tr(
        "Summary of the most recent action. Click to select and copy."));
    m_lastActionLabel->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Preferred);
    layout->addWidget(m_lastActionLabel, 1);

    // ---- middle: indicator ----
    m_indicatorLabel = new QLabel(this);
    m_indicatorLabel->setObjectName(QString::fromLatin1(kObjIndicatorLabel));
    m_indicatorLabel->setAlignment(Qt::AlignCenter);
    m_indicatorLabel->setMinimumWidth(28);
    m_indicatorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_indicatorLabel->setToolTip(tr(
        "Visible when there is a warning or an error to flag. Empty "
        "by default."));
    layout->addWidget(m_indicatorLabel);

    // ---- right side: counts ----
    m_countsLabel = new QLabel(this);
    m_countsLabel->setObjectName(QString::fromLatin1(kObjCountsLabel));
    m_countsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_countsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_countsLabel->setToolTip(tr(
        "Counters for the four model kinds stored under "
        "~/.opencode-meta/roles, /teams, /trials, /projects."));
    layout->addWidget(m_countsLabel);

    renderIndicator();
}

void StatusBar::refreshCountsFromStorage(StorageManager &storageManager)
{
    if (!m_countsLabel) {
        return;
    }

    const int roleCount    = storageManager.listRoles().size();
    const int teamCount    = storageManager.listTeams().size();
    const int trialCount   = storageManager.listTrials().size();
    const int projectCount = storageManager.loadProjects().size();

    m_countsLabel->setText(tr("  R: %1   T: %2   Tr: %3   P: %4  ")
                               .arg(roleCount)
                               .arg(teamCount)
                               .arg(trialCount)
                               .arg(projectCount));
}

void StatusBar::setLastAction(const QString &text)
{
    m_lastAction = text;
    if (m_lastActionLabel) {
        m_lastActionLabel->setText(m_lastAction);
    }
}

void StatusBar::clearLastAction()
{
    setLastAction(QString());
}

void StatusBar::setWarning(const QString &message)
{
    m_indicatorText = message;
    // An empty message is treated as a no-op at the severity layer:
    // the caller must use clearIndicator() to deliberately drop the
    // indicator, so passing "" here should not flip the indicator into
    // "warn" mode without a payload.
    m_severity = message.isEmpty() ? Severity::None : Severity::Warning;
    renderIndicator();
}

void StatusBar::setError(const QString &message)
{
    m_indicatorText = message;
    m_severity = message.isEmpty() ? Severity::None : Severity::Error;
    renderIndicator();
}

void StatusBar::clearIndicator()
{
    m_indicatorText.clear();
    m_severity = Severity::None;
    renderIndicator();
}

QString StatusBar::severityPrefix(Severity s)
{
    switch (s) {
    case Severity::Warning: return QStringLiteral("⚠");
    case Severity::Error:   return QStringLiteral("✕");
    case Severity::None:
    default:                return QString();
    }
}

void StatusBar::renderIndicator()
{
    if (!m_indicatorLabel) {
        return;
    }

    if (m_severity == Severity::None || m_indicatorText.isEmpty()) {
        m_indicatorLabel->clear();
        m_indicatorLabel->setToolTip(QString());
        m_indicatorLabel->setStyleSheet(QString());
        return;
    }

    // Severity here is Warning or Error -- both gated on a non-empty
    // message above. Prefix + message, with a two-space gutter so the
    // glyph and the message stay legible on every platform's default
    // font fallback.
    const QString prefix = severityPrefix(m_severity);
    m_indicatorLabel->setText(QStringLiteral("%1\u2003%2").arg(prefix, m_indicatorText));

    switch (m_severity) {
    case Severity::Warning:
        m_indicatorLabel->setToolTip(tr("Warning: %1").arg(m_indicatorText));
        m_indicatorLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-weight: 600; }")
                .arg(kWarnColor.name()));
        break;
    case Severity::Error:
        m_indicatorLabel->setToolTip(tr("Error: %1").arg(m_indicatorText));
        m_indicatorLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-weight: 600; }")
                .arg(kErrorColor.name()));
        break;
    case Severity::None:
    default:
        m_indicatorLabel->setStyleSheet(QString());
        m_indicatorLabel->setToolTip(QString());
        break;
    }
}
