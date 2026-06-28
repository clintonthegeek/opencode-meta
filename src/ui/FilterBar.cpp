#include "ui/FilterBar.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QShortcut>

bool FilterProxyModel::acceptsRow(int sourceRow,
                                   const QModelIndex &sourceParent) const
{
    return filterAcceptsRow(sourceRow, sourceParent);
}

FilterBar::FilterBar(QWidget *parent)
    : FilterBar(QString(), parent)
{
}

FilterBar::FilterBar(const QString &placeholderText, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_edit = new QLineEdit(this);
    const QString placeholder = placeholderText.isEmpty()
                                    ? tr("Filter...")
                                    : placeholderText;
    m_edit->setPlaceholderText(placeholder);
    m_edit->setClearButtonEnabled(true);
    layout->addWidget(m_edit, 1);

    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_edit);
    esc->setContext(Qt::WidgetShortcut);
    connect(esc, &QShortcut::activated, m_edit, &QLineEdit::clear);

    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::filterChanged);
}

QString FilterBar::filterText() const
{
    return m_edit ? m_edit->text() : QString();
}

void FilterBar::setFilterText(const QString &text)
{
    if (m_edit) {
        m_edit->setText(text);
    }
}

void FilterBar::clearFilter()
{
    if (m_edit) {
        m_edit->clear();
    }
}
