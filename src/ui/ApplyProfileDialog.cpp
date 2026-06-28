#include "ApplyProfileDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

void populateDiffEditor(QTextEdit *edit,
                        const QStringList &lines,
                        const QVector<bool> &isDifferent,
                        const QColor &diffColor)
{
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

ApplyProfileDialog::ApplyProfileDialog(const QString &scopeDescription,
                                       const QString &warningsText,
                                       const QString &summaryText,
                                       const QString &currentConfigText,
                                       const QString &renderedConfigText,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Apply Profile"));

    auto *mainLayout = new QVBoxLayout(this);

    m_scopeLabel = new QLabel(scopeDescription, this);
    m_scopeLabel->setWordWrap(true);
    mainLayout->addWidget(m_scopeLabel);

    m_warningsLabel = new QLabel(warningsText, this);
    m_warningsLabel->setWordWrap(true);
    mainLayout->addWidget(m_warningsLabel);

    auto *summaryLabel = new QLabel(tr("Summary of changes"), this);
    mainLayout->addWidget(summaryLabel);

    m_summaryEdit = new QTextEdit(this);
    m_summaryEdit->setReadOnly(true);
    m_summaryEdit->setPlainText(summaryText);
    mainLayout->addWidget(m_summaryEdit, 0);

    auto *diffRow = new QHBoxLayout();

    auto *leftColumn = new QVBoxLayout();
    auto *leftLabel = new QLabel(tr("Current config"), this);
    m_currentEdit = new QTextEdit(this);
    m_currentEdit->setReadOnly(true);
    leftColumn->addWidget(leftLabel);
    leftColumn->addWidget(m_currentEdit, 1);

    auto *rightColumn = new QVBoxLayout();
    auto *rightLabel = new QLabel(tr("New config (rendered from profile)"), this);
    m_renderedEdit = new QTextEdit(this);
    m_renderedEdit->setReadOnly(true);
    rightColumn->addWidget(rightLabel);
    rightColumn->addWidget(m_renderedEdit, 1);

    diffRow->addLayout(leftColumn, 1);
    diffRow->addLayout(rightColumn, 1);

    mainLayout->addLayout(diffRow, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ApplyProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ApplyProfileDialog::reject);
    mainLayout->addWidget(buttons);

    populateDiff(currentConfigText, renderedConfigText);
}

void ApplyProfileDialog::populateDiff(const QString &currentConfigText,
                                      const QString &renderedConfigText)
{
    const QStringList currentLines = currentConfigText.split(QLatin1Char('\n'));
    const QStringList renderedLines = renderedConfigText.split(QLatin1Char('\n'));

    const int maxLines = qMax(currentLines.size(), renderedLines.size());
    QVector<bool> differentFlags(maxLines, false);
    for (int i = 0; i < maxLines; ++i) {
        const QString left = (i < currentLines.size()) ? currentLines.at(i) : QString();
        const QString right = (i < renderedLines.size()) ? renderedLines.at(i) : QString();
        if (left != right) {
            differentFlags[i] = true;
        }
    }

    populateDiffEditor(m_currentEdit, currentLines, differentFlags, QColor(255, 200, 200));
    populateDiffEditor(m_renderedEdit, renderedLines, differentFlags, QColor(200, 255, 200));
}
