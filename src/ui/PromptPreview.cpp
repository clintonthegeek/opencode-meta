#include "ui/PromptPreview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace {

// Cheap-but-useful LLM token estimate. Most English-trained tokenizers
// (GPT/Claude family) average ~4 characters per token for natural
// language; we round up so the user sees a conservative ballpark. This
// avoids pulling a real BPE tokenizer into a UI widget while still
// giving a useful "is this 200 tokens or 20k tokens?" sanity check.
int approxTokenCount(const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }
    return (text.size() + 3) / 4;
}

QString renderHeader(const QString &roleName,
                     const QString &roleId,
                     const QString &specialistName,
                     const QString &modelId)
{
    QString title;
    if (!roleName.isEmpty()) {
        title = roleName;
    } else if (!roleId.isEmpty()) {
        title = roleId;
    } else {
        title = QStringLiteral("(no Role)");
    }
    if (!roleId.isEmpty() && roleId != roleName) {
        title += QStringLiteral(" [") + roleId + QStringLiteral("]");
    }

    QString sub;
    if (!specialistName.isEmpty()) {
        sub = specialistName;
    }
    if (!modelId.isEmpty()) {
        if (!sub.isEmpty()) {
            sub += QStringLiteral(" &mdash; ");
        }
        sub += modelId;
    }
    if (sub.isEmpty()) {
        sub = QStringLiteral("(no Specialist bound)");
    }

    return QStringLiteral("<b>Effective Prompt</b> &mdash; %1<br>"
                          "<span style='color: gray;'>%2</span>")
        .arg(title.toHtmlEscaped(), sub.toHtmlEscaped());
}

} // namespace

PromptPreview::PromptPreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PromptPreview"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setText(tr("<b>Effective Prompt</b> "
                              "<span style='color: gray;'>(select a Specialist)</span>"));
    layout->addWidget(m_headerLabel);

    m_viewer = new QPlainTextEdit(this);
    m_viewer->setReadOnly(true);
    m_viewer->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_viewer->setPlaceholderText(tr(
        "The merged system prompt for the selected Specialist will appear here. "
        "Pick a row in the Specialists table above to populate this preview."));
    layout->addWidget(m_viewer, 1);

    auto *footerRow = new QHBoxLayout();
    m_tokenLabel = new QLabel(this);
    m_tokenLabel->setTextFormat(Qt::RichText);
    m_tokenLabel->setText(QStringLiteral("<span style='color: gray;'>"
                                         "approx. &mdash; tokens</span>"));
    footerRow->addWidget(m_tokenLabel);
    footerRow->addStretch(1);
    layout->addLayout(footerRow);

    clear();
}

void PromptPreview::setPreview(const QString &roleName,
                               const QString &roleId,
                               const QString &specialistName,
                               const QString &modelId,
                               const QString &roleSystemPrompt,
                               const QString &overrideText)
{
    if (m_headerLabel) {
        m_headerLabel->setText(renderHeader(roleName, roleId,
                                            specialistName, modelId));
    }

    QString body;
    const QString base = roleSystemPrompt.trimmed();
    const QString over = overrideText.trimmed();

    if (!base.isEmpty()) {
        body += base;
    } else {
        body += QStringLiteral("(Role has no system prompt set)");
    }

    if (!over.isEmpty()) {
        if (!body.isEmpty()) {
            body += QLatin1Char('\n');
        }
        body += QStringLiteral("\n--- Specialist Override ---\n");
        body += over;
    }

    if (m_viewer) {
        m_viewer->setPlainText(body);
    }

    const int tokens = approxTokenCount(body);
    if (m_tokenLabel) {
        m_tokenLabel->setText(tr("<span style='color: gray;'>"
                                 "approx. %1 tokens (%2 chars)</span>")
                                  .arg(tokens)
                                  .arg(body.size()));
    }
}

void PromptPreview::clear()
{
    if (m_headerLabel) {
        m_headerLabel->setText(tr("<b>Effective Prompt</b> "
                                  "<span style='color: gray;'>(select a Specialist)</span>"));
    }
    if (m_viewer) {
        m_viewer->clear();
    }
    if (m_tokenLabel) {
        m_tokenLabel->setText(QStringLiteral("<span style='color: gray;'>"
                                             "approx. &mdash; tokens</span>"));
    }
}
